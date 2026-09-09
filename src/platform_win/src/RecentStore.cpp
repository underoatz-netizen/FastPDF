#include "fastpdf/platform/win/RecentStore.h"

#include <shlobj.h>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace fastpdf::platform::win {

namespace {

constexpr DWORD kMaxLogSizeBytes = 1024 * 1024; // 1 MB rotation threshold

std::wstring GetLocalAppDataPath() noexcept {
    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        std::wstring res(path);
        CoTaskMemFree(path);
        return res;
    }
    // Fallback to environment variable
    wchar_t envBuf[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", envBuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(envBuf, len);
    }
    return L".";
}

} // namespace

std::wstring GetAppStateDirectory() noexcept {
    std::wstring localApp = GetLocalAppDataPath();
    std::wstring dir = localApp + L"\\FastPDF";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool LoadRecentFiles(fastpdf::core::recent::RecentState& state) noexcept {
    std::wstring dir = GetAppStateDirectory();
    std::wstring path = dir + L"\\recent.dat";

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        state.entries.clear();
        state.version = fastpdf::core::recent::kCurrentStateVersion;
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }

    std::string text(static_cast<size_t>(fileSize.QuadPart), '\0');
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, text.data(), static_cast<DWORD>(text.size()), &bytesRead, nullptr);
    CloseHandle(hFile);

    if (!ok || bytesRead != text.size()) {
        return false;
    }

    return fastpdf::core::recent::DeserializeRecentState(text, state);
}

bool SaveRecentFiles(const fastpdf::core::recent::RecentState& state) noexcept {
    std::wstring dir = GetAppStateDirectory();
    std::wstring targetPath = dir + L"\\recent.dat";
    std::wstring tempPath = dir + L"\\recent.tmp";

    std::string data = fastpdf::core::recent::SerializeRecentState(state);

    HANDLE hTemp = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hTemp == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesWritten = 0;
    BOOL writeOk = WriteFile(hTemp, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr);
    FlushFileBuffers(hTemp);
    CloseHandle(hTemp);

    if (!writeOk || bytesWritten != data.size()) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    // Atomic replace
    if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    return true;
}

void AppendDiagnosticLog(const std::wstring& message) noexcept {
    std::wstring dir = GetAppStateDirectory();
    std::wstring logPath = dir + L"\\fastpdf.log";
    std::wstring logOldPath = dir + L"\\fastpdf.log.1";

    // Check size for rotation
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER size;
        size.HighPart = fad.nFileSizeHigh;
        size.LowPart = fad.nFileSizeLow;
        if (size.QuadPart >= kMaxLogSizeBytes) {
            // Rotate: delete old, move current to old
            DeleteFileW(logOldPath.c_str());
            MoveFileW(logPath.c_str(), logOldPath.c_str());
        }
    }

    HANDLE hFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    // Build timestamp line: [YYYY-MM-DD HH:MM:SS] message\r\n
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t tsBuf[64]{};
    swprintf_s(tsBuf, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::wstring line = tsBuf + message + L"\r\n";
    DWORD written = 0;
    WriteFile(hFile, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(hFile);
}

} // namespace fastpdf::platform::win
