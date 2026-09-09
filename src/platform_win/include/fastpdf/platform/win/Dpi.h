#pragma once

#include <windows.h>

namespace fastpdf::platform::win {

// DPI of the given window (per-monitor aware). Falls back to the system DPI
// when the window handle is invalid.
UINT GetWindowDpi(HWND hwnd) noexcept;

// Scale factor relative to 96 DPI (1.0 = 100%).
float DpiScaleFactor(UINT dpi) noexcept;

// Scales a raw pixel value by the given DPI scale factor (rounds to nearest).
int ScaleForDpi(int value, UINT dpi) noexcept;

} // namespace fastpdf::platform::win