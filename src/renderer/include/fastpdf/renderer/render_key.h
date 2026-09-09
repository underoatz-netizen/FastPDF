#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

namespace fastpdf::renderer {

// Render quality tier. A Preview render is produced at half the target pixel
// dimensions (minimum 1) so it rasterizes quickly while the user is actively
// scrolling/zooming; a Final render is produced at the full target dimensions
// once the view has settled. Both are drawn into the same laid-out page rect,
// so promoting Preview -> Final never changes page geometry or scroll.
enum class Quality { Preview, Final };

// Scheduling priority. Lower value = higher priority. The bounded scheduler
// runs higher-priority jobs first so visible-page renders are never starved by
// adjacent pre-renders.
enum class RenderPriority {
    Visible = 0,    // current + immediately visible pages
    Adjacent = 1,   // pre-rendered neighbor pages
    Background = 2, // low-priority pre-render
};

// Uniquely identifies one render result. A change in any field means the
// pixels change, so the cache and the scheduler use this as their identity.
// Includes the document epoch so pixels from a closed/replaced document can
// never be reused, plus page index, exact output pixel dimensions, rotation,
// and quality tier.
struct RenderKey {
    std::uint64_t docEpoch = 0;
    int pageIndex = 0;
    int width = 0;    // exact output pixel width
    int height = 0;   // exact output pixel height
    int rotation = 0; // 0..3 (90-degree clockwise steps)
    Quality quality = Quality::Final;

    auto operator<=>(const RenderKey&) const = default;
};

// Hard cap on a rendered bitmap's width/height. Prevents pixel-dimension
// overflow and unbounded memory: any requested dimension is clamped into
// [1, kMaxRenderDimension] (aspect-preserving) before rendering.
inline constexpr int kMaxRenderDimension = 8192;

struct PixelSize {
    int width = 0;
    int height = 0;
};

// Clamps a requested pixel size into [1, kMaxRenderDimension] per axis while
// preserving aspect ratio. Returns at least 1x1.
PixelSize ClampPixelSize(int width, int height) noexcept;

// The preview render size for a given final target size: each dimension halved
// (minimum 1), then clamped to the hard bounds. Aspect ratio is preserved.
PixelSize PreviewSizeFor(int width, int height) noexcept;

// Hash for RenderKey (usable with std::unordered_map).
struct RenderKeyHash {
    std::size_t operator()(const RenderKey& key) const noexcept;
};

}  // namespace fastpdf::renderer
