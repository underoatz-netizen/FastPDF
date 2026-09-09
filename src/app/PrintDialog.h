#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "PrintCommon.h"
#include "PrintWorker.h"
#include "fastpdf/core/print_layout.h"
#include "fastpdf/pdfium/PdfDocument.h"

namespace fastpdf::app::print {

inline constexpr UINT WM_PRINT_PROGRESS = WM_USER + 201;
inline constexpr UINT WM_PRINT_FINISHED = WM_USER + 202;

class PrintDialog {
public:
    PrintDialog() = default;
    ~PrintDialog();

    PrintDialog(const PrintDialog&) = delete;
    PrintDialog& operator=(const PrintDialog&) = delete;

    void Show(HWND hwndParent, const std::wstring& pdfPath,
              const pdfium::PdfSource& pdfSource,
              int currentPage, int totalPages) noexcept;

    bool IsRunning() const noexcept { return isRunning_.load(std::memory_order_relaxed); }

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleDialogMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    void OnInitDialog(HWND hwnd) noexcept;
    void OnPrinterChanged(HWND hwnd) noexcept;
    void OnPaperChanged(HWND hwnd) noexcept;
    void OnOptionChanged(HWND hwnd) noexcept;
    void OnStartPrint(HWND hwnd) noexcept;
    void OnCancelPrint(HWND hwnd) noexcept;
    void OnProgress(const PrintJobProgress& progress) noexcept;
    void OnFinished(const PrintJobResult& result) noexcept;
    void UpdateControlsState(HWND hwnd) noexcept;
    void DrawPreview(HDC hdc, const RECT& previewRect) noexcept;

    HWND hwndDialog_ = nullptr;
    HWND hwndParent_ = nullptr;
    std::wstring pdfPath_;
    pdfium::PdfSource pdfSource_;
    int currentPage_ = 0;
    int totalPages_ = 0;

    PrintDialogOptions options_;
    std::vector<PrinterInfo> printers_;
    std::vector<PaperInfo> currentPapers_;
    std::vector<int> resolvedPages_;

    // Cached preview state
    core::print::TargetMetrics previewMetrics_{};
    core::print::PagePlacement previewPlacement_{};
    double previewPageWidthPt_ = 595.0;
    double previewPageHeightPt_ = 842.0;

    std::atomic<bool> isRunning_{false};
    std::atomic<bool> cancelFlag_{false};
    std::thread workerThread_;
};

} // namespace fastpdf::app::print
