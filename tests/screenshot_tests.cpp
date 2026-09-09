// Focused unit tests for screenshot geometry math and compositor.
// Platform-independent and PDFium-free.

#include <vector>
#include "test_harness.h"
#include "ScreenshotGeometry.h"
#include "ScreenshotCompositor.h"

using namespace fastpdf::app::screenshot;

FASTPDF_TEST(screenshot_geometry_normalize) {
    // Normal orientation
    const Rect r1 = NormalizeRect(10, 20, 50, 60);
    FASTPDF_CHECK_EQ(r1.left, 10);
    FASTPDF_CHECK_EQ(r1.top, 20);
    FASTPDF_CHECK_EQ(r1.right, 50);
    FASTPDF_CHECK_EQ(r1.bottom, 60);
    FASTPDF_CHECK_EQ(r1.width(), 40);
    FASTPDF_CHECK_EQ(r1.height(), 40);

    // Inverted drag (bottom-right to top-left)
    const Rect r2 = NormalizeRect(100, 200, 30, 40);
    FASTPDF_CHECK_EQ(r2.left, 30);
    FASTPDF_CHECK_EQ(r2.top, 40);
    FASTPDF_CHECK_EQ(r2.right, 100);
    FASTPDF_CHECK_EQ(r2.bottom, 200);
    FASTPDF_CHECK_EQ(r2.width(), 70);
    FASTPDF_CHECK_EQ(r2.height(), 160);
}

FASTPDF_TEST(screenshot_geometry_clamp) {
    const Rect r1{-10, -20, 150, 250};
    const Rect clamped = ClampRect(r1, 100, 200);
    FASTPDF_CHECK_EQ(clamped.left, 0);
    FASTPDF_CHECK_EQ(clamped.top, 0);
    FASTPDF_CHECK_EQ(clamped.right, 100);
    FASTPDF_CHECK_EQ(clamped.bottom, 200);

    // Rect completely outside
    const Rect r2{500, 600, 700, 800};
    const Rect clamped2 = ClampRect(r2, 100, 200);
    FASTPDF_CHECK_EQ(clamped2.left, 100);
    FASTPDF_CHECK_EQ(clamped2.top, 200);
    FASTPDF_CHECK_EQ(clamped2.right, 100);
    FASTPDF_CHECK_EQ(clamped2.bottom, 200);
    FASTPDF_CHECK(clamped2.empty());
}

FASTPDF_TEST(screenshot_geometry_intersect) {
    const Rect a{10, 10, 50, 50};
    const Rect b{30, 20, 80, 70};
    const Rect inter = IntersectRects(a, b);
    FASTPDF_CHECK_EQ(inter.left, 30);
    FASTPDF_CHECK_EQ(inter.top, 20);
    FASTPDF_CHECK_EQ(inter.right, 50);
    FASTPDF_CHECK_EQ(inter.bottom, 50);

    // Disjoint
    const Rect c{100, 100, 200, 200};
    const Rect disjoint = IntersectRects(a, c);
    FASTPDF_CHECK(disjoint.empty());
}

FASTPDF_TEST(screenshot_compositor_empty_or_degenerate) {
    // Empty selection
    const Rect emptySel{10, 10, 10, 10};
    const auto bmp1 = CompositeSelection(emptySel, {});
    FASTPDF_CHECK(bmp1.empty());

    // Inverted selection
    const Rect invSel{50, 50, 10, 10};
    const auto bmp2 = CompositeSelection(invSel, {});
    FASTPDF_CHECK(bmp2.empty());
}

FASTPDF_TEST(screenshot_compositor_blank_and_unrendered_areas) {
    // Selection with no pages overlapping -> should produce solid white image
    const Rect sel{0, 0, 10, 10};
    const auto bmp = CompositeSelection(sel, {});
    FASTPDF_CHECK(!bmp.empty());
    FASTPDF_CHECK_EQ(bmp.width, 10);
    FASTPDF_CHECK_EQ(bmp.height, 10);
    FASTPDF_CHECK_EQ(bmp.stride, 40);

    // Every pixel must be 255 (BGRA 255, 255, 255, 255)
    for (std::size_t i = 0; i < bmp.data.size(); ++i) {
        FASTPDF_CHECK_EQ(bmp.data[i], 255);
    }
}

FASTPDF_TEST(screenshot_compositor_page_overlap_and_bounds) {
    // Create a 10x10 dummy page bitmap filled with pure red (B=0, G=0, R=255, A=255)
    fastpdf::renderer::Bitmap pageBmp;
    pageBmp.width = 10;
    pageBmp.height = 10;
    pageBmp.stride = 40;
    pageBmp.data.resize(400);
    for (int i = 0; i < 100; ++i) {
        pageBmp.data[i * 4 + 0] = 0;   // B
        pageBmp.data[i * 4 + 1] = 0;   // G
        pageBmp.data[i * 4 + 2] = 255; // R
        pageBmp.data[i * 4 + 3] = 255; // A
    }

    // Position page at (5, 5) with size 10x10 in viewport
    PageSource src;
    src.pageIndex = 0;
    src.vx = 5;
    src.vy = 5;
    src.vw = 10;
    src.vh = 10;
    src.bitmap = &pageBmp;

    // Selection covering (0, 0) to (10, 10)
    // Overlap with page is (5, 5) to (10, 10)
    const Rect sel{0, 0, 10, 10};
    const auto target = CompositeSelection(sel, {src});
    FASTPDF_CHECK_EQ(target.width, 10);
    FASTPDF_CHECK_EQ(target.height, 10);

    // Coordinate (2, 2) is outside the page: should be white (255, 255, 255, 255)
    const int idxOutside = (2 * target.stride) + (2 * 4);
    FASTPDF_CHECK_EQ(target.data[idxOutside + 0], 255);
    FASTPDF_CHECK_EQ(target.data[idxOutside + 1], 255);
    FASTPDF_CHECK_EQ(target.data[idxOutside + 2], 255);
    FASTPDF_CHECK_EQ(target.data[idxOutside + 3], 255);

    // Coordinate (7, 7) is inside the page overlap: should be red (0, 0, 255, 255)
    const int idxInside = (7 * target.stride) + (7 * 4);
    FASTPDF_CHECK_EQ(target.data[idxInside + 0], 0);
    FASTPDF_CHECK_EQ(target.data[idxInside + 1], 0);
    FASTPDF_CHECK_EQ(target.data[idxInside + 2], 255);
    FASTPDF_CHECK_EQ(target.data[idxInside + 3], 255);
}

FASTPDF_TEST(screenshot_compositor_across_multiple_pages) {
    // Create Page 0 (Blue) at (0, 0) to (10, 10)
    fastpdf::renderer::Bitmap page0;
    page0.width = 10;
    page0.height = 10;
    page0.stride = 40;
    page0.data.resize(400);
    for (int i = 0; i < 100; ++i) {
        page0.data[i * 4 + 0] = 255; // B
        page0.data[i * 4 + 1] = 0;   // G
        page0.data[i * 4 + 2] = 0;   // R
        page0.data[i * 4 + 3] = 255; // A
    }

    // Create Page 1 (Green) at (0, 15) to (10, 25) (a 5px gap in between)
    fastpdf::renderer::Bitmap page1;
    page1.width = 10;
    page1.height = 10;
    page1.stride = 40;
    page1.data.resize(400);
    for (int i = 0; i < 100; ++i) {
        page1.data[i * 4 + 0] = 0;   // B
        page1.data[i * 4 + 1] = 255; // G
        page1.data[i * 4 + 2] = 0;   // R
        page1.data[i * 4 + 3] = 255; // A
    }

    PageSource src0{0, 0, 0, 10, 10, &page0};
    PageSource src1{1, 0, 15, 10, 10, &page1};

    // Selection from (0, 0) to (10, 25) spanning both pages and the gap
    const Rect sel{0, 0, 10, 25};
    const auto target = CompositeSelection(sel, {src0, src1});
    FASTPDF_CHECK_EQ(target.width, 10);
    FASTPDF_CHECK_EQ(target.height, 25);

    // Pixel in Page 0 (e.g. y=5): Blue
    const int idx0 = (5 * target.stride) + (5 * 4);
    FASTPDF_CHECK_EQ(target.data[idx0 + 0], 255);
    FASTPDF_CHECK_EQ(target.data[idx0 + 1], 0);
    FASTPDF_CHECK_EQ(target.data[idx0 + 2], 0);

    // Pixel in gap (e.g. y=12): White
    const int idxGap = (12 * target.stride) + (5 * 4);
    FASTPDF_CHECK_EQ(target.data[idxGap + 0], 255);
    FASTPDF_CHECK_EQ(target.data[idxGap + 1], 255);
    FASTPDF_CHECK_EQ(target.data[idxGap + 2], 255);

    // Pixel in Page 1 (e.g. y=20): Green
    const int idx1 = (20 * target.stride) + (5 * 4);
    FASTPDF_CHECK_EQ(target.data[idx1 + 0], 0);
    FASTPDF_CHECK_EQ(target.data[idx1 + 1], 255);
    FASTPDF_CHECK_EQ(target.data[idx1 + 2], 0);
}

int main() { return fastpdf::test::RunAll(); }
