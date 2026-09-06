#include "RenderEngine.h"

#include <d3dcompiler.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstddef>

namespace screenfx::graphics {
namespace {

bool ReadBinaryFile(const std::wstring& path, std::vector<std::byte>& output) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto end = file.tellg();
    if (end <= 0 || end > 16 * 1024 * 1024) {
        return false;
    }
    output.resize(static_cast<std::size_t>(end));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(output.data()), end);
    return file.good();
}

float Clamp(float value, float lower, float upper) {
    if (!std::isfinite(value)) return lower;
    return value < lower ? lower : (value > upper ? upper : value);
}

} // namespace

RenderEngine::RenderEngine(D3D11Context& graphics) : graphics_(graphics) {
    QueryPerformanceCounter(&startTime_);
    QueryPerformanceFrequency(&frequency_);
}

std::wstring RenderEngine::ShaderPath(const wchar_t* fileName) {
    std::vector<wchar_t> modulePath(32768);
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) return {};
    std::filesystem::path path(modulePath.data(), modulePath.data() + length);
    return (path.parent_path() / L"shaders" / fileName).wstring();
}

bool RenderEngine::CreateSwapChain(const RECT& bounds) {
    if (graphics_.Factory() == nullptr || graphics_.Device() == nullptr || outputWindow_ == nullptr ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return Check(E_INVALIDARG);
    }
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(bounds.right - bounds.left);
    description.Height = static_cast<UINT>(bounds.bottom - bounds.top);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    description.Flags = graphics_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    if (!Check(graphics_.Factory()->CreateSwapChainForComposition(
            graphics_.Device(), &description, nullptr, swapChain.GetAddressOf()))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (!Check(graphics_.Device()->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()))) ||
        !Check(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(compositionDevice_.GetAddressOf()))) ||
        !Check(compositionDevice_->CreateTargetForHwnd(outputWindow_, TRUE, compositionTarget_.GetAddressOf())) ||
        !Check(compositionDevice_->CreateVisual(compositionVisual_.GetAddressOf())) ||
        !Check(compositionVisual_->SetContent(swapChain.Get())) ||
        !Check(compositionTarget_->SetRoot(compositionVisual_.Get())) ||
        !Check(compositionDevice_->Commit())) return false;
    swapChain_ = std::move(swapChain);
    return CreateBackBuffer();
}

bool RenderEngine::CreateBackBuffer() {
    renderTarget_.Reset();
    if (!swapChain_) {
        return Check(E_UNEXPECTED);
    }
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (!Check(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) {
        return false;
    }
    return Check(graphics_.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTarget_.GetAddressOf()));
}

bool RenderEngine::CreateShaders() {
    std::vector<std::byte> vertexBytes;
    std::vector<std::byte> pixelBytes;
    if (!ReadBinaryFile(ShaderPath(L"screenfx_vs.cso"), vertexBytes) ||
        !ReadBinaryFile(ShaderPath(L"screenfx_ps.cso"), pixelBytes)) {
        return Check(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    }
    if (!Check(graphics_.Device()->CreateVertexShader(
            vertexBytes.data(), vertexBytes.size(), nullptr, vertexShader_.GetAddressOf()))) {
        return false;
    }
    if (!Check(graphics_.Device()->CreatePixelShader(
            pixelBytes.data(), pixelBytes.size(), nullptr, pixelShader_.GetAddressOf()))) {
        return false;
    }

    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = sizeof(ShaderConstants);
    constantDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    static_assert(sizeof(ShaderConstants) % 16 == 0);
    static_assert(sizeof(ShaderConstants) == 128);
    static_assert(offsetof(ShaderConstants, tintRed) == 112);
    static_assert(offsetof(ShaderConstants, tintIntensity) == 124);
    if (!Check(graphics_.Device()->CreateBuffer(&constantDescription, nullptr, constantBuffer_.GetAddressOf()))) {
        return false;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    samplerDescription.MaxAnisotropy = 1;
    samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
    return Check(graphics_.Device()->CreateSamplerState(&samplerDescription, sampler_.GetAddressOf()));
}

bool RenderEngine::Initialize(HWND outputWindow, const RECT& bounds) {
    Shutdown();
    outputWindow_ = outputWindow;
    bounds_ = bounds;
    std::lock_guard lock(graphics_.Mutex());
    QueryPerformanceCounter(&startTime_);
    return CreateSwapChain(bounds_) && CreateShaders();
}

void RenderEngine::Shutdown() {
    std::lock_guard lock(graphics_.Mutex());
    if (auto* context = graphics_.ImmediateContext()) {
        context->ClearState();
        context->Flush();
    }
    if (compositionTarget_) compositionTarget_->SetRoot(nullptr);
    if (compositionVisual_) compositionVisual_->SetContent(nullptr);
    if (compositionDevice_) compositionDevice_->Commit();
    compositionTarget_.Reset();
    compositionVisual_.Reset();
    compositionDevice_.Reset();
    ClearSourceViews();
    renderTarget_.Reset();
    swapChain_.Reset();
    vertexShader_.Reset();
    pixelShader_.Reset();
    constantBuffer_.Reset();
    sampler_.Reset();
    outputWindow_ = nullptr;
    frameNumber_ = 0;
    presentedFrames_ = 0;
    droppedFrames_ = 0;
}

bool RenderEngine::Resize(const RECT& bounds) {
    if (!swapChain_ || bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return Check(E_INVALIDARG);
    }
    std::lock_guard lock(graphics_.Mutex());
    graphics_.ImmediateContext()->OMSetRenderTargets(0, nullptr, nullptr);
    renderTarget_.Reset();
    ClearSourceViews();
    const UINT width = static_cast<UINT>(bounds.right - bounds.left);
    const UINT height = static_cast<UINT>(bounds.bottom - bounds.top);
    if (!Check(swapChain_->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                          graphics_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0))) {
        return false;
    }
    bounds_ = bounds;
    return CreateBackBuffer();
}

void RenderEngine::ClearSourceViews() {
    sourceView_.Reset();
    sourceTexture_.Reset();
    sourceViews_ = {};
    nextSourceView_ = 0;
}

bool RenderEngine::CreateSourceView(const CapturedFrame& frame) {
    if (!frame.texture) return Check(E_INVALIDARG);
    if (frame.texture.Get() == sourceTexture_.Get() && sourceView_) return true;
    for (const auto& entry : sourceViews_) {
        if (entry.texture.Get() == frame.texture.Get()) {
            sourceTexture_ = entry.texture;
            sourceView_ = entry.view;
            return true;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    if (!Check(graphics_.Device()->CreateShaderResourceView(frame.texture.Get(), nullptr, view.GetAddressOf())))
        return false;
    sourceViews_[nextSourceView_] = {frame.texture, view};
    nextSourceView_ = (nextSourceView_ + 1) % sourceViews_.size();
    sourceTexture_ = frame.texture;
    sourceView_ = std::move(view);
    return true;
}

bool RenderEngine::DrawFrame(
    const CapturedFrame& frame,
    const core::EffectSettings& effects) {
    if (!renderTarget_ || !vertexShader_ || !pixelShader_ || !frame.texture || frame.size.cx <= 0 || frame.size.cy <= 0) {
        return Check(E_INVALIDARG);
    }

    std::lock_guard lock(graphics_.Mutex());
    if (!CreateSourceView(frame)) {
        return false;
    }

    auto* context = graphics_.ImmediateContext();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!Check(context->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    auto* constants = static_cast<ShaderConstants*>(mapped.pData);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const float elapsed = frequency_.QuadPart == 0
                              ? 0.0F
                              : static_cast<float>(static_cast<double>(now.QuadPart - startTime_.QuadPart) / frequency_.QuadPart);
    constants->screenWidth = static_cast<float>(frame.size.cx);
    constants->screenHeight = static_cast<float>(frame.size.cy);
    constants->time = elapsed;
    constants->frame = static_cast<float>(frameNumber_);
    constants->globalIntensity = Clamp(effects.globalIntensity, 0.0F, 1.0F);
    constants->brightness = Clamp(effects.brightness, -1.0F, 1.0F);
    constants->contrast = Clamp(effects.contrast, 0.0F, 4.0F);
    constants->saturation = Clamp(effects.saturation, 0.0F, 4.0F);
    constants->gamma = Clamp(effects.gamma, 0.1F, 4.0F);
    constants->grayscale = Clamp(effects.grayscale, 0.0F, 1.0F);
    constants->sepia = Clamp(effects.sepia, 0.0F, 1.0F);
    constants->scanlineIntensity = Clamp(effects.scanlineIntensity, 0.0F, 1.0F);
    constants->scanlineSpacing = Clamp(effects.scanlineSpacing, 1.0F, 8.0F);
    constants->scanlineThickness = Clamp(effects.scanlineThickness, 0.05F, 1.0F);
    constants->phosphorIntensity = Clamp(effects.phosphorIntensity, 0.0F, 1.0F);
    constants->pixelSize = Clamp(effects.pixelSize, 1.0F, 32.0F);
    constants->sharpen = Clamp(effects.sharpen, 0.0F, 2.0F);
    constants->chromaticAberration = Clamp(effects.chromaticAberration, 0.0F, 8.0F);
    constants->bloomIntensity = Clamp(effects.bloomIntensity, 0.0F, 2.0F);
    constants->bloomThreshold = Clamp(effects.bloomThreshold, 0.0F, 1.0F);
    constants->bloomRadius = Clamp(effects.bloomRadius, 0.5F, 8.0F);
    constants->vignetteIntensity = Clamp(effects.vignetteIntensity, 0.0F, 1.0F);
    constants->vignetteWidth = Clamp(effects.vignetteWidth, 0.1F, 1.0F);
    constants->grainIntensity = Clamp(effects.grainIntensity, 0.0F, 1.0F);
    constants->grainSize = Clamp(effects.grainSize, 0.25F, 8.0F);
    constants->padding[0] = constants->padding[1] = constants->padding[2] = 0.0F;
    constants->tintRed = Clamp(effects.tintRed, 0.0F, 1.0F);
    constants->tintGreen = Clamp(effects.tintGreen, 0.0F, 1.0F);
    constants->tintBlue = Clamp(effects.tintBlue, 0.0F, 1.0F);
    constants->tintIntensity = Clamp(effects.tintIntensity, 0.0F, 1.0F);
    context->Unmap(constantBuffer_.Get(), 0);

    // The full-screen triangle overwrites every pixel, including alpha.
    context->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(bounds_.right - bounds_.left);
    viewport.Height = static_cast<float>(bounds_.bottom - bounds_.top);
    viewport.MaxDepth = 1.0F;
    context->RSSetViewports(1, &viewport);
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, constantBuffer_.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context->PSSetShaderResources(0, 1, sourceView_.GetAddressOf());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* noSource = nullptr;
    context->PSSetShaderResources(0, 1, &noSource);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return Check(graphics_.Device()->GetDeviceRemovedReason());
}

bool RenderEngine::Render(const CapturedFrame& frame, const core::EffectSettings& effects,
                          core::FramePacingMode framePacing) {
    if (!swapChain_) {
        ++droppedFrames_;
        return Check(E_UNEXPECTED);
    }
    if (!DrawFrame(frame, effects)) {
        ++droppedFrames_;
        return false;
    }
    std::lock_guard lock(graphics_.Mutex());
    const UINT syncInterval = framePacing == core::FramePacingMode::VSync ? 1U : 0U;
    const UINT presentFlags = framePacing == core::FramePacingMode::Uncapped && graphics_.TearingSupported()
                                  ? DXGI_PRESENT_ALLOW_TEARING
                                  : 0U;
    const HRESULT presentResult = swapChain_->Present(syncInterval, presentFlags);
    lastError_ = presentResult;
    if (presentResult != S_OK) {
        ++droppedFrames_;
        return false;
    }
    ++frameNumber_;
    ++presentedFrames_;
    return true;
}

} // namespace screenfx::graphics
