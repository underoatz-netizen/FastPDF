#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fastpdf::renderer {

// Immutable CPU-side BGRA bitmap value (top-down BGRx, matching PDFium's
// FPDFBitmap_BGRx output and Direct2D's DXGI_FORMAT_B8G8R8A8_UNORM). A plain
// value type (owns its pixel bytes) so it can safely cross thread boundaries:
// the render worker produces it and the UI thread consumes it. Lives in
// fastpdf_renderer (a PDFium-free boundary) so the render cache can store
// bitmaps without depending on PDFium.
struct Bitmap {
    int width = 0;
    int height = 0;
    int stride = 0;  // bytes per row (>= width * 4)
    std::vector<std::uint8_t> data;

    bool empty() const noexcept { return data.empty(); }

    // Number of bytes the pixel buffer occupies. Returns 0 for an empty or
    // invalid bitmap. Overflow-safe: a bitmap whose declared dimensions would
    // overflow size_t reports 0 (callers treat that as unaccountable).
    std::size_t sizeBytes() const noexcept;
};

}  // namespace fastpdf::renderer
