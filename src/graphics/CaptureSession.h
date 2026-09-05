#pragma once

#include "D3D11Context.h"

#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace screenfx::graphics {

struct CapturedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    SIZE size{};
    std::uint64_t sequence = 0;
    std::int64_t systemTime = 0;
};

class CaptureSession {
public:
    explicit CaptureSession(D3D11Context& graphics);
    ~CaptureSession();

    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;

    bool Start(HMONITOR monitor, SIZE size, bool uncapped);
    void Stop();
    bool Running() const noexcept { return running_.load(std::memory_order_acquire); }

    bool TryAcquireLatest(CapturedFrame& frame);
    HANDLE FrameEvent() const noexcept { return frameEvent_; }
    std::uint64_t CapturedFrames() const noexcept { return capturedFrames_.load(std::memory_order_relaxed); }
    std::uint64_t DroppedFrames() const noexcept { return droppedFrames_.load(std::memory_order_relaxed); }
    bool BorderlessCaptureAvailable() const noexcept { return borderlessCaptureAvailable_; }

private:
    struct CallbackGuard {
        CaptureSession* owner = nullptr;
        explicit CallbackGuard(CaptureSession* value);
        ~CallbackGuard();
    };

    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const&);
    bool CreateCaptureItem(HMONITOR monitor, winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item);
    bool SetCaptureRate(bool uncapped);

    D3D11Context& graphics_;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token frameArrivedToken_{};
    bool frameHandlerRegistered_ = false;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> latestTexture_;
    SIZE latestSize_{};
    std::uint64_t latestSequence_ = 0;
    std::uint64_t consumedSequence_ = 0;
    std::mutex frameMutex_;
    std::mutex callbackMutex_;
    std::condition_variable callbackCv_;
    std::uint32_t activeCallbacks_ = 0;
    HANDLE frameEvent_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> capturedFrames_{0};
    std::atomic<std::uint64_t> droppedFrames_{0};
    bool borderlessCaptureAvailable_ = false;
};

} // namespace screenfx::graphics
