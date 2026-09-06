#include "D3D11Context.h"

#include <array>
#include <d3d11_4.h>

namespace screenfx::graphics {

void D3D11Context::Shutdown() {
    std::lock_guard lock(mutex_);
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    factory5_.Reset();
    factory_.Reset();
    context_.Reset();
    device_.Reset();
    tearingSupported_ = false;
}

bool D3D11Context::Initialize(D3D_DRIVER_TYPE driver) {
    Shutdown();
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr std::array<D3D_FEATURE_LEVEL, 2> featureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selectedLevel{};
    HRESULT result = D3D11CreateDevice(
        nullptr,
        driver,
        nullptr,
        flags,
        featureLevels.data(),
        static_cast<UINT>(featureLevels.size()),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &selectedLevel,
        context.GetAddressOf());
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDevice(nullptr, driver, nullptr, flags, featureLevels.data(),
                                  static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                  device.ReleaseAndGetAddressOf(), &selectedLevel, context.ReleaseAndGetAddressOf());
    }
    lastError_ = result;
    if (FAILED(result)) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
    lastError_ = context.As(&multithread);
    if (FAILED(lastError_)) return false;
    multithread->SetMultithreadProtected(TRUE);

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    result = device.As(&dxgiDevice);
    if (SUCCEEDED(result)) result = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (SUCCEEDED(result)) result = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    lastError_ = result;
    if (FAILED(result)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory.As(&factory5))) {
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
    }

    device_ = std::move(device);
    context_ = std::move(context);
    factory_ = std::move(factory);
    factory5_ = std::move(factory5);
    tearingSupported_ = tearing == TRUE;
    return true;
}

} // namespace screenfx::graphics
