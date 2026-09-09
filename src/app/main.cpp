// FastPDF application entry point.
//
// Handoff 1 scope: a Unicode Win32 application shell with a minimal
// Direct2D/DirectWrite paint path. PDFium is initialized through the
// fastpdf_pdfium RAII adapter when the pinned artifact is linked; the shell
// stays fully runnable without it.

#include <windows.h>

#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/platform/win/ComInitializer.h>

#include "AppWindow.h"

namespace {

// Per-monitor DPI awareness (Windows 10 1607+). Must be called before any
// window is created.
void EnablePerMonitorDpiAwareness() noexcept {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

} // namespace

int WINAPI wWinMain([[maybe_unused]] HINSTANCE instance, HINSTANCE /*prevInstance*/,
                    PWSTR /*commandLine*/, int /*showCommand*/) {
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