#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <fastpdf/pdfium/PdfDocument.h>
#include <fastpdf/renderer/priority_scheduler.h>

namespace fastpdf::app {

// Bounded pending render queue size. Coalescing by RenderKey keeps this small
// in practice (visible + adjacent pages, preview and final tiers); the hard
// bound guarantees no unbounded queue.
inline constexpr std::size_t kMaxPendingJobs = 16;

// Single persistent render worker that owns the open PDFium document.
//
// The UI thread posts commands; the worker owns all PDFium document access on
// its own thread (open, per-page render, close). A bounded, coalescing
// priority scheduler holds pending render jobs (no unbounded queue). Document
// and view epochs plus per-job ids let the UI discard stale completions after
// open/zoom/resize/scroll races. Every raw PDFium call is additionally
// serialized by fastpdf_pdfium's process-wide call gate; the worker is the
// app's only render thread.
class RenderWorker {
public:
    // A pending render job (the UI builds these; the worker executes them).
    // An alias of the renderer's scheduler job type so the worker's scheduler
    // and the public submission API share one definition.
    using RenderJob = fastpdf::renderer::PriorityScheduler::Job;

    RenderWorker() = default;
    ~RenderWorker();

    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    // Starts the worker thread. Must be called before any command.
    void Start(HWND hwnd, UINT doneMessage);

    // Opens |path| as the current document (closing any prior one) and reports
    // page count + per-page dimensions in a DocumentInfo completion. |docEpoch|
    // identifies the new document generation. Coalesces with any not-yet-
    // processed open.
    void OpenDocument(std::uint64_t docEpoch, std::wstring path);

    // Closes the current document (kept open until replaced or shutdown).
    void CloseDocument();

    // Submits a render job to the coalescing priority scheduler.
    void SubmitRender(RenderJob job);

    // Marks |viewEpoch| as the only valid epoch: pending jobs from older views
    // are dropped, and the UI ignores completions carrying an older epoch.
    void SetViewEpoch(std::uint64_t viewEpoch);

    // Stops the worker and joins the thread. Safe to call once.
    void Shutdown();

    // Search operations (Phase 8)
    // Starts or updates an incremental background search for |query| across the current document.
    // Automatically cancels any previous search query.
    void StartSearch(std::uint64_t searchEpoch, std::wstring query, bool matchCase);

    // Cancels any active search.
    void CancelSearch();

    // Requests PDF-space bounding rects for a specific match range on a page.
    void RequestHighlightRects(std::uint64_t searchEpoch, int pageIndex, int charIndex, int charCount);

    // Diagnostic query for scheduler pending queue depth
    std::size_t pendingQueueDepth() const noexcept {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
        return scheduler_.size();
    }

    enum class ResultKind { DocumentInfo, PageRender, SearchBatch, SearchComplete, HighlightRects };

    // A completion posted back to the UI thread. The UI owns and frees it.
    // Only the members matching |kind| are meaningful.
    struct WorkerCompletion {
        ResultKind kind = ResultKind::DocumentInfo;
        std::uint64_t docEpoch = 0;  // document this completion belongs to
        // Page render payload:
        std::uint64_t viewEpoch = 0;
        std::uint64_t jobId = 0;
        fastpdf::renderer::RenderKey key;
        fastpdf::pdfium::PageRenderResult pageRender;
        // Document info payload:
        fastpdf::pdfium::DocumentInfoResult documentInfo;
        // Search payload:
        std::uint64_t searchEpoch = 0;
        std::vector<fastpdf::pdfium::PageTextRange> searchMatches;
        int searchNextPage = 0;
        int searchTotalPages = 0;
        // Highlight rects payload:
        int highlightPageIndex = 0;
        int highlightCharIndex = 0;
        int highlightCharCount = 0;
        std::vector<fastpdf::pdfium::PdfRect> highlightRects;
    };

private:
    void Run();
    void PostCompletion(WorkerCompletion* completion) noexcept;

    HWND hwnd_ = nullptr;
    UINT doneMessage_ = 0;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;

    // Pending open/close control commands (coalesced).
    bool hasOpen_ = false;
    bool hasClose_ = false;
    std::uint64_t openDocEpoch_ = 0;
    std::wstring openPath_;

    // Current document/view identity on the worker side.
    std::uint64_t currentDocEpoch_ = 0;
    std::uint64_t currentViewEpoch_ = 0;

    // Owned on the worker thread; created and destroyed inside Run().
    std::unique_ptr<fastpdf::pdfium::PdfDocument> document_;

    fastpdf::renderer::PriorityScheduler scheduler_{kMaxPendingJobs};

    // Search state owned by worker
    struct PendingHighlightRequest {
        std::uint64_t searchEpoch = 0;
        int pageIndex = 0;
        int charIndex = 0;
        int charCount = 0;
    };

    bool searchActive_ = false;
    std::uint64_t searchEpoch_ = 0;
    std::wstring searchQuery_;
    bool searchMatchCase_ = false;
    int searchCurrentPage_ = 0;
    std::vector<PendingHighlightRequest> pendingHighlightRequests_;

    std::thread thread_;
};

} // namespace fastpdf::app
