#pragma once

#include <objbase.h>  // IID_PPV_ARGS

#include <d2d1.h>
#include <dwrite.h>

#include <wrl/client.h>

namespace fastpdf::platform::win::d2d {

// Creation helpers for Direct2D/DirectWrite. Ownership of the returned
// objects stays with the caller: per the architecture decision, the UI layer
// owns all Direct2D device resources.

// Creates a single-threaded Direct2D factory.
HRESULT CreateFactory(Microsoft::WRL::ComPtr<ID2D1Factory>& factory) noexcept;

// Creates an HWND render target for the given window and pixel size.
HRESULT CreateHwndRenderTarget(ID2D1Factory& factory, HWND hwnd, const D2D1_SIZE_U& size,
                               Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget>& target) noexcept;

// Creates a DirectWrite factory.
HRESULT CreateDWriteFactory(Microsoft::WRL::ComPtr<IDWriteFactory>& factory) noexcept;

} // namespace fastpdf::platform::win::d2d