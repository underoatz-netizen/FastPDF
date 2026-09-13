// FastPDF application entry point.
//
// Handoff 1 scope: a Unicode Win32 application shell with a minimal
// Direct2D/DirectWrite paint path. PDFium is initialized through the
// fastpdf_pdfium RAII adapter when the pinned artifact is linked; the shell
// stays fully runnable without it.

#include <windows.h>
#include <shellapi.h>

#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/platform/win/ComInitializer.h>

#include "AppWindow.h"
#include "CommandLineOpen.h"

// Opt into the OS-native visual styles (comctl32 v6) so the standard Win32
// viewer chrome - menus, find-panel edit/static/buttons and scrollbars - is
// drawn with current Windows theming and exposes its built-in hover / focus /
// pressed states. This is an OS component (already present on Windows), not an
// added third-party dependency, and it changes only appearance, not behaviour.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

// Per-monitor DPI awareness (Windows 10 1607+). Must be called before any
// window is created.
void EnablePerMonitorDpiAwareness() noexcept {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

} // namespace

int WINAPI wWinMain([[maybe_unused]] HINSTANCE instance, HINSTANCE /*prevInstance*/,
                    [[maybe_unused]] PWSTR commandLine, int /*showCommand*/) {
    EnablePerMonitorDpiAwareness();

    // COM is required by Direct2D/DirectWrite. RAII: uninitialized on exit.
    fastpdf::platform::win::ComInitializer com;
    if (!com.initialized()) {
        MessageBoxW(nullptr, L"Failed to initialize COM.", L"FastPDF",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    // PDFium process-wide init/shutdown, owned by the RAII adapter. When the
    // pinned artifact is absent (FASTPDF_WITH_PDFIUM=OFF) this is a no-op and
    // isAvailable() reports false.
    fastpdf::pdfium::PdfiumLibrary pdfium;

    fastpdf::app::AppWindow window(pdfium);
    if (!window.Create(L"FastPDF")) {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"FastPDF",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    window.Show();

    // Direct command-line PDF open (file association or explicit launch):
    // parse the FULL command line from GetCommandLineW(), which always
    // includes the program name. CommandLineToArgvW unquotes it, so argv[0]
    // is the executable and argv[1] (when argc >= 2) is the explicit PDF
    // path with spaces and Unicode preserved. A no-argument launch has
    // argc == 1 and stays in the normal empty/ready state; the executable is
    // never opened as a PDF. wWinMain's pCmdLine is intentionally not used:
    // it excludes the program name, and CommandLineToArgvW("") would return
    // the executable path as argv[0].
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        const std::wstring pdfPath =
            fastpdf::app::PdfPathFromCommandLine(argc, argv);
        if (!pdfPath.empty()) {
            window.OpenPathFromCommandLine(pdfPath);
        }
        LocalFree(argv);
    }

    MSG message{};
    while (const BOOL result = GetMessageW(&message, nullptr, 0, 0)) {
        if (result == -1) {
            return 1;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}