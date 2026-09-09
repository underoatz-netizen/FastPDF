#pragma once

#include <windows.h>

namespace fastpdf::app::search {

// Installs a window procedure on the search panel that forwards WM_COMMAND
// messages from the panel's child controls (search edit box, prev/next/close
// buttons) to the panel's parent (the main window), which owns the search
// command handling. Without this, a STATIC panel swallows child-control
// notifications and the search UI cannot be closed or driven from its buttons.
//
// Returns the panel's original window procedure (the forwarder chains to it
// for all non-WM_COMMAND messages).
WNDPROC InstallSearchPanelCommandForwarder(HWND panelHwnd) noexcept;

} // namespace fastpdf::app::search