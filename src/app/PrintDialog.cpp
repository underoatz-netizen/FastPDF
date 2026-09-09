#include "PrintDialog.h"

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
#include <cwctype>
#include <iomanip>
#include <sstream>

#include "fastpdf/platform/win/PrintTypes.h"

namespace fastpdf::app::print {

namespace {

inline HMENU ControlIdToHmenu(int id) noexcept {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

// Control IDs
constexpr int IDC_GRP_PRINTER     = 2001;
constexpr int IDC_COMBO_PRINTER   = 2002;

constexpr int IDC_GRP_PAGES       = 2010;
constexpr int IDC_RADIO_ALL       = 2011;
constexpr int IDC_RADIO_CURRENT   = 2012;
constexpr int IDC_RADIO_CUSTOM    = 2013;
constexpr int IDC_EDIT_CUSTOM     = 2014;

constexpr int IDC_GRP_PAPER       = 2020;
constexpr int IDC_COMBO_PAPER     = 2021;

constexpr int IDC_GRP_SCALE       = 2030;
constexpr int IDC_RADIO_FIT       = 2031;
constexpr int IDC_RADIO_ACTUAL    = 2032;

constexpr int IDC_GRP_ORIENT      = 2040;
constexpr int IDC_RADIO_AUTO      = 2041;
constexpr int IDC_RADIO_PORTRAIT  = 2042;
constexpr int IDC_RADIO_LANDSCAPE = 2043;

constexpr int IDC_LBL_COPIES      = 2050;
constexpr int IDC_EDIT_COPIES     = 2051;

constexpr int IDC_GRP_PREVIEW     = 2060;
constexpr int IDC_PREVIEW_CANVAS  = 2061;

constexpr int IDC_PROGRESS_BAR    = 2070;
constexpr int IDC_STATUS_TEXT     = 2071;

constexpr int IDC_BTN_PRINT       = IDOK;
constexpr int IDC_BTN_CANCEL      = IDCANCEL;

} // namespace

PrintDialog::~PrintDialog() {
    cancelFlag_.store(true, std::memory_order_relaxed);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void PrintDialog::Show(HWND hwndParent, const std::wstring& pdfPath,
                       const pdfium::PdfSource& pdfSource,
                       int currentPage, int totalPages) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        return;
    }
    hwndParent_ = hwndParent;
    pdfPath_ = pdfPath;
    pdfSource_ = pdfSource;
    currentPage_ = currentPage;
    totalPages_ = totalPages;

    printers_ = EnumeratePrinters();

    options_.currentPage = currentPage;
    options_.selectionMode = core::print::PageSelectionMode::All;
    options_.scaleMode = core::print::PrintScaleMode::Fit;
    options_.orientation = core::print::PrintOrientation::Auto;
    options_.copies = 1;
    options_.selectedPaperSize = 0;

    if (!printers_.empty()) {
        options_.printerName = printers_[0].name;
    }

    // Inspect first page size for default preview
    if (pdfSource_.isValid()) {
        pdfium::PdfDocument doc(pdfSource_);
        if (doc.isOpen() && totalPages > 0) {
            const int previewIdx = std::clamp(currentPage, 0, totalPages - 1);
            doc.pageSize(previewIdx, previewPageWidthPt_, previewPageHeightPt_);
        }
    }

    // Create modal dialog via DLGTEMPLATE
    std::vector<std::uint8_t> dlgBuffer(2048, 0);
    auto* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuffer.data());
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->dwExtendedStyle = 0;
    dlg->cdit = 0;
    dlg->x = 0;
    dlg->y = 0;
    dlg->cx = 400;
    dlg->cy = 280;

    DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        dlg,
        hwndParent,
        DialogProc,
        reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK PrintDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PrintDialog* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<PrintDialog*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PrintDialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }

    if (self != nullptr) {
        return self->HandleDialogMessage(hwnd, msg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR PrintDialog::HandleDialogMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_INITDIALOG: {
            hwndDialog_ = hwnd;
            OnInitDialog(hwnd);
            return TRUE;
        }
        case WM_PRINT_PROGRESS: {
            auto* prog = reinterpret_cast<PrintJobProgress*>(lParam);
            if (prog != nullptr) {
                OnProgress(*prog);
                delete prog;
            }
            return TRUE;
        }
        case WM_PRINT_FINISHED: {
            auto* res = reinterpret_cast<PrintJobResult*>(lParam);
            if (res != nullptr) {
                OnFinished(*res);
                delete res;
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (id == IDC_COMBO_PRINTER && code == CBN_SELCHANGE) {
                OnPrinterChanged(hwnd);
                return TRUE;
            }
            if (id == IDC_COMBO_PAPER && code == CBN_SELCHANGE) {
                OnPaperChanged(hwnd);
                return TRUE;
            }
            if (code == BN_CLICKED) {
                if (id == IDC_RADIO_ALL || id == IDC_RADIO_CURRENT || id == IDC_RADIO_CUSTOM ||
                    id == IDC_RADIO_FIT || id == IDC_RADIO_ACTUAL ||
                    id == IDC_RADIO_AUTO || id == IDC_RADIO_PORTRAIT || id == IDC_RADIO_LANDSCAPE) {
                    OnOptionChanged(hwnd);
                    return TRUE;
                }
                if (id == IDC_BTN_PRINT) {
                    OnStartPrint(hwnd);
                    return TRUE;
                }
                if (id == IDC_BTN_CANCEL) {
                    OnCancelPrint(hwnd);
                    return TRUE;
                }
            }
            if (code == EN_CHANGE && (id == IDC_EDIT_CUSTOM || id == IDC_EDIT_COPIES)) {
                OnOptionChanged(hwnd);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* pDis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (pDis->CtlID == IDC_PREVIEW_CANVAS) {
                DrawPreview(pDis->hDC, pDis->rcItem);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE: {
            OnCancelPrint(hwnd);
            return TRUE;
        }
        default:
            break;
    }
    return FALSE;
}

void PrintDialog::OnInitDialog(HWND hwnd) noexcept {
    SetWindowTextW(hwnd, L"Print");

    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // Left Column: Controls (x: 15..280)
    // 1. Printer selection
    CreateWindowExW(0, L"BUTTON", L"Printer",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, 10, 270, 50, hwnd, ControlIdToHmenu(IDC_GRP_PRINTER), nullptr, nullptr);

    HWND hComboPrinter = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                         WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                         25, 28, 250, 200, hwnd, ControlIdToHmenu(IDC_COMBO_PRINTER), nullptr, nullptr);

    for (const auto& prn : printers_) {
        std::wstring itemText = prn.name;
        if (prn.isDefault) {
            itemText += L" (Default)";
        }
        SendMessageW(hComboPrinter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(itemText.c_str()));
    }
    if (!printers_.empty()) {
        SendMessageW(hComboPrinter, CB_SETCURSEL, 0, 0);
    }

    // 2. Page Range
    CreateWindowExW(0, L"BUTTON", L"Pages",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, 65, 270, 75, hwnd, ControlIdToHmenu(IDC_GRP_PAGES), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"&All",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                    25, 83, 50, 18, hwnd, ControlIdToHmenu(IDC_RADIO_ALL), nullptr, nullptr);

    std::wstring currentText = L"Cu&rrent (";
    currentText += std::to_wstring(currentPage_ + 1) + L")";
    CreateWindowExW(0, L"BUTTON", currentText.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                    80, 83, 100, 18, hwnd, ControlIdToHmenu(IDC_RADIO_CURRENT), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"C&ustom:",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                    25, 108, 65, 18, hwnd, ControlIdToHmenu(IDC_RADIO_CUSTOM), nullptr, nullptr);

    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                    95, 107, 180, 20, hwnd, ControlIdToHmenu(IDC_EDIT_CUSTOM), nullptr, nullptr);

    CheckRadioButton(hwnd, IDC_RADIO_ALL, IDC_RADIO_CUSTOM, IDC_RADIO_ALL);

    // 3. Paper Choice
    CreateWindowExW(0, L"BUTTON", L"Paper",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, 145, 270, 48, hwnd, ControlIdToHmenu(IDC_GRP_PAPER), nullptr, nullptr);

    CreateWindowExW(0, L"COMBOBOX", nullptr,
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                    25, 163, 250, 200, hwnd, ControlIdToHmenu(IDC_COMBO_PAPER), nullptr, nullptr);

    // 4. Scale
    CreateWindowExW(0, L"BUTTON", L"Scale",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, 198, 130, 68, hwnd, ControlIdToHmenu(IDC_GRP_SCALE), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"&Fit",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                    25, 218, 110, 18, hwnd, ControlIdToHmenu(IDC_RADIO_FIT), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Act&ual Size",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                    25, 240, 110, 18, hwnd, ControlIdToHmenu(IDC_RADIO_ACTUAL), nullptr, nullptr);

    CheckRadioButton(hwnd, IDC_RADIO_FIT, IDC_RADIO_ACTUAL, IDC_RADIO_FIT);

    // 5. Orientation
    CreateWindowExW(0, L"BUTTON", L"Orientation",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    155, 198, 130, 68, hwnd, ControlIdToHmenu(IDC_GRP_ORIENT), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Au&to",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                    165, 216, 50, 18, hwnd, ControlIdToHmenu(IDC_RADIO_AUTO), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"&Portrait",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                    220, 216, 60, 18, hwnd, ControlIdToHmenu(IDC_RADIO_PORTRAIT), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"&Landscape",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                    165, 240, 110, 18, hwnd, ControlIdToHmenu(IDC_RADIO_LANDSCAPE), nullptr, nullptr);

    CheckRadioButton(hwnd, IDC_RADIO_AUTO, IDC_RADIO_LANDSCAPE, IDC_RADIO_AUTO);

    // 6. Copies
    CreateWindowExW(0, L"STATIC", L"Copies:",
                    WS_CHILD | WS_VISIBLE,
                    15, 275, 50, 18, hwnd, ControlIdToHmenu(IDC_LBL_COPIES), nullptr, nullptr);

    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
                    WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_TABSTOP,
                    70, 273, 50, 20, hwnd, ControlIdToHmenu(IDC_EDIT_COPIES), nullptr, nullptr);

    // Right Column: Print Preview (x: 295..485)
    CreateWindowExW(0, L"BUTTON", L"Preview",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    295, 10, 190, 256, hwnd, ControlIdToHmenu(IDC_GRP_PREVIEW), nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    305, 28, 170, 230, hwnd, ControlIdToHmenu(IDC_PREVIEW_CANVAS), nullptr, nullptr);

    // Bottom: Progress bar, status, and buttons
    CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                    WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    15, 305, 470, 16, hwnd, ControlIdToHmenu(IDC_PROGRESS_BAR), nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"Ready to print.",
                    WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                    15, 328, 300, 18, hwnd, ControlIdToHmenu(IDC_STATUS_TEXT), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Print",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    325, 326, 75, 24, hwnd, ControlIdToHmenu(IDC_BTN_PRINT), nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                    410, 326, 75, 24, hwnd, ControlIdToHmenu(IDC_BTN_CANCEL), nullptr, nullptr);

    // Apply font to all children
    EnumChildWindows(hwnd, [](HWND child, LPARAM lpFont) -> BOOL {
        SendMessageW(child, WM_SETFONT, lpFont, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(hFont));

    // Populate papers for currently selected printer
    OnPrinterChanged(hwnd);
}

void PrintDialog::OnPrinterChanged(HWND hwnd) noexcept {
    HWND hComboPrinter = GetDlgItem(hwnd, IDC_COMBO_PRINTER);
    const int prnIdx = static_cast<int>(SendMessageW(hComboPrinter, CB_GETCURSEL, 0, 0));
    if (prnIdx >= 0 && prnIdx < static_cast<int>(printers_.size())) {
        options_.printerName = printers_[prnIdx].name;
    } else {
        options_.printerName.clear();
    }

    // Populate paper sizes
    HWND hComboPaper = GetDlgItem(hwnd, IDC_COMBO_PAPER);
    SendMessageW(hComboPaper, CB_RESETCONTENT, 0, 0);

    currentPapers_ = EnumeratePapers(options_.printerName);
    SendMessageW(hComboPaper, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default Printer Paper"));

    int defaultSel = 0;
    for (std::size_t i = 0; i < currentPapers_.size(); ++i) {
        const auto& p = currentPapers_[i];
        std::wstring str = p.name;
        if (p.widthMm > 0 && p.heightMm > 0) {
            str += L" (" + std::to_wstring(p.widthMm) + L" x " + std::to_wstring(p.heightMm) + L" mm)";
        }
        SendMessageW(hComboPaper, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(str.c_str()));
        // Auto select A4 if present
        if (p.paperSize == DMPAPER_A4 && defaultSel == 0) {
            defaultSel = static_cast<int>(i + 1);
        }
    }
    SendMessageW(hComboPaper, CB_SETCURSEL, defaultSel, 0);

    OnPaperChanged(hwnd);
}

void PrintDialog::OnPaperChanged(HWND hwnd) noexcept {
    HWND hComboPaper = GetDlgItem(hwnd, IDC_COMBO_PAPER);
    const int sel = static_cast<int>(SendMessageW(hComboPaper, CB_GETCURSEL, 0, 0));
    if (sel > 0 && (sel - 1) < static_cast<int>(currentPapers_.size())) {
        options_.selectedPaperSize = currentPapers_[sel - 1].paperSize;
    } else {
        options_.selectedPaperSize = 0;
    }

    OnOptionChanged(hwnd);
}

void PrintDialog::OnOptionChanged(HWND hwnd) noexcept {
    // Selection mode
    if (IsDlgButtonChecked(hwnd, IDC_RADIO_ALL) == BST_CHECKED) {
        options_.selectionMode = core::print::PageSelectionMode::All;
    } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_CURRENT) == BST_CHECKED) {
        options_.selectionMode = core::print::PageSelectionMode::Current;
    } else {
        options_.selectionMode = core::print::PageSelectionMode::Custom;
    }

    // Custom text
    wchar_t buf[256]{};
    GetDlgItemTextW(hwnd, IDC_EDIT_CUSTOM, buf, 255);
    options_.customRangeText = buf;

    // Scale
    if (IsDlgButtonChecked(hwnd, IDC_RADIO_ACTUAL) == BST_CHECKED) {
        options_.scaleMode = core::print::PrintScaleMode::ActualSize;
    } else {
        options_.scaleMode = core::print::PrintScaleMode::Fit;
    }

    // Orientation
    if (IsDlgButtonChecked(hwnd, IDC_RADIO_PORTRAIT) == BST_CHECKED) {
        options_.orientation = core::print::PrintOrientation::Portrait;
    } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_LANDSCAPE) == BST_CHECKED) {
        options_.orientation = core::print::PrintOrientation::Landscape;
    } else {
        options_.orientation = core::print::PrintOrientation::Auto;
    }

    // Copies
    wchar_t copiesBuf[32]{};
    GetDlgItemTextW(hwnd, IDC_EDIT_COPIES, copiesBuf, 31);
    try {
        options_.copies = core::print::ValidateCopies(std::stoi(copiesBuf));
    } catch (...) {
        options_.copies = 1;
    }

    // Query printer HDC to update preview target metrics accurately
    if (!options_.printerName.empty()) {
        platform::win::UniqueHDC hdc(CreateICW(L"WINSPOOL", options_.printerName.c_str(), nullptr, nullptr));
        if (hdc) {
            previewMetrics_.dpiX = GetDeviceCaps(hdc.get(), LOGPIXELSX);
            previewMetrics_.dpiY = GetDeviceCaps(hdc.get(), LOGPIXELSY);
            previewMetrics_.physicalWidth = GetDeviceCaps(hdc.get(), PHYSICALWIDTH);
            previewMetrics_.physicalHeight = GetDeviceCaps(hdc.get(), PHYSICALHEIGHT);
            previewMetrics_.printableWidth = GetDeviceCaps(hdc.get(), HORZRES);
            previewMetrics_.printableHeight = GetDeviceCaps(hdc.get(), VERTRES);
            previewMetrics_.hardwareMarginLeft = GetDeviceCaps(hdc.get(), PHYSICALOFFSETX);
            previewMetrics_.hardwareMarginTop = GetDeviceCaps(hdc.get(), PHYSICALOFFSETY);
        }
    }

    if (previewMetrics_.printableWidth <= 0 || previewMetrics_.printableHeight <= 0) {
        previewMetrics_.dpiX = 300;
        previewMetrics_.dpiY = 300;
        previewMetrics_.physicalWidth = 2480;
        previewMetrics_.physicalHeight = 3508;
        previewMetrics_.printableWidth = 2400;
        previewMetrics_.printableHeight = 3400;
        previewMetrics_.hardwareMarginLeft = 40;
        previewMetrics_.hardwareMarginTop = 54;
    }

    // Compute placement for preview page
    previewPlacement_ = core::print::PrintLayout::ComputePlacement(
        previewPageWidthPt_, previewPageHeightPt_,
        previewMetrics_, options_.scaleMode, options_.orientation);

    UpdateControlsState(hwnd);

    // Invalidate preview canvas
    HWND hCanvas = GetDlgItem(hwnd, IDC_PREVIEW_CANVAS);
    InvalidateRect(hCanvas, nullptr, TRUE);
}

void PrintDialog::UpdateControlsState(HWND hwnd) noexcept {
    const bool customSelected = (options_.selectionMode == core::print::PageSelectionMode::Custom);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_CUSTOM), customSelected);

    bool rangeValid = core::print::ResolvePagesToPrint(
        options_.selectionMode, options_.currentPage,
        options_.customRangeText, totalPages_, resolvedPages_);

    HWND hStatus = GetDlgItem(hwnd, IDC_STATUS_TEXT);
    HWND hPrintBtn = GetDlgItem(hwnd, IDC_BTN_PRINT);

    if (printers_.empty()) {
        SetWindowTextW(hStatus, L"No printers found.");
        EnableWindow(hPrintBtn, FALSE);
    } else if (!rangeValid || resolvedPages_.empty()) {
        SetWindowTextW(hStatus, L"Invalid page range specified.");
        EnableWindow(hPrintBtn, FALSE);
    } else if (previewPlacement_.willCrop && options_.scaleMode == core::print::PrintScaleMode::ActualSize) {
        // Warning: Actual size crops content
        std::wstring warn = L"Warning: Actual Size exceeds printable area (";
        warn += std::to_wstring(resolvedPages_.size()) + L" page";
        if (resolvedPages_.size() > 1) warn += L"s";
        warn += L").";
        SetWindowTextW(hStatus, warn.c_str());
        EnableWindow(hPrintBtn, TRUE);
    } else {
        std::wstring msg = L"Ready to print " + std::to_wstring(resolvedPages_.size()) + L" page";
        if (resolvedPages_.size() > 1) msg += L"s";
        msg += L".";
        SetWindowTextW(hStatus, msg.c_str());
        EnableWindow(hPrintBtn, TRUE);
    }
}

void PrintDialog::DrawPreview(HDC hdc, const RECT& rc) noexcept {
    const int canvasW = rc.right - rc.left;
    const int canvasH = rc.bottom - rc.top;

    // Fill background (neutral light gray)
    HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Paper dimensions in device space
    const double paperW = (previewMetrics_.physicalWidth > 0) ? previewMetrics_.physicalWidth : 2480;
    const double paperH = (previewMetrics_.physicalHeight > 0) ? previewMetrics_.physicalHeight : 3508;

    // Fit paper box into canvas leaving margin
    const double margin = 10.0;
    const double availCanvasW = canvasW - margin * 2.0;
    const double availCanvasH = canvasH - margin * 2.0;

    const double canvasScale = std::min(availCanvasW / paperW, availCanvasH / paperH);
    const int drawPaperW = std::max(10, static_cast<int>(paperW * canvasScale));
    const int drawPaperH = std::max(10, static_cast<int>(paperH * canvasScale));
    const int paperLeft = (canvasW - drawPaperW) / 2;
    const int paperTop = (canvasH - drawPaperH) / 2;

    // Draw paper drop shadow
    RECT shadowRect{ paperLeft + 3, paperTop + 3, paperLeft + drawPaperW + 3, paperTop + drawPaperH + 3 };
    HBRUSH shadowBrush = CreateSolidBrush(RGB(200, 200, 200));
    FillRect(hdc, &shadowRect, shadowBrush);
    DeleteObject(shadowBrush);

    // Draw white paper sheet
    RECT paperRect{ paperLeft, paperTop, paperLeft + drawPaperW, paperTop + drawPaperH };
    HBRUSH paperBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &paperRect, paperBrush);
    DeleteObject(paperBrush);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, paperRect.left, paperRect.top, paperRect.right, paperRect.bottom);

    // Draw hardware margins (dashed line)
    const int hwLeft = static_cast<int>(previewMetrics_.hardwareMarginLeft * canvasScale);
    const int hwTop = static_cast<int>(previewMetrics_.hardwareMarginTop * canvasScale);
    const int hwPrintW = static_cast<int>(previewMetrics_.printableWidth * canvasScale);
    const int hwPrintH = static_cast<int>(previewMetrics_.printableHeight * canvasScale);

    HPEN marginPen = CreatePen(PS_DOT, 1, RGB(210, 210, 210));
    SelectObject(hdc, marginPen);
    Rectangle(hdc, paperLeft + hwLeft, paperTop + hwTop,
              paperLeft + hwLeft + hwPrintW, paperTop + hwTop + hwPrintH);
    DeleteObject(marginPen);

    // Draw laid-out page rectangle inside printable area
    // previewPlacement_ has destX, destY, destWidth, destHeight in printable area coordinates
    const int pageX = paperLeft + hwLeft + static_cast<int>(previewPlacement_.destX * canvasScale);
    const int pageY = paperTop + hwTop + static_cast<int>(previewPlacement_.destY * canvasScale);
    const int pageW = static_cast<int>(previewPlacement_.destWidth * canvasScale);
    const int pageH = static_cast<int>(previewPlacement_.destHeight * canvasScale);

    RECT pageRectBox{ pageX, pageY, pageX + pageW, pageY + pageH };
    // Content box brush (soft blue tint for page, red tint if willCrop)
    COLORREF contentColor = previewPlacement_.willCrop ? RGB(255, 220, 220) : RGB(225, 235, 250);
    COLORREF contentBorder = previewPlacement_.willCrop ? RGB(220, 80, 80) : RGB(70, 130, 210);

    HBRUSH contentBrush = CreateSolidBrush(contentColor);
    FillRect(hdc, &pageRectBox, contentBrush);
    DeleteObject(contentBrush);

    HPEN pageBorderPen = CreatePen(PS_SOLID, 1, contentBorder);
    SelectObject(hdc, pageBorderPen);
    Rectangle(hdc, pageRectBox.left, pageRectBox.top, pageRectBox.right, pageRectBox.bottom);
    DeleteObject(pageBorderPen);

    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    // Draw page text description inside page box
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(80, 80, 80));
    std::wstring label = (previewPlacement_.effectiveOrientation == core::print::PrintOrientation::Landscape) ?
                         L"Landscape" : L"Portrait";
    if (previewPlacement_.willCrop) {
        label += L" (Cropped)";
    }
    DrawTextW(hdc, label.c_str(), -1, &pageRectBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PrintDialog::OnStartPrint(HWND hwnd) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        return;
    }

    if (!core::print::ResolvePagesToPrint(options_.selectionMode, options_.currentPage,
                                         options_.customRangeText, totalPages_, resolvedPages_) ||
        resolvedPages_.empty()) {
        return;
    }

    // Actual size crop check: if cropping, warn user and offer choice
    if (previewPlacement_.willCrop && options_.scaleMode == core::print::PrintScaleMode::ActualSize) {
        const int choice = MessageBoxW(
            hwnd,
            L"Actual Size exceeds the printer's printable bounds and content will be cropped.\n\nDo you want to continue printing with cropping?",
            L"Print Warning - Content Will Be Cropped",
            MB_YESNO | MB_ICONWARNING);
        if (choice != IDYES) {
            return;
        }
    }

    isRunning_.store(true, std::memory_order_relaxed);
    cancelFlag_.store(false, std::memory_order_relaxed);

    // Disable print controls while printing
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_PRINT), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_COMBO_PRINTER), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_COMBO_PAPER), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_ALL), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_CURRENT), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_CUSTOM), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_CUSTOM), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_FIT), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_ACTUAL), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_AUTO), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_PORTRAIT), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_LANDSCAPE), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_COPIES), FALSE);

    HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS_BAR);
    SendMessageW(hProgress, PBM_SETRANGE32, 0, static_cast<LPARAM>(resolvedPages_.size()));
    SendMessageW(hProgress, PBM_SETPOS, 0, 0);

    SetWindowTextW(GetDlgItem(hwnd, IDC_STATUS_TEXT), L"Spooling pages to printer...");

    const HWND hwndDlg = hwnd;
    workerThread_ = std::thread([this, hwndDlg]() {
        auto onProgress = [hwndDlg](const PrintJobProgress& prog) {
            auto* p = new PrintJobProgress(prog);
            PostMessageW(hwndDlg, WM_PRINT_PROGRESS, 0, reinterpret_cast<LPARAM>(p));
        };

        PrintJobResult res = PrintWorker::ExecutePrintJob(
            pdfSource_, options_, resolvedPages_, cancelFlag_, onProgress);

        auto* r = new PrintJobResult(std::move(res));
        PostMessageW(hwndDlg, WM_PRINT_FINISHED, 0, reinterpret_cast<LPARAM>(r));
    });
}

void PrintDialog::OnCancelPrint(HWND hwnd) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        cancelFlag_.store(true, std::memory_order_relaxed);
        SetWindowTextW(GetDlgItem(hwnd, IDC_STATUS_TEXT), L"Canceling print job...");
        return;
    }
    EndDialog(hwnd, IDCANCEL);
}

void PrintDialog::OnProgress(const PrintJobProgress& prog) noexcept {
    HWND hProgress = GetDlgItem(hwndDialog_, IDC_PROGRESS_BAR);
    SendMessageW(hProgress, PBM_SETPOS, static_cast<WPARAM>(prog.currentJobPage), 0);

    std::wstring status = L"Printing page " + std::to_wstring(prog.currentJobPage) +
                          L" of " + std::to_wstring(prog.totalJobPages) + L"...";
    SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), status.c_str());
}

void PrintDialog::OnFinished(const PrintJobResult& result) noexcept {
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    isRunning_.store(false, std::memory_order_relaxed);

    if (result.canceled) {
        SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), L"Print canceled.");
        MessageBoxW(hwndDialog_, L"Printing was canceled.", L"Print Canceled", MB_OK | MB_ICONINFORMATION);
    } else if (!result.success) {
        std::wstring err = L"Printing failed: ";
        err += result.errorMessage.empty() ? L"Unknown error." : result.errorMessage;
        SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), err.c_str());
        MessageBoxW(hwndDialog_, err.c_str(), L"Print Error", MB_OK | MB_ICONERROR);
    } else {
        std::wstring successMsg = L"Successfully sent " + std::to_wstring(result.pagesPrinted) +
                                  L" page(s) to " + options_.printerName + L".";
        SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), successMsg.c_str());
        MessageBoxW(hwndDialog_, successMsg.c_str(), L"Print Complete", MB_OK | MB_ICONINFORMATION);
        EndDialog(hwndDialog_, IDOK);
        return;
    }

    // Re-enable dialog controls if staying in dialog after cancel or error
    EnableWindow(GetDlgItem(hwndDialog_, IDC_BTN_PRINT), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_COMBO_PRINTER), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_COMBO_PAPER), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_ALL), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_CURRENT), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_CUSTOM), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_FIT), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_ACTUAL), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_AUTO), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_PORTRAIT), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_LANDSCAPE), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_EDIT_COPIES), TRUE);
    UpdateControlsState(hwndDialog_);
}

} // namespace fastpdf::app::print
