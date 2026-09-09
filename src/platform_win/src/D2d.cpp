#include "fastpdf/platform/win/D2d.h"

namespace fastpdf::platform::win::d2d {

HRESULT CreateFactory(Microsoft::WRL::ComPtr<ID2D1Factory>& factory) noexcept {
    return D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                             IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
}

HRESULT CreateHwndRenderTarget(ID2D1Factory& factory, HWND hwnd, const D2D1_SIZE_U& size,
                               Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget>& target) noexcept {
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
        D2D1::HwndRenderTargetProperties(hwnd, size);
    return factory.CreateHwndRenderTarget(props, hwndProps,
                                          target.ReleaseAndGetAddressOf());
}

HRESULT CreateDWriteFactory(Microsoft::WRL::ComPtr<IDWriteFactory>& factory) noexcept {
    return DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                               reinterpret_cast<IUnknown**>(factory.ReleaseAndGetAddressOf()));
}

} // namespace fastpdf::platform::win::d2d