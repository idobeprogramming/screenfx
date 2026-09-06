#include "graphics/RenderEngine.h"
#include "graphics/OverlayWindow.h"
#include "platform/Monitors.h"
#include <d3d11sdklayers.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <string_view>

using namespace screenfx;
using Microsoft::WRL::ComPtr;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

namespace screenfx::graphics {
struct RenderEngineTestAccess {
    static bool Initialize(RenderEngine& engine, ID3D11Texture2D* output, UINT width, UINT height) {
        engine.bounds_ = RECT{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        return engine.CreateShaders() && SUCCEEDED(engine.graphics_.Device()->CreateRenderTargetView(
            output, nullptr, engine.renderTarget_.GetAddressOf()));
    }
    static bool Draw(RenderEngine& engine, const CapturedFrame& frame, const core::EffectSettings& effects) {
        return engine.DrawFrame(frame, effects);
    }
};
}

ComPtr<ID3D11Texture2D> Texture(graphics::D3D11Context& device, UINT width, UINT height,
                              UINT bindFlags, const std::vector<unsigned char>* pixels = nullptr) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = bindFlags;
    D3D11_SUBRESOURCE_DATA data{};
    if (pixels) { data.pSysMem = pixels->data(); data.SysMemPitch = width * 4; }
    ComPtr<ID3D11Texture2D> result;
    winrt::check_hresult(device.Device()->CreateTexture2D(&desc, pixels ? &data : nullptr, result.GetAddressOf()));
    return result;
}

std::vector<unsigned char> ReadPixels(graphics::D3D11Context& device, ID3D11Texture2D* texture) {
    std::lock_guard lock(device.Mutex());
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0; desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    winrt::check_hresult(device.Device()->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
    device.ImmediateContext()->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(device.ImmediateContext()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    std::vector<unsigned char> result(desc.Width * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(result.data() + y * desc.Width * 4, static_cast<unsigned char*>(mapped.pData) + y * mapped.RowPitch, desc.Width * 4);
    device.ImmediateContext()->Unmap(staging.Get(), 0);
    return result;
}

void TestShaderPixels() {
    graphics::D3D11Context device;
    Require(device.Initialize(D3D_DRIVER_TYPE_WARP), "D3D11 WARP initialization failed");
    constexpr UINT width = 32, height = 24;
    std::vector<unsigned char> pixels(width * height * 4);
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        const auto i = (y * width + x) * 4;
        pixels[i] = static_cast<unsigned char>(30 + x * 6);
        pixels[i + 1] = static_cast<unsigned char>(40 + y * 7);
        pixels[i + 2] = static_cast<unsigned char>(200 - x * 4);
        pixels[i + 3] = 255;
    }
    auto source = Texture(device, width, height, D3D11_BIND_SHADER_RESOURCE, &pixels);
    auto output = Texture(device, width, height, D3D11_BIND_RENDER_TARGET);
    graphics::RenderEngine engine(device);
    Require(graphics::RenderEngineTestAccess::Initialize(engine, output.Get(), width, height), "Shaders/RTV creation failed");
    graphics::CapturedFrame frame{}; frame.texture = source; frame.size = SIZE{width, height};
    core::EffectSettings effects{};
    Require(graphics::RenderEngineTestAccess::Draw(engine, frame, effects), "Identity draw failed");
    auto identity = ReadPixels(device, output.Get());
    for (size_t i = 0; i < pixels.size(); ++i)
        Require(std::abs(int(identity[i]) - int(pixels[i])) <= 1, "Identity shader changed pixel or rendered black/flipped");
    effects.scanlineIntensity = 1; effects.phosphorIntensity = 1;
    Require(graphics::RenderEngineTestAccess::Draw(engine, frame, effects), "CRT draw failed");
    auto crt = ReadPixels(device, output.Get());
    Require(crt != identity, "CRT effect has no effect");
    size_t darkened = 0;
    for (size_t i = 0; i < crt.size(); ++i) if (i % 4 != 3 && crt[i] < identity[i]) ++darkened;
    Require(darkened > width * height, "CRT scanlines/mask did not darken expected pixels");
    effects.globalIntensity = 0;
    Require(graphics::RenderEngineTestAccess::Draw(engine, frame, effects), "Bypass draw failed");
    Require(ReadPixels(device, output.Get()) == identity, "Global intensity zero did not bypass effects");
    effects = {}; effects.bloomIntensity = 1; effects.bloomThreshold = 1;
    Require(graphics::RenderEngineTestAccess::Draw(engine, frame, effects), "Bloom threshold boundary draw failed");
    Require(ReadPixels(device, output.Get()) == identity, "Bloom threshold one produced invalid color");
    frame.texture.Reset();
    Require(!graphics::RenderEngineTestAccess::Draw(engine, frame, effects), "Null source accepted");
    engine.Shutdown();
    ComPtr<ID3D11InfoQueue> info;
    if (SUCCEEDED(device.Device()->QueryInterface(IID_PPV_ARGS(info.GetAddressOf())))) {
        for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
            SIZE_T bytes = 0; info->GetMessage(i, nullptr, &bytes);
            std::vector<unsigned char> storage(bytes);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            winrt::check_hresult(info->GetMessage(i, message, &bytes));
            if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) {
                std::cerr << message->pDescription << '\n';
                throw std::runtime_error("D3D11 validation error");
            }
        }
    }
    std::cout << "PASS: shader identity, CRT pixels, bypass, bloom boundary, invalid source, D3D11 validation\n";
}

// Opt-in real desktop test: no recording; textures stay in memory and the overlay stays hidden.
void TestDesktopCapture() {
    auto monitors = platform::EnumerateMonitors();
    Require(!monitors.empty(), "No active monitor");
    graphics::D3D11Context device; Require(device.Initialize(), "Hardware D3D11 failed");
    const auto& monitor = monitors.front();
    const SIZE size{monitor.bounds.right - monitor.bounds.left, monitor.bounds.bottom - monitor.bounds.top};
    graphics::CaptureSession capture(device);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!capture.Start(monitor.handle, size, true)) {
            std::wcerr << capture.LastError() << '\n';
            throw std::runtime_error("Desktop capture startup failed");
        }
        graphics::CapturedFrame frame;
        const auto deadline = GetTickCount64() + 5000;
        while (!capture.TryAcquireLatest(frame) && GetTickCount64() < deadline) {
            WaitForSingleObject(capture.FrameEvent(), 100);
            Require(capture.LastError().empty(), "Desktop frame callback failed");
        }
        Require(frame.texture != nullptr, "No captured frame within five seconds");
        auto pixels = ReadPixels(device, frame.texture.Get());
        bool nonblack = false;
        for (size_t i = 0; i < pixels.size(); ++i) if (i % 4 != 3 && pixels[i] > 8) { nonblack = true; break; }
        Require(nonblack, "Desktop capture contains only black pixels");
        graphics::OverlayWindow overlay; Require(overlay.Create(GetModuleHandleW(nullptr)), "Overlay creation failed");
        Require(overlay.CaptureExcluded(), "Overlay exclusion failed");
        overlay.SetBounds(monitor.bounds);
        graphics::RenderEngine renderer(device);
        Require(renderer.Initialize(overlay.Handle(), monitor.bounds), "Composition renderer initialization failed");
        Require(renderer.Render(frame, {}, core::FramePacingMode::Uncapped), "Composition presentation failed");
        Require(renderer.Resize(RECT{0, 0, 640, 480}), "Resize after presentation failed");
        Require(renderer.Render(frame, {}, core::FramePacingMode::VSync), "VSync presentation after resize failed");
        Require(!overlay.IsVisible(), "Diagnostic unexpectedly covered desktop");
        renderer.Shutdown(); capture.Stop(); overlay.Destroy();
    }
    std::cout << "PASS: desktop capture pixels, composition, resize, uncapped/VSync, 3 restart cycles\n";
}

// Opt-in end-to-end check of a small, temporary color patch. No user input,
// files, or full-screen cover: the fixture and overlay are destroyed on failure.
void TestVisibleComposition() {
    const auto monitors = platform::EnumerateMonitors();
    Require(!monitors.empty(), "No monitor for visible test");
    const auto& monitor = monitors.front();
    constexpr LONG width = 256, height = 192;
    const RECT bounds{monitor.bounds.left + 32, monitor.bounds.top + 32,
                      monitor.bounds.left + 32 + width, monitor.bounds.top + 32 + height};
    struct Backdrop {
        HBRUSH brush = CreateSolidBrush(RGB(30, 180, 70));
        HWND window = nullptr;
        ~Backdrop() {
            if (window) DestroyWindow(window);
            UnregisterClassW(L"ScreenFX.TestBackdrop", GetModuleHandleW(nullptr));
            DeleteObject(brush);
        }
    } backdrop;
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"ScreenFX.TestBackdrop"; cls.hbrBackground = backdrop.brush;
    Require(RegisterClassW(&cls) != 0, "Backdrop class registration failed");
    backdrop.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        cls.lpszClassName, L"ScreenFX graphics test", WS_POPUP, bounds.left, bounds.top, width, height,
        nullptr, nullptr, cls.hInstance, nullptr);
    Require(backdrop.window != nullptr, "Backdrop window failed");
    ShowWindow(backdrop.window, SW_SHOWNOACTIVATE); UpdateWindow(backdrop.window);
    graphics::D3D11Context device; Require(device.Initialize(), "Hardware D3D11 failed");
    graphics::CaptureSession capture(device);
    Require(capture.Start(monitor.handle, SIZE{monitor.bounds.right - monitor.bounds.left,
        monitor.bounds.bottom - monitor.bounds.top}, true), "Visible test capture failed");
    graphics::OverlayWindow overlay; Require(overlay.Create(cls.hInstance), "Visible overlay creation failed");
    overlay.SetBounds(bounds);
    graphics::RenderEngine renderer(device);
    Require(renderer.Initialize(overlay.Handle(), bounds), "Visible renderer initialization failed");
    std::vector<unsigned char> colors(width * height * 4);
    for (size_t i = 0; i < colors.size(); i += 4) {
        colors[i] = 180; colors[i + 1] = 40; colors[i + 2] = 200; colors[i + 3] = 255;
    }
    graphics::CapturedFrame source{};
    source.texture = Texture(device, width, height, D3D11_BIND_SHADER_RESOURCE, &colors);
    source.size = SIZE{width, height};
    Require(renderer.Render(source, {}, core::FramePacingMode::Uncapped), "Visible initial presentation failed");
    Require(SetWindowDisplayAffinity(overlay.Handle(), WDA_NONE) != FALSE, "Test overlay inclusion failed");
    overlay.Show();
    auto waitForColor = [&](int blue, int green, int red) {
        const auto deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            graphics::CapturedFrame frame;
            if (capture.TryAcquireLatest(frame)) {
                const auto pixels = ReadPixels(device, frame.texture.Get());
                const size_t x = 32 + width / 2, y = 32 + height / 2;
                const auto i = (y * frame.size.cx + x) * 4;
                if (i + 2 < pixels.size() && std::abs(int(pixels[i]) - blue) <= 5 &&
                    std::abs(int(pixels[i + 1]) - green) <= 5 && std::abs(int(pixels[i + 2]) - red) <= 5) return true;
            }
            if (!capture.LastError().empty()) return false;
            WaitForSingleObject(capture.FrameEvent(), 50);
        }
        return false;
    };
    Require(waitForColor(180, 40, 200), "Composition is not visibly presenting the expected magenta patch");
    const POINT center{bounds.left + width / 2, bounds.top + height / 2};
    Require(WindowFromPoint(center) == backdrop.window, "Overlay intercepted desktop hit testing");
    Require(SetWindowDisplayAffinity(overlay.Handle(), WDA_EXCLUDEFROMCAPTURE) != FALSE, "Test exclusion failed");
    Require(waitForColor(70, 180, 30), "Excluded overlay did not reveal the underlying green desktop pixels to capture");
    overlay.Hide(); capture.Stop(); renderer.Shutdown();
    std::cout << "PASS: visible composition pixels, click-through hit testing, capture exclusion\n";
}

int main(int argc, char** argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        TestShaderPixels();
        if (argc > 1 && std::string_view(argv[1]) == "--capture") TestDesktopCapture();
        if (argc > 1 && std::string_view(argv[1]) == "--desktop") {
            TestDesktopCapture(); TestVisibleComposition();
        }
        winrt::uninit_apartment();
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::wcerr << L"HRESULT 0x" << std::hex << static_cast<unsigned long>(error.code()) << L": " << error.message().c_str() << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
