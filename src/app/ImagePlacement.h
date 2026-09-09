#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fastpdf::app::convert {

// Represents an image decoded via WIC ready to be embedded into a PDF page.
// Pixel data is 32-bpp BGRA. If hasAlpha is true, alpha channel values may vary.
// If hasAlpha is false, every pixel's alpha is treated as 255 (opaque).
struct DecodedImage {
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    std::vector<std::uint8_t> bgraPixels;
};

// Decodes an image file (PNG, JPEG, JPG, BMP) using Windows Imaging Component (WIC).
// Converts pixels to 32bpp BGRA format and checks if image uses alpha channel.
// Returns true on success, false if file cannot be read, format unsupported, or dimensions invalid.
bool DecodeImageFileWithWic(const std::wstring& filePath, DecodedImage& outImage) noexcept;

// A4 paper dimensions in PDF points (72 points = 1 inch, 25.4 mm = 1 inch):
// 210 mm = 8.2677 inches = 595.27559 points
// 297 mm = 11.6929 inches = 841.88976 points
inline constexpr double kA4WidthPoints = 595.276;
inline constexpr double kA4HeightPoints = 841.890;
inline constexpr double kDefaultMarginPoints = 18.0; // small fixed margin (~0.25 inch / 6.35 mm)

// Page geometry for placing an image onto an A4 page:
// Auto portrait/landscape: if image is wider than tall (width > height), page is landscape.
// Otherwise, portrait.
// Image is scaled to fit within printable area (A4 minus margins) preserving aspect ratio (no stretch).
// Position is centered within printable area.
struct PagePlacement {
    double pageWidth = 0.0;
    double pageHeight = 0.0;
    double imageX = 0.0;
    double imageY = 0.0;
    double imageWidth = 0.0;
    double imageHeight = 0.0;
    bool isLandscape = false;
};

// Calculates page dimensions and image placement for a given image width and height.
PagePlacement ComputeImagePlacement(int imageWidthPixels, int imageHeightPixels,
                                    double marginPoints = kDefaultMarginPoints) noexcept;

} // namespace fastpdf::app::convert
