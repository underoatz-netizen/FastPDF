#pragma once

#include <cstdint>
#include <vector>
#include <fastpdf/renderer/bitmap.h>
#include "ScreenshotGeometry.h"

namespace fastpdf::app::screenshot {

// Source page placement in viewport coordinates along with its CPU bitmap.
struct PageSource {
    int pageIndex = 0;
    // Page rect in viewport pixel coordinates
    int vx = 0;
    int vy = 0;
    int vw = 0;
    int vh = 0;
    const fastpdf::renderer::Bitmap* bitmap = nullptr;
};

// Composites the selected rectangle from multiple PageSources onto an offscreen
// target Bitmap initialized to solid white (RGB 255, 255, 255, A 255).
//
// Empty/not-ready areas (gaps between pages, margin around pages, or unrendered pages)
// remain clean solid white.
// If selection is empty or width/height <= 0, returns an empty Bitmap.
fastpdf::renderer::Bitmap CompositeSelection(
    const Rect& selection,
    const std::vector<PageSource>& pages) noexcept;

} // namespace fastpdf::app::screenshot
