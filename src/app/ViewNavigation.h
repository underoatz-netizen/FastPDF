#pragma once

// Pure normal-View navigation math for FastPDF. Kept free of Windows and
// PDFium so it can be unit-tested without a renderer or a window. The app
// layer (AppWindow) feeds it raw input deltas, layout sizes and mode flags
// and applies the results through the canonical ScrollBy/ScrollTo path, so
// drag/scrollbar/keyboard activity keeps the existing preview-to-final
// render behavior.
//
// Units: all sizes/offsets are layout pixels (DIPs scaled by the window DPI).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace fastpdf::app::navigation {

// Small consistent arrow-key increment, in DIPs at 96 DPI. Scaled by the
// window DPI (ArrowStepPx) so the step feels identical across monitors. This
// is intentionally much smaller than a viewport page step: arrows nudge,
// Space/PageUp move by pages.
inline constexpr double kArrowStepDip = 32.0;

// Reference DPI for DIP scaling.
inline constexpr double kReferenceDpi = 96.0;

// Win32 SCROLLINFO range (signed 32-bit).
inline constexpr long long kScrollIntMax =
    static_cast<long long>(std::numeric_limits<int>::max());

// Arrow-key vertical increment for the given window DPI. Never negative;
// falls back to 96 DPI for degenerate input.
inline double ArrowStepPx(double dpi) noexcept {
    if (!(dpi > 0.0) || !std::isfinite(dpi)) {
        dpi = kReferenceDpi;
    }
    return kArrowStepDip * dpi / kReferenceDpi;
}

// Viewport page step ("approximately one viewport") for Space/Shift+Space
// and the scrollbar page zones. Never negative.
inline double PageStepPx(double viewportHeightPx) noexcept {
    if (!std::isfinite(viewportHeightPx) || viewportHeightPx <= 0.0) {
        return 0.0;
    }
    return viewportHeightPx;
}

// Whether incremental scrolling is allowed in the current mode. Normal View
// only: the document must be Ready, and presentation/screenshot modes have
// their own input models that must remain untouched.
inline bool CanNormalViewScroll(bool documentReady, bool presenting,
                                bool screenshotActive) noexcept {
    return documentReady && !presenting && !screenshotActive;
}

// Whether a press at |pageAtPoint| (the layout's pageAtContentPoint result,
// -1 when over margin/gap) may start click-drag hand panning. In addition to
// CanNormalViewScroll, a visible notification toast owns left-clicks
// (Save-PNG button / dismiss), so a drag must never start under it.
inline bool ShouldStartHandPan(bool documentReady, bool presenting,
                               bool screenshotActive, bool toastVisible,
                               int pageAtPoint) noexcept {
    return CanNormalViewScroll(documentReady, presenting, screenshotActive) &&
           !toastVisible && pageAtPoint >= 0;
}

// Panned scroll offset for one axis from an anchored origin: the content
// follows the cursor 1:1 (grab-and-drag). The caller clamps the result into
// the layout bounds (clampScrollX/clampScrollY).
inline double PanScrollOffset(double originScrollPx, int originPointPx,
                              int currentPointPx) noexcept {
    return originScrollPx +
           static_cast<double>(originPointPx - currentPointPx);
}

// Native Win32 vertical-scrollbar parameters computed from layout geometry.
struct VScrollParams {
    int nMin = 0;
    int nMax = 0;
    int nPage = 0;
    int nPos = 0;
    bool enabled = false;
};

// Overflow-safe double -> clamped int in [0, INT_MAX] for scrollbar fields.
inline int ClampToScrollInt(double value) noexcept {
    if (!std::isfinite(value) || value <= 0.0) {
        return 0;
    }
    const double clamped = std::min(value, static_cast<double>(kScrollIntMax));
    return static_cast<int>(std::llround(clamped));
}

// Maps document content height / viewport height / canonical scroll offset to
// native scrollbar parameters. Semantics match SetScrollInfo with
// SIF_RANGE | SIF_PAGE | SIF_POS and nMin == 0: the maximum thumb position is
// (nMax - nPage), which corresponds to (contentHeight - viewportHeight).
// Disabled (and zeroed) whenever the content fits the viewport or the
// geometry is degenerate, so the bar can never report an out-of-bounds
// position.
inline VScrollParams ComputeVScroll(double contentHeightPx,
                                    double viewportHeightPx,
                                    double scrollYPx) noexcept {
    VScrollParams params;
    if (!std::isfinite(contentHeightPx) || !std::isfinite(viewportHeightPx) ||
        !std::isfinite(scrollYPx)) {
        return params;
    }
    if (contentHeightPx <= viewportHeightPx || contentHeightPx <= 0.0 ||
        viewportHeightPx <= 0.0) {
        return params;
    }
    params.nMin = 0;
    params.nMax = ClampToScrollInt(contentHeightPx);
    params.nPage = ClampToScrollInt(viewportHeightPx);
    const int maxPos = params.nMax - params.nPage;
    if (maxPos <= 0) {
        // The content exceeds the viewport only below integer resolution
        // (or past INT_MAX saturation): nothing useful to scroll.
        params.nMax = 0;
        params.nPage = 0;
        params.nPos = 0;
        params.enabled = false;
        return params;
    }
    const long long pos = std::llround(scrollYPx);
    params.nPos =
        static_cast<int>(std::clamp(pos, 0LL, static_cast<long long>(maxPos)));
    params.enabled = true;
    return params;
}

// Maps a scrollbar thumb-track position to a canonical scroll offset. The
// track range is [nMin, nMax - nPage] (matching ComputeVScroll output); the
// ends map exactly to 0 and |maxScrollYPx|, and the mapping is monotonic, so
// dragging the thumb can never produce an out-of-bounds offset.
inline double ThumbTrackToOffset(int trackPos, int nMin, int nMax, int nPage,
                                 double maxScrollYPx) noexcept {
    if (!std::isfinite(maxScrollYPx) || maxScrollYPx <= 0.0) {
        return 0.0;
    }
    const long long low = static_cast<long long>(nMin);
    const long long high =
        static_cast<long long>(nMax) - static_cast<long long>(nPage);
    if (high <= low) {
        return 0.0;
    }
    const double fraction =
        std::clamp((static_cast<double>(trackPos) - static_cast<double>(low)) /
                       (static_cast<double>(high) - static_cast<double>(low)),
                   0.0, 1.0);
    return fraction * maxScrollYPx;
}

}  // namespace fastpdf::app::navigation
