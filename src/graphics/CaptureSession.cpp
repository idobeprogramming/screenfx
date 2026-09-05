#include "CaptureSession.h"

#include <windows.graphics.directx.direct3d11.interop.h>

#include <chrono>
#include <cwchar>

namespace screenfx::graphics {
namespace {

using winrt::Windows::Foundation::TimeSpan;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccess;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccessKind;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

std::wstring HResultMessage(HRESULT result) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%08lX", static_cast<unsigned long>(result));
    return buffer;
}

std::wstring DescribeCaptureError(const wchar_t* operation, HRESULT result) {
    if (result == HRESULT_FROM_WIN32(ERROR_SERVICE_DOES_NOT_EXIST)) {
        return std::wstring(operation) +
               L" — service Windows Graphics Capture indisponible dans cette session (0x80070424).";
    }
    return std::wstring(operation) + L" (" + HResultMessage(result) + L").";
}

IDirect3DDevice CreateWinrtDevice(ID3D11Device* device) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())));
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
    return inspectable.as<IDirect3DDevice>();
}

} // namespace

CaptureSession::CaptureSession(D3D11Context& graphics) : graphics_(graphics) {
    frameEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

CaptureSession::CallbackGuard::CallbackGuard(CaptureSession* value) : owner(value) {
    if (owner != nullptr) {
        std::lock_guard lock(owner->callbackMutex_);
        ++owner->activeCallbacks_;
    }
}

CaptureSession::CallbackGuard::~CallbackGuard() {
    if (owner != nullptr) {
        {
            std::lock_guard lock(owner->callbackMutex_);
            if (owner->activeCallbacks_ > 0) {
                --owner->activeCallbacks_;
            }
        }
        owner->callbackCv_.notify_all();
    }
}

CaptureSession::~CaptureSession() {
    Stop();
    if (frameEvent_ != nullptr) {
        CloseHandle(frameEvent_);
        frameEvent_ = nullptr;
    }
}

std::wstring CaptureSession::LastError() const {
    std::lock_guard lock(errorMutex_);
    return lastError_;
}

void CaptureSession::SetLastError(std::wstring message) {
    std::lock_guard lock(errorMutex_);
    lastError_ = std::move(message);
}

bool CaptureSession::CreateCaptureItem(HMONITOR monitor, GraphicsCaptureItem& item) {
    try {
        auto interopFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        const HRESULT result = interopFactory->CreateForMonitor(
            monitor,
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item));
        if (FAILED(result)) {
            SetLastError(DescribeCaptureError(L"CreateForMonitor a échoué", result));
            return false;
        }
        return item != nullptr;
    } catch (const winrt::hresult_error& error) {
        SetLastError(DescribeCaptureError(L"Windows Graphics Capture a renvoyé", error.code()));
        return false;
    } catch (...) {
        SetLastError(L"Windows Graphics Capture a renvoyé une erreur inconnue.");
        return false;
    }
}

bool CaptureSession::SetCaptureRate(bool uncapped) {
    if (!session_) {
        return false;
    }

    try {
        if (auto session5 = session_.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession5>()) {
            // Zero asks Windows Graphics Capture to deliver frames as soon as the
            // compositor has a new frame. Present() controls the optional VSync
            // policy; the uncapped path adds no software frame interval.
            const auto interval = uncapped ? std::chrono::milliseconds(0) : std::chrono::milliseconds(16);
            session5.MinUpdateInterval(TimeSpan(interval));
            return true;
        }
    } catch (...) {
    }
    return false;
}

bool CaptureSession::Start(HMONITOR monitor, SIZE size, bool uncapped) {
    Stop();
    SetLastError({});
    borderlessCaptureAvailable_ = false;
    callbackErrorNotified_.store(false, std::memory_order_relaxed);
    if (monitor == nullptr || size.cx <= 0 || size.cy <= 0 || graphics_.Device() == nullptr) {
        SetLastError(L"Le moniteur, sa taille ou le périphérique Direct3D est invalide.");
        return false;
    }

    GraphicsCaptureItem item{nullptr};
    if (!CreateCaptureItem(monitor, item)) {
        return false;
    }

    try {
        winrtDevice_ = CreateWinrtDevice(graphics_.Device());
        item_ = item;
        framePool_ = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice_,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            3,
            winrt::Windows::Graphics::SizeInt32{size.cx, size.cy});
        session_ = framePool_.CreateCaptureSession(item_);
        session_.IsCursorCaptureEnabled(false);

        if (auto session3 = session_.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession3>()) {
            try {
                session3.IsBorderRequired(false);
                borderlessCaptureAvailable_ = true;
            } catch (...) {
                borderlessCaptureAvailable_ = false;
            }
        }
        SetCaptureRate(uncapped);
        frameArrivedToken_ = framePool_.FrameArrived({this, &CaptureSession::OnFrameArrived});
        frameHandlerRegistered_ = true;
        running_.store(true, std::memory_order_release);
        capturedFrames_.store(0, std::memory_order_relaxed);
        droppedFrames_.store(0, std::memory_order_relaxed);
        latestSequence_ = 0;
        consumedSequence_ = 0;
        session_.StartCapture();
        return true;
    } catch (const winrt::hresult_error& error) {
        SetLastError(DescribeCaptureError(L"Initialisation de la capture impossible", error.code()));
        Stop();
        return false;
    } catch (...) {
        SetLastError(L"Initialisation de la capture impossible (erreur inconnue).");
        Stop();
        return false;
    }
}

void CaptureSession::Stop() {
    running_.store(false, std::memory_order_release);
    if (framePool_ && frameHandlerRegistered_) {
        try {
            framePool_.FrameArrived(frameArrivedToken_);
        } catch (...) {
        }
        frameHandlerRegistered_ = false;
        frameArrivedToken_ = {};
    }
    try {
        if (session_) {
            session_.Close();
        }
        if (framePool_) {
            framePool_.Close();
        }
    } catch (...) {
    }
    session_ = nullptr;
    framePool_ = nullptr;
    item_ = nullptr;
    winrtDevice_ = nullptr;
    borderlessCaptureAvailable_ = false;
    callbackErrorNotified_.store(false, std::memory_order_relaxed);
    {
        std::unique_lock lock(callbackMutex_);
        callbackCv_.wait(lock, [this] { return activeCallbacks_ == 0; });
    }
    {
        std::lock_guard lock(frameMutex_);
        latestTexture_.Reset();
        latestSize_ = {};
        latestSequence_ = 0;
        consumedSequence_ = 0;
    }
}

void CaptureSession::OnFrameArrived(
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
    winrt::Windows::Foundation::IInspectable const&) {
    CallbackGuard callbackGuard(this);
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    try {
        auto frame = sender.TryGetNextFrame();
        if (!frame) {
            return;
        }

        const auto contentSize = frame.ContentSize();
        auto source = frame.Surface().as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface>();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceTexture;
        winrt::check_hresult(source.as<::IUnknown>()->QueryInterface(IID_PPV_ARGS(sourceTexture.GetAddressOf())));

        std::lock_guard graphicsLock(graphics_.Mutex());
        D3D11_TEXTURE2D_DESC sourceDescription{};
        sourceTexture->GetDesc(&sourceDescription);

        std::lock_guard frameLock(frameMutex_);
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        if (!latestTexture_ || latestSize_.cx != contentSize.Width || latestSize_.cy != contentSize.Height) {
            D3D11_TEXTURE2D_DESC destinationDescription = sourceDescription;
            destinationDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            destinationDescription.Usage = D3D11_USAGE_DEFAULT;
            destinationDescription.CPUAccessFlags = 0;
            destinationDescription.MiscFlags = 0;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> destination;
            winrt::check_hresult(graphics_.Device()->CreateTexture2D(
                &destinationDescription,
                nullptr,
                destination.GetAddressOf()));
            latestTexture_ = std::move(destination);
            latestSize_ = SIZE{contentSize.Width, contentSize.Height};
        }

        if (latestSequence_ != consumedSequence_) {
            droppedFrames_.fetch_add(1, std::memory_order_relaxed);
        }
        graphics_.ImmediateContext()->CopyResource(latestTexture_.Get(), sourceTexture.Get());
        latestSequence_++;
        capturedFrames_.fetch_add(1, std::memory_order_relaxed);
        callbackErrorNotified_.store(false, std::memory_order_relaxed);
        if (frameEvent_ != nullptr) {
            SetEvent(frameEvent_);
        }
    } catch (const winrt::hresult_error& error) {
        SetLastError(DescribeCaptureError(L"FrameArrived — copie GPU impossible", error.code()));
        droppedFrames_.fetch_add(1, std::memory_order_relaxed);
        if (!callbackErrorNotified_.exchange(true, std::memory_order_relaxed) && frameEvent_ != nullptr) {
            SetEvent(frameEvent_);
        }
    } catch (...) {
        SetLastError(L"Capture/FrameArrived: erreur inconnue pendant la copie GPU.");
        droppedFrames_.fetch_add(1, std::memory_order_relaxed);
        if (!callbackErrorNotified_.exchange(true, std::memory_order_relaxed) && frameEvent_ != nullptr) {
            SetEvent(frameEvent_);
        }
    }
}

bool CaptureSession::TryAcquireLatest(CapturedFrame& frame) {
    std::lock_guard lock(frameMutex_);
    if (!latestTexture_ || latestSequence_ == consumedSequence_) {
        return false;
    }
    frame.texture = latestTexture_;
    frame.size = latestSize_;
    frame.sequence = latestSequence_;
    frame.systemTime = 0;
    consumedSequence_ = latestSequence_;
    return true;
}

} // namespace screenfx::graphics
