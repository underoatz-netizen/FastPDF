// Focused Win32 test for the search panel command-forwarding fix.
//
// The search panel is a STATIC child of the main window, and its child
// controls (edit box, prev/next/close buttons) are reparented onto it. Child
// controls send WM_COMMAND to their parent (the panel), which as a STATIC
// control would otherwise swallow them, leaving the search UI unable to be
// closed or driven from its buttons. InstallSearchPanelCommandForwarder
// subclasses the panel to forward WM_COMMAND to the main window.
//
// PDFium-free and app-shell-free: only SearchPanelRouting.cpp is compiled in.

#include <windows.h>

#include <cstdint>

#include "test_harness.h"
#include "SearchPanelRouting.h"

namespace {

constexpr UINT_PTR kPanelId = 10100;
constexpr UINT_PTR kCloseButtonId = 17;  // mirrors kIdSearchClose
constexpr UINT_PTR kEditId = 10101;

struct CommandRecord {
    bool received = false;
    WPARAM wParam = 0;
    LPARAM lParam = 0;
};

CommandRecord g_record;

LRESULT CALLBACK TestParentProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam) {
    if (msg == WM_COMMAND) {
        g_record.received = true;
        g_record.wParam = wParam;
        g_record.lParam = lParam;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct TestWindows {
    HWND parent = nullptr;
    HWND panel = nullptr;
    HWND button = nullptr;
    HWND edit = nullptr;
    HINSTANCE instance = nullptr;
};

TestWindows CreateSearchPanelFixture() {
    TestWindows w;
    w.instance = GetModuleHandleW(nullptr);

    static const wchar_t kParentClass[] = L"FastPDF.SearchRoutingTestParent";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TestParentProc;
    wc.hInstance = w.instance;
    wc.lpszClassName = kParentClass;
    RegisterClassExW(&wc);

    w.parent = CreateWindowExW(0, kParentClass, L"",
                               WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, nullptr,
                               nullptr, w.instance, nullptr);

    // Panel: STATIC child of the main window (mirrors ShowSearchUI).
    w.panel = CreateWindowExW(0, L"STATIC", nullptr,
                              WS_CHILD | WS_VISIBLE | WS_BORDER | SS_NOTIFY,
                              0, 0, 100, 30, w.parent,
                              reinterpret_cast<HMENU>(kPanelId), w.instance,
                              nullptr);

    // Close button: created as a child of the main window, then reparented
    // onto the panel (mirrors ShowSearchUI).
    w.button = CreateWindowExW(0, L"BUTTON", L"X",
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               0, 0, 20, 20, w.parent,
                               reinterpret_cast<HMENU>(kCloseButtonId),
                               w.instance, nullptr);
    SetParent(w.button, w.panel);

    // Edit box: created as a child of the main window, then reparented onto
    // the panel (mirrors ShowSearchUI).
    w.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             0, 0, 60, 20, w.parent,
                             reinterpret_cast<HMENU>(kEditId), w.instance,
                             nullptr);
    SetParent(w.edit, w.panel);

    return w;
}

void DestroySearchPanelFixture(TestWindows& w) {
    if (w.button != nullptr) {
        DestroyWindow(w.button);
    }
    if (w.edit != nullptr) {
        DestroyWindow(w.edit);
    }
    if (w.panel != nullptr) {
        DestroyWindow(w.panel);
    }
    if (w.parent != nullptr) {
        DestroyWindow(w.parent);
    }
    if (w.instance != nullptr) {
        UnregisterClassW(L"FastPDF.SearchRoutingTestParent", w.instance);
    }
}

} // namespace

FASTPDF_TEST(search_panel_without_forwarder_swallows_button_command) {
    TestWindows w = CreateSearchPanelFixture();
    FASTPDF_CHECK(w.parent != nullptr);
    FASTPDF_CHECK(w.panel != nullptr);
    FASTPDF_CHECK(w.button != nullptr);

    // A STATIC panel does not forward child-control commands: clicking the
    // close button must NOT reach the main window. This documents the bug the
    // forwarder fixes.
    g_record = CommandRecord{};
    SendMessageW(w.button, BM_CLICK, 0, 0);
    FASTPDF_CHECK(!g_record.received);

    DestroySearchPanelFixture(w);
}

FASTPDF_TEST(search_panel_forwarder_delivers_close_button_command) {
    TestWindows w = CreateSearchPanelFixture();
    FASTPDF_CHECK(w.parent != nullptr);
    FASTPDF_CHECK(w.panel != nullptr);
    FASTPDF_CHECK(w.button != nullptr);

    fastpdf::app::search::InstallSearchPanelCommandForwarder(w.panel);

    g_record = CommandRecord{};
    SendMessageW(w.button, BM_CLICK, 0, 0);
    FASTPDF_CHECK(g_record.received);
    FASTPDF_CHECK_EQ(LOWORD(g_record.wParam), kCloseButtonId);
    FASTPDF_CHECK_EQ(reinterpret_cast<HWND>(g_record.lParam), w.button);

    DestroySearchPanelFixture(w);
}

FASTPDF_TEST(search_panel_forwarder_delivers_edit_en_change) {
    TestWindows w = CreateSearchPanelFixture();
    FASTPDF_CHECK(w.parent != nullptr);
    FASTPDF_CHECK(w.panel != nullptr);
    FASTPDF_CHECK(w.edit != nullptr);

    fastpdf::app::search::InstallSearchPanelCommandForwarder(w.panel);

    // The edit control notifies its parent (the panel) with EN_CHANGE; the
    // forwarder must pass the notification through to the main window with
    // the original wParam/lParam so the app can react to search text changes.
    g_record = CommandRecord{};
    SendMessageW(w.panel, WM_COMMAND,
                 MAKEWPARAM(kEditId, EN_CHANGE),
                 reinterpret_cast<LPARAM>(w.edit));
    FASTPDF_CHECK(g_record.received);
    FASTPDF_CHECK_EQ(LOWORD(g_record.wParam), kEditId);
    FASTPDF_CHECK_EQ(HIWORD(g_record.wParam), EN_CHANGE);
    FASTPDF_CHECK_EQ(reinterpret_cast<HWND>(g_record.lParam), w.edit);

    DestroySearchPanelFixture(w);
}

FASTPDF_TEST(search_panel_forwarder_passes_through_other_messages) {
    TestWindows w = CreateSearchPanelFixture();
    FASTPDF_CHECK(w.parent != nullptr);
    FASTPDF_CHECK(w.panel != nullptr);

    fastpdf::app::search::InstallSearchPanelCommandForwarder(w.panel);

    // Non-WM_COMMAND messages must still reach the original STATIC proc: the
    // panel still stores and returns its window text.
    SetWindowTextW(w.panel, L"panel text");
    wchar_t buf[32]{};
    GetWindowTextW(w.panel, buf, 32);
    FASTPDF_CHECK_EQ(std::wstring(buf), std::wstring(L"panel text"));

    DestroySearchPanelFixture(w);
}

int main() { return fastpdf::test::RunAll(); }