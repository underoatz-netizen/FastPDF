#include "SearchPanelRouting.h"

namespace fastpdf::app::search {

namespace {

WNDPROC g_originalPanelProc = nullptr;

LRESULT CALLBACK SearchPanelSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam) {
    if (msg == WM_COMMAND) {
        // Child controls (edit box, prev/next/close buttons) send WM_COMMAND
        // to their parent, the panel. Forward it to the main window so the
        // app's command handler receives it.
        HWND mainWin = GetParent(hwnd);
        SendMessageW(mainWin, WM_COMMAND, wParam, lParam);
        return 0;
    }
    return CallWindowProcW(g_originalPanelProc, hwnd, msg, wParam, lParam);
}

} // namespace

WNDPROC InstallSearchPanelCommandForwarder(HWND panelHwnd) noexcept {
    g_originalPanelProc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrW(panelHwnd, GWLP_WNDPROC));
    SetWindowLongPtrW(panelHwnd, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(SearchPanelSubclassProc));
    return g_originalPanelProc;
}

} // namespace fastpdf::app::search