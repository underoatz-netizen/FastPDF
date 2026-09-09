#include "RenderWorker.h"

#include <fastpdf/pdfium/PdfDocument.h>

namespace fastpdf::app {

RenderWorker::~RenderWorker() { Shutdown(); }

void RenderWorker::Start(HWND hwnd, UINT doneMessage) {
    hwnd_ = hwnd;
    doneMessage_ = doneMessage;
    thread_ = std::thread(&RenderWorker::Run, this);
}

void RenderWorker::OpenDocument(std::uint64_t docEpoch, std::wstring path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hasOpen_ = true;
        hasClose_ = false;
        openDocEpoch_ = docEpoch;
        openPath_ = std::move(path);
        scheduler_.clear();  // a new document supersedes all pending renders
    }
    cv_.notify_one();
}

void RenderWorker::CloseDocument() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hasClose_ = true;
        hasOpen_ = false;
        scheduler_.clear();
    }
    cv_.notify_one();
}

void RenderWorker::SubmitRender(RenderJob job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scheduler_.push(std::move(job));
    }
    cv_.notify_one();
}

void RenderWorker::SetViewEpoch(std::uint64_t viewEpoch) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentViewEpoch_ = viewEpoch;
        scheduler_.removeStaleViewEpochs(viewEpoch);
    }
    cv_.notify_one();
}

void RenderWorker::StartSearch(std::uint64_t searchEpoch, std::wstring query, bool matchCase) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        searchActive_ = !query.empty();
        searchEpoch_ = searchEpoch;
        searchQuery_ = std::move(query);
        searchMatchCase_ = matchCase;
        searchCurrentPage_ = 0;
        pendingHighlightRequests_.clear();
    }
    cv_.notify_one();
}

void RenderWorker::CancelSearch() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        searchActive_ = false;
        searchQuery_.clear();
        pendingHighlightRequests_.clear();
    }
    cv_.notify_one();
}

void RenderWorker::RequestHighlightRects(std::uint64_t searchEpoch, int pageIndex, int charIndex, int charCount) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingHighlightRequests_.push_back({searchEpoch, pageIndex, charIndex, charCount});
    }
    cv_.notify_one();
}

void RenderWorker::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return;
        }
        shutdown_ = true;
    }
    cv_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void RenderWorker::PostCompletion(WorkerCompletion* completion) noexcept {
    // The UI thread owns |completion| once posted; it frees it when handling
    // the completion message. If the window is gone, PostMessage fails and we
    // free it here.
    if (!PostMessageW(hwnd_, doneMessage_, 0,
                      reinterpret_cast<LPARAM>(completion))) {
        delete completion;
    }
}

void RenderWorker::Run() {
    for (;;) {
        bool doOpen = false;
        bool doClose = false;
        std::uint64_t openEpoch = 0;
        std::wstring openPath;
        std::optional<RenderJob> job;
        std::optional<PendingHighlightRequest> highlightReq;
        bool doSearchStep = false;
        std::uint64_t searchEpoch = 0;
        std::wstring searchQuery;
        bool searchMatchCase = false;
        int searchPage = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return shutdown_ || hasOpen_ || hasClose_ ||
                       scheduler_.size() > 0 ||
                       !pendingHighlightRequests_.empty() ||
                       searchActive_;
            });
            if (shutdown_) {
                break;
            }
            if (hasClose_) {
                doClose = true;
                hasClose_ = false;
            } else if (hasOpen_) {
                doOpen = true;
                openEpoch = openDocEpoch_;
                openPath = std::move(openPath_);
                hasOpen_ = false;
            } else if (!pendingHighlightRequests_.empty()) {
                highlightReq = pendingHighlightRequests_.front();
                pendingHighlightRequests_.erase(pendingHighlightRequests_.begin());
            } else if (scheduler_.size() > 0) {
                job = scheduler_.pop();
                // Staleness check against the worker's current identity,
                // performed under the lock: currentViewEpoch_ is written by
                // the UI thread in SetViewEpoch, so it must be read under the
                // same lock. Drop jobs that belong to a replaced document or
                // an old view.
                if (job.has_value() &&
                    (job->key.docEpoch != currentDocEpoch_ ||
                     job->viewEpoch != currentViewEpoch_)) {
                    job.reset();
                }
            } else if (searchActive_) {
                doSearchStep = true;
                searchEpoch = searchEpoch_;
                searchQuery = searchQuery_;
                searchMatchCase = searchMatchCase_;
                searchPage = searchCurrentPage_;
            }
        }

        if (doClose) {
            document_.reset();
            currentDocEpoch_ = 0;
            searchActive_ = false;
            pendingHighlightRequests_.clear();
            continue;
        }

        if (doOpen) {
            document_.reset();
            currentDocEpoch_ = openEpoch;
            searchActive_ = false;
            pendingHighlightRequests_.clear();

            auto* completion = new WorkerCompletion();
            completion->kind = ResultKind::DocumentInfo;
            completion->docEpoch = openEpoch;

            fastpdf::pdfium::OpenError error = fastpdf::pdfium::OpenError::None;
            fastpdf::pdfium::PdfSource source =
                fastpdf::pdfium::PdfSource::Load(openPath, error);
            if (!source.isValid()) {
                completion->documentInfo.error = error;
            } else {
                document_ = std::make_unique<fastpdf::pdfium::PdfDocument>(
                    source);
                if (!document_->isOpen()) {
                    completion->documentInfo.error = document_->openError();
                    document_.reset();
                } else {
                    completion->documentInfo.pageCount = document_->pageCount();
                    completion->documentInfo.pages.reserve(static_cast<size_t>(
                        completion->documentInfo.pageCount));
                    for (int i = 0; i < completion->documentInfo.pageCount;
                         ++i) {
                        double width = 0.0;
                        double height = 0.0;
                        if (document_->pageSize(i, width, height)) {
                            completion->documentInfo.pages.push_back(
                                {width, height});
                        } else {
                            completion->documentInfo.pages.push_back({0.0, 0.0});
                        }
                    }
                    completion->documentInfo.ok = true;
                }
            }
            PostCompletion(completion);
            continue;
        }

        if (highlightReq.has_value()) {
            if (!document_ || !document_->isOpen()) {
                continue;
            }
            auto* completion = new WorkerCompletion();
            completion->kind = ResultKind::HighlightRects;
            completion->docEpoch = currentDocEpoch_;
            completion->searchEpoch = highlightReq->searchEpoch;
            completion->highlightPageIndex = highlightReq->pageIndex;
            completion->highlightCharIndex = highlightReq->charIndex;
            completion->highlightCharCount = highlightReq->charCount;
            completion->highlightRects = document_->getTextRects(
                highlightReq->pageIndex, highlightReq->charIndex, highlightReq->charCount);
            PostCompletion(completion);
            continue;
        }

        if (job.has_value()) {
            if (!document_ || !document_->isOpen()) {
                continue;  // No open document; drop.
            }

            auto* completion = new WorkerCompletion();
            completion->kind = ResultKind::PageRender;
            completion->docEpoch = job->key.docEpoch;
            completion->viewEpoch = job->viewEpoch;
            completion->jobId = job->jobId;
            completion->key = job->key;
            completion->pageRender.pageIndex = job->key.pageIndex;

            fastpdf::pdfium::Bitmap bitmap;
            double renderDurationMs = 0.0;
            if (document_->renderPage(job->key.pageIndex, job->key.width,
                                      job->key.height, job->key.rotation, bitmap,
                                      renderDurationMs)) {
                completion->pageRender.ok = true;
                completion->pageRender.bitmap = std::move(bitmap);
                completion->pageRender.renderDurationMs = renderDurationMs;
            } else {
                completion->pageRender.ok = false;
                completion->pageRender.error =
                    fastpdf::pdfium::OpenError::Unknown;
            }
            PostCompletion(completion);
            continue;
        }

        if (doSearchStep) {
            if (!document_ || !document_->isOpen()) {
                std::lock_guard<std::mutex> lock(mutex_);
                searchActive_ = false;
                continue;
            }

            const int totalPages = document_->pageCount();
            if (searchPage >= totalPages) {
                // Search completed
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    searchActive_ = false;
                }
                auto* completion = new WorkerCompletion();
                completion->kind = ResultKind::SearchComplete;
                completion->docEpoch = currentDocEpoch_;
                completion->searchEpoch = searchEpoch;
                completion->searchTotalPages = totalPages;
                PostCompletion(completion);
                continue;
            }

            // Search next batch of up to 4 pages per step to yield to render/input
            constexpr int kPagesPerStep = 4;
            const int endPage = std::min(searchPage + kPagesPerStep, totalPages);
            std::vector<fastpdf::pdfium::PageTextRange> matches;

            for (int p = searchPage; p < endPage; ++p) {
                // Check if search was cancelled while working
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!searchActive_ || searchEpoch_ != searchEpoch) {
                        break;
                    }
                }
                auto pageMatches = document_->searchPage(p, searchQuery, searchMatchCase);
                if (!pageMatches.empty()) {
                    matches.insert(matches.end(), pageMatches.begin(), pageMatches.end());
                }
            }

            // Update searchCurrentPage_
            bool finished = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (searchActive_ && searchEpoch_ == searchEpoch) {
                    searchCurrentPage_ = endPage;
                    if (searchCurrentPage_ >= totalPages) {
                        searchActive_ = false;
                        finished = true;
                    }
                } else {
                    continue; // cancelled
                }
            }

            auto* completion = new WorkerCompletion();
            completion->kind = finished ? ResultKind::SearchComplete : ResultKind::SearchBatch;
            completion->docEpoch = currentDocEpoch_;
            completion->searchEpoch = searchEpoch;
            completion->searchMatches = std::move(matches);
            completion->searchNextPage = endPage;
            completion->searchTotalPages = totalPages;
            PostCompletion(completion);
            continue;
        }
    }

    // Destroy the document on the worker thread (creator-thread-affine).
    document_.reset();
}

} // namespace fastpdf::app
