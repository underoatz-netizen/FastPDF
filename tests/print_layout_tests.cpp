#include "fastpdf/core/print_layout.h"
#include "test_harness.h"

#include <cmath>
#include <vector>

using namespace fastpdf::core::print;

// Test: ResolveOrientation
FASTPDF_TEST(PrintLayout_ResolveOrientation) {
    // Explicit requested portrait/landscape
    FASTPDF_CHECK(PrintLayout::ResolveOrientation(PrintOrientation::Portrait, 800.0, 600.0) == PrintOrientation::Portrait);
    FASTPDF_CHECK(PrintLayout::ResolveOrientation(PrintOrientation::Landscape, 600.0, 800.0) == PrintOrientation::Landscape);

    // Auto orientation: page wider than tall -> Landscape
    FASTPDF_CHECK(PrintLayout::ResolveOrientation(PrintOrientation::Auto, 842.0, 595.0) == PrintOrientation::Landscape);
    // Auto orientation: page taller than wide -> Portrait
    FASTPDF_CHECK(PrintLayout::ResolveOrientation(PrintOrientation::Auto, 595.0, 842.0) == PrintOrientation::Portrait);
    // Auto orientation: square -> Portrait
    FASTPDF_CHECK(PrintLayout::ResolveOrientation(PrintOrientation::Auto, 500.0, 500.0) == PrintOrientation::Portrait);
}

// Test: ParsePrintPageRange
FASTPDF_TEST(PrintLayout_ParsePrintPageRange) {
    std::vector<int> pages;

    // Single page
    FASTPDF_CHECK(ParsePrintPageRange(L"1", 5, pages));
    FASTPDF_CHECK_EQ(1, static_cast<int>(pages.size()));
    FASTPDF_CHECK_EQ(0, pages[0]);

    // Range "1-3, 5"
    FASTPDF_CHECK(ParsePrintPageRange(L"1-3, 5", 5, pages));
    FASTPDF_CHECK_EQ(4, static_cast<int>(pages.size()));
    FASTPDF_CHECK_EQ(0, pages[0]);
    FASTPDF_CHECK_EQ(1, pages[1]);
    FASTPDF_CHECK_EQ(2, pages[2]);
    FASTPDF_CHECK_EQ(4, pages[3]);

    // Deduplication and ordering: "4, 2-3, 2, 1"
    FASTPDF_CHECK(ParsePrintPageRange(L"4, 2-3, 2, 1", 5, pages));
    FASTPDF_CHECK_EQ(4, static_cast<int>(pages.size()));
    FASTPDF_CHECK_EQ(0, pages[0]);
    FASTPDF_CHECK_EQ(1, pages[1]);
    FASTPDF_CHECK_EQ(2, pages[2]);
    FASTPDF_CHECK_EQ(3, pages[3]);

    // Clamping beyond totalPages
    FASTPDF_CHECK(ParsePrintPageRange(L"3-10", 4, pages));
    FASTPDF_CHECK_EQ(2, static_cast<int>(pages.size()));
    FASTPDF_CHECK_EQ(2, pages[0]); // page 3 -> 2
    FASTPDF_CHECK_EQ(3, pages[1]); // page 4 -> 3

    // Invalid / empty
    FASTPDF_CHECK(!ParsePrintPageRange(L"", 5, pages));
    FASTPDF_CHECK(!ParsePrintPageRange(L"abc", 5, pages));
    FASTPDF_CHECK(!ParsePrintPageRange(L"10-20", 5, pages));
    FASTPDF_CHECK(!ParsePrintPageRange(L"0", 5, pages));
}

// Test: ResolvePagesToPrint
FASTPDF_TEST(PrintLayout_ResolvePagesToPrint) {
    std::vector<int> pages;

    // All pages
    FASTPDF_CHECK(ResolvePagesToPrint(PageSelectionMode::All, 1, L"", 3, pages));
    FASTPDF_CHECK_EQ(3, static_cast<int>(pages.size()));
    FASTPDF_CHECK_EQ(0, pages[0]);
    FASTPDF_CHECK_EQ(1, pages[1]);
    FASTPDF_CHECK_EQ(2, pages[2]);

    // Current page
    FASTPDF_CHECK(ResolvePagesToPrint(PageSelectionMode::Current, 2, L"", 5, pages));
    FASTPDF_CHECK_EQ(1, static_cast<int>(pages.size()));
    FASTPDF_CHECK_EQ(2, pages[0]);

    // Custom
    FASTPDF_CHECK(ResolvePagesToPrint(PageSelectionMode::Custom, 0, L"2, 4", 5, pages));
    FASTPDF_CHECK_EQ(2, static_cast<int>(pages.size()));
    FASTPDF_CHECK_EQ(1, pages[0]);
    FASTPDF_CHECK_EQ(3, pages[1]);
}

// Test: ValidateCopies
FASTPDF_TEST(PrintLayout_ValidateCopies) {
    FASTPDF_CHECK_EQ(1, ValidateCopies(0));
    FASTPDF_CHECK_EQ(1, ValidateCopies(-5));
    FASTPDF_CHECK_EQ(1, ValidateCopies(1));
    FASTPDF_CHECK_EQ(5, ValidateCopies(5));
    FASTPDF_CHECK_EQ(999, ValidateCopies(1000));
}

// Test: ComputePlacement Fit mode
FASTPDF_TEST(PrintLayout_ComputePlacement_Fit) {
    TargetMetrics metrics{};
    metrics.dpiX = 300;
    metrics.dpiY = 300;
    // Printable area 2400 x 3400
    metrics.printableWidth = 2400;
    metrics.printableHeight = 3400;
    metrics.physicalWidth = 2480;
    metrics.physicalHeight = 3508;
    metrics.hardwareMarginLeft = 40;
    metrics.hardwareMarginTop = 54;

    // A4 PDF (595.276 x 841.89 points) at 300 DPI:
    // Natural pixels = 595.276 * 300 / 72 ≈ 2480 x 3508
    const double a4PtW = 595.276;
    const double a4PtH = 841.890;

    auto placement = PrintLayout::ComputePlacement(
        a4PtW, a4PtH, metrics, PrintScaleMode::Fit, PrintOrientation::Portrait);

    FASTPDF_CHECK(!placement.willCrop);
    FASTPDF_CHECK(placement.destWidth > 0);
    FASTPDF_CHECK(placement.destHeight > 0);
    FASTPDF_CHECK(placement.destWidth <= metrics.printableWidth);
    FASTPDF_CHECK(placement.destHeight <= metrics.printableHeight);
    FASTPDF_CHECK(placement.destX >= 0);
    FASTPDF_CHECK(placement.destY >= 0);
    FASTPDF_CHECK(placement.scale < 1.0); // scaled down slightly to fit within hardware margins
}

// Test: ComputePlacement ActualSize mode and Crop detection
FASTPDF_TEST(PrintLayout_ComputePlacement_ActualSize_Crop) {
    TargetMetrics metrics{};
    metrics.dpiX = 300;
    metrics.dpiY = 300;
    metrics.printableWidth = 2400;
    metrics.printableHeight = 3400;
    metrics.physicalWidth = 2480;
    metrics.physicalHeight = 3508;

    // An A4 page at 100% scale is 2480 x 3508 px at 300 DPI,
    // which exceeds printable area (2400 x 3400) -> must flag willCrop = true
    const double a4PtW = 595.276;
    const double a4PtH = 841.890;

    auto placement = PrintLayout::ComputePlacement(
        a4PtW, a4PtH, metrics, PrintScaleMode::ActualSize, PrintOrientation::Portrait);

    FASTPDF_CHECK(placement.willCrop);
    FASTPDF_CHECK(placement.cropRatio > 0.0);
    FASTPDF_CHECK(placement.scale == 1.0);
    // Page dimensions at 100%
    FASTPDF_CHECK_EQ(static_cast<int>(std::round(a4PtW * 300.0 / 72.0)), placement.destWidth);
    FASTPDF_CHECK_EQ(static_cast<int>(std::round(a4PtH * 300.0 / 72.0)), placement.destHeight);

    // A small page (e.g. 200 x 200 pt) fits completely -> willCrop = false
    auto smallPlacement = PrintLayout::ComputePlacement(
        200.0, 200.0, metrics, PrintScaleMode::ActualSize, PrintOrientation::Portrait);

    FASTPDF_CHECK(!smallPlacement.willCrop);
    FASTPDF_CHECK(smallPlacement.cropRatio == 0.0);
    FASTPDF_CHECK(smallPlacement.scale == 1.0);
    FASTPDF_CHECK(smallPlacement.destWidth < metrics.printableWidth);
    FASTPDF_CHECK(smallPlacement.destHeight < metrics.printableHeight);
}

// Test: Landscape Auto-rotation placement
FASTPDF_TEST(PrintLayout_ComputePlacement_LandscapeAuto) {
    TargetMetrics metrics{};
    metrics.dpiX = 300;
    metrics.dpiY = 300;
    // Portrait paper printable area
    metrics.printableWidth = 2400;
    metrics.printableHeight = 3400;
    metrics.physicalWidth = 2480;
    metrics.physicalHeight = 3508;

    // Landscape page: 800 pt wide x 400 pt tall
    auto placement = PrintLayout::ComputePlacement(
        800.0, 400.0, metrics, PrintScaleMode::Fit, PrintOrientation::Auto);

    FASTPDF_CHECK(placement.effectiveOrientation == PrintOrientation::Landscape);
    FASTPDF_CHECK(!placement.willCrop);
    FASTPDF_CHECK(placement.destWidth <= metrics.printableWidth);
    FASTPDF_CHECK(placement.destHeight <= metrics.printableHeight);
}

int main() {
    return fastpdf::test::RunAll();
}
