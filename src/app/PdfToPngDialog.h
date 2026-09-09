#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#ifdef DrawStatusText
#undef DrawStatusText
#endif

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "PdfToPngOptions.h"
#include "PdfToPngWorker.h"

namespace fastpdf::app::convert {

// Progress and completion messages sent to the dialog HWND
inline constexpr UINT WM_CONVERT_PROGRESS = WM_USER + 101;
inline constexpr UINT WM_CONVERT_FINISHED = WM_USER + 102;

// The PDF to PNG conversion dialog and non-modal conversion runner.
// Owns the dialog window, page selection, quality presets, folder picker,
// and manages the cancelable background conversion thread.
class PdfToPngDialog {
public:
    PdfToPngDialog() = default;
    ~PdfToPngDialog();

    PdfToPngDialog(const PdfToPngDialog&) = delete;
    PdfToPngDialog& operator=(const PdfToPngDialog&) = delete;

    // Shows the modal dialog. |hwndParent| is the main application window.
    // |pdfPath| is the path to the currently open PDF.
    // |currentPage| is the 0-based page index currently viewed.
    // |totalPages| is the total number of pages in the document.
    void Show(HWND hwndParent, const std::wstring& pdfPath,
              int currentPage, int totalPages) noexcept;

    // Checks if a background conversion is currently running.
    bool IsRunning() const noexcept { return isRunning_.load(std::memory_order_relaxed); }

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleDialogMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    void OnInitDialog(HWND hwnd) noexcept;
    void OnBrowseFolder(HWND hwnd) noexcept;
    void OnStartConversion(HWND hwnd) noexcept;
    void OnCancelConversion(HWND hwnd) noexcept;
    void OnProgress(const BatchProgress& progress) noexcept;
    void OnFinished(const BatchConversionResult& result) noexcept;
    void UpdateControlsState(HWND hwnd) noexcept;

    HWND hwndDialog_ = nullptr;
    HWND hwndParent_ = nullptr;
    std::wstring pdfPath_;
    int currentPage_ = 0;
    int totalPages_ = 0;

    ConversionOptions options_;
    std::vector<int> resolvedPages_;

    std::atomic<bool> isRunning_{false};
    std::atomic<bool> cancelFlag_{false};
    std::thread workerThread_;
};

} // namespace fastpdf::app::convert
