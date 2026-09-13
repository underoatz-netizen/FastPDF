// Exhaustive unit tests for the pure continuous-page layout and viewport math
// in fastpdf_core (fastpdf/core/layout.h). No PDFium, no Windows, no UI.
//
// Covers: mixed page sizes, prefix offsets / viewport lookup, page
// navigation, Fit Width / Fit Page / 100% / arbitrary zoom (25%-800% clamp),
// and the page anchor (page index, normalized x/y, viewport point) so zoom /
// window resize preserve logical location.

#include <fastpdf/core/layout.h>

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "test_harness.h"

namespace {

using fastpdf::core::layout::CaptureAnchor;
using fastpdf::core::layout::ClampZoomPercent;
using fastpdf::core::layout::ContinuousLayout;
using fastpdf::core::layout::FitMode;
using fastpdf::core::layout::FitPageZoomPercent;
using fastpdf::core::layout::FitWidthZoomPercent;
using fastpdf::core::layout::PageRect;
using fastpdf::core::layout::PageSize;
using fastpdf::core::layout::PixelsPerPoint;
using fastpdf::core::layout::RestoreAnchor;
using fastpdf::core::layout::ScrollTopForPageTop;
using fastpdf::core::layout::ViewAnchor;
using fastpdf::core::layout::ZoomPercentForPixelsPerPoint;

constexpr double kDpi = 96.0;  // 1 point == 1 layout unit at 100%.

bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

// A mixed-size document: portrait, landscape, small, large.
std::vector<PageSize> MixedPages() {
    return {
        {612.0, 792.0},   // US Letter portrait
        {792.0, 612.0},   // US Letter landscape
        {300.0, 400.0},   // small
        {1224.0, 1584.0}, // large (2x letter)
    };
}

// ---------------------------------------------------------------------------
// Zoom math
// ---------------------------------------------------------------------------

FASTPDF_TEST(zoom_clamp_lower_bound) {
    FASTPDF_CHECK(Near(ClampZoomPercent(10.0), 25.0));
    FASTPDF_CHECK(Near(ClampZoomPercent(0.0), 25.0));
    FASTPDF_CHECK(Near(ClampZoomPercent(-50.0), 25.0));
}

FASTPDF_TEST(zoom_clamp_upper_bound) {
    FASTPDF_CHECK(Near(ClampZoomPercent(900.0), 800.0));
    FASTPDF_CHECK(Near(ClampZoomPercent(10000.0), 800.0));
}

FASTPDF_TEST(zoom_clamp_keeps_in_range_values) {
    FASTPDF_CHECK(Near(ClampZoomPercent(25.0), 25.0));
    FASTPDF_CHECK(Near(ClampZoomPercent(100.0), 100.0));
    FASTPDF_CHECK(Near(ClampZoomPercent(137.5), 137.5));
    FASTPDF_CHECK(Near(ClampZoomPercent(800.0), 800.0));
}

FASTPDF_TEST(pixels_per_point_at_100_percent_is_dpi_over_72) {
    FASTPDF_CHECK(Near(PixelsPerPoint(100.0, 96.0), 96.0 / 72.0));
    FASTPDF_CHECK(Near(PixelsPerPoint(100.0, 144.0), 144.0 / 72.0));
}

FASTPDF_TEST(pixels_per_point_scales_linearly_with_zoom) {
    FASTPDF_CHECK(Near(PixelsPerPoint(200.0, 96.0), 2.0 * 96.0 / 72.0));
    FASTPDF_CHECK(Near(PixelsPerPoint(50.0, 96.0), 0.5 * 96.0 / 72.0));
}

FASTPDF_TEST(pixels_per_point_clamps_zoom) {
    // 1000% clamps to 800%.
    FASTPDF_CHECK(Near(PixelsPerPoint(1000.0, 96.0), 8.0 * 96.0 / 72.0));
    // 1% clamps to 25%.
    FASTPDF_CHECK(Near(PixelsPerPoint(1.0, 96.0), 0.25 * 96.0 / 72.0));
}

FASTPDF_TEST(zoom_percent_round_trips_pixels_per_point) {
    for (const double zoom : {25.0, 50.0, 100.0, 137.5, 250.0, 800.0}) {
        const double ppp = PixelsPerPoint(zoom, kDpi);
        FASTPDF_CHECK(Near(ZoomPercentForPixelsPerPoint(ppp, kDpi), zoom));
    }
}

FASTPDF_TEST(fit_width_zoom) {
    // At 96 DPI, 100% == 96/72 = 1.333 units per point, so a 612-pt page is
    // 816 units wide at 100%. Fitting it into a 612-unit viewport is 75%.
    FASTPDF_CHECK(Near(FitWidthZoomPercent(612.0, 612.0, kDpi), 75.0));
    // Viewport 306 units -> 37.5%.
    FASTPDF_CHECK(Near(FitWidthZoomPercent(306.0, 612.0, kDpi), 37.5));
    // Viewport 1224 units -> 150%.
    FASTPDF_CHECK(Near(FitWidthZoomPercent(1224.0, 612.0, kDpi), 150.0));
    // At 72 DPI, 100% == 1 unit per point, so fitting 612 into 612 is 100%.
    FASTPDF_CHECK(Near(FitWidthZoomPercent(612.0, 612.0, 72.0), 100.0));
}

FASTPDF_TEST(fit_page_zoom_uses_smaller_axis) {
    // 612x792 page in a 612x792 viewport at 96 DPI -> 75%.
    FASTPDF_CHECK(Near(FitPageZoomPercent(612.0, 792.0, 612.0, 792.0, kDpi),
                       75.0));
    // Width is the constraint: viewport 306 wide, 792 tall -> 37.5%.
    FASTPDF_CHECK(Near(FitPageZoomPercent(306.0, 792.0, 612.0, 792.0, kDpi),
                       37.5));
    // Height is the constraint: viewport 612 wide, 396 tall -> 37.5%.
    FASTPDF_CHECK(Near(FitPageZoomPercent(612.0, 396.0, 612.0, 792.0, kDpi),
                       37.5));
    // At 72 DPI a 612x792 page in a 612x792 viewport is 100%.
    FASTPDF_CHECK(Near(FitPageZoomPercent(612.0, 792.0, 612.0, 792.0, 72.0),
                       100.0));
}

// ---------------------------------------------------------------------------
// ContinuousLayout construction and mixed page sizes
// ---------------------------------------------------------------------------

FASTPDF_TEST(layout_rejects_empty_and_invalid) {
    FASTPDF_CHECK(!ContinuousLayout::Create({}, 1.0, 10.0).has_value());
    FASTPDF_CHECK(!ContinuousLayout::Create(MixedPages(), 0.0, 10.0).has_value());
    FASTPDF_CHECK(!ContinuousLayout::Create(MixedPages(), 1.0, -1.0).has_value());
    std::vector<PageSize> bad = MixedPages();
    bad[1].widthPoints = 0.0;
    FASTPDF_CHECK(!ContinuousLayout::Create(bad, 1.0, 10.0).has_value());
}

FASTPDF_TEST(layout_content_width_is_widest_page) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Widest page is 1224 pt -> 1224 units at 100%.
    FASTPDF_CHECK(Near(layout->contentWidth(), 1224.0));
}

FASTPDF_TEST(layout_prefix_offsets_stack_pages_with_gap) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Page 0 top = 0.
    FASTPDF_CHECK(Near(layout->pageTop(0), 0.0));
    // Page 1 top = page0 height + gap = 792 + 10.
    FASTPDF_CHECK(Near(layout->pageTop(1), 802.0));
    // Page 2 top = 802 + 612 + 10 = 1424.
    FASTPDF_CHECK(Near(layout->pageTop(2), 1424.0));
    // Page 3 top = 1424 + 400 + 10 = 1834.
    FASTPDF_CHECK(Near(layout->pageTop(3), 1834.0));
    // Total height = 1834 + 1584 = 3418 (no trailing gap).
    FASTPDF_CHECK(Near(layout->contentHeight(), 3418.0));
}

FASTPDF_TEST(layout_page_rect_centers_horizontally) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    const PageRect rect = layout->pageRect(0);
    // Page 0 is 612 wide in a 1224 content -> left = (1224-612)/2 = 306.
    FASTPDF_CHECK(Near(rect.left, 306.0));
    FASTPDF_CHECK(Near(rect.top, 0.0));
    FASTPDF_CHECK(Near(rect.width, 612.0));
    FASTPDF_CHECK(Near(rect.height, 792.0));
    // Widest page is flush left.
    FASTPDF_CHECK(Near(layout->pageRect(3).left, 0.0));
}

FASTPDF_TEST(layout_scales_page_sizes_with_zoom) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 2.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // At 2x, page 0 is 1224 wide x 1584 tall.
    FASTPDF_CHECK(Near(layout->pageRect(0).width, 1224.0));
    FASTPDF_CHECK(Near(layout->pageRect(0).height, 1584.0));
    // Content width = 2448.
    FASTPDF_CHECK(Near(layout->contentWidth(), 2448.0));
}

// ---------------------------------------------------------------------------
// Viewport page determination
// ---------------------------------------------------------------------------

FASTPDF_TEST(page_at_content_y_resolves_each_page) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    FASTPDF_CHECK_EQ(layout->pageAtContentY(0.0), 0);
    FASTPDF_CHECK_EQ(layout->pageAtContentY(500.0), 0);
    FASTPDF_CHECK_EQ(layout->pageAtContentY(802.0), 1);
    FASTPDF_CHECK_EQ(layout->pageAtContentY(1424.0), 2);
    FASTPDF_CHECK_EQ(layout->pageAtContentY(1834.0), 3);
    FASTPDF_CHECK_EQ(layout->pageAtContentY(3400.0), 3);
}

FASTPDF_TEST(page_at_content_y_in_gap_resolves_to_page_above) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Gap between page 0 (ends at 792) and page 1 (starts at 802).
    FASTPDF_CHECK_EQ(layout->pageAtContentY(795.0), 0);
    FASTPDF_CHECK_EQ(layout->pageAtContentY(801.0), 0);
}

FASTPDF_TEST(page_at_content_y_clamps_out_of_range) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    FASTPDF_CHECK_EQ(layout->pageAtContentY(-100.0), 0);
    FASTPDF_CHECK_EQ(layout->pageAtContentY(1e9), 3);
}

// ---------------------------------------------------------------------------
// Direct page-content hit test (double-click-to-fit gesture)
// ---------------------------------------------------------------------------

FASTPDF_TEST(page_at_content_point_hits_each_page_box) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Page 0: 306..918 x 0..792 -> center (612, 396).
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(612.0, 396.0), 0);
    // Page 1: 216..1008 x 802..1414 -> center (612, 1108).
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(612.0, 1108.0), 1);
    // Page 2: 462..762 x 1424..1824 -> center (612, 1624).
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(612.0, 1624.0), 2);
    // Page 3: 0..1224 x 1834..3418 -> center (612, 2626).
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(612.0, 2626.0), 3);
}

FASTPDF_TEST(page_at_content_point_rejects_gap_and_margin) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Vertical gap between page 0 (ends 792) and page 1 (starts 802): even
    // though pageAtContentY would report page 0, a direct hit must miss.
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(612.0, 797.0), -1);
    // Horizontal margin to the left of page 0 (its left edge is 306).
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(100.0, 396.0), -1);
    // Horizontal margin to the right of page 0 (its right edge is 918).
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(1000.0, 396.0), -1);
}

FASTPDF_TEST(page_at_content_point_rejects_outside_document) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Above the document and past the last page's bottom edge (3418).
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(612.0, -5.0), -1);
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(612.0, 3420.0), -1);
    // Past the content width (1224) anywhere vertically.
    FASTPDF_CHECK_EQ(layout->pageAtContentPoint(1300.0, 396.0), -1);
}

FASTPDF_TEST(visible_page_range_single_page_viewport) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Viewport showing the top of page 0, 500 tall.
    auto [first, last] = layout->visiblePageRange(0.0, 500.0);
    FASTPDF_CHECK_EQ(first, 0);
    FASTPDF_CHECK_EQ(last, 0);
    // Viewport spanning the gap between page 0 and page 1.
    std::tie(first, last) = layout->visiblePageRange(700.0, 200.0);
    FASTPDF_CHECK_EQ(first, 0);
    FASTPDF_CHECK_EQ(last, 1);
}

FASTPDF_TEST(visible_page_range_spans_multiple_pages) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Viewport from 700 to 1500 covers page 0 (bottom), page 1, page 2 (top).
    auto [first, last] = layout->visiblePageRange(700.0, 800.0);
    FASTPDF_CHECK_EQ(first, 0);
    FASTPDF_CHECK_EQ(last, 2);
}

FASTPDF_TEST(visible_page_range_clamps_to_document) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    auto [first, last] = layout->visiblePageRange(3000.0, 1000.0);
    FASTPDF_CHECK_EQ(first, 3);
    FASTPDF_CHECK_EQ(last, 3);
    // Negative scroll (content narrower than viewport) still resolves page 0.
    // Viewport from -50 to 750 covers page 0 (0..792) only; page 1 starts at
    // 802, beyond the viewport bottom.
    std::tie(first, last) = layout->visiblePageRange(-50.0, 800.0);
    FASTPDF_CHECK_EQ(first, 0);
    FASTPDF_CHECK_EQ(last, 0);
}

// ---------------------------------------------------------------------------
// Scroll clamping
// ---------------------------------------------------------------------------

FASTPDF_TEST(scroll_y_clamps_to_content) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Content 3418 tall, viewport 1000 -> max scroll 2418.
    FASTPDF_CHECK(Near(layout->maxScrollY(1000.0), 2418.0));
    FASTPDF_CHECK(Near(layout->clampScrollY(-50.0, 1000.0), 0.0));
    FASTPDF_CHECK(Near(layout->clampScrollY(5000.0, 1000.0), 2418.0));
    FASTPDF_CHECK(Near(layout->clampScrollY(1000.0, 1000.0), 1000.0));
}

FASTPDF_TEST(scroll_x_centers_narrow_content) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Content 1224 wide, viewport 2000 -> centered at (1224-2000)/2 = -388.
    FASTPDF_CHECK(Near(layout->clampScrollX(0.0, 2000.0), -388.0));
    FASTPDF_CHECK(Near(layout->clampScrollX(123.0, 2000.0), -388.0));
}

FASTPDF_TEST(scroll_x_clamps_wide_content) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Content 1224 wide, viewport 800 -> max scroll 424.
    FASTPDF_CHECK(Near(layout->maxScrollX(800.0), 424.0));
    FASTPDF_CHECK(Near(layout->clampScrollX(-10.0, 800.0), 0.0));
    FASTPDF_CHECK(Near(layout->clampScrollX(1000.0, 800.0), 424.0));
}

// ---------------------------------------------------------------------------
// Page navigation
// ---------------------------------------------------------------------------

FASTPDF_TEST(scroll_top_for_page_top) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    FASTPDF_CHECK(Near(ScrollTopForPageTop(*layout, 0, 1000.0), 0.0));
    FASTPDF_CHECK(Near(ScrollTopForPageTop(*layout, 1, 1000.0), 802.0));
    FASTPDF_CHECK(Near(ScrollTopForPageTop(*layout, 2, 1000.0), 1424.0));
    // Last page top 1834, but max scroll is 2418, so it stays at 1834.
    FASTPDF_CHECK(Near(ScrollTopForPageTop(*layout, 3, 1000.0), 1834.0));
}

FASTPDF_TEST(scroll_top_for_page_top_clamps_at_document_end) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // A tall viewport (3000) means max scroll is 418; page 3 top 1834 clamps
    // to 418 so the end of the document is not scrolled past.
    FASTPDF_CHECK(Near(ScrollTopForPageTop(*layout, 3, 3000.0), 418.0));
}

// ---------------------------------------------------------------------------
// Page anchor: zoom / resize preserve logical location
// ---------------------------------------------------------------------------

FASTPDF_TEST(anchor_capture_and_restore_identity_at_same_scale) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    const double scrollX = 0.0, scrollY = 1000.0;
    const double vw = 800.0, vh = 600.0;
    const ViewAnchor anchor =
        CaptureAnchor(*layout, scrollX, scrollY, vw, vh, 400.0, 300.0);
    double outX = 0.0, outY = 0.0;
    RestoreAnchor(*layout, anchor, vw, vh, outX, outY);
    FASTPDF_CHECK(Near(outX, scrollX));
    FASTPDF_CHECK(Near(outY, scrollY));
}

FASTPDF_TEST(anchor_preserves_logical_location_across_zoom) {
    const auto layout100 = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    const auto layout200 = ContinuousLayout::Create(MixedPages(), 2.0, 10.0);
    FASTPDF_CHECK(layout100.has_value());
    FASTPDF_CHECK(layout200.has_value());

    // Capture at 100%: viewport center over page 1, cursor at center.
    const double vw = 800.0, vh = 600.0;
    const ViewAnchor anchor =
        CaptureAnchor(*layout100, 0.0, 1000.0, vw, vh, 400.0, 300.0);
    FASTPDF_CHECK_EQ(anchor.page, 1);

    // Restore at 200% with the same viewport size: the same logical point
    // must land at the same viewport point (400, 300).
    double outX = 0.0, outY = 0.0;
    RestoreAnchor(*layout200, anchor, vw, vh, outX, outY);
    const auto [cx, cy] = fastpdf::core::layout::AnchorContentPosition(
        *layout200, anchor);
    FASTPDF_CHECK(Near(cx - outX, 400.0));
    FASTPDF_CHECK(Near(cy - outY, 300.0));
}

FASTPDF_TEST(anchor_preserves_logical_location_across_resize) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());

    // Capture at 800x600, then restore into a 1200x900 viewport at the same
    // scale. The logical location must stay under the same viewport point.
    const ViewAnchor anchor =
        CaptureAnchor(*layout, 0.0, 1000.0, 800.0, 600.0, 400.0, 300.0);
    double outX = 0.0, outY = 0.0;
    RestoreAnchor(*layout, anchor, 1200.0, 900.0, outX, outY);
    const auto [cx, cy] =
        fastpdf::core::layout::AnchorContentPosition(*layout, anchor);
    FASTPDF_CHECK(Near(cx - outX, 400.0));
    FASTPDF_CHECK(Near(cy - outY, 300.0));
}

FASTPDF_TEST(anchor_normalized_coordinates_are_in_unit_range) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // Cursor far outside the content still yields normalized coords in [0,1].
    const ViewAnchor anchor =
        CaptureAnchor(*layout, 0.0, 0.0, 800.0, 600.0, 10000.0, 10000.0);
    FASTPDF_CHECK(anchor.nx >= 0.0 && anchor.nx <= 1.0);
    FASTPDF_CHECK(anchor.ny >= 0.0 && anchor.ny <= 1.0);
}

FASTPDF_TEST(anchor_zoom_at_cursor_keeps_cursor_stable) {
    // Simulate Ctrl+mouse-wheel zoom at the cursor: capture the anchor at the
    // cursor, rebuild at a new scale, restore. The content under the cursor
    // must remain under the cursor.
    const auto layout100 = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    const auto layout150 = ContinuousLayout::Create(MixedPages(), 1.5, 10.0);
    FASTPDF_CHECK(layout100.has_value());
    FASTPDF_CHECK(layout150.has_value());

    const double vw = 800.0, vh = 600.0;
    const double cursorX = 250.0, cursorY = 180.0;
    const ViewAnchor anchor =
        CaptureAnchor(*layout100, 0.0, 1000.0, vw, vh, cursorX, cursorY);

    double outX = 0.0, outY = 0.0;
    RestoreAnchor(*layout150, anchor, vw, vh, outX, outY);
    const auto [cx, cy] =
        fastpdf::core::layout::AnchorContentPosition(*layout150, anchor);
    FASTPDF_CHECK(Near(cx - outX, cursorX));
    FASTPDF_CHECK(Near(cy - outY, cursorY));
}

FASTPDF_TEST(anchor_restore_clamps_scroll) {
    const auto layout = ContinuousLayout::Create(MixedPages(), 1.0, 10.0);
    FASTPDF_CHECK(layout.has_value());
    // An anchor pointing far past the end of the document must clamp.
    ViewAnchor anchor;
    anchor.page = 3;
    anchor.nx = 0.5;
    anchor.ny = 1.0;
    anchor.viewportX = 400.0;
    anchor.viewportY = 300.0;
    double outX = 0.0, outY = 0.0;
    RestoreAnchor(*layout, anchor, 800.0, 600.0, outX, outY);
    FASTPDF_CHECK(Near(outY, layout->maxScrollY(600.0)));
}

}  // namespace

int main() { return fastpdf::test::RunAll(); }
