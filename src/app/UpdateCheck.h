// Manual-only update check for FastPDF.
//
// Boundary: this module performs NO network I/O on the UI or render worker
// thread. The pure parsing/comparison helpers below are synchronous and
// unit-testable; the WinHTTP fetch runs on a short-lived background thread
// owned by UpdateChecker and reports back through a small heap-owned
// UpdateCheckResult posted to the UI message queue.
//
// Policy (never relax without an architecture decision):
//   - Manual only: Start() is called solely from the Help > Check for Updates
//     command. There is no startup, timer, or polling trigger.
//   - HTTPS only to the fixed api.github.com endpoint below, with system
//     certificate validation, finite timeouts, and a bounded response body.
//   - Only the release `tag_name` is parsed. No installer is downloaded,
//     installed, or executed. The only outbound navigation offered is the
//     fixed repository releases page, opened after explicit user confirmation.
//   - No document data or telemetry is transmitted.

#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

namespace fastpdf::app::update {

// Fixed endpoint identity. Not configurable at runtime: accepting arbitrary
// URLs (including URLs parsed from JSON) is out of scope by design.
inline constexpr wchar_t kApiHost[] = L"api.github.com";
inline constexpr wchar_t kApiPath[] =
    L"/repos/underoatz-netizen/FastPDF/releases/latest";
// Fixed releases page offered to the user after explicit confirmation. Never
// derived from server response data.
inline constexpr wchar_t kReleasesPageUrl[] =
    L"https://github.com/underoatz-netizen/FastPDF/releases";

// Numeric semantic version (major.minor.patch).
struct ReleaseVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// Parses "M.m.p" with an optional single leading 'v'/'V' and surrounding
// ASCII whitespace. Missing trailing components default to 0 ("1.2" ==
// 1.2.0). Returns false for empty input, non-numeric components, empty
// components, more than three components, or any trailing garbage
// (e.g. "-beta" suffixes are rejected as malformed).
bool TryParseReleaseVersion(std::string_view text, ReleaseVersion& out) noexcept;

// Lexicographic numeric comparison: -1 if a < b, 0 if equal, +1 if a > b.
int CompareVersions(const ReleaseVersion& a, const ReleaseVersion& b) noexcept;

// Formats a version as "M.m.p" for display and result payloads.
std::string FormatVersion(const ReleaseVersion& version);

// Extracts the GitHub release `tag_name` string value from a JSON document.
// Minimal single-key parser: locates the "tag_name" key followed by a JSON
// string value (with backslash-escape handling) and returns that value.
// Returns false when the key is missing, the value is not a well-formed
// non-empty JSON string, or the value exceeds 64 characters.
bool TryExtractTagName(std::string_view json, std::string& tagOut);

// Small owned completion object. Allocated on the background thread, posted
// to the UI thread via PostMessage; the poster deletes it when delivery
// fails and the UI deletes it after handling.
struct UpdateCheckResult {
    bool success = false;
    bool updateAvailable = false;
    char currentVersion[32] = {};
    char latestVersion[32] = {};
    char error[256] = {};
};

// Owns at most one in-flight manual update check. Safe to destroy while a
// check is running: a pending background result is invalidated and dropped
// (never posted to a destroyed window), and shared state outlives both sides
// via shared ownership.
class UpdateChecker {
public:
    UpdateChecker();
    ~UpdateChecker();

    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    // Starts a manual check on a background thread. Returns false (without
    // side effects) when a check is already active. |notifyMessage| is posted
    // to |notifyHwnd| with lParam carrying an UpdateCheckResult* (possibly
    // null on internal allocation failure).
    bool Start(HWND notifyHwnd, UINT notifyMessage) noexcept;

    bool is_checking() const noexcept;

    // Invalidates any in-flight result so it is dropped instead of posted.
    // Never blocks: the background thread finishes on its own under the
    // finite WinHTTP timeouts.
    void Cancel() noexcept;

private:
    struct SharedState {
        std::atomic<bool> checking{false};
        std::atomic<unsigned> generation{0};
    };
    std::shared_ptr<SharedState> state_;
};

}  // namespace fastpdf::app::update
