#pragma once

#include <windows.h>
#include <fastpdf/renderer/bitmap.h>
#include <string>
#include <vector>

namespace fastpdf::app::screenshot {

// Encodes a BGRA (or BGRx) Bitmap to PNG in memory using WIC (Windows Imaging Component).
// Returns an empty vector on failure.
std::vector<std::uint8_t> EncodePngWithWic(const fastpdf::renderer::Bitmap& bitmap) noexcept;

// Decodes a PNG stream/buffer into width, height, and BGRA pixels using WIC.
// Useful for round-trip testing and verification.
bool DecodePngWithWic(const std::vector<std::uint8_t>& pngBytes,
                     int& outWidth, int& outHeight,
                     std::vector<std::uint8_t>& outBgra) noexcept;

// Saves a BGRA Bitmap as a PNG file at |filePath| using WIC.
bool SavePngToFile(const fastpdf::renderer::Bitmap& bitmap, const std::wstring& filePath) noexcept;

// Copies a BGRA Bitmap to the Windows clipboard as CF_DIBV5 and, where practical,
// as "PNG" registered clipboard format.
// |hwndOwner| is the window owning the clipboard operation.
// Returns true if at least one format was successfully placed on the clipboard.
bool CopyBitmapToClipboard(HWND hwndOwner, const fastpdf::renderer::Bitmap& bitmap) noexcept;

} // namespace fastpdf::app::screenshot
