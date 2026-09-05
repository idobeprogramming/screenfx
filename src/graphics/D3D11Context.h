#pragma once

#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <mutex>

namespace screenfx::graphics {

class D3D11Context {
public:
    bool Initialize();

    ID3D11Device* Device() const noexcept { return device_.Get(); }
    ID3D11DeviceContext* ImmediateContext() const noexcept { return context_.Get(); }
    IDXGIFactory2* Factory() const noexcept { return factory_.Get(); }
    IDXGIFactory5* Factory5() const noexcept { return factory5_.Get(); }
    bool TearingSupported() const noexcept { return tearingSupported_; }
    std::mutex& Mutex() noexcept { return mutex_; }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5_;
    bool tearingSupported_ = false;
    std::mutex mutex_;
};

} // namespace screenfx::graphics
