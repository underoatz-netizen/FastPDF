#include "fastpdf/core/print_layout.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <set>

namespace fastpdf::core::print {

namespace {

std::wstring_view Trim(std::wstring_view s) noexcept {
    while (!s.empty() && std::iswspace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::iswspace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool ParseInt(std::wstring_view s, int& outVal) noexcept {
    s = Trim(s);
    if (s.empty()) {
        return false;
    }
    long long val = 0;
    for (wchar_t ch : s) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        val = val * 10 + (ch - L'0');
        if (val > 1'000'000) {
            return false;
        }
    }
    outVal = static_cast<int>(val);
    return true;
}

} // namespace

PrintOrientation PrintLayout::ResolveOrientation(
    PrintOrientation requested,
    double pageWidthPoints,
    double pageHeightPoints) noexcept {
    if (requested == PrintOrientation::Portrait) {
        return PrintOrientation::Portrait;
    }
    if (requested == PrintOrientation::Landscape) {
        return PrintOrientation::Landscape;
    }
    // Auto: match the aspect ratio of the page
    return (pageWidthPoints > pageHeightPoints) ? PrintOrientation::Landscape : PrintOrientation::Portrait;
}

PagePlacement PrintLayout::ComputePlacement(
    double pageWidthPoints,
    double pageHeightPoints,
    const TargetMetrics& metrics,
    PrintScaleMode scaleMode,
    PrintOrientation orientation) noexcept {

    PagePlacement placement{};

    if (pageWidthPoints <= 0.0 || pageHeightPoints <= 0.0 ||
        metrics.printableWidth <= 0 || metrics.printableHeight <= 0 ||
        metrics.dpiX <= 0 || metrics.dpiY <= 0) {
        return placement;
    }

    placement.effectiveOrientation = ResolveOrientation(orientation, pageWidthPoints, pageHeightPoints);

    // Depending on orientation vs target printable area:
    // If the target printer device is rotated by DEVMODE, printableWidth and printableHeight
    // reflect the paper orientation (e.g. landscape means printableWidth > printableHeight).
    // If effective orientation requires landscape relative to portrait paper or vice-versa,
    // we take into account dimensions.
    // However, when rendering, we consider the target paper's orientation.
    // If the page's effective orientation matches the paper, orient dimensions directly.
    double srcW = pageWidthPoints;
    double srcH = pageHeightPoints;

    // Convert PDF points to target device pixels (72 points per inch)
    // 1 pt = dpi / 72.0 pixels
    const double pxPerPtX = static_cast<double>(metrics.dpiX) / 72.0;
    const double pxPerPtY = static_cast<double>(metrics.dpiY) / 72.0;

    double naturalWidthPx = srcW * pxPerPtX;
    double naturalHeightPx = srcH * pxPerPtY;

    // Check if effective orientation requires 90-degree swap of content relative to target
    // (e.g. target is portrait, effective orientation is landscape without DEVMODE rotation)
    const bool targetIsLandscape = metrics.printableWidth > metrics.printableHeight;
    const bool contentIsLandscape = (placement.effectiveOrientation == PrintOrientation::Landscape);
    
    // Note: When DEVMODE dmOrientation is set, printer HDC printableWidth/printableHeight is already
    // oriented. If targetIsLandscape != contentIsLandscape, content dimensions rotate 90 degrees to fit.
    if (targetIsLandscape != contentIsLandscape) {
        std::swap(naturalWidthPx, naturalHeightPx);
    }

    const double availW = static_cast<double>(metrics.printableWidth);
    const double availH = static_cast<double>(metrics.printableHeight);

    if (scaleMode == PrintScaleMode::Fit) {
        // Fit proportionally inside printable area
        const double scaleX = availW / naturalWidthPx;
        const double scaleY = availH / naturalHeightPx;
        const double scale = std::min(scaleX, scaleY);

        placement.scale = scale;
        placement.destWidth = std::max(1, static_cast<int>(std::round(naturalWidthPx * scale)));
        placement.destHeight = std::max(1, static_cast<int>(std::round(naturalHeightPx * scale)));
        placement.willCrop = false;
        placement.cropRatio = 0.0;
    } else {
        // Actual Size (100% scale)
        placement.scale = 1.0;
        placement.destWidth = std::max(1, static_cast<int>(std::round(naturalWidthPx)));
        placement.destHeight = std::max(1, static_cast<int>(std::round(naturalHeightPx)));

        // Check if content exceeds printable bounds (hardware margins or paper size)
        if (placement.destWidth > metrics.printableWidth || placement.destHeight > metrics.printableHeight) {
            placement.willCrop = true;
            const double contentArea = naturalWidthPx * naturalHeightPx;
            const double visibleW = std::min(naturalWidthPx, availW);
            const double visibleH = std::min(naturalHeightPx, availH);
            const double visibleArea = visibleW * visibleH;
            placement.cropRatio = (contentArea > 0.0) ? std::max(0.0, 1.0 - (visibleArea / contentArea)) : 0.0;
        } else {
            placement.willCrop = false;
            placement.cropRatio = 0.0;
        }
    }

    // Center the page within the printable area
    placement.destX = (metrics.printableWidth - placement.destWidth) / 2;
    placement.destY = (metrics.printableHeight - placement.destHeight) / 2;

    return placement;
}

bool ParsePrintPageRange(std::wstring_view text, int totalPages,
                         std::vector<int>& outPageIndices) noexcept {
    outPageIndices.clear();
    if (totalPages <= 0) {
        return false;
    }

    std::set<int> uniquePages;
    std::size_t start = 0;

    while (start < text.size()) {
        const std::size_t commaPos = text.find(L',', start);
        const std::wstring_view token = (commaPos == std::wstring_view::npos)
                                            ? text.substr(start)
                                            : text.substr(start, commaPos - start);
        start = (commaPos == std::wstring_view::npos) ? text.size() : commaPos + 1;

        const std::wstring_view trimmedToken = Trim(token);
        if (trimmedToken.empty()) {
            continue;
        }

        const std::size_t dashPos = trimmedToken.find(L'-');
        if (dashPos != std::wstring_view::npos) {
            const std::wstring_view p1Str = trimmedToken.substr(0, dashPos);
            const std::wstring_view p2Str = trimmedToken.substr(dashPos + 1);
            int p1 = 0;
            int p2 = 0;
            if (!ParseInt(p1Str, p1) || !ParseInt(p2Str, p2)) {
                return false;
            }
            if (p1 < 1 || p2 < 1) {
                return false;
            }
            if (p1 > p2) {
                std::swap(p1, p2);
            }
            const int clampedStart = std::max(1, p1);
            const int clampedEnd = std::min(totalPages, p2);
            for (int p = clampedStart; p <= clampedEnd; ++p) {
                uniquePages.insert(p - 1);
            }
        } else {
            int p = 0;
            if (!ParseInt(trimmedToken, p)) {
                return false;
            }
            if (p >= 1 && p <= totalPages) {
                uniquePages.insert(p - 1);
            }
        }
    }

    if (uniquePages.empty()) {
        return false;
    }

    outPageIndices.assign(uniquePages.begin(), uniquePages.end());
    return true;
}

bool ResolvePagesToPrint(PageSelectionMode mode, int currentPage,
                         std::wstring_view customRangeText, int totalPages,
                         std::vector<int>& outPageIndices) noexcept {
    outPageIndices.clear();
    if (totalPages <= 0) {
        return false;
    }

    switch (mode) {
        case PageSelectionMode::All:
            outPageIndices.resize(totalPages);
            for (int i = 0; i < totalPages; ++i) {
                outPageIndices[i] = i;
            }
            return true;

        case PageSelectionMode::Current:
            if (currentPage >= 0 && currentPage < totalPages) {
                outPageIndices.push_back(currentPage);
                return true;
            }
            return false;

        case PageSelectionMode::Custom:
            return ParsePrintPageRange(customRangeText, totalPages, outPageIndices);
    }
    return false;
}

} // namespace fastpdf::core::print
