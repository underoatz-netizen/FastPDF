#include "ScreenshotCompositor.h"
#include <algorithm>
#include <cstring>

namespace fastpdf::app::screenshot {

fastpdf::renderer::Bitmap CompositeSelection(
    const Rect& selection,
    const std::vector<PageSource>& pages) noexcept {

    const int targetW = selection.width();
    const int targetH = selection.height();
    if (targetW <= 0 || targetH <= 0) {
        return fastpdf::renderer::Bitmap{};
    }

    // Safety checks against overflow or absurdly large dimensions
    constexpr int kMaxDimension = 16384;
    if (targetW > kMaxDimension || targetH > kMaxDimension) {
        return fastpdf::renderer::Bitmap{};
    }

    const int stride = targetW * 4;
    const std::size_t totalBytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(targetH);
    if (totalBytes / static_cast<std::size_t>(stride) != static_cast<std::size_t>(targetH)) {
        return fastpdf::renderer::Bitmap{};
    }

    fastpdf::renderer::Bitmap target;
    target.width = targetW;
    target.height = targetH;
    target.stride = stride;
    try {
        // Initialize entire target area to solid white (BGRA: 255, 255, 255, 255).
        target.data.assign(totalBytes, 255);
    } catch (...) {
        return fastpdf::renderer::Bitmap{};
    }

    // Copy overlapping pixels from each available page bitmap
    for (const auto& page : pages) {
        if (page.bitmap == nullptr || page.bitmap->empty() ||
            page.bitmap->width <= 0 || page.bitmap->height <= 0 ||
            page.vw <= 0 || page.vh <= 0) {
            continue;
        }

        const Rect pageRect{page.vx, page.vy, page.vx + page.vw, page.vy + page.vh};
        const Rect overlap = IntersectRects(selection, pageRect);
        if (overlap.empty()) {
            continue;
        }

        // Overlap coordinates relative to page origin in viewport
        // and relative to target origin (selection.left, selection.top).
        const int pageOverLeft = overlap.left - page.vx;
        const int pageOverTop = overlap.top - page.vy;
        const int overlapW = overlap.width();
        const int overlapH = overlap.height();

        const int targetOverLeft = overlap.left - selection.left;
        const int targetOverTop = overlap.top - selection.top;

        const double scaleX = static_cast<double>(page.bitmap->width) / static_cast<double>(page.vw);
        const double scaleY = static_cast<double>(page.bitmap->height) / static_cast<double>(page.vh);

        const bool is1to1 = (page.bitmap->width == page.vw && page.bitmap->height == page.vh);

        for (int row = 0; row < overlapH; ++row) {
            const int targetY = targetOverTop + row;
            if (targetY < 0 || targetY >= targetH) {
                continue;
            }
            std::uint8_t* dstRow = target.data.data() + static_cast<std::size_t>(targetY) * stride;

            if (is1to1) {
                const int srcY = pageOverTop + row;
                if (srcY < 0 || srcY >= page.bitmap->height) {
                    continue;
                }
                const std::uint8_t* srcRow = page.bitmap->data.data() + static_cast<std::size_t>(srcY) * page.bitmap->stride;
                const int srcOffsetBytes = (pageOverLeft) * 4;
                const int dstOffsetBytes = (targetOverLeft) * 4;
                const int copyBytes = overlapW * 4;
                if (srcOffsetBytes + copyBytes <= page.bitmap->stride &&
                    dstOffsetBytes + copyBytes <= stride) {
                    std::memcpy(dstRow + dstOffsetBytes, srcRow + srcOffsetBytes, copyBytes);
                }
            } else {
                // Scaled sampling (e.g. preview bitmap or display scaling differences)
                const int srcY = std::clamp(static_cast<int>((pageOverTop + row) * scaleY), 0, page.bitmap->height - 1);
                const std::uint8_t* srcRow = page.bitmap->data.data() + static_cast<std::size_t>(srcY) * page.bitmap->stride;

                for (int col = 0; col < overlapW; ++col) {
                    const int targetX = targetOverLeft + col;
                    if (targetX < 0 || targetX >= targetW) {
                        continue;
                    }
                    const int srcX = std::clamp(static_cast<int>((pageOverLeft + col) * scaleX), 0, page.bitmap->width - 1);

                    const std::uint8_t* srcPixel = srcRow + srcX * 4;
                    std::uint8_t* dstPixel = dstRow + targetX * 4;

                    dstPixel[0] = srcPixel[0]; // B
                    dstPixel[1] = srcPixel[1]; // G
                    dstPixel[2] = srcPixel[2]; // R
                    dstPixel[3] = 255;         // A (opaque document content)
                }
            }
        }
    }

    return target;
}

} // namespace fastpdf::app::screenshot
