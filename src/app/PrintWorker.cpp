#include "PrintWorker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winspool.h>

#include <algorithm>
#include <cmath>

#include "fastpdf/platform/win/PrintTypes.h"
#include "fastpdf/renderer/bitmap.h"

namespace fastpdf::app::print {

namespace {

// Helper to fill TargetMetrics from an HDC
core::print::TargetMetrics GetTargetMetrics(HDC hdc) noexcept {
    core::print::TargetMetrics m{};
    m.dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    m.dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    m.physicalWidth = GetDeviceCaps(hdc, PHYSICALWIDTH);
    m.physicalHeight = GetDeviceCaps(hdc, PHYSICALHEIGHT);
    m.printableWidth = GetDeviceCaps(hdc, HORZRES);
    m.printableHeight = GetDeviceCaps(hdc, VERTRES);
    m.hardwareMarginLeft = GetDeviceCaps(hdc, PHYSICALOFFSETX);
    m.hardwareMarginTop = GetDeviceCaps(hdc, PHYSICALOFFSETY);
    return m;
}

// Fallback raster printing if renderPageToDC fails
bool FallbackRasterPrint(
    const pdfium::PdfDocument& doc,
    int pageIndex,
    HDC hdc,
    const core::print::PagePlacement& placement) noexcept {

    if (placement.destWidth <= 0 || placement.destHeight <= 0) {
        return false;
    }

    renderer::Bitmap bmp;
    double duration = 0.0;
    // Render bitmap at destination pixel resolution (clamped to max reasonable size)
    const int renderW = std::clamp(placement.destWidth, 1, 4096);
    const int renderH = std::clamp(placement.destHeight, 1, 4096);

    const int rot = (placement.effectiveOrientation == core::print::PrintOrientation::Landscape) ? 1 : 0;
    if (!doc.renderPage(pageIndex, renderW, renderH, rot, bmp, duration)) {
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bmp.width;
    bmi.bmiHeader.biHeight = -bmp.height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    const int lines = StretchDIBits(
        hdc,
        placement.destX, placement.destY, placement.destWidth, placement.destHeight,
        0, 0, bmp.width, bmp.height,
        bmp.data.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY);

    return lines != GDI_ERROR && lines > 0;
}

} // namespace

PrintJobResult PrintWorker::ExecutePrintJob(
    const pdfium::PdfSource& source,
    const PrintDialogOptions& options,
    const std::vector<int>& pageIndices,
    std::atomic<bool>& cancelFlag,
    std::function<void(const PrintJobProgress&)> onProgress) noexcept {

    PrintJobResult result{};
    if (!source.isValid() || pageIndices.empty() || options.printerName.empty()) {
        result.errorMessage = L"Invalid print request or empty page list.";
        return result;
    }

    // Open printer to get default DEVMODE
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterW(const_cast<LPWSTR>(options.printerName.c_str()), &hPrinter, nullptr)) {
        result.errorMessage = L"Could not open printer.";
        return result;
    }

    LONG devModeSize = DocumentPropertiesW(nullptr, hPrinter, const_cast<LPWSTR>(options.printerName.c_str()),
                                           nullptr, nullptr, 0);
    std::vector<BYTE> devModeBuf;
    DEVMODEW* pDevMode = nullptr;
    if (devModeSize > 0) {
        devModeBuf.resize(devModeSize);
        pDevMode = reinterpret_cast<DEVMODEW*>(devModeBuf.data());
        if (DocumentPropertiesW(nullptr, hPrinter, const_cast<LPWSTR>(options.printerName.c_str()),
                                pDevMode, nullptr, DM_OUT_BUFFER) < 0) {
            pDevMode = nullptr;
        }
    }
    ClosePrinter(hPrinter);

    if (pDevMode != nullptr) {
        // Apply copies
        if (options.copies > 1) {
            pDevMode->dmCopies = static_cast<short>(options.copies);
            pDevMode->dmFields |= DM_COPIES;
        }
        // Apply paper size if selected
        if (options.selectedPaperSize > 0) {
            pDevMode->dmPaperSize = options.selectedPaperSize;
            pDevMode->dmFields |= DM_PAPERSIZE;
        }
        // If orientation is explicitly requested:
        if (options.orientation == core::print::PrintOrientation::Landscape) {
            pDevMode->dmOrientation = DMORIENT_LANDSCAPE;
            pDevMode->dmFields |= DM_ORIENTATION;
        } else if (options.orientation == core::print::PrintOrientation::Portrait) {
            pDevMode->dmOrientation = DMORIENT_PORTRAIT;
            pDevMode->dmFields |= DM_ORIENTATION;
        }
    }

    // Create printer DC
    platform::win::UniqueHDC hdc(CreateDCW(L"WINSPOOL", options.printerName.c_str(), nullptr, pDevMode));
    if (!hdc) {
        result.errorMessage = L"Failed to create printer device context.";
        return result;
    }

    // Open PDF document on this worker thread
    pdfium::PdfDocument doc(source);
    if (!doc.isOpen()) {
        result.errorMessage = L"Failed to open PDF document for printing.";
        return result;
    }

    DOCINFOW docInfo{};
    docInfo.cbSize = sizeof(DOCINFOW);
    docInfo.lpszDocName = L"FastPDF Document";

    if (StartDocW(hdc.get(), &docInfo) <= 0) {
        result.errorMessage = L"Failed to start printer document job.";
        return result;
    }

    const int totalJobPages = static_cast<int>(pageIndices.size());
    int currentJobPage = 0;

    for (int pageIndex : pageIndices) {
        if (cancelFlag.load(std::memory_order_relaxed)) {
            result.canceled = true;
            AbortDoc(hdc.get());
            return result;
        }

        currentJobPage++;
        if (onProgress) {
            PrintJobProgress prog{};
            prog.currentPageIndex = pageIndex;
            prog.currentJobPage = currentJobPage;
            prog.totalJobPages = totalJobPages;
            prog.canceled = false;
            onProgress(prog);
        }

        double widthPt = 0.0;
        double heightPt = 0.0;
        if (!doc.pageSize(pageIndex, widthPt, heightPt)) {
            continue;
        }

        // If Auto orientation, check if we need to dynamically adjust page orientation
        // in devMode per page. If ResetDC is supported, we can update DEVMODE.
        // Otherwise ComputePlacement handles aspect-based layout rotation.
        if (options.orientation == core::print::PrintOrientation::Auto && pDevMode != nullptr) {
            const short desiredOrient = (widthPt > heightPt) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            if (pDevMode->dmOrientation != desiredOrient) {
                pDevMode->dmOrientation = desiredOrient;
                pDevMode->dmFields |= DM_ORIENTATION;
                ResetDCW(hdc.get(), pDevMode);
            }
        }

        if (StartPage(hdc.get()) <= 0) {
            AbortDoc(hdc.get());
            result.errorMessage = L"Printer StartPage failed.";
            return result;
        }

        const auto metrics = GetTargetMetrics(hdc.get());

        // Check if ActualSize would crop
        auto placement = core::print::PrintLayout::ComputePlacement(
            widthPt, heightPt, metrics, options.scaleMode, options.orientation);

        // Render page directly to HDC (PDFium vector path)
        const int rot = (placement.effectiveOrientation == core::print::PrintOrientation::Landscape &&
                         metrics.printableWidth <= metrics.printableHeight) ? 1 : 0;

        bool renderSuccess = doc.renderPageToDC(
            pageIndex, hdc.get(),
            placement.destX, placement.destY,
            placement.destWidth, placement.destHeight,
            rot);

        // Fallback to raster path if direct HDC render fails
        if (!renderSuccess) {
            renderSuccess = FallbackRasterPrint(doc, pageIndex, hdc.get(), placement);
        }

        if (EndPage(hdc.get()) <= 0) {
            AbortDoc(hdc.get());
            result.errorMessage = L"Printer EndPage failed.";
            return result;
        }

        if (renderSuccess) {
            result.pagesPrinted++;
        }
    }

    if (EndDoc(hdc.get()) <= 0) {
        result.errorMessage = L"Printer EndDoc failed.";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace fastpdf::app::print
