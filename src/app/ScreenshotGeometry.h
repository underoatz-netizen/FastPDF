#pragma once

#include <algorithm>
#include <cstdint>

namespace fastpdf::app::screenshot {

// Viewport pixel rectangle (top-left inclusive, right-bottom exclusive or inclusive coordinates).
// Coordinates are in viewport pixel space (client area of the document view).
struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const noexcept { return right - left; }
    int height() const noexcept { return bottom - top; }
    bool empty() const noexcept { return right <= left || bottom <= top; }
};

// Normalizes two arbitrary mouse points (start and current/end) into a valid Rect
// where left <= right and top <= bottom.
inline Rect NormalizeRect(int x1, int y1, int x2, int y2) noexcept {
    return Rect{
        std::min(x1, x2),
        std::min(y1, y2),
        std::max(x1, x2),
        std::max(y1, y2)
    };
}

// Clamps |rect| into bounds [0, 0, maxWidth, maxHeight].
inline Rect ClampRect(const Rect& rect, int maxWidth, int maxHeight) noexcept {
    const int maxW = std::max(0, maxWidth);
    const int maxH = std::max(0, maxHeight);
    const int l = std::clamp(rect.left, 0, maxW);
    const int t = std::clamp(rect.top, 0, maxH);
    const int r = std::clamp(rect.right, 0, maxW);
    const int b = std::clamp(rect.bottom, 0, maxH);
    return Rect{l, t, std::max(l, r), std::max(t, b)};
}

// Intersects two rectangles.
inline Rect IntersectRects(const Rect& a, const Rect& b) noexcept {
    const int l = std::max(a.left, b.left);
    const int t = std::max(a.top, b.top);
    const int r = std::min(a.right, b.right);
    const int btm = std::min(a.bottom, b.bottom);
    if (r <= l || btm <= t) {
        return Rect{0, 0, 0, 0};
    }
    return Rect{l, t, r, btm};
}

} // namespace fastpdf::app::screenshot
