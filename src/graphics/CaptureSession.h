#pragma once
#include "D3D11Context.h"
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <memory>
#include <string>
#include <cstdint>

namespace screenfx::graphics {
struct CapturedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    SIZE size{};
    std::uint64_t sequence = 0;
    std::int64_t systemTime = 0;
    // Prevents recycling this texture until every consumer releases the frame.
    std::shared_ptr<void> lease;
};
class CaptureSession {
public:
    explicit CaptureSession(D3D11Context& graphics);
    ~CaptureSession();
    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;
    bool Start(HMONITOR monitor, SIZE size, bool uncapped);
    void Stop();
    bool Running() const;
    bool TryAcquireLatest(CapturedFrame& frame);
    HANDLE FrameEvent() const noexcept;
    std::uint64_t CapturedFrames() const;
    std::uint64_t DroppedFrames() const;
    bool BorderlessCaptureAvailable() const noexcept { return borderlessCaptureAvailable_; }
    std::wstring LastError() const;
private:
    struct State;
    static void OnFrameArrived(const std::shared_ptr<State>& state,
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender);
    D3D11Context& graphics_;
    std::shared_ptr<State> state_;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token frameToken_{}, closedToken_{};
    bool frameRegistered_ = false, closedRegistered_ = false;
    bool borderlessCaptureAvailable_ = false;
};
}
