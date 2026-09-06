#include "graphics/RenderEngine.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using namespace screenfx;
using Microsoft::WRL::ComPtr;
using Bytes = std::vector<unsigned char>;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

namespace screenfx::graphics {
struct RenderEngineTestAccess {
    static void Initialize(RenderEngine& e, ID3D11Texture2D* output, UINT width, UINT height) {
        e.bounds_ = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        Require(e.CreateShaders(), "Load production shaders");
        winrt::check_hresult(e.graphics_.Device()->CreateRenderTargetView(output, nullptr, e.renderTarget_.GetAddressOf()));
    }
    static ComPtr<ID3D11PixelShader> Reference(RenderEngine& e) {
        ComPtr<ID3DBlob> bytes;
        winrt::check_hresult(D3DReadFileToBlob(e.ShaderPath(L"screenfx_reference_ps.cso").c_str(), bytes.GetAddressOf()));
        ComPtr<ID3D11PixelShader> shader;
        winrt::check_hresult(e.graphics_.Device()->CreatePixelShader(bytes->GetBufferPointer(), bytes->GetBufferSize(), nullptr, shader.GetAddressOf()));
        return shader;
    }
    static auto Shader(RenderEngine& e) { return e.pixelShader_; }
    static auto View(RenderEngine& e) { return e.sourceView_; }
    static void SetShader(RenderEngine& e, ID3D11PixelShader* shader) { e.pixelShader_ = shader; }
    static void Draw(RenderEngine& e, const CapturedFrame& frame, const core::EffectSettings& effects) {
        Require(e.DrawFrame(frame, effects), "DrawFrame failed");
    }
    static void RedrawReference(RenderEngine& e, ID3D11PixelShader* reference) {
        // Keep the exact constants, including animation time, from the previous draw.
        std::lock_guard lock(e.graphics_.Mutex());
        auto* ctx = e.graphics_.ImmediateContext();
        ctx->OMSetRenderTargets(1, e.renderTarget_.GetAddressOf(), nullptr);
        ctx->PSSetShader(reference, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, e.sourceView_.GetAddressOf());
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* empty = nullptr;
        ctx->PSSetShaderResources(0, 1, &empty);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    static bool CacheEmpty(RenderEngine& e) {
        return !e.sourceView_ && !e.sourceTexture_ && std::all_of(e.sourceViews_.begin(), e.sourceViews_.end(),
            [](const auto& entry) { return !entry.texture && !entry.view; });
    }
};
}
using Access = graphics::RenderEngineTestAccess;

ComPtr<ID3D11Texture2D> Texture(graphics::D3D11Context& gpu, UINT width, UINT height, UINT flags, const Bytes* bytes = nullptr) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = flags; desc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA data{};
    if (bytes) { data.pSysMem = bytes->data(); data.SysMemPitch = width * 4; }
    ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(gpu.Device()->CreateTexture2D(&desc, bytes ? &data : nullptr, texture.GetAddressOf()));
    return texture;
}
Bytes Read(graphics::D3D11Context& gpu, ID3D11Texture2D* output) {
    D3D11_TEXTURE2D_DESC desc{}; output->GetDesc(&desc);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    winrt::check_hresult(gpu.Device()->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
    auto* ctx = gpu.ImmediateContext();
    ctx->CopyResource(staging.Get(), output);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    Bytes result(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(result.data() + static_cast<size_t>(y) * desc.Width * 4,
            static_cast<unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, desc.Width * 4);
    ctx->Unmap(staging.Get(), 0);
    return result;
}
Bytes Pattern(UINT width, UINT height) {
    Bytes bytes(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        const size_t i = (static_cast<size_t>(y) * width + x) * 4;
        bytes[i] = static_cast<unsigned char>((x * 255) / (width - 1));
        bytes[i + 1] = static_cast<unsigned char>((y * 255) / (height - 1));
        bytes[i + 2] = (x + y) % 2 ? 255 : 0; bytes[i + 3] = 255;
    }
    return bytes;
}
core::EffectSettings Crt() {
    core::EffectSettings e;
    e.scanlineIntensity = 0.65F; e.phosphorIntensity = 0.55F; e.vignetteIntensity = 0.25F;
    e.tintRed = 1; e.tintGreen = 0.8F; e.tintBlue = 0.5F; e.tintIntensity = 0.4F;
    return e;
}
core::EffectSettings Heavy() {
    auto e = Crt();
    e.gamma = 1.2F; e.contrast = 1.1F; e.saturation = 0.8F; e.sepia = 0.1F;
    e.pixelSize = 2; e.sharpen = 0.35F; e.chromaticAberration = 1.5F;
    e.bloomIntensity = 0.6F; e.grainIntensity = 0.08F;
    return e;
}
void Verify(graphics::D3D11Context& gpu) {
    constexpr UINT width = 64, height = 48;
    const auto pixels = Pattern(width, height);
    auto source = Texture(gpu, width, height, D3D11_BIND_SHADER_RESOURCE, &pixels);
    auto output = Texture(gpu, width, height, D3D11_BIND_RENDER_TARGET);
    graphics::CapturedFrame frame{}; frame.texture = source; frame.size = {width, height};
    graphics::RenderEngine engine(gpu); Access::Initialize(engine, output.Get(), width, height);
    auto reference = Access::Reference(engine);
    std::vector<core::EffectSettings> cases{{}, Crt(), Heavy()};
    for (float global : {0.0F, 0.5F, 1.0F}) {
        auto e = Heavy(); e.globalIntensity = global; cases.push_back(e);
    }
    for (float boundary : {0.0F, 0.001F, 0.00101F, 1.0F}) {
        auto e = Heavy(); e.sharpen = e.grainIntensity = e.phosphorIntensity = e.bloomIntensity = boundary;
        e.pixelSize = 1.0F + boundary; e.chromaticAberration = boundary;
        cases.push_back(e); e.bloomThreshold = 1; cases.push_back(e);
    }
    auto negative = Heavy(); negative.brightness = -0.5F; negative.gamma = 1; cases.push_back(negative);
    std::mt19937 random(42);
    auto unit = [&] { return static_cast<float>(random() % 1001) / 1000.0F; };
    for (int i = 0; i < 80; ++i) {
        auto e = Heavy();
        e.globalIntensity = i % 3 == 0 ? 1 : unit();
        e.brightness = unit() * 2 - 1; e.contrast = unit() * 4;
        e.saturation = unit() * 4; e.gamma = i % 2 ? 1 : 0.1F + unit() * 3.9F;
        e.grayscale = unit(); e.sepia = unit(); e.pixelSize = 1 + unit() * 31;
        e.sharpen = unit() * 2; e.chromaticAberration = i % 2 ? 0 : unit() * 8;
        e.bloomIntensity = unit() * 2; e.bloomThreshold = unit(); e.bloomRadius = 0.5F + unit() * 7.5F;
        e.scanlineIntensity = unit(); e.scanlineSpacing = 1 + unit() * 7; e.scanlineThickness = 0.05F + unit() * 0.95F;
        e.phosphorIntensity = unit(); e.vignetteIntensity = unit(); e.vignetteWidth = 0.1F + unit() * 0.9F;
        e.grainIntensity = unit(); e.grainSize = 0.25F + unit() * 7.75F;
        e.tintRed = unit(); e.tintGreen = unit(); e.tintBlue = unit(); e.tintIntensity = unit();
        cases.push_back(e);
    }
    int maximumDifference = 0;
    for (size_t n = 0; n < cases.size(); ++n) {
        Access::Draw(engine, frame, cases[n]); const auto optimized = Read(gpu, output.Get());
        Access::RedrawReference(engine, reference.Get()); const auto original = Read(gpu, output.Get());
        for (size_t i = 0; i < original.size(); ++i) {
            const int difference = std::abs(int(optimized[i]) - int(original[i]));
            maximumDifference = std::max(difference, maximumDifference);
            if (difference > 1) throw std::runtime_error("Shader mismatch in case " + std::to_string(n) + ", byte " + std::to_string(i) + ", delta " + std::to_string(difference));
        }
    }
    // Rotate capture textures and prove view reuse by COM identity, with strong
    // references preventing an allocator from recycling a discarded view address.
    std::array<ComPtr<ID3D11Texture2D>, 5> pool;
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> views;
    for (size_t i = 0; i < pool.size(); ++i) {
        Bytes solid(width * height * 4, static_cast<unsigned char>(20 + i * 40));
        for (size_t p = 3; p < solid.size(); p += 4) solid[p] = 255;
        pool[i] = Texture(gpu, width, height, D3D11_BIND_SHADER_RESOURCE, &solid);
    }
    for (size_t n = 0; n < 100; ++n) {
        const size_t index = n % 4; frame.texture = pool[index];
        Access::Draw(engine, frame, {});
        if (n < 4) views[index] = Access::View(engine);
        else Require(views[index].Get() == Access::View(engine).Get(), "Rotating textures recreated an SRV");
        const auto actual = Read(gpu, output.Get());
        for (size_t p = 0; p < actual.size(); ++p)
            Require(actual[p] == (p % 4 == 3 ? 255 : 20 + index * 40), "Stale source or uncovered output pixel");
    }
    frame.texture = pool[4]; Access::Draw(engine, frame, {});
    frame.texture = pool[0]; Access::Draw(engine, frame, {});
    Require(Access::View(engine).Get() != views[0].Get(), "Oldest cache entry was not evicted");
    engine.Shutdown(); Require(Access::CacheEmpty(engine), "Shutdown retained capture resources");
    std::cout << "PASS: " << cases.size() << " shader comparisons (max channel delta " << maximumDifference
              << "), 100 texture rotations, cache eviction/cleanup, full target coverage\n";
}

template<class T> T QueryResult(ID3D11DeviceContext* ctx, ID3D11Query* query) {
    T data{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        const HRESULT hr = ctx->GetData(query, &data, sizeof(data), 0);
        if (hr == S_OK) return data;
        winrt::check_hresult(hr);
        Require(std::chrono::steady_clock::now() < deadline, "GPU query timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
double Measure(graphics::D3D11Context& gpu, graphics::RenderEngine& engine,
               const graphics::CapturedFrame& frame, const core::EffectSettings& effects, ID3D11PixelShader* shader) {
    ComPtr<ID3D11Query> disjoint, start, stop;
    D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    winrt::check_hresult(gpu.Device()->CreateQuery(&desc, disjoint.GetAddressOf()));
    desc.Query = D3D11_QUERY_TIMESTAMP;
    winrt::check_hresult(gpu.Device()->CreateQuery(&desc, start.GetAddressOf()));
    winrt::check_hresult(gpu.Device()->CreateQuery(&desc, stop.GetAddressOf()));
    Access::SetShader(engine, shader);
    for (int i = 0; i < 8; ++i) Access::Draw(engine, frame, effects);
    auto* ctx = gpu.ImmediateContext();
    ctx->Begin(disjoint.Get()); ctx->End(start.Get());
    constexpr int draws = 48;
    for (int i = 0; i < draws; ++i) Access::Draw(engine, frame, effects);
    ctx->End(stop.Get()); ctx->End(disjoint.Get()); ctx->Flush();
    const auto frequency = QueryResult<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(ctx, disjoint.Get());
    Require(!frequency.Disjoint && frequency.Frequency != 0, "GPU clock changed; rerun benchmark");
    const auto first = QueryResult<UINT64>(ctx, start.Get());
    const auto last = QueryResult<UINT64>(ctx, stop.Get());
    return static_cast<double>(last - first) * 1000.0 / static_cast<double>(frequency.Frequency) / draws;
}
void Benchmark(graphics::D3D11Context& gpu) {
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC desc{};
    winrt::check_hresult(gpu.Device()->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf())));
    winrt::check_hresult(dxgi->GetAdapter(adapter.GetAddressOf()));
    winrt::check_hresult(adapter->GetDesc(&desc));
    std::wcout << L"Adapter: " << desc.Description << L'\n';
    constexpr UINT width = 3840, height = 2160;
    const auto pixels = Pattern(width, height);
    auto source = Texture(gpu, width, height, D3D11_BIND_SHADER_RESOURCE, &pixels);
    auto output = Texture(gpu, width, height, D3D11_BIND_RENDER_TARGET);
    graphics::CapturedFrame frame{}; frame.texture = source; frame.size = {width, height};
    graphics::RenderEngine engine(gpu); Access::Initialize(engine, output.Get(), width, height);
    auto original = Access::Reference(engine); auto optimized = Access::Shader(engine);
    const std::array<std::pair<const char*, core::EffectSettings>, 3> cases{{{"Neutral", {}}, {"CRT + tint", Crt()}, {"Heavy", Heavy()}}};
    std::cout << "3840x2160 offscreen GPU pass; median of 5 alternating pairs, 48 draws per sample.\n";
    std::cout << "Scenario,reference_ms,optimized_ms,reduction_percent\n" << std::fixed << std::setprecision(4);
    for (const auto& [name, effects] : cases) {
        std::array<double, 5> before{}, after{};
        for (size_t i = 0; i < before.size(); ++i) {
            if (i % 2 == 0) {
                before[i] = Measure(gpu, engine, frame, effects, original.Get());
                after[i] = Measure(gpu, engine, frame, effects, optimized.Get());
            } else {
                after[i] = Measure(gpu, engine, frame, effects, optimized.Get());
                before[i] = Measure(gpu, engine, frame, effects, original.Get());
            }
        }
        std::sort(before.begin(), before.end()); std::sort(after.begin(), after.end());
        std::cout << name << ',' << before[2] << ',' << after[2] << ',' << (1 - after[2] / before[2]) * 100 << '\n';
    }
    engine.Shutdown();
}
int main(int argc, char** argv) {
    try {
        const bool benchmark = argc == 2 && std::string_view(argv[1]) == "--benchmark";
        Require(argc == 1 || benchmark, "Usage: screenfx_performance_tests [--benchmark]");
        graphics::D3D11Context gpu;
        Require(gpu.Initialize(benchmark ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP), "D3D11 initialization failed");
        Verify(gpu);
        if (benchmark) Benchmark(gpu);
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::wcerr << error.message().c_str() << '\n'; return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
