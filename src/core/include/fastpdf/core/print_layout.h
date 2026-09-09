#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fastpdf::core::print {

enum class PrintScaleMode {
    Fit,        // Fit page proportionally into printable area
    ActualSize  // 100% scale (1 pt = 1/72 inch on device)
};

enum class PrintOrientation {
    Auto,      // Chooses portrait vs landscape based on page aspect ratio
    Portrait,
    Landscape
};

enum class PageSelectionMode {
    All,
    Current,
    Custom
};

// Physical dimensions and hardware margins of paper/printer target in device coordinates (pixels)
struct TargetMetrics {
    int dpiX = 300;
    int dpiY = 300;
    int physicalWidth = 2480;       // Total paper width in dots (PHYSICALWIDTH)
    int physicalHeight = 3508;      // Total paper height in dots (PHYSICALHEIGHT)
    int printableWidth = 2400;      // Printable area width (HORZRES)
    int printableHeight = 3400;     // Printable area height (VERTRES)
    int hardwareMarginLeft = 40;    // Hardware margin left (PHYSICALOFFSETX)
    int hardwareMarginTop = 54;     // Hardware margin top (PHYSICALOFFSETY)
};

// Page layout result computed by PrintLayout for a single page
struct PagePlacement {
    // Positioning in device printable coordinates (relative to printable area top-left)
    int destX = 0;
    int destY = 0;
    int destWidth = 0;
    int destHeight = 0;

    // Effective orientation determined for this page (Portrait or Landscape)
    PrintOrientation effectiveOrientation = PrintOrientation::Portrait;

    // Effective scale factor applied (1.0 = actual size)
    double scale = 1.0;

    // Crop warning: true if ActualSize exceeds printable bounds (would be cropped)
    bool willCrop = false;
    double cropRatio = 0.0; // max(0, 1.0 - printableArea / pageSize) if cropped
};

// Pure layout computer for print & print-preview
class PrintLayout {
public:
    // Determines effective page orientation based on user choice & page dimensions
    static PrintOrientation ResolveOrientation(
        PrintOrientation requested,
        double pageWidthPoints,
        double pageHeightPoints) noexcept;

    // Computes layout placement for a single page onto the target device/paper
    static PagePlacement ComputePlacement(
        double pageWidthPoints,
        double pageHeightPoints,
        const TargetMetrics& metrics,
        PrintScaleMode scaleMode,
        PrintOrientation orientation) noexcept;
};

// Parses 1-based page range string like "1-3, 5, 7" into sorted, deduplicated 0-based page indices
bool ParsePrintPageRange(std::wstring_view text, int totalPages,
                         std::vector<int>& outPageIndices) noexcept;

// Resolves 0-based page indices to print based on mode, current page, and custom text
bool ResolvePagesToPrint(PageSelectionMode mode, int currentPage,
                         std::wstring_view customRangeText, int totalPages,
                         std::vector<int>& outPageIndices) noexcept;

// Validates copies count (must be >= 1, clamped to max safe 999)
inline int ValidateCopies(int copies) noexcept {
    if (copies < 1) return 1;
    if (copies > 999) return 999;
    return copies;
}

} // namespace fastpdf::core::print
