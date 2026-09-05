#include "D3D11Context.h"

#include <array>

namespace screenfx::graphics {

bool D3D11Context::Initialize() {
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
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels.data(),
        static_cast<UINT>(featureLevels.size()),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &selectedLevel,
        context.GetAddressOf());
    if (FAILED(result)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    result = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()));
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
