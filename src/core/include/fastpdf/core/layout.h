#pragma once

// Pure continuous-page layout and viewport math for the FastPDF viewer.
//
// This module is intentionally platform-independent and PDFium-free: it deals
// only in PDF points (1/72 inch), layout units (the viewer draws in physical
// pixels, but the math never assumes a unit), and dimensionless normalized
// page coordinates. All Windows/PDFium concerns stay in the other boundaries.
//
// Coordinate conventions
// ----------------------
// * Content space: page 0 occupies x in [0, contentWidth), y starting at 0.
//   Every page is centered horizontally inside contentWidth (the widest page
//   at the current scale) and pages stack vertically with a gap between them.
// * Viewport origin: (scrollX, scrollY) is the content-space point shown at
//   the top-left of the viewport. When the content is narrower than the
//   viewport, scrollX is fixed at a negative value that centers the content
//   ((contentWidth - viewportWidth) / 2); otherwise scrollX is clamped into
//   [0, contentWidth - viewportWidth]. Vertically the content is top-aligned
//   when shorter than the viewport and scrollY is clamped into
//   [0, contentHeight - viewportHeight] otherwise.

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace fastpdf::core::layout {

// PDF points per inch (a PDF point is 1/72 inch).
inline constexpr double kPointsPerInch = 72.0;

// Arbitrary (user) zoom is clamped to this range, as a percentage where 100%
// means "actual size": one PDF point occupies dpi/72 physical pixels.
inline constexpr double kMinZoomPercent = 25.0;
inline constexpr double kMaxZoomPercent = 800.0;

// One document page, measured in PDF points.
struct PageSize {
    double widthPoints = 0.0;
    double heightPoints = 0.0;
};

enum class FitMode {
    Custom,    // Arbitrary zoom percentage (clamped to kMin..kMaxZoomPercent).
    FitWidth,  // The fit page's width equals the viewport width.
    FitPage,   // The fit page fits entirely inside the viewport.
};

// Clamps an arbitrary zoom percentage into [kMinZoomPercent, kMaxZoomPercent].
double ClampZoomPercent(double percent) noexcept;

// Pixels (layout units) per PDF point for a zoom percentage at the given
// pixels-per-inch density. zoom 100% == actual size == dpi/72 units per point.
double PixelsPerPoint(double zoomPercent, double dpi) noexcept;

// Inverse of PixelsPerPoint: the zoom percentage that produces the given
// pixels-per-point at the given density.
double ZoomPercentForPixelsPerPoint(double pixelsPerPoint, double dpi) noexcept;

// The zoom percentage that makes a page of |pageWidthPoints| fill exactly
// |viewportWidth| layout units (Fit Width), and the percentage that fits the
// whole page box inside the viewport (Fit Page).
double FitWidthZoomPercent(double viewportWidth, double pageWidthPoints,
                           double dpi) noexcept;
double FitPageZoomPercent(double viewportWidth, double viewportHeight,
                          double pageWidthPoints, double pageHeightPoints,
                          double dpi) noexcept;

// A page's laid-out box in content space. |width|/|height| are the page's
// PDF-point size scaled by the layout's pixels-per-point.
struct PageRect {
    int pageIndex = 0;
    double left = 0.0;
    double top = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// Immutable continuous single-column layout of a fixed document at a fixed
// scale. Value type (owns vectors); cheap to rebuild on every zoom/viewport
// change. All indices are bounds-checked by the caller via pageCount().
class ContinuousLayout {
public:
    ContinuousLayout() = default;

    // Builds a layout for |pages| (PDF points, all dimensions > 0) at the
    // given |pixelsPerPoint| with a vertical |pageGap| between pages (>= 0).
    // Returns nullopt for an empty page list or invalid parameters.
    static std::optional<ContinuousLayout> Create(
        const std::vector<PageSize>& pages, double pixelsPerPoint,
        double pageGap) noexcept;

    int pageCount() const noexcept { return static_cast<int>(pages_.size()); }
    double pixelsPerPoint() const noexcept { return pixelsPerPoint_; }
    double pageGap() const noexcept { return pageGap_; }
    bool empty() const noexcept { return pages_.empty(); }

    // Page |index|'s unscaled size in PDF points.
    const PageSize& pageSize(int index) const noexcept;

    // The laid-out content-space box of page |index|.
    PageRect pageRect(int index) const noexcept;

    // Total laid-out content size in layout units.
    double contentWidth() const noexcept { return contentWidth_; }
    double contentHeight() const noexcept { return contentHeight_; }

    // Content-space Y of page |index|'s top edge.
    double pageTop(int index) const noexcept;

    // The page whose vertical span contains content-space |y|. Points in the
    // gap between two pages resolve to the page above; out-of-range values
    // clamp to the first/last page.
    int pageAtContentY(double y) const noexcept;

    // The page whose laid-out box contains the content-space point (|x|, |y|),
    // or -1 when the point is not directly over page content: in the horizontal
    // margin beside a page, in the vertical gap between pages, or outside the
    // document. Unlike pageAtContentY this check is two-dimensional, so input
    // handling can distinguish a click on the page from a click on the
    // surrounding background/gap.
    int pageAtContentPoint(double x, double y) const noexcept;

    // The inclusive page range [first, last] visible in a viewport whose top
    // is at content-space |scrollY| and that is |viewportHeight| tall. Returns
    // {-1, -1} when nothing is visible (cannot happen with a valid layout and
    // a non-degenerate viewport).
    std::pair<int, int> visiblePageRange(double scrollY,
                                         double viewportHeight) const noexcept;

    // Maximum valid scroll offset in each axis (0 when the content fits).
    double maxScrollY(double viewportHeight) const noexcept;
    double maxScrollX(double viewportWidth) const noexcept;

    // Clamps a horizontal viewport origin into the valid range, centering the
    // content when it is narrower than the viewport.
    double clampScrollX(double scrollX, double viewportWidth) const noexcept;
    double clampScrollY(double scrollY, double viewportHeight) const noexcept;

private:
    ContinuousLayout(std::vector<PageSize> pages, double pixelsPerPoint,
                     double pageGap, std::vector<double> tops,
                     double contentWidth, double contentHeight) noexcept;

    std::vector<PageSize> pages_;
    double pixelsPerPoint_ = 0.0;
    double pageGap_ = 0.0;
    std::vector<double> tops_;  // Content-space Y of each page's top edge.
    double contentWidth_ = 0.0;
    double contentHeight_ = 0.0;
};

// A stable logical location for zoom/resize anchoring:
//   (page index, normalized x/y inside that page's laid-out box)
// plus the viewport-space point (viewportX, viewportY) that the location must
// keep tracking. After a zoom or a window resize the caller rebuilds the
// layout and calls RestoreAnchor so the same logical location stays under the
// same viewport point - that is what keeps page geometry from jumping.
struct ViewAnchor {
    int page = 0;
    double nx = 0.5;  // normalized inside the page box, clamped to [0, 1]
    double ny = 0.0;
    double viewportX = 0.0;  // viewport-space offset the anchor tracks
    double viewportY = 0.0;
};

// Captures the anchor for the content point currently shown at the
// viewport-space reference point (refViewportX, refViewportY) - typically the
// cursor or the viewport center. The reference point is kept verbatim as the
// anchor's viewportX/Y so zoom-at-cursor keeps the cursor stable; resize code
// may overwrite viewportX/Y with a new desired location before restoring.
ViewAnchor CaptureAnchor(const ContinuousLayout& layout, double scrollX,
                         double scrollY, double viewportWidth,
                         double viewportHeight, double refViewportX,
                         double refViewportY) noexcept;

// Content-space position of an anchor's logical location under |layout|.
std::pair<double, double> AnchorContentPosition(
    const ContinuousLayout& layout, const ViewAnchor& anchor) noexcept;

// Computes the clamped scroll that places |anchor|'s logical location at the
// anchor's viewportX/viewportY under |layout| (which may have a different
// scale and/or viewport size than the layout the anchor was captured from).
void RestoreAnchor(const ContinuousLayout& layout, const ViewAnchor& anchor,
                   double viewportWidth, double viewportHeight,
                   double& outScrollX, double& outScrollY) noexcept;

// Scroll that brings page |index|'s top edge to the top of the viewport,
// clamped so the end of the document cannot be scrolled past.
double ScrollTopForPageTop(const ContinuousLayout& layout, int index,
                           double viewportHeight) noexcept;

}  // namespace fastpdf::core::layout
