#pragma once

#include "D3D11Context.h"
#include "CaptureSession.h"
#include "../core/Settings.h"

#include <windows.h>
#include <wrl/client.h>

#include <cstdint>

namespace screenfx::graphics {

class RenderEngine {
public:
    explicit RenderEngine(D3D11Context& graphics);

    bool Initialize(HWND outputWindow, const RECT& bounds);
    void Shutdown();
    bool Render(const CapturedFrame& frame, const core::EffectSettings& effects, core::FramePacingMode framePacing);
    bool Resize(const RECT& bounds);

    std::uint64_t PresentedFrames() const noexcept { return presentedFrames_; }
    std::uint64_t DroppedFrames() const noexcept { return droppedFrames_; }

private:
    struct ShaderConstants {
        float screenWidth;
        float screenHeight;
        float time;
        float frame;

        float globalIntensity;
        float brightness;
        float contrast;
        float saturation;
        float gamma;
        float grayscale;
        float sepia;
        float scanlineIntensity;
        float scanlineSpacing;
        float scanlineThickness;
        float phosphorIntensity;
        float pixelSize;
        float sharpen;
        float chromaticAberration;
        float bloomIntensity;
        float bloomThreshold;
        float bloomRadius;
        float vignetteIntensity;
        float vignetteWidth;
        float grainIntensity;
        float grainSize;
        float padding[3];
    };

    bool CreateSwapChain(const RECT& bounds);
    bool CreateShaders();
    bool CreateBackBuffer();
    bool CreateSourceView(const CapturedFrame& frame);
    static std::wstring ShaderPath(const wchar_t* fileName);

    D3D11Context& graphics_;
    HWND outputWindow_ = nullptr;
    RECT bounds_{};
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceTexture_;
    std::uint64_t frameNumber_ = 0;
    std::uint64_t presentedFrames_ = 0;
    std::uint64_t droppedFrames_ = 0;
    LARGE_INTEGER startTime_{};
};

} // namespace screenfx::graphics
