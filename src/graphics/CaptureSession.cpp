#include "CaptureSession.h"
#include <windows.graphics.directx.direct3d11.interop.h>
#include <array>
#include <mutex>
#include <cwchar>
#include <utility>

namespace screenfx::graphics {
using namespace winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
namespace {
std::wstring CaptureError(const wchar_t* operation, HRESULT result) {
    wchar_t code[32]{};
    swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(result));
    return std::wstring(operation) + L" (" + code + L").";
}
struct FrameCloser {
    Direct3D11CaptureFrame frame{nullptr};
    ~FrameCloser() { if (frame) { try { frame.Close(); } catch (...) {} } }
};
struct TextureSlot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    SIZE size{};
};
}
struct CaptureSession::State {
    mutable std::mutex mutex;
    bool running = false;
    D3D11Context* graphics = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    IDirect3DDevice device{nullptr};
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    CapturedFrame latest;
    std::array<std::shared_ptr<TextureSlot>, 4> slots;
    std::uint64_t captured = 0, dropped = 0;
    std::wstring error;
    ~State() { if (event) CloseHandle(event); }
    void Fail(std::wstring message) {
        error = std::move(message); running = false;
        if (event) SetEvent(event);
    }
};
CaptureSession::CaptureSession(D3D11Context& graphics) : graphics_(graphics), state_(std::make_shared<State>()) {}
CaptureSession::~CaptureSession() { Stop(); }
bool CaptureSession::Running() const { std::lock_guard lock(state_->mutex); return state_->running; }
HANDLE CaptureSession::FrameEvent() const noexcept { return state_->event; }
std::uint64_t CaptureSession::CapturedFrames() const { std::lock_guard lock(state_->mutex); return state_->captured; }
std::uint64_t CaptureSession::DroppedFrames() const { std::lock_guard lock(state_->mutex); return state_->dropped; }
std::wstring CaptureSession::LastError() const { std::lock_guard lock(state_->mutex); return state_->error; }

bool CaptureSession::Start(HMONITOR monitor, SIZE size, bool /*uncapped*/) {
    Stop(); state_ = std::make_shared<State>();
    auto state = state_;
    if (!state->event || !monitor || size.cx <= 0 || size.cy <= 0 || !graphics_.Device()) {
        state->Fail(L"Le moniteur ou les ressources de capture ne sont pas disponibles."); return false;
    }
    try {
        auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(factory->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)));
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(graphics_.Device()->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf())));
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        state->device = inspectable.as<IDirect3DDevice>();
        state->poolSize = item_.Size();
        if (state->poolSize.Width <= 0 || state->poolSize.Height <= 0) winrt::throw_hresult(E_INVALIDARG);
        state->graphics = &graphics_;
        framePool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(state->device,
            DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, state->poolSize);
        session_ = framePool_.CreateCaptureSession(item_);
        if (auto optional = session_.try_as<IGraphicsCaptureSession2>()) optional.IsCursorCaptureEnabled(false);
        if (auto optional = session_.try_as<IGraphicsCaptureSession3>()) {
            try { optional.IsBorderRequired(false); borderlessCaptureAvailable_ = !optional.IsBorderRequired(); } catch (...) {}
        }
        // Presentation controls VSync; neither capture mode adds a software frame cap.
        if (auto optional = session_.try_as<IGraphicsCaptureSession5>()) {
            try { optional.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan{0}); } catch (...) {}
        }
        frameToken_ = framePool_.FrameArrived([state](auto const& sender, auto const&) { OnFrameArrived(state, sender); });
        frameRegistered_ = true;
        closedToken_ = item_.Closed([state](auto const&, auto const&) {
            std::lock_guard lock(state->mutex);
            if (state->running) state->Fail(L"Le moniteur capturé a été fermé ou déconnecté.");
        });
        closedRegistered_ = true;
        { std::lock_guard lock(state->mutex); state->running = true; }
        session_.StartCapture(); return true;
    } catch (const winrt::hresult_error& error) {
        std::lock_guard lock(state->mutex); state->Fail(CaptureError(L"Démarrage de la capture impossible", error.code()));
    } catch (...) {
        std::lock_guard lock(state->mutex); state->Fail(L"Démarrage de la capture impossible.");
    }
    Stop(); return false;
}
void CaptureSession::Stop() {
    // Late callbacks only retain their own State, never this. Invalidate the device
    // under their lock before revoking events, then close without holding the lock.
    {
        std::lock_guard lock(state_->mutex);
        state_->running = false; state_->graphics = nullptr;
        state_->latest = {}; state_->slots = {}; state_->device = nullptr;
    }
    if (framePool_ && frameRegistered_) { try { framePool_.FrameArrived(frameToken_); } catch (...) {} }
    if (item_ && closedRegistered_) { try { item_.Closed(closedToken_); } catch (...) {} }
    frameRegistered_ = closedRegistered_ = false;
    if (session_) { try { session_.Close(); } catch (...) {} }
    if (framePool_) { try { framePool_.Close(); } catch (...) {} }
    session_ = nullptr; framePool_ = nullptr; item_ = nullptr;
    frameToken_ = {}; closedToken_ = {}; borderlessCaptureAvailable_ = false;
}
void CaptureSession::OnFrameArrived(const std::shared_ptr<State>& state, Direct3D11CaptureFramePool const& sender) {
    std::lock_guard stateLock(state->mutex);
    if (!state->running) return;
    try {
        FrameCloser lease{sender.TryGetNextFrame()};
        if (!lease.frame) return;
        const auto size = lease.frame.ContentSize();
        if (size.Width <= 0 || size.Height <= 0) { ++state->dropped; return; }
        if (size.Width != state->poolSize.Width || size.Height != state->poolSize.Height) {
            lease.frame.Close(); lease.frame = nullptr;
            sender.Recreate(state->device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, size);
            state->poolSize = size; ++state->dropped; return;
        }
        // The WinRT surface wraps a texture; QI<ID3D11Texture2D> on the surface fails.
        auto access = lease.frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(source.GetAddressOf())));
        D3D11_TEXTURE2D_DESC sourceDesc{}; source->GetDesc(&sourceDesc);
        if (static_cast<UINT>(size.Width) > sourceDesc.Width || static_cast<UINT>(size.Height) > sourceDesc.Height)
            winrt::throw_hresult(E_INVALIDARG);
        if (state->latest.texture) ++state->dropped;
        state->latest = {};
        std::shared_ptr<TextureSlot> slot;
        for (auto& candidate : state->slots) {
            if (!candidate) candidate = std::make_shared<TextureSlot>();
            if (candidate.use_count() == 1) { slot = candidate; break; }
        }
        if (!slot) { ++state->dropped; return; }
        auto& graphics = *state->graphics;
        std::lock_guard graphicsLock(graphics.Mutex());
        if (!slot->texture || slot->size.cx != size.Width || slot->size.cy != size.Height) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = size.Width; desc.Height = size.Height; desc.MipLevels = 1; desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            winrt::check_hresult(graphics.Device()->CreateTexture2D(&desc, nullptr, slot->texture.ReleaseAndGetAddressOf()));
            slot->size = SIZE{size.Width, size.Height};
        }
        D3D11_BOX box{0, 0, 0, static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 1};
        graphics.ImmediateContext()->CopySubresourceRegion(slot->texture.Get(), 0, 0, 0, 0, source.Get(), 0, &box);
        winrt::check_hresult(graphics.Device()->GetDeviceRemovedReason());
        state->latest.texture = slot->texture; state->latest.lease = slot; state->latest.size = slot->size;
        state->latest.sequence = ++state->captured;
        state->latest.systemTime = lease.frame.SystemRelativeTime().count();
        SetEvent(state->event);
    } catch (const winrt::hresult_error& error) {
        ++state->dropped; state->Fail(CaptureError(L"Lecture de l’image capturée impossible", error.code()));
    } catch (...) {
        ++state->dropped; state->Fail(L"Lecture de l’image capturée impossible.");
    }
}
bool CaptureSession::TryAcquireLatest(CapturedFrame& frame) {
    std::lock_guard lock(state_->mutex);
    if (!state_->running || !state_->latest.texture) return false;
    frame = std::move(state_->latest); state_->latest = {}; return true;
}
}
