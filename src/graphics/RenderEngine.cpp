#include "RenderEngine.h"

#include <d3dcompiler.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace screenfx::graphics {
namespace {

bool ReadBinaryFile(const std::wstring& path, std::vector<std::byte>& output) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto end = file.tellg();
    if (end <= 0) {
        return false;
    }
    output.resize(static_cast<std::size_t>(end));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(output.data()), end);
    return file.good();
}

float Clamp(float value, float lower, float upper) {
    return value < lower ? lower : (value > upper ? upper : value);
}

} // namespace

RenderEngine::RenderEngine(D3D11Context& graphics) : graphics_(graphics) {
    QueryPerformanceCounter(&startTime_);
}

std::wstring RenderEngine::ShaderPath(const wchar_t* fileName) {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::wstring(L"shaders\\") + fileName;
    }
    std::filesystem::path path(modulePath, modulePath + length);
    return (path.parent_path() / L"shaders" / fileName).wstring();
}

bool RenderEngine::CreateSwapChain(const RECT& bounds) {
    if (graphics_.Factory() == nullptr || graphics_.Device() == nullptr || outputWindow_ == nullptr) {
        return false;
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
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    description.Flags = graphics_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    if (FAILED(graphics_.Factory()->CreateSwapChainForHwnd(
            graphics_.Device(), outputWindow_, &description, nullptr, nullptr, swapChain.GetAddressOf()))) {
        return false;
    }
    graphics_.Factory()->MakeWindowAssociation(outputWindow_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    swapChain_ = std::move(swapChain);
    return CreateBackBuffer();
}

bool RenderEngine::CreateBackBuffer() {
    renderTarget_.Reset();
    if (!swapChain_) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) {
        return false;
    }
    return SUCCEEDED(graphics_.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTarget_.GetAddressOf()));
}

bool RenderEngine::CreateShaders() {
    std::vector<std::byte> vertexBytes;
    std::vector<std::byte> pixelBytes;
    if (!ReadBinaryFile(ShaderPath(L"screenfx_vs.cso"), vertexBytes) ||
        !ReadBinaryFile(ShaderPath(L"screenfx_ps.cso"), pixelBytes)) {
        return false;
    }
    if (FAILED(graphics_.Device()->CreateVertexShader(
            vertexBytes.data(), vertexBytes.size(), nullptr, vertexShader_.GetAddressOf()))) {
        return false;
    }
    if (FAILED(graphics_.Device()->CreatePixelShader(
            pixelBytes.data(), pixelBytes.size(), nullptr, pixelShader_.GetAddressOf()))) {
        return false;
    }

    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = sizeof(ShaderConstants);
    constantDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(graphics_.Device()->CreateBuffer(&constantDescription, nullptr, constantBuffer_.GetAddressOf()))) {
        return false;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(graphics_.Device()->CreateSamplerState(&samplerDescription, sampler_.GetAddressOf()));
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
    sourceView_.Reset();
    sourceTexture_.Reset();
    renderTarget_.Reset();
    swapChain_.Reset();
    vertexShader_.Reset();
    pixelShader_.Reset();
    constantBuffer_.Reset();
    sampler_.Reset();
    outputWindow_ = nullptr;
    frameNumber_ = 0;
}

bool RenderEngine::Resize(const RECT& bounds) {
    if (!swapChain_) {
        bounds_ = bounds;
        return false;
    }
    std::lock_guard lock(graphics_.Mutex());
    renderTarget_.Reset();
    const UINT width = static_cast<UINT>(bounds.right - bounds.left);
    const UINT height = static_cast<UINT>(bounds.bottom - bounds.top);
    if (FAILED(swapChain_->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                          graphics_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0))) {
        return false;
    }
    bounds_ = bounds;
    return CreateBackBuffer();
}

bool RenderEngine::CreateSourceView(const CapturedFrame& frame) {
    if (!frame.texture || frame.texture.Get() == sourceTexture_.Get()) {
        return sourceView_ != nullptr;
    }
    sourceView_.Reset();
    sourceTexture_ = frame.texture;
    return SUCCEEDED(graphics_.Device()->CreateShaderResourceView(sourceTexture_.Get(), nullptr, sourceView_.GetAddressOf()));
}

bool RenderEngine::Render(
    const CapturedFrame& frame,
    const core::EffectSettings& effects,
    core::FramePacingMode framePacing) {
    if (!swapChain_ || !renderTarget_ || !vertexShader_ || !pixelShader_ || !frame.texture) {
        ++droppedFrames_;
        return false;
    }

    std::lock_guard lock(graphics_.Mutex());
    if (!CreateSourceView(frame)) {
        ++droppedFrames_;
        return false;
    }

    auto* context = graphics_.ImmediateContext();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        ++droppedFrames_;
        return false;
    }
    auto* constants = static_cast<ShaderConstants*>(mapped.pData);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const float elapsed = frequency.QuadPart == 0
                              ? 0.0F
                              : static_cast<float>(static_cast<double>(now.QuadPart - startTime_.QuadPart) / frequency.QuadPart);
    constants->screenWidth = static_cast<float>(frame.size.cx);
    constants->screenHeight = static_cast<float>(frame.size.cy);
    constants->time = elapsed;
    constants->frame = static_cast<float>(frameNumber_);
    constants->globalIntensity = Clamp(effects.globalIntensity, 0.0F, 1.0F);
    constants->brightness = effects.brightness;
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
    context->Unmap(constantBuffer_.Get(), 0);

    const float clearColor[4]{0.0F, 0.0F, 0.0F, 1.0F};
    context->ClearRenderTargetView(renderTarget_.Get(), clearColor);
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

    const UINT syncInterval = framePacing == core::FramePacingMode::VSync ? 1U : 0U;
    const UINT presentFlags = framePacing == core::FramePacingMode::Uncapped && graphics_.TearingSupported()
                                  ? DXGI_PRESENT_ALLOW_TEARING
                                  : 0U;
    const HRESULT presentResult = swapChain_->Present(syncInterval, presentFlags);
    if (presentResult == DXGI_ERROR_DEVICE_REMOVED || presentResult == DXGI_ERROR_DEVICE_RESET) {
        ++droppedFrames_;
        return false;
    }
    if (FAILED(presentResult)) {
        ++droppedFrames_;
        return false;
    }
    ++frameNumber_;
    ++presentedFrames_;
    return true;
}

} // namespace screenfx::graphics
