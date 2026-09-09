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

#include "ImageToPdfWorker.h"

namespace fastpdf::app::convert {

// Custom messages sent to the dialog HWND
inline constexpr UINT WM_IMAGE_TO_PDF_PROGRESS = WM_USER + 201;
inline constexpr UINT WM_IMAGE_TO_PDF_FINISHED = WM_USER + 202;

// The Image to PDF conversion dialog and non-modal conversion runner.
// Owns the dialog window, reorderable image list, add/remove/move up/move down buttons,
// native save file picker, and manages the off-UI-thread background conversion.
class ImageToPdfDialog {
public:
    ImageToPdfDialog() = default;
    ~ImageToPdfDialog();

    ImageToPdfDialog(const ImageToPdfDialog&) = delete;
    ImageToPdfDialog& operator=(const ImageToPdfDialog&) = delete;

    // Shows the modal dialog. |hwndParent| is the main application window.
    // |initialImages| are optional pre-populated images (e.g. from drag-and-drop).
    void Show(HWND hwndParent, const std::vector<std::wstring>& initialImages = {}) noexcept;

    // Checks if background conversion is currently running.
    bool IsRunning() const noexcept { return isRunning_.load(std::memory_order_relaxed); }

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleDialogMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    void OnInitDialog(HWND hwnd) noexcept;
    void OnAddImages(HWND hwnd) noexcept;
    void OnRemoveSelected(HWND hwnd) noexcept;
    void OnMoveUp(HWND hwnd) noexcept;
    void OnMoveDown(HWND hwnd) noexcept;
    void OnStartConversion(HWND hwnd) noexcept;
    void OnCancelConversion(HWND hwnd) noexcept;
    void OnProgress(const ImageToPdfProgress& progress) noexcept;
    void OnFinished(const ImageToPdfResult& result) noexcept;
    void UpdateControlsState(HWND hwnd) noexcept;
    void RefreshList(HWND hwnd) noexcept;

    HWND hwndDialog_ = nullptr;
    HWND hwndParent_ = nullptr;
    HWND hwndList_ = nullptr;

    std::vector<std::wstring> imagePaths_;

    std::atomic<bool> isRunning_{false};
    std::atomic<bool> cancelFlag_{false};
    std::thread workerThread_;
};

} // namespace fastpdf::app::convert
