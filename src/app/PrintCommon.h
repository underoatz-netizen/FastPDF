#pragma once

#include <string>
#include <vector>
#include "fastpdf/core/print_layout.h"

namespace fastpdf::app::print {

struct PrinterInfo {
    std::wstring name;
    bool isDefault = false;
};

struct PaperInfo {
    std::wstring name;
    short paperSize = 0; // DMPAPER_* constant
    int widthMm = 0;
    int heightMm = 0;
};

struct PrintDialogOptions {
    std::wstring printerName;
    core::print::PageSelectionMode selectionMode = core::print::PageSelectionMode::All;
    int currentPage = 0;
    std::wstring customRangeText;
    core::print::PrintScaleMode scaleMode = core::print::PrintScaleMode::Fit;
    core::print::PrintOrientation orientation = core::print::PrintOrientation::Auto;
    short selectedPaperSize = 0; // 0 = use printer default
    int copies = 1;
};

// Enumerates local and network printers visible to the current Windows user
std::vector<PrinterInfo> EnumeratePrinters() noexcept;

// Gets system default printer name
std::wstring GetDefaultPrinterName() noexcept;

// Enumerates supported paper sizes for the specified printer
std::vector<PaperInfo> EnumeratePapers(const std::wstring& printerName) noexcept;

} // namespace fastpdf::app::print
