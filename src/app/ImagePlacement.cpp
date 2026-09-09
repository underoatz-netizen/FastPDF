#include "ImagePlacement.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")

namespace fastpdf::app::convert {

namespace {

// Checks if any pixel has alpha != 255
bool CheckAlphaUsed(const std::uint8_t* bgra, size_t pixelCount) noexcept {
    for (size_t i = 0; i < pixelCount; ++i) {
        if (bgra[i * 4 + 3] != 255) {
            return true;
        }
    }
    return false;
}

} // namespace

bool DecodeImageFileWithWic(const std::wstring& filePath, DecodedImage& outImage) noexcept {
    outImage.width = 0;
    outImage.height = 0;
    outImage.hasAlpha = false;
    outImage.bgraPixels.clear();

    if (filePath.empty()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        filePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        return false;
    }

    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || frameCount == 0) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    // Hard dimension limit: [1, 16384] to prevent memory overflow
    if (FAILED(hr) || width == 0 || height == 0 || width > 16384 || height > 16384) {
        return false;
    }

    // Inspect native pixel format to see if source format can have alpha
    WICPixelFormatGUID sourceFormat{};
    bool sourceMayHaveAlpha = false;
    if (SUCCEEDED(frame->GetPixelFormat(&sourceFormat))) {
        if (sourceFormat == GUID_WICPixelFormat32bppBGRA ||
            sourceFormat == GUID_WICPixelFormat32bppRGBA ||
            sourceFormat == GUID_WICPixelFormat32bppPRGBA ||
            sourceFormat == GUID_WICPixelFormat32bppPBGRA ||
            sourceFormat == GUID_WICPixelFormat64bppRGBA ||
            sourceFormat == GUID_WICPixelFormat64bppPRGBA) {
            sourceMayHaveAlpha = true;
        }
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        return false;
    }

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return false;
    }

    const UINT stride = width * 4;
    const size_t totalBytes = static_cast<size_t>(stride) * static_cast<size_t>(height);
    std::vector<std::uint8_t> pixels(totalBytes);

    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(totalBytes), pixels.data());
    if (FAILED(hr)) {
        return false;
    }

    bool hasAlpha = false;
    if (sourceMayHaveAlpha) {
        hasAlpha = CheckAlphaUsed(pixels.data(), static_cast<size_t>(width) * static_cast<size_t>(height));
    }

    outImage.width = static_cast<int>(width);
    outImage.height = static_cast<int>(height);
    outImage.hasAlpha = hasAlpha;
    outImage.bgraPixels = std::move(pixels);
    return true;
}

PagePlacement ComputeImagePlacement(int imageWidthPixels, int imageHeightPixels,
                                    double marginPoints) noexcept {
    PagePlacement placement;
    if (imageWidthPixels <= 0 || imageHeightPixels <= 0) {
        return placement;
    }

    // Auto portrait/landscape:
    // If width > height, use landscape (pageWidth = 841.890, pageHeight = 595.276)
    // Otherwise, portrait (pageWidth = 595.276, pageHeight = 841.890)
    if (imageWidthPixels > imageHeightPixels) {
        placement.isLandscape = true;
        placement.pageWidth = kA4HeightPoints;
        placement.pageHeight = kA4WidthPoints;
    } else {
        placement.isLandscape = false;
        placement.pageWidth = kA4WidthPoints;
        placement.pageHeight = kA4HeightPoints;
    }

    // Clamp margin to reasonable bounds (must leave printable area)
    const double maxMarginX = placement.pageWidth * 0.45;
    const double maxMarginY = placement.pageHeight * 0.45;
    const double actualMargin = std::max(0.0, std::min(marginPoints, std::min(maxMarginX, maxMarginY)));

    const double printableWidth = placement.pageWidth - (2.0 * actualMargin);
    const double printableHeight = placement.pageHeight - (2.0 * actualMargin);

    const double scaleX = printableWidth / static_cast<double>(imageWidthPixels);
    const double scaleY = printableHeight / static_cast<double>(imageHeightPixels);
    const double scale = std::min(scaleX, scaleY);

    placement.imageWidth = static_cast<double>(imageWidthPixels) * scale;
    placement.imageHeight = static_cast<double>(imageHeightPixels) * scale;

    // Centered in the page
    placement.imageX = actualMargin + (printableWidth - placement.imageWidth) / 2.0;
    placement.imageY = actualMargin + (printableHeight - placement.imageHeight) / 2.0;

    return placement;
}

} // namespace fastpdf::app::convert
