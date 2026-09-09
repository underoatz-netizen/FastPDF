#include "fastpdf/platform/win/Dpi.h"

namespace fastpdf::platform::win {

UINT GetWindowDpi(HWND hwnd) noexcept {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi != 0 ? dpi : GetDpiForSystem();
}

float DpiScaleFactor(UINT dpi) noexcept {
    return static_cast<float>(dpi) / 96.0f;
}

int ScaleForDpi(int value, UINT dpi) noexcept {
    return static_cast<int>((static_cast<float>(value) * DpiScaleFactor(dpi)) + 0.5f);
}

} // namespace fastpdf::platform::win