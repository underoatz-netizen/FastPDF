// fastpdf_view_navigation_tests - focused, PDFium-free tests for the
// normal-View navigation helpers (fastpdf::app::navigation):
//   * arrow-key increment (small, consistent, DPI-scaled) and viewport page
//     step for Space/Shift+Space and scrollbar page zones
//   * anchored hand-pan offset math and the mode/capture start predicate
//     (screenshot/presentation/toast/margin must never start a drag)
//   * native vertical-scrollbar range/position mapping: enablement, bounds,
//     monotonic thumb mapping and overflow-safe handling of huge documents
//   * layout-backed bounds: panned offsets clamped through ContinuousLayout
//     never leave [0, maxScrollY]
//
// The module is pure (no Windows, no PDFium); layout-backed checks link only
// fastpdf_core.

#include <cmath>
#include <limits>
#include <vector>

#include "ViewNavigation.h"
#include "fastpdf/core/layout.h"
#include "test_harness.h"

namespace {

using fastpdf::app::navigation::ArrowStepPx;
using fastpdf::app::navigation::CanNormalViewScroll;
using fastpdf::app::navigation::ClampToScrollInt;
using fastpdf::app::navigation::ComputeVScroll;
using fastpdf::app::navigation::PageStepPx;
using fastpdf::app::navigation::PanScrollOffset;
using fastpdf::app::navigation::ShouldStartHandPan;
using fastpdf::app::navigation::ThumbTrackToOffset;

constexpr double kEps = 1e-9;

bool Near(double a, double b) { return std::fabs(a - b) < kEps; }

}  // namespace

FASTPDF_TEST(view_navigation_arrow_step_small_and_dpi_scaled) {
    // Small: well below any realistic viewport page step.
    FASTPDF_CHECK(ArrowStepPx(96.0) < 100.0);
    FASTPDF_CHECK(ArrowStepPx(96.0) > 0.0);
    // Consistent identity at the reference DPI.
    FASTPDF_CHECK(Near(ArrowStepPx(96.0), 32.0));
    // DPI-scaled: double the density, double the pixels.
    FASTPDF_CHECK(Near(ArrowStepPx(192.0), 64.0));
    FASTPDF_CHECK(Near(ArrowStepPx(144.0), 48.0));
    // Monotonic across densities.
    FASTPDF_CHECK(ArrowStepPx(96.0) < ArrowStepPx(120.0));
    FASTPDF_CHECK(ArrowStepPx(120.0) < ArrowStepPx(192.0));
    // Degenerate DPI falls back instead of collapsing to zero/NaN.
    FASTPDF_CHECK(ArrowStepPx(0.0) > 0.0);
    FASTPDF_CHECK(ArrowStepPx(-50.0) > 0.0);
}

FASTPDF_TEST(view_navigation_page_step_matches_viewport) {
    FASTPDF_CHECK(Near(PageStepPx(800.0), 800.0));
    FASTPDF_CHECK(Near(PageStepPx(1080.0), 1080.0));
    // Degenerate viewports never scroll backwards.
    FASTPDF_CHECK(Near(PageStepPx(0.0), 0.0));
    FASTPDF_CHECK(Near(PageStepPx(-10.0), 0.0));
}

FASTPDF_TEST(view_navigation_pan_offset_anchored) {
    // Content follows the cursor 1:1 from the anchored origin.
    FASTPDF_CHECK(Near(PanScrollOffset(100.0, 50, 70), 80.0));
    FASTPDF_CHECK(Near(PanScrollOffset(100.0, 70, 50), 120.0));
    FASTPDF_CHECK(Near(PanScrollOffset(0.0, 200, 200), 0.0));
    FASTPDF_CHECK(Near(PanScrollOffset(250.0, -10, -40), 280.0));
}

FASTPDF_TEST(view_navigation_can_scroll_modes) {
    FASTPDF_CHECK(CanNormalViewScroll(true, false, false));
    FASTPDF_CHECK(!CanNormalViewScroll(false, false, false));
    FASTPDF_CHECK(!CanNormalViewScroll(true, true, false));
    FASTPDF_CHECK(!CanNormalViewScroll(true, false, true));
    FASTPDF_CHECK(!CanNormalViewScroll(false, true, true));
}

FASTPDF_TEST(view_navigation_pan_start_predicate) {
    // Press directly over page content in the normal Ready view starts.
    FASTPDF_CHECK(ShouldStartHandPan(true, false, false, false, 0));
    FASTPDF_CHECK(ShouldStartHandPan(true, false, false, false, 3));
    // Every competing mode/overlay vetoes the drag.
    FASTPDF_CHECK(!ShouldStartHandPan(false, false, false, false, 0));
    FASTPDF_CHECK(!ShouldStartHandPan(true, true, false, false, 0));
    FASTPDF_CHECK(!ShouldStartHandPan(true, false, true, false, 0));
    FASTPDF_CHECK(!ShouldStartHandPan(true, false, false, true, 0));
    // Margin/gap presses (pageAtContentPoint == -1) never start a drag.
    FASTPDF_CHECK(!ShouldStartHandPan(true, false, false, false, -1));
}

FASTPDF_TEST(view_navigation_vscroll_disabled_when_content_fits) {
    const auto fits = ComputeVScroll(500.0, 800.0, 0.0);
    FASTPDF_CHECK(!fits.enabled);
    FASTPDF_CHECK_EQ(fits.nPos, 0);
    const auto equal = ComputeVScroll(800.0, 800.0, 0.0);
    FASTPDF_CHECK(!equal.enabled);
    const auto degenerate = ComputeVScroll(0.0, 0.0, 0.0);
    FASTPDF_CHECK(!degenerate.enabled);
}

FASTPDF_TEST(view_navigation_vscroll_range_and_position) {
    // 2000 px of content in an 800 px viewport: 1200 px of travel.
    const auto params = ComputeVScroll(2000.0, 800.0, 100.0);
    FASTPDF_CHECK(params.enabled);
    FASTPDF_CHECK_EQ(params.nMin, 0);
    FASTPDF_CHECK_EQ(params.nMax - params.nPage, 1200);
    FASTPDF_CHECK_EQ(params.nPos, 100);
    // Positions clamp into [0, max] and never escape.
    FASTPDF_CHECK_EQ(ComputeVScroll(2000.0, 800.0, 5000.0).nPos, 1200);
    FASTPDF_CHECK_EQ(ComputeVScroll(2000.0, 800.0, -40.0).nPos, 0);
    FASTPDF_CHECK_EQ(ComputeVScroll(2000.0, 800.0, 1200.0).nPos, 1200);
}

FASTPDF_TEST(view_navigation_vscroll_thumb_track_monotonic_and_bounded) {
    const double maxScroll = 1200.0;
    // Ends map exactly to the document bounds.
    FASTPDF_CHECK(Near(ThumbTrackToOffset(0, 0, 2000, 800, maxScroll), 0.0));
    FASTPDF_CHECK(
        Near(ThumbTrackToOffset(1200, 0, 2000, 800, maxScroll), maxScroll));
    // Monotonic across the track.
    double prev = -1.0;
    for (int track = 0; track <= 1200; track += 137) {
        const double offset =
            ThumbTrackToOffset(track, 0, 2000, 800, maxScroll);
        FASTPDF_CHECK(offset >= prev);
        FASTPDF_CHECK(offset >= 0.0 && offset <= maxScroll);
        prev = offset;
    }
    // Midpoint maps to the middle of the document.
    FASTPDF_CHECK(Near(ThumbTrackToOffset(600, 0, 2000, 800, maxScroll),
                       maxScroll * 0.5));
    // Out-of-range track input clamps instead of escaping.
    FASTPDF_CHECK(Near(ThumbTrackToOffset(-50, 0, 2000, 800, maxScroll), 0.0));
    FASTPDF_CHECK(Near(ThumbTrackToOffset(99999, 0, 2000, 800, maxScroll),
                       maxScroll));
    // Degenerate geometry never divides by zero.
    FASTPDF_CHECK(Near(ThumbTrackToOffset(0, 0, 0, 0, 0.0), 0.0));
}

FASTPDF_TEST(view_navigation_vscroll_huge_documents_stay_in_int_range) {
    // A pathological multi-thousand-page document: values saturate at
    // INT_MAX instead of overflowing.
    const auto huge =
        ComputeVScroll(4.0e9, 800.0, 3.0e9);
    FASTPDF_CHECK(huge.enabled);
    FASTPDF_CHECK(huge.nMax <= std::numeric_limits<int>::max());
    FASTPDF_CHECK(huge.nPage <= std::numeric_limits<int>::max());
    FASTPDF_CHECK(huge.nPos >= 0 && huge.nPos <= huge.nMax - huge.nPage);
    FASTPDF_CHECK_EQ(ClampToScrollInt(1.0e18),
                     std::numeric_limits<int>::max());
    FASTPDF_CHECK_EQ(ClampToScrollInt(-5.0), 0);
}

FASTPDF_TEST(view_navigation_panned_offsets_clamped_by_layout) {
    using fastpdf::core::layout::ContinuousLayout;
    using fastpdf::core::layout::PageSize;
    const std::vector<PageSize> pages = {{100.0, 100.0}, {100.0, 100.0}};
    const auto layout = ContinuousLayout::Create(pages, /*pixelsPerPoint=*/2.0,
                                                 /*pageGap=*/8.0);
    FASTPDF_CHECK(layout.has_value());
    const double viewport = 200.0;
    const double maxY = layout->maxScrollY(viewport);
    FASTPDF_CHECK(maxY > 0.0);
    // A large drag from the origin clamps to the document end ...
    const double dragged = PanScrollOffset(0.0, 0, -100000);
    FASTPDF_CHECK_EQ(layout->clampScrollY(dragged, viewport), maxY);
    // ... and the reverse drag clamps to the document start.
    const double draggedBack = PanScrollOffset(maxY, 0, 100000);
    FASTPDF_CHECK_EQ(layout->clampScrollY(draggedBack, viewport), 0.0);
    // Arrow steps from mid-document stay inside the bounds.
    const double stepped = maxY * 0.5 + ArrowStepPx(96.0);
    const double clamped = layout->clampScrollY(stepped, viewport);
    FASTPDF_CHECK(clamped >= 0.0 && clamped <= maxY);
}

int main() {
    return fastpdf::test::RunAll();
}
