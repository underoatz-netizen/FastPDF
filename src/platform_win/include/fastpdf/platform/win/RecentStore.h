#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include <fastpdf/core/recent_files.h>

namespace fastpdf::platform::win {

// Structure representing a single PDF-space rectangle (PDF points, bottom-left origin or normalized)
struct PdfRect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

// Returns the FastPDF application state directory under %LOCALAPPDATA%\FastPDF.
// Ensures the directory exists with normal user permissions.
std::wstring GetAppStateDirectory() noexcept;

// Reads the versioned recent files state atomically from %LOCALAPPDATA%\FastPDF\recent.dat
bool LoadRecentFiles(fastpdf::core::recent::RecentState& state) noexcept;

// Saves the versioned recent files state atomically to %LOCALAPPDATA%\FastPDF\recent.dat
// using write-to-temp and atomic MoveFileExW / ReplaceFileW.
bool SaveRecentFiles(const fastpdf::core::recent::RecentState& state) noexcept;

// Appends a diagnostic log message to %LOCALAPPDATA%\FastPDF\fastpdf.log
// with a bounded file size rotation policy (rotates fastpdf.log -> fastpdf.log.1 at 1MB, max 2 files).
// Documents contents are NEVER logged.
void AppendDiagnosticLog(const std::wstring& message) noexcept;

} // namespace fastpdf::platform::win
