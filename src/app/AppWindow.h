#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <fastpdf/core/layout.h>
#include <fastpdf/core/recent_files.h>
#include <fastpdf/core/search_types.h>
#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/platform/win/RecentStore.h>
#include <fastpdf/renderer/idle.h>
#include <fastpdf/renderer/lru_cache.h>
#include <fastpdf/renderer/render_key.h>

#include "Benchmark.h"
#include "RenderWorker.h"

namespace fastpdf::app {

// CPU bitmap cache budget (bytes). Matches the Phase-3 target of a 128 MiB
// internal cache; the LRU cache evicts past this bound.
inline constexpr std::size_t kCpuCacheBytes = 128ULL * 1024ULL * 1024ULL;

// Idle delay before preview renders are promoted to final quality. Within the
// 100-150 ms requirement from the product spec.
inline constexpr std::uint64_t kPromotionDelayMs = 130;

// Adjacent pre-render window (previous / next pages) at lower priority.
inline constexpr int kAdjacentPrev = 1;
inline constexpr int kAdjacentNext = 2;

// Hard cap on simultaneously requested visible pages (bounds the scheduler and
// cache regardless of viewport/page geometry).
inline constexpr int kMaxVisiblePages = 8;

// Owns the main window (HWND) and all Direct2D/DirectWrite device resources.
// Per the architecture decision, the UI layer is the sole owner of the HWND
// and the Direct2D device resources; PDFium stays behind fastpdf_pdfium and
// is only ever touched by the RenderWorker thread.
//
// Phase 3 replaces the Phase-2 reference pipeline (re-open per render, one
// request in flight) with a persistent single renderer:
//   * RenderWorker owns the open PdfDocument across all page renders (no
//     re-open per page), fed by a bounded coalescing priority scheduler.
//   * The UI owns a byte-bounded LRU CPU bitmap cache (keyed by RenderKey:
//     doc epoch / page / pixel dims / rotation / quality) plus a bounded D2D
//     cache pruned to the currently wanted keys.
//   * While the view is moving, visible pages render at half-size preview
//     quality; after ~130 ms of idleness they are promoted to final quality,
//     drawn into the same page rect so geometry never changes.
//   * Document/view epochs and per-job ids let the UI accept only current
//     doc/view/key completions and discard stale ones.
class AppWindow {
public:
    explicit AppWindow(const fastpdf::pdfium::PdfiumLibrary& pdfium) noexcept;
    ~AppWindow();

    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    bool Create(const wchar_t* title) noexcept;
    void Show() noexcept;

    // Opens |path| as the current document (used for direct command-line
    // launches). Safe to call once the window has been created.
    void OpenPathFromCommandLine(const std::wstring& path) noexcept;

private:
    enum class ViewState { Empty, Loading, Ready, Error };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void OnPaint() noexcept;
    void OnResize(UINT width, UINT height) noexcept;
    void OnDpiChanged(UINT dpi, const RECT& suggestedRect) noexcept;
    void OnOpenFile() noexcept;
    void OnDropFile(const std::wstring& path) noexcept;
    void OnWorkerDone(RenderWorker::WorkerCompletion* completion) noexcept;
    void OnDocumentInfoDone(RenderWorker::WorkerCompletion* completion) noexcept;
    void OnPageRenderDone(RenderWorker::WorkerCompletion* completion) noexcept;
    void OnPromotionTimer() noexcept;
    bool EnsureDeviceResources() noexcept;
    void DiscardDeviceResources() noexcept;
    void DrawStatusText(ID2D1HwndRenderTarget& target) noexcept;
    void DrawPages(ID2D1HwndRenderTarget& target) noexcept;
    void DrawPageBitmap(ID2D1HwndRenderTarget& target, int pageIndex) noexcept;
    void DrawBitmap(ID2D1HwndRenderTarget& target,
                    const fastpdf::renderer::RenderKey& key,
                    const fastpdf::renderer::Bitmap& bitmap,
                    const fastpdf::core::layout::PageRect& rect) noexcept;
    bool GetOrCreateD2dBitmap(
        ID2D1HwndRenderTarget& target, const fastpdf::renderer::RenderKey& key,
        const fastpdf::renderer::Bitmap& bitmap,
        Microsoft::WRL::ComPtr<ID2D1Bitmap>& out) noexcept;
    void Log(const std::wstring& message) const noexcept;
    std::wstring UserMessageForError(fastpdf::pdfium::OpenError error) const noexcept;

    // Layout / viewport helpers.
    void RebuildLayout() noexcept;
    void RequestRenders() noexcept;
    void MarkInputActivity() noexcept;
    void SubmitRender(fastpdf::renderer::RenderKey key,
                      fastpdf::renderer::RenderPriority priority) noexcept;
    void PruneD2dCache(
        const std::vector<fastpdf::renderer::RenderKey>& wantedKeys) noexcept;
    fastpdf::renderer::RenderKey MakeKey(
        int pageIndex, fastpdf::renderer::PixelSize size,
        fastpdf::renderer::Quality quality) const noexcept;
    void UpdateStatusText() noexcept;
    void SetFitMode(fastpdf::core::layout::FitMode mode) noexcept;
    void SetZoomPercent(double percent) noexcept;
    void ZoomAt(double newPercent, double cursorX, double cursorY) noexcept;
    void ScrollBy(double dx, double dy) noexcept;
    void GoToPage(int pageIndex) noexcept;
    void OnMouseWheel(WPARAM wParam, LPARAM lParam) noexcept;
    void OnKeyDown(WPARAM wParam) noexcept;
    void OnCommand(WPARAM wParam) noexcept;
    void OnLeftButtonDown() noexcept;
    // Double-click on rendered page content fits the current page to the
    // viewport (the existing Fit Page command/state). A double-click on the
    // surrounding margin or the gap between pages is ignored.
    void OnPageDoubleClick(int x, int y) noexcept;
    void OpenPath(const std::wstring& path) noexcept;
    void ResetDocument() noexcept;
    void RequestDocumentInfo() noexcept;
    void RequestInitialPreview() noexcept;
    void PromoteAfterInitialPreview() noexcept;
    void FlushPostFirstFrameWork() noexcept;
    double ViewportWidth() const noexcept;
    double ViewportHeight() const noexcept;
    int CurrentPageIndex() const noexcept;

    // Presentation mode (Phase 4).
    void EnterPresentation() noexcept;
    void ExitPresentation() noexcept;
    void RebuildPresentationLayout() noexcept;
    void RequestPresentationRenders() noexcept;
    void DrawPresentation(ID2D1HwndRenderTarget& target) noexcept;
    void PresentationNext() noexcept;
    void PresentationPrev() noexcept;
    void PresentationGoTo(int page) noexcept;
    bool PresentationPageCached(int page) const noexcept;
    void HandlePresentationKey(std::uint32_t vk, bool ctrl) noexcept;
    void SaveWindowPlacement() noexcept;
    void RestoreWindowPlacement() noexcept;

    // Screenshot mode (Phase 5).
    void EnterScreenshotMode() noexcept;
    void CancelScreenshotMode() noexcept;
    void OnScreenshotMouseDown(int x, int y) noexcept;
    void OnScreenshotMouseMove(int x, int y) noexcept;
    void OnScreenshotMouseUp(int x, int y) noexcept;
    void CaptureScreenshot() noexcept;
    void OnSavePngClicked() noexcept;
    void HideNotification() noexcept;
    void DrawScreenshotOverlay(ID2D1HwndRenderTarget& target) noexcept;
    void DrawNotificationToast(ID2D1HwndRenderTarget& target) noexcept;

    // Convert mode (Phase 6A & 6B).
    void OnConvertPdfToPng() noexcept;
    void OnConvertImageToPdf(const std::vector<std::wstring>& initialImages = {}) noexcept;

    // Print mode (Phase 7).
    void OnPrint() noexcept;

    // Search mode & UI (Phase 8).
    void ShowSearchUI() noexcept;
    void HideSearchUI() noexcept;
    // Applies the DPI-scaled find-panel geometry to the panel and its child
    // controls and refreshes the control font when the DPI changes. Shared by
    // the initial show and live WM_DPICHANGED transitions; idempotent.
    void LayoutSearchPanel() noexcept;
    void OnSearchTextChanged() noexcept;
    void SearchNext() noexcept;
    void SearchPrev() noexcept;
    void GoToMatch(size_t matchIndex) noexcept;
    void DrawSearchHighlights(ID2D1HwndRenderTarget& target) noexcept;
    void UpdateSearchStatus() noexcept;

    // Recent files (Phase 8).
    void LoadRecentFilesState() noexcept;
    void SaveCurrentToRecentFiles() noexcept;
    void UpdateRecentMenu() noexcept;
    void OnOpenRecent(size_t index) noexcept;

    HWND hwnd_ = nullptr;
    const fastpdf::pdfium::PdfiumLibrary& pdfium_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> statusTextFormat_;
    // Right-aligned status metadata (page / total / zoom). Separate from
    // statusTextFormat_ so the toast text keeps its leading alignment.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> statusMetaTextFormat_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> statusBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> statusMetaBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errorBrush_;
    UINT dpi_ = 96;

    RenderWorker worker_;
    ViewState state_ = ViewState::Empty;
    std::wstring currentPath_;
    std::wstring statusText_;
    // Right side of the status line while a document is Ready: page indicator,
    // zoom and (when enabled) diagnostics.
    std::wstring statusMeta_;
    std::wstring errorMessage_;

    // Document/view identity. docEpoch_ changes per document open; viewEpoch_
    // changes per view-scale change. Both are echoed through the worker so the
    // UI can accept only current completions.
    std::uint64_t docEpoch_ = 0;
    std::uint64_t viewEpoch_ = 0;
    std::uint64_t nextJobId_ = 1;

    // Document geometry (PDF points) reported by the worker.
    std::optional<fastpdf::pdfium::DocumentInfoResult> documentInfo_;

    // Continuous layout at the current zoom (rebuilt on zoom/resize).
    std::optional<fastpdf::core::layout::ContinuousLayout> layout_;
    fastpdf::core::layout::FitMode fitMode_ = fastpdf::core::layout::FitMode::FitWidth;
    double zoomPercent_ = 100.0;
    double scrollX_ = 0.0;
    double scrollY_ = 0.0;

    // Render pipeline state (Phase 3):
    //   cpuCache_     - byte-bounded LRU cache of rendered CPU bitmaps keyed
    //                   by RenderKey (doc epoch/page/dims/rotation/quality).
    //   d2dBitmaps_   - bounded device-dependent bitmap cache, pruned to the
    //                   currently wanted keys and cleared on device loss.
    //   pendingKeys_  - keys submitted but not yet completed (dedup).
    //   renderScale_  - pixels-per-point the current cache/jobs were made at;
    //                   a change bumps the view epoch and clears the caches.
    fastpdf::renderer::CpuBitmapCache cpuCache_{kCpuCacheBytes};
    std::map<fastpdf::renderer::RenderKey, Microsoft::WRL::ComPtr<ID2D1Bitmap>>
        d2dBitmaps_;
    std::set<fastpdf::renderer::RenderKey> pendingKeys_;
    double renderScale_ = 0.0;

    // Adaptive rendering: preview while the view is active, final after idle.
    fastpdf::renderer::IdleDebounce idleDebounce_{kPromotionDelayMs};
    bool previewActive_ = false;

    // Presentation mode (Phase 4). The normal continuous layout (layout_,
    // scrollX_/scrollY_, fitMode_, zoomPercent_) is left untouched while
    // presenting so exiting restores the prior view state cleanly. A separate
    // single-page presentation layout is built for the current page at Fit
    // Page zoom and rendered centered on a black background.
    bool presenting_ = false;
    int presentationPage_ = 0;
    std::optional<fastpdf::core::layout::ContinuousLayout> presentationLayout_;
    double presentationRenderScale_ = 0.0;
    HMENU savedMenu_ = nullptr;
    WINDOWPLACEMENT savedPlacement_{};
    bool savedPlacementValid_ = false;

    // Screenshot mode (Phase 5).
    bool screenshotActive_ = false;
    bool screenshotDragging_ = false;
    POINT screenshotStartPoint_{0, 0};
    POINT screenshotCurrentPoint_{0, 0};

    // Copied notification / Save PNG toast state.
    bool toastVisible_ = false;
    std::wstring toastMessage_;
    fastpdf::renderer::Bitmap lastScreenshotBitmap_;
    D2D1_RECT_F toastSaveButtonRect_{0, 0, 0, 0};
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> toastBgBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> toastTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> toastButtonBgBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> toastButtonBorderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlayDimBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlayBorderBrush_;

    // Search state (Phase 8).
    bool searchVisible_ = false;
    HWND hwndSearchPanel_ = nullptr;
    HWND hwndSearchEdit_ = nullptr;
    HWND hwndSearchPrev_ = nullptr;
    HWND hwndSearchNext_ = nullptr;
    HWND hwndSearchClose_ = nullptr;
    HWND hwndSearchCount_ = nullptr;
    HFONT searchFont_ = nullptr;
    // DPI the current searchFont_ was created for (0 = none yet). Lets a DPI
    // transition recreate the font only when the height actually changes.
    UINT searchFontDpi_ = 0;

    std::uint64_t searchEpoch_ = 0;
    std::wstring searchCurrentQuery_;
    std::vector<fastpdf::pdfium::PageTextRange> searchMatches_;
    int searchCurrentMatchIndex_ = -1;
    bool searchInProgress_ = false;

    // Cached visible highlights (PDF page -> list of PDF points rects)
    struct HighlightRectCache {
        int pageIndex = -1;
        int charIndex = -1;
        int charCount = 0;
        std::vector<fastpdf::pdfium::PdfRect> rects;
    };
    std::vector<HighlightRectCache> currentMatchRects_;

    // Search highlight brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> searchHighlightBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> searchActiveHighlightBrush_;

    // Recent files state (Phase 8).
    fastpdf::core::recent::RecentState recentState_;
    HMENU recentMenu_ = nullptr;

    // Diagnostics and metrics (Phase 9)
    bool diagnosticsEnabled_ = false;
    std::uint64_t appStartupTimeMs_ = 0;
    std::uint64_t docOpenRequestTimeMs_ = 0;
    double lastFrameTimeMs_ = 0.0;

    // Benchmark instrumentation (P0): active only when FASTPDF_BENCHMARK is
    // set; records startup/open/first-frame phases as JSON.
    Benchmark benchmark_;

    // Initial-open fast path (P2): the initial visible page renders as a
    // half-size preview first; final quality and adjacent pages are requested
    // only after that preview is actually presented (EndDraw). Logging and
    // recent-file persistence are deferred until then so they cannot block the
    // first render.
    bool initialPreviewPending_ = false;
    bool postFirstFrameWorkPending_ = false;
    bool firstFramePresentedLogged_ = false;
    bool firstPreviewRenderLogged_ = false;
    bool firstFinalRenderLogged_ = false;
};

} // namespace fastpdf::app
