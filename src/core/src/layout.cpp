// Pure continuous-page layout and viewport math. No Windows or PDFium code.

#include "fastpdf/core/layout.h"

#include <algorithm>
#include <cmath>

namespace fastpdf::core::layout {

namespace {

double Clamp(double value, double low, double high) noexcept {
    if (high < low) {
        return low;
    }
    return std::clamp(value, low, high);
}

}  // namespace

double ClampZoomPercent(double percent) noexcept {
    return Clamp(percent, kMinZoomPercent, kMaxZoomPercent);
}

double PixelsPerPoint(double zoomPercent, double dpi) noexcept {
    if (dpi <= 0.0) {
        dpi = 96.0;
    }
    return ClampZoomPercent(zoomPercent) / 100.0 * dpi / kPointsPerInch;
}

double ZoomPercentForPixelsPerPoint(double pixelsPerPoint, double dpi) noexcept {
    if (pixelsPerPoint <= 0.0) {
        return kMinZoomPercent;
    }
    if (dpi <= 0.0) {
        dpi = 96.0;
    }
    return pixelsPerPoint * kPointsPerInch / dpi * 100.0;
}

double FitWidthZoomPercent(double viewportWidth, double pageWidthPoints,
                           double dpi) noexcept {
    if (viewportWidth <= 0.0 || pageWidthPoints <= 0.0) {
        return 100.0;
    }
    if (dpi <= 0.0) {
        dpi = 96.0;
    }
    // pixelsPerPoint = viewportWidth / pageWidthPoints.
    return viewportWidth / pageWidthPoints * kPointsPerInch / dpi * 100.0;
}

double FitPageZoomPercent(double viewportWidth, double viewportHeight,
                          double pageWidthPoints, double pageHeightPoints,
                          double dpi) noexcept {
    if (viewportWidth <= 0.0 || viewportHeight <= 0.0 ||
        pageWidthPoints <= 0.0 || pageHeightPoints <= 0.0) {
        return 100.0;
    }
    if (dpi <= 0.0) {
        dpi = 96.0;
    }
    const double scale =
        std::min(viewportWidth / pageWidthPoints,
                 viewportHeight / pageHeightPoints);
    return scale * kPointsPerInch / dpi * 100.0;
}

ContinuousLayout::ContinuousLayout(std::vector<PageSize> pages,
                                   double pixelsPerPoint, double pageGap,
                                   std::vector<double> tops,
                                   double contentWidth,
                                   double contentHeight) noexcept
    : pages_(std::move(pages)),
      pixelsPerPoint_(pixelsPerPoint),
      pageGap_(pageGap),
      tops_(std::move(tops)),
      contentWidth_(contentWidth),
      contentHeight_(contentHeight) {}

std::optional<ContinuousLayout> ContinuousLayout::Create(
    const std::vector<PageSize>& pages, double pixelsPerPoint,
    double pageGap) noexcept {
    if (pages.empty() || pixelsPerPoint <= 0.0 || pageGap < 0.0) {
        return std::nullopt;
    }

    std::vector<double> tops;
    tops.reserve(pages.size());
    double contentHeight = 0.0;
    double contentWidth = 0.0;
    for (const PageSize& page : pages) {
        if (page.widthPoints <= 0.0 || page.heightPoints <= 0.0) {
            return std::nullopt;
        }
        const double scaledWidth = page.widthPoints * pixelsPerPoint;
        const double scaledHeight = page.heightPoints * pixelsPerPoint;
        tops.push_back(contentHeight);
        contentHeight += scaledHeight + pageGap;
        contentWidth = std::max(contentWidth, scaledWidth);
    }
    if (!pages.empty()) {
        contentHeight -= pageGap;  // No gap after the last page.
    }

    return ContinuousLayout(pages, pixelsPerPoint, pageGap, std::move(tops),
                            contentWidth, contentHeight);
}

const PageSize& ContinuousLayout::pageSize(int index) const noexcept {
    // Callers check pageCount(); returning the first page keeps out-of-range
    // calls from being UB even when a caller has a bug.
    const size_t clamped = std::min<size_t>(static_cast<size_t>(index),
                                            pages_.empty() ? 0 : pages_.size() - 1);
    return pages_[clamped];
}

double ContinuousLayout::pageTop(int index) const noexcept {
    if (tops_.empty()) {
        return 0.0;
    }
    const size_t clamped = std::min<size_t>(static_cast<size_t>(index),
                                            tops_.size() - 1);
    return tops_[clamped];
}

PageRect ContinuousLayout::pageRect(int index) const noexcept {
    if (pages_.empty()) {
        return {};
    }
    const size_t clamped = std::min<size_t>(static_cast<size_t>(index),
                                            pages_.size() - 1);
    const PageSize& page = pages_[clamped];
    const double width = page.widthPoints * pixelsPerPoint_;
    const double height = page.heightPoints * pixelsPerPoint_;
    return PageRect{static_cast<int>(clamped),
                    (contentWidth_ - width) * 0.5, tops_[clamped],
                    width, height};
}

int ContinuousLayout::pageAtContentY(double y) const noexcept {
    if (pages_.empty()) {
        return 0;
    }
    if (y < 0.0) {
        return 0;
    }
    // Largest page whose top <= y. When y sits in a gap, this is the page
    // above the gap (its bottom is the smaller value).
    const auto it = std::upper_bound(tops_.begin(), tops_.end(), y);
    const size_t index = static_cast<size_t>(it - tops_.begin());
    if (index == 0) {
        return 0;
    }
    return static_cast<int>(index - 1);
}

std::pair<int, int> ContinuousLayout::visiblePageRange(
    double scrollY, double viewportHeight) const noexcept {
    if (pages_.empty() || viewportHeight <= 0.0) {
        return {-1, -1};
    }

    // First page whose bottom edge is strictly below the viewport top.
    const double viewTop = scrollY;
    int first = -1;
    if (viewTop < 0.0) {
        first = 0;
    } else {
        const auto it = std::upper_bound(tops_.begin(), tops_.end(), viewTop);
        const size_t index = static_cast<size_t>(it - tops_.begin());
        if (index == 0) {
            first = 0;
        } else {
            // Check whether the page above the cut is still visible: it is
            // when its bottom (top + height) is below viewTop.
            const int candidate = static_cast<int>(index - 1);
            const double bottom = tops_[static_cast<size_t>(candidate)] +
                                  pageRect(candidate).height;
            first = bottom > viewTop ? candidate : static_cast<int>(index);
        }
    }

    if (first < 0 || first >= pageCount()) {
        return {-1, -1};
    }

    // Last page whose top edge is strictly above the viewport bottom.
    const double viewBottom = scrollY + viewportHeight;
    const auto it = std::upper_bound(tops_.begin(), tops_.end(), viewBottom);
    const size_t lastIndex = static_cast<size_t>(it - tops_.begin());
    int last = static_cast<int>(lastIndex);  // one past the last visible page
    // tops[last] >= viewBottom means page last-1 is the last visible one,
    // unless a page top equals viewBottom exactly (then it is not visible).
    last = std::max(first, last - 1);
    if (last >= pageCount()) {
        last = pageCount() - 1;
    }
    if (last < first) {
        return {-1, -1};
    }
    return {first, last};
}

double ContinuousLayout::maxScrollY(double viewportHeight) const noexcept {
    if (viewportHeight <= 0.0) {
        return 0.0;
    }
    return std::max(0.0, contentHeight_ - viewportHeight);
}

double ContinuousLayout::maxScrollX(double viewportWidth) const noexcept {
    if (viewportWidth <= 0.0) {
        return 0.0;
    }
    return std::max(0.0, contentWidth_ - viewportWidth);
}

double ContinuousLayout::clampScrollX(double scrollX,
                                      double viewportWidth) const noexcept {
    if (viewportWidth <= 0.0) {
        return 0.0;
    }
    if (contentWidth_ <= viewportWidth) {
        // Center the narrower content.
        return (contentWidth_ - viewportWidth) * 0.5;
    }
    return Clamp(scrollX, 0.0, contentWidth_ - viewportWidth);
}

double ContinuousLayout::clampScrollY(double scrollY,
                                      double viewportHeight) const noexcept {
    if (viewportHeight <= 0.0) {
        return 0.0;
    }
    if (contentHeight_ <= viewportHeight) {
        return 0.0;  // Top-aligned when the content fits vertically.
    }
    return Clamp(scrollY, 0.0, contentHeight_ - viewportHeight);
}

ViewAnchor CaptureAnchor(const ContinuousLayout& layout, double scrollX,
                         double scrollY, double viewportWidth,
                         double viewportHeight, double refViewportX,
                         double refViewportY) noexcept {
    ViewAnchor anchor;
    if (layout.empty()) {
        return anchor;
    }
    const double contentX = scrollX + refViewportX;
    const double contentY = scrollY + refViewportY;
    anchor.page = layout.pageAtContentY(contentY);
    const PageRect rect = layout.pageRect(anchor.page);
    if (rect.width > 0.0 && rect.height > 0.0) {
        anchor.nx = Clamp((contentX - rect.left) / rect.width, 0.0, 1.0);
        anchor.ny = Clamp((contentY - rect.top) / rect.height, 0.0, 1.0);
    }
    anchor.viewportX = Clamp(refViewportX, 0.0, std::max(0.0, viewportWidth));
    anchor.viewportY = Clamp(refViewportY, 0.0, std::max(0.0, viewportHeight));
    return anchor;
}

std::pair<double, double> AnchorContentPosition(
    const ContinuousLayout& layout, const ViewAnchor& anchor) noexcept {
    if (layout.empty()) {
        return {0.0, 0.0};
    }
    const PageRect rect = layout.pageRect(anchor.page);
    return {rect.left + rect.width * anchor.nx,
            rect.top + rect.height * anchor.ny};
}

void RestoreAnchor(const ContinuousLayout& layout, const ViewAnchor& anchor,
                   double viewportWidth, double viewportHeight,
                   double& outScrollX, double& outScrollY) noexcept {
    if (layout.empty()) {
        outScrollX = 0.0;
        outScrollY = 0.0;
        return;
    }
    const auto [contentX, contentY] = AnchorContentPosition(layout, anchor);
    outScrollX = layout.clampScrollX(contentX - anchor.viewportX,
                                     viewportWidth);
    outScrollY = layout.clampScrollY(contentY - anchor.viewportY,
                                     viewportHeight);
}

double ScrollTopForPageTop(const ContinuousLayout& layout, int index,
                           double viewportHeight) noexcept {
    if (layout.empty()) {
        return 0.0;
    }
    return layout.clampScrollY(layout.pageTop(index), viewportHeight);
}

}  // namespace fastpdf::core::layout
