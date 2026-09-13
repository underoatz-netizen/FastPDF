// Manual-only asynchronous update check. See UpdateCheck.h for the policy.

#include "UpdateCheck.h"

#include <winhttp.h>

#include <cstdio>
#include <cstring>
#include <new>

#include <fastpdf/core/version.h>

namespace fastpdf::app::update {

namespace {

constexpr std::size_t kMaxResponseBytes = 64 * 1024;
constexpr std::size_t kReadChunkBytes = 8 * 1024;
constexpr std::size_t kMaxTagBytes = 64;

constexpr DWORD kResolveTimeoutMs = 5000;
constexpr DWORD kConnectTimeoutMs = 5000;
constexpr DWORD kSendTimeoutMs = 5000;
constexpr DWORD kReceiveTimeoutMs = 8000;

// Scoped WinHTTP handle: finite lifetime, always closed.
class ScopedHInternet {
public:
    explicit ScopedHInternet(HINTERNET handle) noexcept : handle_(handle) {}
    ~ScopedHInternet() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }
    ScopedHInternet(const ScopedHInternet&) = delete;
    ScopedHInternet& operator=(const ScopedHInternet&) = delete;
    HINTERNET get() const noexcept { return handle_; }

private:
    HINTERNET handle_ = nullptr;
};

bool IsAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view TrimAscii(std::string_view text) noexcept {
    while (!text.empty() && IsAsciiSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsAsciiSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

void SetError(UpdateCheckResult& result, const char* message) noexcept {
    if (message == nullptr) {
        result.error[0] = '\0';
        return;
    }
    std::size_t n = std::strlen(message);
    if (n > sizeof(result.error) - 1) {
        n = sizeof(result.error) - 1;
    }
    std::memcpy(result.error, message, n);
    result.error[n] = '\0';
}

void SetVersionText(char (&buffer)[32], std::string_view text) noexcept {
    const std::size_t n =
        text.size() < sizeof(buffer) - 1 ? text.size() : sizeof(buffer) - 1;
    std::memcpy(buffer, text.data(), n);
    buffer[n] = '\0';
}

// Builds "FastPDF/<version>" for the mandatory GitHub API User-Agent header.
std::wstring BuildUserAgent() {
    const std::string_view version = fastpdf::core::versionString();
    std::wstring agent = L"FastPDF/";
    agent.reserve(8 + version.size());
    for (const char c : version) {
        agent.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return agent;
}

// Synchronous WinHTTP GET of the fixed latest-release endpoint. Runs ONLY on
// the update background thread. Returns true with |bodyOut| holding the
// bounded response body, or false with |errorOut| describing the failure.
bool FetchLatestReleaseBody(std::string& bodyOut, std::string& errorOut) {
    bodyOut.clear();

    const std::wstring userAgent = BuildUserAgent();
    ScopedHInternet session(WinHttpOpen(
        userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.get() == nullptr) {
        errorOut = "Could not start the network request.";
        return false;
    }
    if (WinHttpSetTimeouts(session.get(), kResolveTimeoutMs, kConnectTimeoutMs,
                           kSendTimeoutMs, kReceiveTimeoutMs) == FALSE) {
        errorOut = "Could not configure the network timeouts.";
        return false;
    }

    ScopedHInternet connection(
        WinHttpConnect(session.get(), kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (connection.get() == nullptr) {
        errorOut = "Could not connect to the update server.";
        return false;
    }

    // WINHTTP_FLAG_SECURE enforces HTTPS; the default flags keep system
    // certificate validation enabled (no SECURITY_FLAG_IGNORE_* here).
    ScopedHInternet request(WinHttpOpenRequest(
        connection.get(), L"GET", kApiPath, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (request.get() == nullptr) {
        errorOut = "Could not create the update request.";
        return false;
    }

    const std::wstring headers =
        L"User-Agent: " + userAgent + L"\r\nAccept: application/vnd.github+json\r\n";
    if (WinHttpAddRequestHeaders(request.get(), headers.c_str(),
                                static_cast<DWORD>(headers.size()),
                                WINHTTP_ADDREQ_FLAG_ADD) == FALSE) {
        errorOut = "Could not prepare the update request.";
        return false;
    }

    if (WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE) {
        errorOut = "Could not send the update request. Check your connection.";
        return false;
    }
    if (WinHttpReceiveResponse(request.get(), nullptr) == FALSE) {
        errorOut = "No response from the update server. Check your connection.";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(request.get(),
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        errorOut = "Could not read the update server response.";
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        char buffer[96] = {};
        std::snprintf(buffer, sizeof(buffer),
                      "The update server returned HTTP %lu.",
                      static_cast<unsigned long>(statusCode));
        errorOut = buffer;
        return false;
    }

    for (;;) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request.get(), &available) == FALSE) {
            errorOut = "The update response was interrupted.";
            bodyOut.clear();
            return false;
        }
        if (available == 0) {
            break;
        }
        char chunk[kReadChunkBytes];
        DWORD chunkSize = 0;
        const DWORD toRead = available < kReadChunkBytes ? available : kReadChunkBytes;
        if (WinHttpReadData(request.get(), chunk, toRead, &chunkSize) == FALSE) {
            errorOut = "The update response was interrupted.";
            bodyOut.clear();
            return false;
        }
        if (chunkSize == 0) {
            break;
        }
        if (bodyOut.size() + chunkSize > kMaxResponseBytes) {
            errorOut = "The update response was too large.";
            bodyOut.clear();
            return false;
        }
        bodyOut.append(chunk, chunkSize);
    }
    return true;
}

}  // namespace

bool TryParseReleaseVersion(std::string_view text, ReleaseVersion& out) noexcept {
    out = ReleaseVersion{};
    text = TrimAscii(text);
    if (text.empty()) {
        return false;
    }
    if (text.front() == 'v' || text.front() == 'V') {
        text.remove_prefix(1);
        if (text.empty()) {
            return false;
        }
    }

    int parts[3] = {0, 0, 0};
    int partIndex = 0;
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        if (partIndex > 2) {
            return false;  // More than three numeric components.
        }
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        int value = 0;
        std::size_t digits = 0;
        while (i < n && text[i] >= '0' && text[i] <= '9') {
            value = value * 10 + (text[i] - '0');
            if (value > 999999999) {
                return false;  // Overflow guard: not a sane version.
            }
            ++i;
            ++digits;
        }
        if (digits == 0) {
            return false;
        }
        parts[partIndex++] = value;
        if (i == n) {
            break;
        }
        if (text[i] != '.') {
            return false;  // Reject trailing garbage ("-beta", "x", ...).
        }
        ++i;
        if (i == n) {
            return false;  // Trailing dot: empty component.
        }
    }
    if (partIndex == 0) {
        return false;
    }
    out.major = parts[0];
    out.minor = partIndex > 1 ? parts[1] : 0;
    out.patch = partIndex > 2 ? parts[2] : 0;
    return true;
}

int CompareVersions(const ReleaseVersion& a, const ReleaseVersion& b) noexcept {
    if (a.major != b.major) {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch) {
        return a.patch < b.patch ? -1 : 1;
    }
    return 0;
}

std::string FormatVersion(const ReleaseVersion& version) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%d.%d.%d", version.major,
                  version.minor, version.patch);
    return std::string(buffer);
}

bool TryExtractTagName(std::string_view json, std::string& tagOut) {
    tagOut.clear();
    static constexpr std::string_view kKey = "\"tag_name\"";
    std::size_t searchFrom = 0;
    while (searchFrom < json.size()) {
        const std::size_t keyPos = json.find(kKey, searchFrom);
        if (keyPos == std::string_view::npos) {
            return false;
        }
        std::size_t i = keyPos + kKey.size();
        while (i < json.size() && IsAsciiSpace(json[i])) {
            ++i;
        }
        if (i >= json.size() || json[i] != ':') {
            searchFrom = keyPos + 1;  // Not a key-value pair; keep looking.
            continue;
        }
        ++i;
        while (i < json.size() && IsAsciiSpace(json[i])) {
            ++i;
        }
        if (i >= json.size() || json[i] != '"') {
            return false;  // tag_name present but not a JSON string.
        }
        ++i;
        std::string value;
        value.reserve(16);
        bool closed = false;
        while (i < json.size()) {
            const char c = json[i];
            if (c == '\\') {
                if (i + 1 >= json.size()) {
                    return false;
                }
                value.push_back(json[i + 1]);
                i += 2;
            } else if (c == '"') {
                closed = true;
                break;
            } else {
                value.push_back(c);
                ++i;
            }
            if (value.size() > kMaxTagBytes) {
                return false;
            }
        }
        if (!closed || value.empty()) {
            return false;
        }
        tagOut = value;
        return true;
    }
    return false;
}

UpdateChecker::UpdateChecker() : state_(std::make_shared<SharedState>()) {}

UpdateChecker::~UpdateChecker() { Cancel(); }

bool UpdateChecker::is_checking() const noexcept {
    return state_->checking.load(std::memory_order_acquire);
}

void UpdateChecker::Cancel() noexcept {
    // Invalidate any in-flight completion; the background thread observes the
    // generation and drops its result instead of posting it. Never blocks.
    state_->generation.fetch_add(1, std::memory_order_acq_rel);
}

namespace {

// Thread payload. Holds shared ownership of the checker state so a destroyed
// UpdateChecker (or window) is never touched after the fact; only the HWND
// is used weakly via PostMessage, which fails safely for a dead window.
struct CheckJob {
    HWND hwnd;
    UINT message;
    std::shared_ptr<std::atomic<bool>> checking;
    std::shared_ptr<std::atomic<unsigned>> generation;
    unsigned jobGeneration;
};

void FinishJob(CheckJob* job, UpdateCheckResult* result) noexcept {
    // Drop superseded results (Cancel/shutdown raced us); the owner deletes
    // the undelivered result here.
    if (job->generation->load(std::memory_order_acquire) != job->jobGeneration) {
        delete result;
        job->checking->store(false, std::memory_order_release);
        delete job;
        return;
    }
    if (PostMessageW(job->hwnd, job->message, 0,
                     reinterpret_cast<LPARAM>(result)) == FALSE) {
        // Window gone or queue failed: the owner cleans up the result.
        delete result;
    }
    job->checking->store(false, std::memory_order_release);
    delete job;
}

DWORD WINAPI CheckThreadEntry(LPVOID param) noexcept {
    CheckJob* job = static_cast<CheckJob*>(param);

    UpdateCheckResult* result = new (std::nothrow) UpdateCheckResult();
    if (result == nullptr) {
        // Allocation failure still clears the in-flight flag; the UI treats a
        // null completion as an internal error.
        if (job->generation->load(std::memory_order_acquire) == job->jobGeneration) {
            PostMessageW(job->hwnd, job->message, 0, 0);
        }
        job->checking->store(false, std::memory_order_release);
        delete job;
        return 0;
    }

    const std::string_view currentText = fastpdf::core::versionString();
    ReleaseVersion current{};
    if (!TryParseReleaseVersion(currentText, current)) {
        SetError(*result, "The installed version could not be read.");
        SetVersionText(result->currentVersion, currentText);
        FinishJob(job, result);
        return 0;
    }
    SetVersionText(result->currentVersion, FormatVersion(current));

    std::string body;
    std::string fetchError;
    if (!FetchLatestReleaseBody(body, fetchError)) {
        SetError(*result, fetchError.empty() ? "The update check failed."
                                             : fetchError.c_str());
        FinishJob(job, result);
        return 0;
    }

    std::string tag;
    if (!TryExtractTagName(body, tag)) {
        SetError(*result, "The update response was not recognized.");
        FinishJob(job, result);
        return 0;
    }

    ReleaseVersion latest{};
    if (!TryParseReleaseVersion(tag, latest)) {
        SetError(*result, "The latest version could not be read.");
        FinishJob(job, result);
        return 0;
    }
    SetVersionText(result->latestVersion, FormatVersion(latest));

    result->success = true;
    result->updateAvailable = CompareVersions(current, latest) < 0;
    FinishJob(job, result);
    return 0;
}

}  // namespace

bool UpdateChecker::Start(HWND notifyHwnd, UINT notifyMessage) noexcept {
    bool expected = false;
    if (!state_->checking.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return false;  // A check is already active: no duplicate work.
    }
    const unsigned jobGeneration =
        state_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;

    // Shared ownership split into type-erased pieces for the thread payload.
    auto checking = std::shared_ptr<std::atomic<bool>>(
        state_, &state_->checking);
    auto generation = std::shared_ptr<std::atomic<unsigned>>(
        state_, &state_->generation);
    CheckJob* job = new (std::nothrow)
        CheckJob{notifyHwnd, notifyMessage, checking, generation, jobGeneration};
    if (job == nullptr) {
        state_->checking.store(false, std::memory_order_release);
        return false;
    }
    HANDLE thread = CreateThread(nullptr, 0, &CheckThreadEntry, job, 0, nullptr);
    if (thread == nullptr) {
        state_->checking.store(false, std::memory_order_release);
        delete job;
        return false;
    }
    // Detached: the thread owns its payload and state; the finite WinHTTP
    // timeouts bound its lifetime. Closing the handle does not stop it.
    CloseHandle(thread);
    return true;
}

}  // namespace fastpdf::app::update
