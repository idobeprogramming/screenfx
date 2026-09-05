#include "CaptureSession.h"

#include <windows.graphics.directx.direct3d11.interop.h>

#include <chrono>

namespace screenfx::graphics {
namespace {

using winrt::Windows::Foundation::TimeSpan;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccess;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccessKind;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

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

CaptureSession::~CaptureSession() {
    Stop();
    if (frameEvent_ != nullptr) {
        CloseHandle(frameEvent_);
        frameEvent_ = nullptr;
    }
}

bool CaptureSession::CreateCaptureItem(HMONITOR monitor, GraphicsCaptureItem& item) {
    try {
        auto interopFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interopFactory->CreateForMonitor(
            monitor,
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item)));
        return item != nullptr;
    } catch (...) {
        return false;
    }
}

bool CaptureSession::SetCaptureRate(bool uncapped) {
    if (!session_) {
        return false;
    }

    try {
        if (auto session5 = session_.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession5>()) {
            // A 1 ms interval avoids the documented 0 ms fallback on some builds
            // while leaving the application without a practical software cap.
            const auto interval = uncapped ? std::chrono::milliseconds(1) : std::chrono::milliseconds(16);
            session5.MinUpdateInterval(TimeSpan(interval));
            return true;
        }
    } catch (...) {
    }
    return false;
}

bool CaptureSession::Start(HMONITOR monitor, SIZE size, bool uncapped) {
    Stop();
    if (monitor == nullptr || size.cx <= 0 || size.cy <= 0 || graphics_.Device() == nullptr) {
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
        framePool_.FrameArrived({this, &CaptureSession::OnFrameArrived});
        running_.store(true, std::memory_order_release);
        capturedFrames_.store(0, std::memory_order_relaxed);
        droppedFrames_.store(0, std::memory_order_relaxed);
        latestSequence_ = 0;
        consumedSequence_ = 0;
        session_.StartCapture();
        return true;
    } catch (...) {
        Stop();
        return false;
    }
}

void CaptureSession::Stop() {
    running_.store(false, std::memory_order_release);
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

        graphics_.ImmediateContext()->CopyResource(latestTexture_.Get(), sourceTexture.Get());
        latestSequence_++;
        capturedFrames_.fetch_add(1, std::memory_order_relaxed);
        if (frameEvent_ != nullptr) {
            SetEvent(frameEvent_);
        }
    } catch (...) {
        droppedFrames_.fetch_add(1, std::memory_order_relaxed);
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
