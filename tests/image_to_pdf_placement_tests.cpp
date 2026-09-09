#include <windows.h>
#include <cmath>
#include "test_harness.h"
#include "ImagePlacement.h"
#include "DropRouting.h"

using namespace fastpdf::app;
using namespace fastpdf::app::convert;

FASTPDF_TEST(drop_routing_supported_images) {
    FASTPDF_CHECK(IsSupportedImagePath(L"C:\\photos\\test.png"));
    FASTPDF_CHECK(IsSupportedImagePath(L"C:\\photos\\test.PNG"));
    FASTPDF_CHECK(IsSupportedImagePath(L"photo.jpg"));
    FASTPDF_CHECK(IsSupportedImagePath(L"photo.JPG"));
    FASTPDF_CHECK(IsSupportedImagePath(L"photo.jpeg"));
    FASTPDF_CHECK(IsSupportedImagePath(L"photo.JPEG"));
    FASTPDF_CHECK(IsSupportedImagePath(L"graphic.bmp"));
    FASTPDF_CHECK(IsSupportedImagePath(L"graphic.BMP"));

    FASTPDF_CHECK(!IsSupportedImagePath(L"document.pdf"));
    FASTPDF_CHECK(!IsSupportedImagePath(L"document.txt"));
    FASTPDF_CHECK(!IsSupportedImagePath(L"archive.zip"));
    FASTPDF_CHECK(!IsSupportedImagePath(L"photo.png.exe"));
    FASTPDF_CHECK(!IsSupportedImagePath(L""));
}

FASTPDF_TEST(drop_routing_filter_images) {
    std::vector<std::wstring> input = {
        L"a.png", L"b.pdf", L"c.jpg", L"d.txt", L"e.bmp", L"f.jpeg"
    };
    std::vector<std::wstring> filtered = FilterSupportedImagePaths(input);
    FASTPDF_CHECK_EQ(filtered.size(), 4u);
    FASTPDF_CHECK(filtered[0] == L"a.png");
    FASTPDF_CHECK(filtered[1] == L"c.jpg");
    FASTPDF_CHECK(filtered[2] == L"e.bmp");
    FASTPDF_CHECK(filtered[3] == L"f.jpeg");
}

FASTPDF_TEST(image_placement_landscape_auto_orientation) {
    // 800 x 600 image (width > height) -> Must produce Landscape A4 page
    // A4 Landscape: width = 841.890, height = 595.276
    PagePlacement p = ComputeImagePlacement(800, 600, 18.0);
    FASTPDF_CHECK(p.isLandscape);
    FASTPDF_CHECK(std::abs(p.pageWidth - kA4HeightPoints) < 0.01);
    FASTPDF_CHECK(std::abs(p.pageHeight - kA4WidthPoints) < 0.01);

    // Margins are 18.0 pt on each side
    const double printableW = p.pageWidth - 36.0;  // 805.890
    const double printableH = p.pageHeight - 36.0; // 559.276

    // Aspect ratio preserved: 800 / 600 = 1.33333...
    const double imgAspect = p.imageWidth / p.imageHeight;
    FASTPDF_CHECK(std::abs(imgAspect - (800.0 / 600.0)) < 0.001);

    // Must fit entirely inside printable area
    FASTPDF_CHECK(p.imageWidth <= printableW + 0.001);
    FASTPDF_CHECK(p.imageHeight <= printableH + 0.001);

    // Centered placement
    FASTPDF_CHECK(p.imageX >= 17.99);
    FASTPDF_CHECK(p.imageY >= 17.99);
    FASTPDF_CHECK(p.imageX + p.imageWidth <= p.pageWidth - 17.99);
    FASTPDF_CHECK(p.imageY + p.imageHeight <= p.pageHeight - 17.99);
}

FASTPDF_TEST(image_placement_portrait_auto_orientation) {
    // 600 x 800 image (width < height) -> Must produce Portrait A4 page
    // A4 Portrait: width = 595.276, height = 841.890
    PagePlacement p = ComputeImagePlacement(600, 800, 18.0);
    FASTPDF_CHECK(!p.isLandscape);
    FASTPDF_CHECK(std::abs(p.pageWidth - kA4WidthPoints) < 0.01);
    FASTPDF_CHECK(std::abs(p.pageHeight - kA4HeightPoints) < 0.01);

    const double printableW = p.pageWidth - 36.0;
    const double printableH = p.pageHeight - 36.0;

    const double imgAspect = p.imageWidth / p.imageHeight;
    FASTPDF_CHECK(std::abs(imgAspect - (600.0 / 800.0)) < 0.001);

    FASTPDF_CHECK(p.imageWidth <= printableW + 0.001);
    FASTPDF_CHECK(p.imageHeight <= printableH + 0.001);

    FASTPDF_CHECK(p.imageX >= 17.99);
    FASTPDF_CHECK(p.imageY >= 17.99);
    FASTPDF_CHECK(p.imageX + p.imageWidth <= p.pageWidth - 17.99);
    FASTPDF_CHECK(p.imageY + p.imageHeight <= p.pageHeight - 17.99);
}

FASTPDF_TEST(image_placement_square_image) {
    // Square image (e.g. 500 x 500) -> Defaults to portrait A4
    PagePlacement p = ComputeImagePlacement(500, 500, 18.0);
    FASTPDF_CHECK(!p.isLandscape);
    FASTPDF_CHECK(std::abs(p.pageWidth - kA4WidthPoints) < 0.01);
    FASTPDF_CHECK(std::abs(p.pageHeight - kA4HeightPoints) < 0.01);

    // Image aspect ratio must be 1.0 (square, no stretch)
    FASTPDF_CHECK(std::abs(p.imageWidth - p.imageHeight) < 0.001);
}

FASTPDF_TEST(image_placement_invalid_dimensions) {
    PagePlacement p1 = ComputeImagePlacement(0, 100);
    FASTPDF_CHECK_EQ(p1.pageWidth, 0.0);
    FASTPDF_CHECK_EQ(p1.imageWidth, 0.0);

    PagePlacement p2 = ComputeImagePlacement(-50, 100);
    FASTPDF_CHECK_EQ(p2.pageWidth, 0.0);
}

int main() {
    return fastpdf::test::RunAll();
}
