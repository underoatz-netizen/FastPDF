#include "test_harness.h"
#include <fastpdf/core/print_layout.h>
#include <fastpdf/pdfium/PdfDocument.h>
#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/platform/win/ComInitializer.h>
#include <fastpdf/platform/win/PrintTypes.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winspool.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include "PrintCommon.h"
#include "PrintWorker.h"

using namespace fastpdf;

// Generates minimal multi-page test PDF in memory
static std::string GenerateTestPdfBytes() {
    std::string pdf;
    pdf.reserve(4096);
    pdf += "%PDF-1.4\n";
    std::vector<size_t> offsets;

    auto add_obj = [&](const std::string& body) {
        offsets.push_back(pdf.size());
        const size_t id = offsets.size();
        pdf += std::to_string(id) + " 0 obj\n" + body + "\nendobj\n";
        return id;
    };

    add_obj("<< /Type /Catalog /Pages 2 0 R >>");
    add_obj("<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>");
    // Page 1: Portrait A4 (595 x 842 pt)
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources <<>> >>");
    // Page 2: Landscape A4 (842 x 595 pt)
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources <<>> >>");
    // Page 3: Square (400 x 400 pt)
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Resources <<>> >>");

    const size_t xref_start = pdf.size();
    pdf += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t o : offsets) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%010zu 00000 n \n", o);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) +
           " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref_start) + "\n%%EOF\n";
    return pdf;
}

// Tests printer enumeration and paper listing
FASTPDF_TEST(Print_EnumerationAndPapers) {
    auto printers = app::print::EnumeratePrinters();
    // In any standard Windows environment or CI, printers may exist (e.g. Microsoft Print to PDF)
    // Verify that enumeration does not crash and handles printer names safely
    for (const auto& prn : printers) {
        FASTPDF_CHECK(!prn.name.empty());
        auto papers = app::print::EnumeratePapers(prn.name);
        // Each paper should have valid name if papers returned
        for (const auto& p : papers) {
            FASTPDF_CHECK(!p.name.empty());
        }
    }
}

// Tests end-to-end spooling with Microsoft Print to PDF if present
FASTPDF_TEST(Print_Smoke_MicrosoftPrintToPdf) {
    std::string pdfData = GenerateTestPdfBytes();
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(pdfData.begin(), pdfData.end());

    pdfium::PdfDocument doc(bytes);
    FASTPDF_CHECK(doc.isOpen());
    FASTPDF_CHECK_EQ(3, doc.pageCount());

    // Check if Microsoft Print to PDF is installed
    auto printers = app::print::EnumeratePrinters();
    std::wstring pdfPrinterName;
    for (const auto& prn : printers) {
        if (prn.name.find(L"Microsoft Print to PDF") != std::wstring::npos) {
            pdfPrinterName = prn.name;
            break;
        }
    }

    if (pdfPrinterName.empty()) {
        std::printf("Notice: 'Microsoft Print to PDF' not found on system; skipping output spool smoke.\n");
        return;
    }

    // Set up temp output file
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    wchar_t tempFile[MAX_PATH]{};
    GetTempFileNameW(tempPath, L"PRN", 0, tempFile);
    DeleteFileW(tempFile);
    std::wstring outputFile = std::wstring(tempFile) + L".pdf";

    // Set up print options
    app::print::PrintDialogOptions options{};
    options.printerName = pdfPrinterName;
    options.scaleMode = core::print::PrintScaleMode::Fit;
    options.orientation = core::print::PrintOrientation::Auto;
    options.copies = 1;

    // Direct Microsoft Print to PDF output to file via DOCINFO
    platform::win::UniqueHDC hdc(CreateDCW(L"WINSPOOL", pdfPrinterName.c_str(), nullptr, nullptr));
    FASTPDF_CHECK(static_cast<bool>(hdc));

    DOCINFOW di{};
    di.cbSize = sizeof(DOCINFOW);
    di.lpszDocName = L"FastPDF Test Print";
    di.lpszOutput = outputFile.c_str(); // Diverts PORTPROMPT to file without UI prompt!

    int startDocRes = StartDocW(hdc.get(), &di);
    FASTPDF_CHECK(startDocRes > 0);

    // Print pages 0 and 1
    std::vector<int> pages = {0, 1};
    int pagesPrinted = 0;

    for (int pIdx : pages) {
        double wPt = 0.0, hPt = 0.0;
        FASTPDF_CHECK(doc.pageSize(pIdx, wPt, hPt));

        FASTPDF_CHECK(StartPage(hdc.get()) > 0);

        core::print::TargetMetrics m{};
        m.dpiX = GetDeviceCaps(hdc.get(), LOGPIXELSX);
        m.dpiY = GetDeviceCaps(hdc.get(), LOGPIXELSY);
        m.printableWidth = GetDeviceCaps(hdc.get(), HORZRES);
        m.printableHeight = GetDeviceCaps(hdc.get(), VERTRES);
        m.physicalWidth = GetDeviceCaps(hdc.get(), PHYSICALWIDTH);
        m.physicalHeight = GetDeviceCaps(hdc.get(), PHYSICALHEIGHT);
        m.hardwareMarginLeft = GetDeviceCaps(hdc.get(), PHYSICALOFFSETX);
        m.hardwareMarginTop = GetDeviceCaps(hdc.get(), PHYSICALOFFSETY);

        auto placement = core::print::PrintLayout::ComputePlacement(
            wPt, hPt, m, options.scaleMode, options.orientation);

        const int rot = (placement.effectiveOrientation == core::print::PrintOrientation::Landscape &&
                         m.printableWidth <= m.printableHeight) ? 1 : 0;

        bool ok = doc.renderPageToDC(pIdx, hdc.get(),
                                     placement.destX, placement.destY,
                                     placement.destWidth, placement.destHeight,
                                     rot);
        FASTPDF_CHECK(ok);

        FASTPDF_CHECK(EndPage(hdc.get()) > 0);
        pagesPrinted++;
    }

    FASTPDF_CHECK(EndDoc(hdc.get()) > 0);
    FASTPDF_CHECK_EQ(2, pagesPrinted);

    // Verify printed PDF file exists and has non-zero size
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(outputFile, ec);
    FASTPDF_CHECK(!ec);
    FASTPDF_CHECK(fileSize > 0u);

    // Clean up temp output file
    std::filesystem::remove(outputFile, ec);
}

int main() {
    fastpdf::platform::win::ComInitializer com;
    fastpdf::pdfium::PdfiumLibrary lib;
    return fastpdf::test::RunAll();
}
