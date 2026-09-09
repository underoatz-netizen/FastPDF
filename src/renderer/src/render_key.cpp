#include "fastpdf/renderer/render_key.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace fastpdf::renderer {

PixelSize ClampPixelSize(int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return {1, 1};
    }
    int w = width;
    int h = height;
    if (w > kMaxRenderDimension || h > kMaxRenderDimension) {
        const double scale = std::min(
            static_cast<double>(kMaxRenderDimension) / static_cast<double>(w),
            static_cast<double>(kMaxRenderDimension) / static_cast<double>(h));
        w = std::max(1, static_cast<int>(std::lround(static_cast<double>(w) * scale)));
        h = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * scale)));
    }
    return {std::clamp(w, 1, kMaxRenderDimension),
            std::clamp(h, 1, kMaxRenderDimension)};
}

PixelSize PreviewSizeFor(int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return {1, 1};
    }
    return ClampPixelSize(std::max(1, width / 2), std::max(1, height / 2));
}

namespace {

std::size_t CombineHash(std::size_t seed, std::size_t value) noexcept {
    // SplitMix-style mixing keeps the hash well-distributed and deterministic
    // across runs (std::hash<std::size_t> is not guaranteed deterministic).
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

}  // namespace

std::size_t RenderKeyHash::operator()(const RenderKey& key) const noexcept {
    std::size_t h = std::hash<std::uint64_t>{}(key.docEpoch);
    h = CombineHash(h, std::hash<int>{}(key.pageIndex));
    h = CombineHash(h, std::hash<int>{}(key.width));
    h = CombineHash(h, std::hash<int>{}(key.height));
    h = CombineHash(h, std::hash<int>{}(key.rotation));
    h = CombineHash(h, std::hash<int>{}(static_cast<int>(key.quality)));
    return h;
}

}  // namespace fastpdf::renderer
