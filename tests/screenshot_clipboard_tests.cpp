// Focused unit tests for WIC PNG encoding/decoding and clipboard safety.
// Tests run without requiring an active GUI or PDFium.

#include <windows.h>
#include <vector>
#include "test_harness.h"
#include <fastpdf/platform/win/ComInitializer.h>
#include "ScreenshotClipboard.h"

using namespace fastpdf::app::screenshot;

FASTPDF_TEST(screenshot_wic_png_roundtrip) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    // Create a 16x16 test pattern with known BGRA colors
    fastpdf::renderer::Bitmap original;
    original.width = 16;
    original.height = 16;
    original.stride = 64;
    original.data.resize(1024);

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int idx = y * 64 + x * 4;
            original.data[idx + 0] = static_cast<std::uint8_t>(x * 16);     // B
            original.data[idx + 1] = static_cast<std::uint8_t>(y * 16);     // G
            original.data[idx + 2] = static_cast<std::uint8_t>((x + y) * 8); // R
            original.data[idx + 3] = 255;                                   // A
        }
    }

    // Encode to PNG bytes
    const std::vector<std::uint8_t> pngBytes = EncodePngWithWic(original);
    FASTPDF_CHECK(!pngBytes.empty());
    // PNG file signature: 0x89 'P' 'N' 'G' 0x0D 0x0A 0x1A 0x0A
    FASTPDF_CHECK_EQ(pngBytes[0], 0x89);
    FASTPDF_CHECK_EQ(pngBytes[1], 'P');
    FASTPDF_CHECK_EQ(pngBytes[2], 'N');
    FASTPDF_CHECK_EQ(pngBytes[3], 'G');

    // Decode back from PNG bytes
    int decWidth = 0;
    int decHeight = 0;
    std::vector<std::uint8_t> decPixels;
    const bool ok = DecodePngWithWic(pngBytes, decWidth, decHeight, decPixels);
    FASTPDF_CHECK(ok);
    FASTPDF_CHECK_EQ(decWidth, 16);
    FASTPDF_CHECK_EQ(decHeight, 16);
    FASTPDF_CHECK_EQ(decPixels.size(), 1024u);

    // Verify lossless match of RGB bytes
    for (std::size_t i = 0; i < decPixels.size(); i += 4) {
        FASTPDF_CHECK_EQ(decPixels[i + 0], original.data[i + 0]); // B
        FASTPDF_CHECK_EQ(decPixels[i + 1], original.data[i + 1]); // G
        FASTPDF_CHECK_EQ(decPixels[i + 2], original.data[i + 2]); // R
    }
}

FASTPDF_TEST(screenshot_wic_invalid_inputs) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    fastpdf::renderer::Bitmap emptyBmp;
    const auto pngBytes = EncodePngWithWic(emptyBmp);
    FASTPDF_CHECK(pngBytes.empty());

    int w = 0, h = 0;
    std::vector<std::uint8_t> out;
    FASTPDF_CHECK(!DecodePngWithWic({}, w, h, out));
    const std::vector<std::uint8_t> corrupt = {1, 2, 3, 4, 5};
    FASTPDF_CHECK(!DecodePngWithWic(corrupt, w, h, out));
}

FASTPDF_TEST(screenshot_clipboard_safe_with_null_window) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    fastpdf::renderer::Bitmap emptyBmp;
    FASTPDF_CHECK(!CopyBitmapToClipboard(nullptr, emptyBmp));

    // A small valid bitmap copied to clipboard with nullptr or valid HWND
    fastpdf::renderer::Bitmap validBmp;
    validBmp.width = 4;
    validBmp.height = 4;
    validBmp.stride = 16;
    validBmp.data.assign(64, 255);

    const bool copied = CopyBitmapToClipboard(nullptr, validBmp);
    FASTPDF_CHECK(copied);
}

int main() { return fastpdf::test::RunAll(); }
