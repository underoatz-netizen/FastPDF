#include "AppWindow.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <fastpdf/core/version.h>
#include <fastpdf/platform/win/D2d.h>
#include <fastpdf/platform/win/Dpi.h>
#include <fastpdf/renderer/render_key.h>

#include "DropRouting.h"
#include "Presentation.h"
#include "ScreenshotGeometry.h"
#include "ScreenshotCompositor.h"
#include "ScreenshotClipboard.h"
#include "PdfToPngDialog.h"
#include "ImageToPdfDialog.h"
#include "PrintDialog.h"
#include "resource.h"

namespace fastpdf::app {

namespace {

constexpr wchar_t kWindowClassName[] = L"FastPDF.MainWindow";
constexpr UINT kWorkerDoneMessage = WM_APP + 1;
constexpr UINT_PTR kPromotionTimerId = 1;
constexpr UINT_PTR kToastTimerId = 2;
constexpr UINT_PTR kIdFileOpen = 1;
constexpr UINT_PTR kIdFitWidth = 2;
constexpr UINT_PTR kIdFitPage = 3;
constexpr UINT_PTR kIdZoom100 = 4;
constexpr UINT_PTR kIdZoomIn = 5;
constexpr UINT_PTR kIdZoomOut = 6;
constexpr UINT_PTR kIdNextPage = 7;
constexpr UINT_PTR kIdPrevPage = 8;
constexpr UINT_PTR kIdPresent = 9;
constexpr UINT_PTR kIdScreenshot = 10;
constexpr UINT_PTR kIdConvertPdfToPng = 11;
constexpr UINT_PTR kIdConvertImageToPdf = 12;
constexpr UINT_PTR kIdPrint = 13;
constexpr UINT_PTR kIdFind = 14;
constexpr UINT_PTR kIdSearchPrev = 15;
constexpr UINT_PTR kIdSearchNext = 16;
constexpr UINT_PTR kIdSearchClose = 17;
constexpr UINT_PTR kIdRecentBase = 1000;
constexpr UINT_PTR kIdRecentClear = 1100;
constexpr UINT_PTR kIdToggleDiagnostics = 1200;

const D2D1_COLOR_F kBackgroundColor = D2D1::ColorF(0.11f, 0.11f, 0.12f, 1.0f);
const D2D1_COLOR_F kStatusColor = D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.0f);
const D2D1_COLOR_F kErrorColor = D2D1::ColorF(0.95f, 0.55f, 0.55f, 1.0f);
const D2D1_COLOR_F kPresentationBackground = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
const D2D1_COLOR_F kSearchHighlightColor = D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.35f); // yellow translucent
const D2D1_COLOR_F kSearchActiveHighlightColor = D2D1::ColorF(1.0f, 0.55f, 0.0f, 0.60f); // orange translucent

// Converts a UTF-8 narrow string to a wide string.
std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), length);
    return result;
}

// Returns the directory containing the executable, for the log file.
std::wstring ExecutableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return path.substr(0, slash);
}

// Computes the render pixel size for a laid-out page, guarding against pixel
// overflow and enforcing the hard dimension bounds (aspect-preserving).
fastpdf::renderer::PixelSize ComputeRenderSize(double width, double height) noexcept {
    // Clamp the raw layout size into a safe range before converting to int, so
    // lround can never overflow for a degenerate (huge) PDF page.
    constexpr double kSafeMaxPixels = 1000000.0;
    const double w = std::clamp(width, 1.0, kSafeMaxPixels);
    const double h = std::clamp(height, 1.0, kSafeMaxPixels);
    return fastpdf::renderer::ClampPixelSize(
        std::max(1, static_cast<int>(std::lround(w))),
        std::max(1, static_cast<int>(std::lround(h))));
}

} // namespace

AppWindow::AppWindow(const fastpdf::pdfium::PdfiumLibrary& pdfium) noexcept
    : pdfium_(pdfium) {}

AppWindow::~AppWindow() {
    // Stop the worker before destroying the window so it never posts to a
    // dead HWND. The worker joins here; the persistent document is destroyed
    // on the worker thread inside Run().
    worker_.Shutdown();
    DiscardDeviceResources();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool AppWindow::Create(const wchar_t* title) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AppWindow::WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = nullptr;  // Everything is painted in WM_PAINT.
    wc.lpszClassName = kWindowClassName;
    // Embedded application icon (see IDI_APP_ICON in app.rc) for the title bar,
    // task bar and Alt+Tab. LoadIconW falls back to the default if absent.
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));

    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi_ = fastpdf::platform::win::GetWindowDpi(nullptr);
    const int width = fastpdf::platform::win::ScaleForDpi(1024, dpi_);
    const int height = fastpdf::platform::win::ScaleForDpi(768, dpi_);

    const HWND created = CreateWindowExW(
        0, kWindowClassName, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, wc.hInstance, this);
    if (created == nullptr) {
        hwnd_ = nullptr;  // WM_NCCREATE may have set it before the failure.
        return false;
    }
    hwnd_ = created;

    // Menu: File > Open... (Ctrl+O), plus View zoom/navigation commands.
    HMENU menu = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, kIdFileOpen, L"&Open...\tCtrl+O");

    // Recent Files submenu
    recentMenu_ = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(recentMenu_), L"&Recent Files");

    AppendMenuW(fileMenu, MF_STRING, kIdPrint, L"&Print...\tCtrl+P");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");

    HMENU viewMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_STRING, kIdFitWidth, L"Fit &Width\tCtrl+2");
    AppendMenuW(viewMenu, MF_STRING, kIdFitPage, L"Fit &Page\tCtrl+0");
    AppendMenuW(viewMenu, MF_STRING, kIdZoom100, L"100%\tCtrl+1");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, kIdFind, L"&Find...\tCtrl+F");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, kIdZoomIn, L"Zoom &In");
    AppendMenuW(viewMenu, MF_STRING, kIdZoomOut, L"Zoom &Out");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, kIdNextPage, L"Next &Page\tPage Down");
    AppendMenuW(viewMenu, MF_STRING, kIdPrevPage, L"Previous &Page\tPage Up");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, kIdPresent, L"&Present\tF11");
    AppendMenuW(viewMenu, MF_STRING, kIdScreenshot, L"&Screenshot\tCtrl+S");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");

    HMENU convertMenu = CreatePopupMenu();
    AppendMenuW(convertMenu, MF_STRING, kIdConvertPdfToPng, L"&PDF to PNG...");
    AppendMenuW(convertMenu, MF_STRING, kIdConvertImageToPdf, L"&Image to PDF...");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(convertMenu), L"&Convert");

    SetMenu(hwnd_, menu);

    // Accept drag-and-drop of a single PDF file.
    DragAcceptFiles(hwnd_, TRUE);

    // Load recent files state and populate menu
    LoadRecentFilesState();
    UpdateRecentMenu();

    worker_.Start(hwnd_, kWorkerDoneMessage);
    statusText_ = L"Press Ctrl+O or File > Open to open a PDF.";

    appStartupTimeMs_ = GetTickCount64();
    Log(L"FastPDF startup complete (HWND=" + std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd_)) +
        L", DPI=" + std::to_wstring(dpi_) + L")");
    return true;
}

void AppWindow::Show() noexcept {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
}

LRESULT CALLBACK AppWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            OnPaint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_SIZE: {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            OnResize(width, height);
            return 0;
        }
        case WM_DPICHANGED: {
            const UINT dpi = HIWORD(wParam);
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            OnDpiChanged(dpi, *suggested);
            return 0;
        }
        case WM_COMMAND: {
            if (lParam != 0 && (HWND)lParam == hwndSearchEdit_ && HIWORD(wParam) == EN_CHANGE) {
                OnSearchTextChanged();
                return 0;
            }
            OnCommand(LOWORD(wParam));
            return 0;
        }
        case WM_KEYDOWN: {
            OnKeyDown(wParam);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            if (screenshotActive_) {
                OnScreenshotMouseDown(x, y);
            } else if (toastVisible_) {
                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);
                if (fx >= toastSaveButtonRect_.left && fx <= toastSaveButtonRect_.right &&
                    fy >= toastSaveButtonRect_.top && fy <= toastSaveButtonRect_.bottom) {
                    OnSavePngClicked();
                } else {
                    HideNotification();
                }
            } else {
                OnLeftButtonDown();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (screenshotActive_) {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                OnScreenshotMouseMove(x, y);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (screenshotActive_) {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                OnScreenshotMouseUp(x, y);
                return 0;
            }
            break;
        }
        case WM_SETCURSOR: {
            if (screenshotActive_) {
                SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_CROSS)));
                return TRUE;
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            OnMouseWheel(wParam, lParam);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == kPromotionTimerId) {
                OnPromotionTimer();
                return 0;
            }
            if (wParam == kToastTimerId) {
                HideNotification();
                return 0;
            }
            break;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT fileCount = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            if (fileCount == 1) {
                wchar_t file[MAX_PATH]{};
                if (DragQueryFileW(drop, 0, file, MAX_PATH) > 0) {
                    if (IsPdfPath(file)) {
                        OnDropFile(file);
                    } else if (IsSupportedImagePath(file)) {
                        OnConvertImageToPdf({ file });
                    }
                }
            } else if (fileCount > 1) {
                std::vector<std::wstring> droppedImages;
                droppedImages.reserve(fileCount);
                for (UINT i = 0; i < fileCount; ++i) {
                    wchar_t file[MAX_PATH]{};
                    if (DragQueryFileW(drop, i, file, MAX_PATH) > 0) {
                        if (IsSupportedImagePath(file)) {
                            droppedImages.push_back(file);
                        }
                    }
                }
                if (!droppedImages.empty()) {
                    OnConvertImageToPdf(droppedImages);
                }
            }
            DragFinish(drop);
            return 0;
        }
        case kWorkerDoneMessage: {
            auto* completion =
                reinterpret_cast<RenderWorker::WorkerCompletion*>(lParam);
            OnWorkerDone(completion);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;  // Fully painted in WM_PAINT; avoid flicker.
        case WM_DESTROY:
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void AppWindow::OnPaint() noexcept {
    if (!EnsureDeviceResources()) {
        return;
    }
    const std::uint64_t paintStart = GetTickCount64();
    renderTarget_->BeginDraw();  // Returns void; errors surface via EndDraw.
    if (presenting_) {
        DrawPresentation(*renderTarget_.Get());
    } else {
        renderTarget_->Clear(kBackgroundColor);
        DrawPages(*renderTarget_.Get());
        DrawSearchHighlights(*renderTarget_.Get());
        DrawStatusText(*renderTarget_.Get());
        if (screenshotActive_) {
            DrawScreenshotOverlay(*renderTarget_.Get());
        }
        if (toastVisible_) {
            DrawNotificationToast(*renderTarget_.Get());
        }
    }
    const HRESULT hr = renderTarget_->EndDraw();
    lastFrameTimeMs_ = static_cast<double>(GetTickCount64() - paintStart);
    if (hr == D2DERR_RECREATE_TARGET) {
        // Device lost (e.g. display mode change): drop all device-dependent
        // resources and schedule a repaint, which recreates them and redraws
        // the pages from the retained CPU bitmaps.
        DiscardDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void AppWindow::OnResize(UINT width, UINT height) noexcept {
    if (renderTarget_ != nullptr && width > 0 && height > 0) {
        renderTarget_->Resize(D2D1::SizeU(width, height));
    }
    if (presenting_) {
        // Rebuild the single-page presentation layout for the new viewport and
        // re-request the pre-render set; the page stays centered.
        if (state_ == ViewState::Ready && width > 0 && height > 0) {
            RebuildPresentationLayout();
            RequestPresentationRenders();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (state_ == ViewState::Ready && layout_.has_value() && width > 0 &&
        height > 0) {
        // Preserve the logical location across the resize using the anchor.
        const fastpdf::core::layout::ViewAnchor anchor =
            fastpdf::core::layout::CaptureAnchor(
                *layout_, scrollX_, scrollY_, ViewportWidth(), ViewportHeight(),
                ViewportWidth() * 0.5, ViewportHeight() * 0.5);
        RebuildLayout();
        fastpdf::core::layout::RestoreAnchor(
            *layout_, anchor, ViewportWidth(), ViewportHeight(), scrollX_,
            scrollY_);
        MarkInputActivity();
        RequestRenders();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OnDpiChanged(UINT dpi, const RECT& suggestedRect) noexcept {
    dpi_ = dpi;
    SetWindowPos(hwnd_, nullptr, suggestedRect.left, suggestedRect.top,
                 suggestedRect.right - suggestedRect.left,
                 suggestedRect.bottom - suggestedRect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    // The text format is DPI-dependent; recreate it on the next paint.
    statusTextFormat_.Reset();
    if (presenting_) {
        if (state_ == ViewState::Ready) {
            RebuildPresentationLayout();
            RequestPresentationRenders();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (state_ == ViewState::Ready && layout_.has_value()) {
        const fastpdf::core::layout::ViewAnchor anchor =
            fastpdf::core::layout::CaptureAnchor(
                *layout_, scrollX_, scrollY_, ViewportWidth(), ViewportHeight(),
                ViewportWidth() * 0.5, ViewportHeight() * 0.5);
        RebuildLayout();
        fastpdf::core::layout::RestoreAnchor(
            *layout_, anchor, ViewportWidth(), ViewportHeight(), scrollX_,
            scrollY_);
        MarkInputActivity();
        RequestRenders();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OnCommand(WPARAM wParam) noexcept {
    switch (LOWORD(wParam)) {
        case kIdFileOpen:
            OnOpenFile();
            break;
        case kIdPresent:
            if (presenting_) {
                ExitPresentation();
            } else {
                EnterPresentation();
            }
            break;
        case kIdFitWidth:
            SetFitMode(fastpdf::core::layout::FitMode::FitWidth);
            break;
        case kIdFitPage:
            SetFitMode(fastpdf::core::layout::FitMode::FitPage);
            break;
        case kIdZoom100:
            SetZoomPercent(100.0);
            break;
        case kIdZoomIn:
            ZoomAt(zoomPercent_ * 1.25, ViewportWidth() * 0.5,
                   ViewportHeight() * 0.5);
            break;
        case kIdZoomOut:
            ZoomAt(zoomPercent_ / 1.25, ViewportWidth() * 0.5,
                   ViewportHeight() * 0.5);
            break;
        case kIdNextPage:
            GoToPage(CurrentPageIndex() + 1);
            break;
        case kIdPrevPage:
            GoToPage(CurrentPageIndex() - 1);
            break;
        case kIdScreenshot:
            if (!presenting_ && state_ == ViewState::Ready) {
                EnterScreenshotMode();
            }
            break;
        case kIdConvertPdfToPng:
            if (!presenting_ && state_ == ViewState::Ready) {
                OnConvertPdfToPng();
            }
            break;
        case kIdConvertImageToPdf:
            if (!presenting_) {
                OnConvertImageToPdf();
            }
            break;
        case kIdPrint:
            if (!presenting_ && state_ == ViewState::Ready) {
                OnPrint();
            }
            break;
        case kIdFind:
            if (!presenting_ && state_ == ViewState::Ready) {
                ShowSearchUI();
            }
            break;
        case kIdSearchPrev:
            SearchPrev();
            break;
        case kIdSearchNext:
            SearchNext();
            break;
        case kIdSearchClose:
            HideSearchUI();
            break;
        case kIdRecentClear:
            recentState_.entries.clear();
            fastpdf::platform::win::SaveRecentFiles(recentState_);
            UpdateRecentMenu();
            break;
        case kIdToggleDiagnostics:
            diagnosticsEnabled_ = !diagnosticsEnabled_;
            Log(L"Developer diagnostics toggle: " + std::wstring(diagnosticsEnabled_ ? L"ENABLED" : L"DISABLED"));
            UpdateStatusText();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        default:
            if (LOWORD(wParam) >= kIdRecentBase && LOWORD(wParam) < kIdRecentBase + fastpdf::core::recent::kMaxRecentFiles) {
                OnOpenRecent(LOWORD(wParam) - kIdRecentBase);
            }
            break;
    }
}

void AppWindow::OnKeyDown(WPARAM wParam) noexcept {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (presenting_) {
        HandlePresentationKey(static_cast<std::uint32_t>(wParam), ctrl);
        return;
    }

    if (ctrl) {
        switch (wParam) {
            case 'O':
                OnOpenFile();
                return;
            case 'F':
                if (!presenting_ && state_ == ViewState::Ready) {
                    ShowSearchUI();
                }
                return;
            case 'S':
                if (!presenting_ && state_ == ViewState::Ready) {
                    EnterScreenshotMode();
                }
                return;
            case 'P':
                if (!presenting_ && state_ == ViewState::Ready) {
                    OnPrint();
                }
                return;
            case '0':
                SetFitMode(fastpdf::core::layout::FitMode::FitPage);
                return;
            case '1':
                SetZoomPercent(100.0);
                return;
            case '2':
                SetFitMode(fastpdf::core::layout::FitMode::FitWidth);
                return;
            case 'D':
                SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(kIdToggleDiagnostics, 0), 0);
                return;
            default:
                break;
        }
    }
    switch (wParam) {
        case VK_ESCAPE:
            if (searchVisible_) {
                HideSearchUI();
                return;
            }
            if (screenshotActive_) {
                CancelScreenshotMode();
                return;
            }
            if (toastVisible_) {
                HideNotification();
                return;
            }
            break;
        case VK_F11:
            EnterPresentation();
            return;
        case VK_PRIOR:  // Page Up
            GoToPage(CurrentPageIndex() - 1);
            return;
        case VK_NEXT:  // Page Down
            GoToPage(CurrentPageIndex() + 1);
            return;
        case VK_HOME:
            GoToPage(0);
            return;
        case VK_END:
            if (layout_.has_value()) {
                GoToPage(layout_->pageCount() - 1);
            }
            return;
        default:
            break;
    }
}

void AppWindow::OnMouseWheel(WPARAM wParam, LPARAM lParam) noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    const short wheelDelta = static_cast<short>(HIWORD(wParam));

    if (presenting_) {
        // Mouse wheel navigates pages: wheel up = previous, wheel down = next.
        if (wheelDelta > 0) {
            PresentationPrev();
        } else if (wheelDelta < 0) {
            PresentationNext();
        }
        return;
    }

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrl) {
        // Cursor-centered zoom. The cursor position is in screen coordinates;
        // convert to client coordinates.
        POINT cursor{static_cast<short>(LOWORD(lParam)),
                     static_cast<short>(HIWORD(lParam))};
        ScreenToClient(hwnd_, &cursor);
        const double factor = std::pow(1.1, wheelDelta / 120.0);
        ZoomAt(zoomPercent_ * factor, static_cast<double>(cursor.x),
               static_cast<double>(cursor.y));
    } else {
        // Vertical scroll: one wheel notch scrolls ~3 lines of the viewport.
        const double lines = wheelDelta / 120.0;
        ScrollBy(0.0, -lines * ViewportHeight() * 0.1);
    }
}

void AppWindow::OnLeftButtonDown() noexcept {
    // In presentation mode a left click advances to the next page.
    if (presenting_) {
        PresentationNext();
    }
}

void AppWindow::EnterPresentation() noexcept {
    if (presenting_ || state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    SaveWindowPlacement();

    // Hide the menu and switch to a borderless popup covering the monitor.
    savedMenu_ = GetMenu(hwnd_);
    SetMenu(hwnd_, nullptr);
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);

    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi)) {
        const RECT& r = mi.rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, r.left, r.top, r.right - r.left,
                     r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        SetWindowPos(hwnd_, HWND_TOP, 0, 0,
                     static_cast<int>(ViewportWidth()),
                     static_cast<int>(ViewportHeight()),
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    presenting_ = true;
    presentationPage_ = CurrentPageIndex();

    // Drop any pending normal-view renders and stop the preview promotion
    // timer; presentation renders at final quality only.
    KillTimer(hwnd_, kPromotionTimerId);
    previewActive_ = false;
    ++viewEpoch_;
    pendingKeys_.clear();
    d2dBitmaps_.clear();
    worker_.SetViewEpoch(viewEpoch_);

    RebuildPresentationLayout();
    RequestPresentationRenders();
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    Log(L"Presentation entered at page " + std::to_wstring(presentationPage_));
}

void AppWindow::ExitPresentation() noexcept {
    if (!presenting_) {
        return;
    }
    presenting_ = false;
    presentationLayout_.reset();
    presentationRenderScale_ = 0.0;

    // Restore the menu and the normal window style/placement.
    if (savedMenu_ != nullptr) {
        SetMenu(hwnd_, savedMenu_);
        savedMenu_ = nullptr;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
    RestoreWindowPlacement();

    // Drop stale presentation renders and re-request the normal view.
    ++viewEpoch_;
    pendingKeys_.clear();
    d2dBitmaps_.clear();
    worker_.SetViewEpoch(viewEpoch_);
    if (state_ == ViewState::Ready && layout_.has_value()) {
        RequestRenders();
        UpdateStatusText();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    Log(L"Presentation exited");
}

void AppWindow::SaveWindowPlacement() noexcept {
    savedPlacementValid_ = false;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd_, &placement)) {
        savedPlacement_ = placement;
        savedPlacementValid_ = true;
    }
}

void AppWindow::RestoreWindowPlacement() noexcept {
    if (savedPlacementValid_) {
        SetWindowPlacement(hwnd_, &savedPlacement_);
        savedPlacementValid_ = false;
    } else {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
    }
}

void AppWindow::RebuildPresentationLayout() noexcept {
    if (!presenting_ || !documentInfo_.has_value()) {
        return;
    }
    if (presentationPage_ < 0 ||
        presentationPage_ >= static_cast<int>(documentInfo_->pages.size())) {
        return;
    }
    const auto& info = documentInfo_->pages[static_cast<size_t>(presentationPage_)];
    const double dpi = static_cast<double>(dpi_);
    const double zoom = fastpdf::core::layout::FitPageZoomPercent(
        ViewportWidth(), ViewportHeight(), info.widthPoints, info.heightPoints,
        dpi);
    const double pixelsPerPoint = fastpdf::core::layout::PixelsPerPoint(zoom, dpi);
    const double gap = fastpdf::platform::win::DpiScaleFactor(dpi_) * 8.0;
    presentationLayout_ = fastpdf::core::layout::ContinuousLayout::Create(
        {{info.widthPoints, info.heightPoints}}, pixelsPerPoint, gap);
}

void AppWindow::RequestPresentationRenders() noexcept {
    if (!presenting_ || !presentationLayout_.has_value() || !documentInfo_.has_value()) {
        return;
    }

    // When the presentation scale changed (resize/DPI), every cached bitmap and
    // in-flight render is at the old scale. Bump the view epoch so stale
    // completions are dropped and clear the pending set.
    const double scale = presentationLayout_->pixelsPerPoint();
    if (std::fabs(scale - presentationRenderScale_) > 1e-9) {
        ++viewEpoch_;
        presentationRenderScale_ = scale;
        pendingKeys_.clear();
        d2dBitmaps_.clear();
        worker_.SetViewEpoch(viewEpoch_);
    }

    const int docPageCount = documentInfo_->pageCount;
    const auto preRender =
        fastpdf::app::presentation::PreRenderPages(presentationPage_, docPageCount);

    std::vector<fastpdf::renderer::RenderKey> wantedKeys;
    wantedKeys.reserve(preRender.size());

    const double dpi = static_cast<double>(dpi_);
    const double vpWidth = ViewportWidth();
    const double vpHeight = ViewportHeight();

    for (const auto& [page, priority] : preRender) {
        if (page < 0 || page >= static_cast<int>(documentInfo_->pages.size())) {
            continue;
        }
        const auto& pageInfo = documentInfo_->pages[static_cast<size_t>(page)];
        const double zoom = fastpdf::core::layout::FitPageZoomPercent(
            vpWidth, vpHeight, pageInfo.widthPoints, pageInfo.heightPoints, dpi);
        const double ppp = fastpdf::core::layout::PixelsPerPoint(zoom, dpi);
        const fastpdf::renderer::PixelSize finalSize = ComputeRenderSize(
            pageInfo.widthPoints * ppp, pageInfo.heightPoints * ppp);
        const fastpdf::renderer::RenderKey finalKey =
            MakeKey(page, finalSize, fastpdf::renderer::Quality::Final);
        wantedKeys.push_back(finalKey);
        SubmitRender(finalKey, static_cast<fastpdf::renderer::RenderPriority>(priority));
    }

    PruneD2dCache(wantedKeys);
}

void AppWindow::DrawPresentation(ID2D1HwndRenderTarget& target) noexcept {
    target.Clear(kPresentationBackground);
    if (!presenting_ || !presentationLayout_.has_value()) {
        return;
    }
    const fastpdf::core::layout::PageRect rect = presentationLayout_->pageRect(0);
    const fastpdf::renderer::PixelSize finalSize =
        ComputeRenderSize(rect.width, rect.height);
    const fastpdf::renderer::RenderKey finalKey =
        MakeKey(presentationPage_, finalSize, fastpdf::renderer::Quality::Final);
    const fastpdf::renderer::Bitmap* bitmap = cpuCache_.get(finalKey);
    if (bitmap == nullptr || bitmap->data.empty()) {
        return;  // Still rendering; the black background is the placeholder.
    }

    // Center the page in the viewport.
    const D2D1_SIZE_F size = target.GetSize();
    const float left = (size.width - static_cast<float>(rect.width)) * 0.5f;
    const float top = (size.height - static_cast<float>(rect.height)) * 0.5f;
    const D2D1_RECT_F dest =
        D2D1::RectF(left, top, left + static_cast<float>(rect.width),
                    top + static_cast<float>(rect.height));

    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBitmap;
    if (!GetOrCreateD2dBitmap(target, finalKey, *bitmap, d2dBitmap)) {
        return;
    }
    target.DrawBitmap(d2dBitmap.Get(), dest, 1.0f,
                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void AppWindow::PresentationNext() noexcept {
    if (!presenting_ || !documentInfo_.has_value()) {
        return;
    }
    PresentationGoTo(presentationPage_ + 1);
}

void AppWindow::PresentationPrev() noexcept {
    if (!presenting_ || !documentInfo_.has_value()) {
        return;
    }
    PresentationGoTo(presentationPage_ - 1);
}

void AppWindow::PresentationGoTo(int page) noexcept {
    if (!presenting_ || !documentInfo_.has_value()) {
        return;
    }
    const int clamped =
        fastpdf::app::presentation::ClampPage(page, documentInfo_->pageCount);
    if (clamped == presentationPage_) {
        return;
    }

    // Timing evidence: was the target page already cached before the advance?
    const std::uint64_t start = GetTickCount64();
    const bool cached = PresentationPageCached(clamped);

    presentationPage_ = clamped;
    RebuildPresentationLayout();
    RequestPresentationRenders();
    InvalidateRect(hwnd_, nullptr, FALSE);

    const std::uint64_t elapsed = GetTickCount64() - start;
    Log(L"Presentation page " + std::to_wstring(presentationPage_) +
        L" (cached=" + std::wstring(cached ? L"yes" : L"no") +
        L", advance=" + std::to_wstring(elapsed) + L" ms)");
}

bool AppWindow::PresentationPageCached(int page) const noexcept {
    if (!documentInfo_.has_value() || page < 0 ||
        page >= static_cast<int>(documentInfo_->pages.size())) {
        return false;
    }
    const auto& pageInfo = documentInfo_->pages[static_cast<size_t>(page)];
    const double dpi = static_cast<double>(dpi_);
    const double zoom = fastpdf::core::layout::FitPageZoomPercent(
        ViewportWidth(), ViewportHeight(), pageInfo.widthPoints,
        pageInfo.heightPoints, dpi);
    const double ppp = fastpdf::core::layout::PixelsPerPoint(zoom, dpi);
    const fastpdf::renderer::PixelSize finalSize = ComputeRenderSize(
        pageInfo.widthPoints * ppp, pageInfo.heightPoints * ppp);
    const fastpdf::renderer::RenderKey finalKey =
        MakeKey(page, finalSize, fastpdf::renderer::Quality::Final);
    return cpuCache_.contains(finalKey);
}

void AppWindow::HandlePresentationKey(std::uint32_t vk, bool ctrl) noexcept {
    switch (fastpdf::app::presentation::MapKey(vk, ctrl)) {
        case fastpdf::app::presentation::Action::Next:
            PresentationNext();
            break;
        case fastpdf::app::presentation::Action::Prev:
            PresentationPrev();
            break;
        case fastpdf::app::presentation::Action::First:
            PresentationGoTo(0);
            break;
        case fastpdf::app::presentation::Action::Last:
            if (documentInfo_.has_value()) {
                PresentationGoTo(documentInfo_->pageCount - 1);
            }
            break;
        case fastpdf::app::presentation::Action::Exit:
            ExitPresentation();
            break;
        case fastpdf::app::presentation::Action::Toggle:
            ExitPresentation();
            break;
        case fastpdf::app::presentation::Action::None:
        default:
            break;
    }
}

void AppWindow::EnterScreenshotMode() noexcept {
    if (screenshotActive_ || presenting_ || state_ != ViewState::Ready) {
        return;
    }
    HideNotification();
    screenshotActive_ = true;
    screenshotDragging_ = false;
    screenshotStartPoint_ = POINT{0, 0};
    screenshotCurrentPoint_ = POINT{0, 0};
    SetCapture(hwnd_);
    SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_CROSS)));
    InvalidateRect(hwnd_, nullptr, FALSE);
    Log(L"Screenshot mode entered");
}

void AppWindow::CancelScreenshotMode() noexcept {
    if (!screenshotActive_) {
        return;
    }
    if (screenshotDragging_) {
        ReleaseCapture();
    }
    screenshotActive_ = false;
    screenshotDragging_ = false;
    screenshotStartPoint_ = POINT{0, 0};
    screenshotCurrentPoint_ = POINT{0, 0};
    InvalidateRect(hwnd_, nullptr, FALSE);
    Log(L"Screenshot mode cancelled");
}

void AppWindow::OnScreenshotMouseDown(int x, int y) noexcept {
    screenshotDragging_ = true;
    screenshotStartPoint_ = POINT{x, y};
    screenshotCurrentPoint_ = POINT{x, y};
    SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OnScreenshotMouseMove(int x, int y) noexcept {
    if (!screenshotDragging_) {
        return;
    }
    screenshotCurrentPoint_ = POINT{x, y};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OnScreenshotMouseUp(int x, int y) noexcept {
    if (!screenshotDragging_) {
        return;
    }
    screenshotCurrentPoint_ = POINT{x, y};
    ReleaseCapture();
    screenshotDragging_ = false;
    screenshotActive_ = false;

    CaptureScreenshot();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::CaptureScreenshot() noexcept {
    const int maxW = static_cast<int>(ViewportWidth());
    const int maxH = static_cast<int>(ViewportHeight());
    const fastpdf::app::screenshot::Rect rawRect =
        fastpdf::app::screenshot::NormalizeRect(
            screenshotStartPoint_.x, screenshotStartPoint_.y,
            screenshotCurrentPoint_.x, screenshotCurrentPoint_.y);
    const fastpdf::app::screenshot::Rect selection =
        fastpdf::app::screenshot::ClampRect(rawRect, maxW, maxH);

    // If selection is too small (e.g. click without drag, < 4x4), cancel silently.
    if (selection.width() < 4 || selection.height() < 4) {
        Log(L"Screenshot selection too small; cancelled");
        return;
    }

    if (!layout_.has_value()) {
        Log(L"Screenshot failed: no document layout");
        return;
    }

    // Build PageSource list for pages in view or overlapping selection
    std::vector<fastpdf::app::screenshot::PageSource> sources;
    const int pageCount = layout_->pageCount();
    for (int p = 0; p < pageCount; ++p) {
        const fastpdf::core::layout::PageRect prect = layout_->pageRect(p);
        const int vx = static_cast<int>(std::lround(prect.left - scrollX_));
        const int vy = static_cast<int>(std::lround(prect.top - scrollY_));
        const int vw = static_cast<int>(std::lround(prect.width));
        const int vh = static_cast<int>(std::lround(prect.height));

        const fastpdf::app::screenshot::Rect pr{vx, vy, vx + vw, vy + vh};
        if (fastpdf::app::screenshot::IntersectRects(selection, pr).empty()) {
            continue;
        }

        const fastpdf::renderer::PixelSize finalSize =
            ComputeRenderSize(prect.width, prect.height);
        const fastpdf::renderer::RenderKey finalKey =
            MakeKey(p, finalSize, fastpdf::renderer::Quality::Final);
        const fastpdf::renderer::Bitmap* bmp = cpuCache_.get(finalKey);

        if (bmp == nullptr || bmp->empty()) {
            // Fall back to preview if available
            const fastpdf::renderer::PixelSize previewSize =
                fastpdf::renderer::PreviewSizeFor(finalSize.width, finalSize.height);
            const fastpdf::renderer::RenderKey previewKey =
                MakeKey(p, previewSize, fastpdf::renderer::Quality::Preview);
            bmp = cpuCache_.get(previewKey);
        }

        sources.push_back({p, vx, vy, vw, vh, bmp});
    }

    fastpdf::renderer::Bitmap captured =
        fastpdf::app::screenshot::CompositeSelection(selection, sources);

    if (captured.empty()) {
        toastMessage_ = L"Screenshot failed: memory or empty selection.";
        toastVisible_ = true;
        SetTimer(hwnd_, kToastTimerId, 4000, nullptr);
        Log(L"Screenshot composition failed");
        return;
    }

    lastScreenshotBitmap_ = std::move(captured);
    const bool copied =
        fastpdf::app::screenshot::CopyBitmapToClipboard(hwnd_, lastScreenshotBitmap_);

    if (copied) {
        toastMessage_ = L"\x2713 Copied to clipboard";
        Log(L"Screenshot copied to clipboard (" +
            std::to_wstring(lastScreenshotBitmap_.width) + L"x" +
            std::to_wstring(lastScreenshotBitmap_.height) + L")");
    } else {
        toastMessage_ = L"Clipboard copy failed.";
        Log(L"Screenshot clipboard copy failed");
    }

    toastVisible_ = true;
    SetTimer(hwnd_, kToastTimerId, 4000, nullptr);
}

void AppWindow::HideNotification() noexcept {
    if (!toastVisible_) {
        return;
    }
    KillTimer(hwnd_, kToastTimerId);
    toastVisible_ = false;
    toastSaveButtonRect_ = D2D1::RectF(0, 0, 0, 0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OnSavePngClicked() noexcept {
    if (lastScreenshotBitmap_.empty()) {
        return;
    }
    HideNotification();

    wchar_t filename[MAX_PATH] = L"Screenshot.png";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Save Screenshot as PNG";

    if (GetSaveFileNameW(&ofn)) {
        const bool saved =
            fastpdf::app::screenshot::SavePngToFile(lastScreenshotBitmap_, filename);
        if (saved) {
            toastMessage_ = L"\x2713 PNG saved successfully";
            Log(L"Screenshot PNG saved to file");
        } else {
            toastMessage_ = L"Failed to save PNG file.";
            Log(L"Screenshot PNG save failed");
        }
        toastVisible_ = true;
        SetTimer(hwnd_, kToastTimerId, 4000, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void AppWindow::DrawScreenshotOverlay(ID2D1HwndRenderTarget& target) noexcept {
    if (!screenshotActive_) {
        return;
    }
    const int maxW = static_cast<int>(ViewportWidth());
    const int maxH = static_cast<int>(ViewportHeight());
    const fastpdf::app::screenshot::Rect rawRect =
        fastpdf::app::screenshot::NormalizeRect(
            screenshotStartPoint_.x, screenshotStartPoint_.y,
            screenshotCurrentPoint_.x, screenshotCurrentPoint_.y);
    const fastpdf::app::screenshot::Rect sel =
        fastpdf::app::screenshot::ClampRect(rawRect, maxW, maxH);

    if (overlayDimBrush_ == nullptr) {
        target.CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &overlayDimBrush_);
    }
    if (overlayBorderBrush_ == nullptr) {
        target.CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.95f), &overlayBorderBrush_);
    }

    if (overlayDimBrush_ == nullptr || overlayBorderBrush_ == nullptr) {
        return;
    }

    const float viewW = static_cast<float>(maxW);
    const float viewH = static_cast<float>(maxH);

    if (screenshotDragging_ && sel.width() > 0 && sel.height() > 0) {
        const float sl = static_cast<float>(sel.left);
        const float st = static_cast<float>(sel.top);
        const float sr = static_cast<float>(sel.right);
        const float sb = static_cast<float>(sel.bottom);

        // Top band
        if (st > 0.0f) {
            target.FillRectangle(D2D1::RectF(0.0f, 0.0f, viewW, st), overlayDimBrush_.Get());
        }
        // Bottom band
        if (sb < viewH) {
            target.FillRectangle(D2D1::RectF(0.0f, sb, viewW, viewH), overlayDimBrush_.Get());
        }
        // Left band
        if (sl > 0.0f) {
            target.FillRectangle(D2D1::RectF(0.0f, st, sl, sb), overlayDimBrush_.Get());
        }
        // Right band
        if (sr < viewW) {
            target.FillRectangle(D2D1::RectF(sr, st, viewW, sb), overlayDimBrush_.Get());
        }

        // Selection rectangle border (cyan/accent blue, 2.0f stroke)
        target.DrawRectangle(D2D1::RectF(sl, st, sr, sb), overlayBorderBrush_.Get(), 2.0f);
    } else {
        // Dim the entire viewport while awaiting mouse drag
        target.FillRectangle(D2D1::RectF(0.0f, 0.0f, viewW, viewH), overlayDimBrush_.Get());
    }
}

void AppWindow::DrawNotificationToast(ID2D1HwndRenderTarget& target) noexcept {
    if (!toastVisible_ || statusTextFormat_ == nullptr) {
        return;
    }

    if (toastBgBrush_ == nullptr) {
        target.CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.95f), &toastBgBrush_);
    }
    if (toastTextBrush_ == nullptr) {
        target.CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f), &toastTextBrush_);
    }
    if (toastButtonBgBrush_ == nullptr) {
        target.CreateSolidColorBrush(D2D1::ColorF(0.28f, 0.28f, 0.32f, 1.0f), &toastButtonBgBrush_);
    }
    if (toastButtonBorderBrush_ == nullptr) {
        target.CreateSolidColorBrush(D2D1::ColorF(0.45f, 0.45f, 0.50f, 1.0f), &toastButtonBorderBrush_);
    }

    if (toastBgBrush_ == nullptr || toastTextBrush_ == nullptr) {
        return;
    }

    const float dpiScale = fastpdf::platform::win::DpiScaleFactor(dpi_);
    const float toastW = 320.0f * dpiScale;
    const float toastH = 48.0f * dpiScale;
    const float margin = 24.0f * dpiScale;

    const float viewW = static_cast<float>(ViewportWidth());
    const float viewH = static_cast<float>(ViewportHeight());

    const float left = viewW - toastW - margin;
    const float top = viewH - toastH - margin;
    const float right = left + toastW;
    const float bottom = top + toastH;

    const D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(
        D2D1::RectF(left, top, right, bottom), 6.0f * dpiScale, 6.0f * dpiScale);

    target.FillRoundedRectangle(roundedBox, toastBgBrush_.Get());
    if (toastButtonBorderBrush_ != nullptr) {
        target.DrawRoundedRectangle(roundedBox, toastButtonBorderBrush_.Get(), 1.0f);
    }

    // Text layout
    const float textMargin = 12.0f * dpiScale;
    const bool hasSaveBtn = !lastScreenshotBitmap_.empty();
    const float buttonW = hasSaveBtn ? (80.0f * dpiScale) : 0.0f;

    const D2D1_RECT_F textRect = D2D1::RectF(
        left + textMargin, top + 10.0f * dpiScale,
        right - textMargin - buttonW, bottom - 10.0f * dpiScale);

    target.DrawText(
        toastMessage_.c_str(), static_cast<UINT32>(toastMessage_.size()),
        statusTextFormat_.Get(), textRect, toastTextBrush_.Get());

    if (hasSaveBtn && toastButtonBgBrush_ != nullptr) {
        const float btnLeft = right - buttonW - textMargin;
        const float btnTop = top + 8.0f * dpiScale;
        const float btnRight = right - textMargin;
        const float btnBottom = bottom - 8.0f * dpiScale;

        toastSaveButtonRect_ = D2D1::RectF(btnLeft, btnTop, btnRight, btnBottom);
        const D2D1_ROUNDED_RECT btnBox = D2D1::RoundedRect(
            toastSaveButtonRect_, 4.0f * dpiScale, 4.0f * dpiScale);

        target.FillRoundedRectangle(btnBox, toastButtonBgBrush_.Get());
        if (toastButtonBorderBrush_ != nullptr) {
            target.DrawRoundedRectangle(btnBox, toastButtonBorderBrush_.Get(), 1.0f);
        }

        const std::wstring btnLabel = L"Save PNG";
        target.DrawText(
            btnLabel.c_str(), static_cast<UINT32>(btnLabel.size()),
            statusTextFormat_.Get(), toastSaveButtonRect_, toastTextBrush_.Get());
    } else {
        toastSaveButtonRect_ = D2D1::RectF(0, 0, 0, 0);
    }
}

void AppWindow::OnOpenFile() noexcept {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"PDF files (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Open PDF";

    if (GetOpenFileNameW(&ofn) == 0) {
        return;  // User cancelled.
    }
    OpenPath(file);
}

void AppWindow::OnDropFile(const std::wstring& path) noexcept {
    // Only a single .pdf file is opened through the drop path; anything else
    // (images, multi-file drops) is ignored here (image-to-PDF drag-drop
    // arrives with the conversion phase).
    if (!IsPdfPath(path)) {
        return;
    }
    OpenPath(path);
}

void AppWindow::OpenPath(const std::wstring& path) noexcept {
    if (presenting_) {
        ExitPresentation();  // Opening a new document leaves presentation.
    }
    ResetDocument();
    currentPath_ = path;
    state_ = ViewState::Loading;
    statusText_ = L"Opening " + currentPath_ + L"...";
    docOpenRequestTimeMs_ = GetTickCount64();
    firstVisiblePageMeasured_ = false;
    Log(L"Open requested: " + currentPath_);
    RequestDocumentInfo();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::ResetDocument() noexcept {
    CancelScreenshotMode();
    HideNotification();
    HideSearchUI();
    lastScreenshotBitmap_ = fastpdf::renderer::Bitmap{};
    documentInfo_.reset();
    layout_.reset();
    presentationLayout_.reset();
    presentationRenderScale_ = 0.0;
    cpuCache_.clear();
    d2dBitmaps_.clear();
    pendingKeys_.clear();
    renderScale_ = 0.0;
    scrollX_ = 0.0;
    scrollY_ = 0.0;
    fitMode_ = fastpdf::core::layout::FitMode::FitWidth;
    zoomPercent_ = 100.0;
    errorMessage_.clear();
    previewActive_ = false;
    currentMatchRects_.clear();
}

void AppWindow::RequestDocumentInfo() noexcept {
    ++docEpoch_;
    ++viewEpoch_;
    nextJobId_ = 1;
    worker_.OpenDocument(docEpoch_, currentPath_);
}

void AppWindow::OnWorkerDone(
    RenderWorker::WorkerCompletion* completion) noexcept {
    // The worker allocated |completion|; we own it here regardless of
    // staleness.
    if (completion->kind == RenderWorker::ResultKind::DocumentInfo) {
        OnDocumentInfoDone(completion);
    } else if (completion->kind == RenderWorker::ResultKind::PageRender) {
        OnPageRenderDone(completion);
    } else if (completion->kind == RenderWorker::ResultKind::SearchBatch ||
               completion->kind == RenderWorker::ResultKind::SearchComplete) {
        if (completion->searchEpoch == searchEpoch_ && completion->docEpoch == docEpoch_) {
            if (!completion->searchMatches.empty()) {
                const bool hadNone = searchMatches_.empty();
                for (auto& m : completion->searchMatches) {
                    if (searchMatches_.size() < fastpdf::core::search::kMaxSearchResults) {
                        searchMatches_.push_back(m);
                    }
                }
                if (hadNone && !searchMatches_.empty()) {
                    searchCurrentMatchIndex_ = 0;
                    GoToMatch(0);
                }
            }
            if (completion->kind == RenderWorker::ResultKind::SearchComplete) {
                searchInProgress_ = false;
            }
            UpdateSearchStatus();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    } else if (completion->kind == RenderWorker::ResultKind::HighlightRects) {
        if (completion->searchEpoch == searchEpoch_ && completion->docEpoch == docEpoch_) {
            HighlightRectCache cache;
            cache.pageIndex = completion->highlightPageIndex;
            cache.charIndex = completion->highlightCharIndex;
            cache.charCount = completion->highlightCharCount;
            cache.rects = std::move(completion->highlightRects);
            currentMatchRects_.push_back(std::move(cache));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    delete completion;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OnDocumentInfoDone(
    RenderWorker::WorkerCompletion* completion) noexcept {
    if (completion->docEpoch != docEpoch_) {
        return;  // Stale: a newer document open superseded this one.
    }
    if (!completion->documentInfo.ok) {
        state_ = ViewState::Error;
        errorMessage_ = UserMessageForError(completion->documentInfo.error);
        Log(L"Open failed: " + currentPath_ + L" (error=" +
            std::to_wstring(static_cast<int>(completion->documentInfo.error)) +
            L")");
        return;
    }

    documentInfo_ = std::move(completion->documentInfo);
    state_ = ViewState::Ready;
    Log(L"Opened: " + currentPath_ + L" (pages=" +
        std::to_wstring(documentInfo_->pageCount) + L", open=" +
        std::to_wstring(documentInfo_->openDurationMs) + L" ms)");

    // A freshly opened document is idle: render final quality immediately.
    previewActive_ = false;
    idleDebounce_.NoteActivity(GetTickCount64());

    RebuildLayout();
    RequestRenders();
    UpdateStatusText();

    // Add to recent files
    SaveCurrentToRecentFiles();
}

void AppWindow::OnPageRenderDone(
    RenderWorker::WorkerCompletion* completion) noexcept {
    // Accept only completions from the current view and document; everything
    // else is stale (an open/zoom/resize/scroll race).
    if (completion->viewEpoch != viewEpoch_ ||
        completion->key.docEpoch != docEpoch_) {
        return;
    }

    pendingKeys_.erase(completion->key);

    if (completion->pageRender.ok &&
        !completion->pageRender.bitmap.data.empty()) {
        cpuCache_.put(completion->key,
                      std::move(completion->pageRender.bitmap));

        // A final render supersedes the preview for the same page: drop the
        // preview so the page shows at full quality and the cache stays tight.
        if (completion->key.quality == fastpdf::renderer::Quality::Final) {
            const fastpdf::renderer::PixelSize previewSize =
                fastpdf::renderer::PreviewSizeFor(completion->key.width,
                                                  completion->key.height);
            const fastpdf::renderer::RenderKey previewKey{
                completion->key.docEpoch, completion->key.pageIndex,
                previewSize.width, previewSize.height,
                completion->key.rotation,
                fastpdf::renderer::Quality::Preview};
            cpuCache_.erase(previewKey);
            d2dBitmaps_.erase(previewKey);
        }

        const wchar_t* tier =
            completion->key.quality == fastpdf::renderer::Quality::Preview
                ? L"preview"
                : L"final";
        const std::size_t queueDepth = worker_.pendingQueueDepth();
        const std::size_t cacheBytes = cpuCache_.currentBytes();
        const double hitRatePct = cpuCache_.hitRate() * 100.0;
        Log(L"Rendered page " + std::to_wstring(completion->key.pageIndex) +
            L" " + tier + L" (" +
            std::to_wstring(completion->pageRender.bitmap.width) + L"x" +
            std::to_wstring(completion->pageRender.bitmap.height) + L", " +
            std::to_wstring(completion->pageRender.renderDurationMs) + L" ms) " +
            L"[queueDepth=" + std::to_wstring(queueDepth) +
            L", cache=" + std::to_wstring(cacheBytes / 1024) + L" KB" +
            L", hitRate=" + std::to_wstring(static_cast<int>(hitRatePct)) + L"%]");

        // First visible page latency measurement
        if (!firstVisiblePageMeasured_ && docOpenRequestTimeMs_ != 0) {
            const int curPage = CurrentPageIndex();
            if (completion->key.pageIndex == curPage) {
                firstVisiblePageMeasured_ = true;
                const std::uint64_t latency = GetTickCount64() - docOpenRequestTimeMs_;
                Log(L"First visible page latency: " + std::to_wstring(latency) +
                    L" ms (page " + std::to_wstring(curPage) + L")");
            }
        }
    }
    if (!presenting_) {
        UpdateStatusText();
    }
}

void AppWindow::RebuildLayout() noexcept {
    if (!documentInfo_.has_value() || documentInfo_->pages.empty()) {
        layout_.reset();
        return;
    }

    std::vector<fastpdf::core::layout::PageSize> pages;
    pages.reserve(documentInfo_->pages.size());
    for (const auto& info : documentInfo_->pages) {
        pages.push_back({info.widthPoints, info.heightPoints});
    }

    const double dpi = static_cast<double>(dpi_);
    double pixelsPerPoint = 0.0;
    if (fitMode_ == fastpdf::core::layout::FitMode::FitWidth) {
        const double pageWidth = pages[0].widthPoints;
        zoomPercent_ = fastpdf::core::layout::FitWidthZoomPercent(
            ViewportWidth(), pageWidth, dpi);
        pixelsPerPoint = fastpdf::core::layout::PixelsPerPoint(zoomPercent_, dpi);
    } else if (fitMode_ == fastpdf::core::layout::FitMode::FitPage) {
        const double pageWidth = pages[0].widthPoints;
        const double pageHeight = pages[0].heightPoints;
        zoomPercent_ = fastpdf::core::layout::FitPageZoomPercent(
            ViewportWidth(), ViewportHeight(), pageWidth, pageHeight, dpi);
        pixelsPerPoint = fastpdf::core::layout::PixelsPerPoint(zoomPercent_, dpi);
    } else {
        pixelsPerPoint = fastpdf::core::layout::PixelsPerPoint(zoomPercent_, dpi);
    }

    const double gap = fastpdf::platform::win::DpiScaleFactor(dpi_) * 8.0;
    layout_ = fastpdf::core::layout::ContinuousLayout::Create(
        pages, pixelsPerPoint, gap);
    if (layout_.has_value()) {
        scrollX_ = layout_->clampScrollX(scrollX_, ViewportWidth());
        scrollY_ = layout_->clampScrollY(scrollY_, ViewportHeight());
    }
}

void AppWindow::MarkInputActivity() noexcept {
    idleDebounce_.NoteActivity(GetTickCount64());
    previewActive_ = true;
    SetTimer(hwnd_, kPromotionTimerId, static_cast<UINT>(kPromotionDelayMs),
             nullptr);
}

void AppWindow::OnPromotionTimer() noexcept {
    if (presenting_) {
        return;  // Presentation renders at final quality; no promotion needed.
    }
    if (!idleDebounce_.ShouldPromote(GetTickCount64())) {
        return;  // Still within the idle window; the timer fires again.
    }
    KillTimer(hwnd_, kPromotionTimerId);
    if (previewActive_) {
        previewActive_ = false;
        RequestRenders();
        UpdateStatusText();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

fastpdf::renderer::RenderKey AppWindow::MakeKey(
    int pageIndex, fastpdf::renderer::PixelSize size,
    fastpdf::renderer::Quality quality) const noexcept {
    return fastpdf::renderer::RenderKey{docEpoch_, pageIndex, size.width,
                                        size.height, 0, quality};
}

void AppWindow::RequestRenders() noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }

    // When the zoom scale changed, every cached bitmap and in-flight render is
    // at the old scale. Bump the view epoch so stale completions are dropped,
    // clear the caches, and tell the worker to drop stale pending jobs.
    const double scale = layout_->pixelsPerPoint();
    if (std::fabs(scale - renderScale_) > 1e-9) {
        ++viewEpoch_;
        renderScale_ = scale;
        cpuCache_.clear();
        d2dBitmaps_.clear();
        pendingKeys_.clear();
        worker_.SetViewEpoch(viewEpoch_);
    }

    const auto [first, last] =
        layout_->visiblePageRange(scrollY_, ViewportHeight());
    if (first < 0) {
        return;
    }

    // Build the wanted page set: visible pages (highest priority), capped, plus
    // adjacent pre-render pages (lower priority), clamped to the document.
    struct Want {
        int page;
        fastpdf::renderer::RenderPriority priority;
    };
    std::vector<Want> wanted;

    const int pageCount = layout_->pageCount();
    const int lastVisible = std::min(last, first + kMaxVisiblePages - 1);
    for (int i = first; i <= lastVisible; ++i) {
        wanted.push_back({i, fastpdf::renderer::RenderPriority::Visible});
    }
    for (int i = first - kAdjacentPrev; i < first; ++i) {
        if (i >= 0) {
            wanted.push_back({i, fastpdf::renderer::RenderPriority::Adjacent});
        }
    }
    for (int i = lastVisible + 1;
         i <= lastVisible + kAdjacentNext && i < pageCount; ++i) {
        wanted.push_back({i, fastpdf::renderer::RenderPriority::Adjacent});
    }

    std::vector<fastpdf::renderer::RenderKey> wantedKeys;
    wantedKeys.reserve(wanted.size() * 2);

    for (const Want& want : wanted) {
        const fastpdf::core::layout::PageRect rect =
            layout_->pageRect(want.page);
        const fastpdf::renderer::PixelSize finalSize =
            ComputeRenderSize(rect.width, rect.height);
        const fastpdf::renderer::PixelSize previewSize =
            fastpdf::renderer::PreviewSizeFor(finalSize.width, finalSize.height);

        const fastpdf::renderer::RenderKey finalKey =
            MakeKey(want.page, finalSize, fastpdf::renderer::Quality::Final);
        const fastpdf::renderer::RenderKey previewKey =
            MakeKey(want.page, previewSize, fastpdf::renderer::Quality::Preview);

        if (previewActive_) {
            // Actively moving: request a fast preview (half dimensions) so
            // pixels appear quickly; promotion to final happens on idle.
            wantedKeys.push_back(previewKey);
            SubmitRender(previewKey, want.priority);
        } else {
            // Idle: request final quality. The preview key is kept as the
            // drawable fallback while the final render is still in flight.
            wantedKeys.push_back(finalKey);
            wantedKeys.push_back(previewKey);
            SubmitRender(finalKey, want.priority);
        }
    }

    PruneD2dCache(wantedKeys);
}

void AppWindow::SubmitRender(fastpdf::renderer::RenderKey key,
                             fastpdf::renderer::RenderPriority priority) noexcept {
    if (cpuCache_.contains(key) || pendingKeys_.count(key) != 0) {
        return;  // Already cached or already requested (no duplicate work).
    }
    pendingKeys_.insert(key);
    RenderWorker::RenderJob job;
    job.key = key;
    job.priority = priority;
    job.viewEpoch = viewEpoch_;
    job.jobId = nextJobId_++;
    worker_.SubmitRender(job);
}

void AppWindow::PruneD2dCache(
    const std::vector<fastpdf::renderer::RenderKey>& wantedKeys) noexcept {
    for (auto it = d2dBitmaps_.begin(); it != d2dBitmaps_.end();) {
        if (std::find(wantedKeys.begin(), wantedKeys.end(), it->first) ==
            wantedKeys.end()) {
            it = d2dBitmaps_.erase(it);
        } else {
            ++it;
        }
    }
}

void AppWindow::SetFitMode(fastpdf::core::layout::FitMode mode) noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    fitMode_ = mode;
    const fastpdf::core::layout::ViewAnchor anchor =
        fastpdf::core::layout::CaptureAnchor(
            *layout_, scrollX_, scrollY_, ViewportWidth(), ViewportHeight(),
            ViewportWidth() * 0.5, ViewportHeight() * 0.5);
    RebuildLayout();
    fastpdf::core::layout::RestoreAnchor(
        *layout_, anchor, ViewportWidth(), ViewportHeight(), scrollX_, scrollY_);
    MarkInputActivity();
    RequestRenders();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::SetZoomPercent(double percent) noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    fitMode_ = fastpdf::core::layout::FitMode::Custom;
    ZoomAt(percent, ViewportWidth() * 0.5, ViewportHeight() * 0.5);
}

void AppWindow::ZoomAt(double newPercent, double cursorX, double cursorY) noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    const double clamped = fastpdf::core::layout::ClampZoomPercent(newPercent);
    if (std::fabs(clamped - zoomPercent_) < 1e-6) {
        return;
    }
    // Capture the anchor at the cursor so the content under the cursor stays
    // put while zooming.
    const fastpdf::core::layout::ViewAnchor anchor =
        fastpdf::core::layout::CaptureAnchor(
            *layout_, scrollX_, scrollY_, ViewportWidth(), ViewportHeight(),
            cursorX, cursorY);
    zoomPercent_ = clamped;
    fitMode_ = fastpdf::core::layout::FitMode::Custom;
    RebuildLayout();
    fastpdf::core::layout::RestoreAnchor(
        *layout_, anchor, ViewportWidth(), ViewportHeight(), scrollX_, scrollY_);
    MarkInputActivity();
    RequestRenders();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::ScrollBy(double dx, double dy) noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    scrollX_ = layout_->clampScrollX(scrollX_ + dx, ViewportWidth());
    scrollY_ = layout_->clampScrollY(scrollY_ + dy, ViewportHeight());
    MarkInputActivity();
    RequestRenders();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::GoToPage(int pageIndex) noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    const int clamped =
        std::clamp(pageIndex, 0, layout_->pageCount() - 1);
    scrollY_ = fastpdf::core::layout::ScrollTopForPageTop(
        *layout_, clamped, ViewportHeight());
    scrollX_ = layout_->clampScrollX(scrollX_, ViewportWidth());
    MarkInputActivity();
    RequestRenders();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int AppWindow::CurrentPageIndex() const noexcept {
    if (!layout_.has_value()) {
        return 0;
    }
    return layout_->pageAtContentY(scrollY_ + ViewportHeight() * 0.5);
}

double AppWindow::ViewportWidth() const noexcept {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return static_cast<double>(std::max(1L, client.right - client.left));
}

double AppWindow::ViewportHeight() const noexcept {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return static_cast<double>(std::max(1L, client.bottom - client.top));
}

void AppWindow::UpdateStatusText() noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    const int current = CurrentPageIndex() + 1;
    const int total = layout_->pageCount();
    const int zoom = static_cast<int>(std::lround(zoomPercent_));
    statusText_ = currentPath_ + L"   " + std::to_wstring(current) + L" / " +
                  std::to_wstring(total) + L"   " + std::to_wstring(zoom) +
                  L"%";

    if (diagnosticsEnabled_) {
        const std::size_t cacheKB = cpuCache_.currentBytes() / 1024;
        const double hitRate = cpuCache_.hitRate() * 100.0;
        const std::size_t qDepth = worker_.pendingQueueDepth();
        statusText_ += L"   [DIAG: Q=" + std::to_wstring(qDepth) +
                       L" Cache=" + std::to_wstring(cacheKB) + L"KB (" +
                       std::to_wstring(static_cast<int>(hitRate)) + L"%) " +
                       L"Frame=" + std::to_wstring(static_cast<int>(lastFrameTimeMs_)) + L"ms]";
    }
}

bool AppWindow::EnsureDeviceResources() noexcept {
    if (d2dFactory_ == nullptr) {
        if (FAILED(fastpdf::platform::win::d2d::CreateFactory(d2dFactory_))) {
            return false;
        }
    }
    if (dwriteFactory_ == nullptr) {
        if (FAILED(fastpdf::platform::win::d2d::CreateDWriteFactory(dwriteFactory_))) {
            return false;
        }
    }
    if (statusTextFormat_ == nullptr) {
        const float fontSize = fastpdf::platform::win::DpiScaleFactor(dpi_) * 16.0f;
        if (FAILED(dwriteFactory_->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                fontSize, L"en-US", &statusTextFormat_))) {
            return false;
        }
    }
    if (renderTarget_ == nullptr) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT32>(client.right - client.left),
            static_cast<UINT32>(client.bottom - client.top));
        if (FAILED(fastpdf::platform::win::d2d::CreateHwndRenderTarget(
                *d2dFactory_.Get(), hwnd_, size, renderTarget_))) {
            return false;
        }
    }
    if (statusBrush_ == nullptr) {
        if (FAILED(renderTarget_->CreateSolidColorBrush(kStatusColor, &statusBrush_))) {
            return false;
        }
    }
    if (errorBrush_ == nullptr) {
        if (FAILED(renderTarget_->CreateSolidColorBrush(kErrorColor, &errorBrush_))) {
            return false;
        }
    }
    if (searchHighlightBrush_ == nullptr) {
        if (FAILED(renderTarget_->CreateSolidColorBrush(kSearchHighlightColor, &searchHighlightBrush_))) {
            return false;
        }
    }
    if (searchActiveHighlightBrush_ == nullptr) {
        if (FAILED(renderTarget_->CreateSolidColorBrush(kSearchActiveHighlightColor, &searchActiveHighlightBrush_))) {
            return false;
        }
    }
    return true;
}

void AppWindow::DiscardDeviceResources() noexcept {
    d2dBitmaps_.clear();  // Device-dependent; re-uploaded from CPU bitmaps.
    errorBrush_.Reset();
    statusBrush_.Reset();
    searchHighlightBrush_.Reset();
    searchActiveHighlightBrush_.Reset();
    statusTextFormat_.Reset();
    renderTarget_.Reset();
    toastBgBrush_.Reset();
    toastTextBrush_.Reset();
    toastButtonBgBrush_.Reset();
    toastButtonBorderBrush_.Reset();
    overlayDimBrush_.Reset();
    overlayBorderBrush_.Reset();
    // The D2D and DWrite factories are DPI-independent and survive.
}

void AppWindow::DrawPages(ID2D1HwndRenderTarget& target) noexcept {
    if (state_ != ViewState::Ready || !layout_.has_value()) {
        return;
    }
    const auto [first, last] =
        layout_->visiblePageRange(scrollY_, ViewportHeight());
    if (first < 0) {
        return;
    }
    for (int page = first; page <= last; ++page) {
        DrawPageBitmap(target, page);
    }
}

void AppWindow::DrawPageBitmap(ID2D1HwndRenderTarget& target,
                               int pageIndex) noexcept {
    if (!layout_.has_value()) {
        return;
    }
    const fastpdf::core::layout::PageRect rect = layout_->pageRect(pageIndex);
    const fastpdf::renderer::PixelSize finalSize =
        ComputeRenderSize(rect.width, rect.height);

    // Prefer the final-quality bitmap; fall back to the preview (drawn into
    // the same page rect so page geometry never changes).
    const fastpdf::renderer::RenderKey finalKey =
        MakeKey(pageIndex, finalSize, fastpdf::renderer::Quality::Final);
    const fastpdf::renderer::Bitmap* bitmap = cpuCache_.get(finalKey);
    if (bitmap != nullptr && !bitmap->data.empty()) {
        DrawBitmap(target, finalKey, *bitmap, rect);
        return;
    }

    const fastpdf::renderer::PixelSize previewSize =
        fastpdf::renderer::PreviewSizeFor(finalSize.width, finalSize.height);
    const fastpdf::renderer::RenderKey previewKey =
        MakeKey(pageIndex, previewSize, fastpdf::renderer::Quality::Preview);
    bitmap = cpuCache_.get(previewKey);
    if (bitmap != nullptr && !bitmap->data.empty()) {
        DrawBitmap(target, previewKey, *bitmap, rect);
    }
    // Otherwise the page is still a placeholder (nothing rendered yet).
}

void AppWindow::DrawBitmap(ID2D1HwndRenderTarget& target,
                           const fastpdf::renderer::RenderKey& key,
                           const fastpdf::renderer::Bitmap& bmp,
                           const fastpdf::core::layout::PageRect& rect) noexcept {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBitmap;
    if (!GetOrCreateD2dBitmap(target, key, bmp, d2dBitmap)) {
        return;
    }

    // Draw the page at its laid-out content-space rect, offset by the scroll.
    // A preview bitmap (half dimensions) is drawn into the same rect as a
    // final bitmap, so quality changes never move the page.
    const float left = static_cast<float>(rect.left - scrollX_);
    const float top = static_cast<float>(rect.top - scrollY_);
    const float right = static_cast<float>(rect.left + rect.width - scrollX_);
    const float bottom = static_cast<float>(rect.top + rect.height - scrollY_);
    const D2D1_RECT_F dest = D2D1::RectF(left, top, right, bottom);
    target.DrawBitmap(d2dBitmap.Get(), dest, 1.0f,
                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

bool AppWindow::GetOrCreateD2dBitmap(
    ID2D1HwndRenderTarget& target, const fastpdf::renderer::RenderKey& key,
    const fastpdf::renderer::Bitmap& bmp,
    Microsoft::WRL::ComPtr<ID2D1Bitmap>& out) noexcept {
    // Reuse the device-dependent D2D bitmap when already uploaded; create it
    // from the immutable CPU BGRA value on the UI thread otherwise. The D2D
    // cache is bounded (pruned to wanted keys) and cleared on device loss.
    const auto cached = d2dBitmaps_.find(key);
    if (cached != d2dBitmaps_.end() && cached->second != nullptr) {
        out = cached->second;
        return true;
    }
    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(bmp.width), static_cast<UINT32>(bmp.height));
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    if (FAILED(target.CreateBitmap(size, bmp.data.data(),
                                   static_cast<UINT32>(bmp.stride), props,
                                   &out))) {
        return false;
    }
    d2dBitmaps_[key] = out;
    return true;
}

void AppWindow::DrawStatusText(ID2D1HwndRenderTarget& target) noexcept {
    if (statusTextFormat_ == nullptr || statusBrush_ == nullptr) {
        return;
    }

    std::wstring text;
    ID2D1SolidColorBrush* brush = statusBrush_.Get();
    switch (state_) {
        case ViewState::Empty:
        case ViewState::Loading:
            text = statusText_;
            break;
        case ViewState::Error:
            text = errorMessage_;
            brush = errorBrush_.Get();
            break;
        case ViewState::Ready:
            text = statusText_;
            break;
    }

    if (text.empty() || brush == nullptr) {
        return;
    }

    const D2D1_SIZE_F size = target.GetSize();
    const float margin = fastpdf::platform::win::DpiScaleFactor(dpi_) * 16.0f;
    const D2D1_RECT_F layout =
        D2D1::RectF(margin, margin, size.width - margin, size.height - margin);
    target.DrawText(text.c_str(), static_cast<UINT32>(text.size()),
                    statusTextFormat_.Get(), layout, brush);
}

std::wstring AppWindow::UserMessageForError(fastpdf::pdfium::OpenError error) const noexcept {
    switch (error) {
        case fastpdf::pdfium::OpenError::MissingFile:
            return L"The file could not be found.";
        case fastpdf::pdfium::OpenError::Unreadable:
            return L"The file could not be read.";
        case fastpdf::pdfium::OpenError::Corrupted:
            return L"The PDF could not be opened. It may be damaged or not a PDF.";
        case fastpdf::pdfium::OpenError::PasswordRequired:
            return L"This PDF requires a password.";
        case fastpdf::pdfium::OpenError::UnsupportedSecurity:
            return L"This PDF uses an unsupported security scheme.";
        case fastpdf::pdfium::OpenError::PdfiumUnavailable:
            return L"PDF support is not available in this build.";
        default:
            return L"The PDF could not be opened.";
    }
}

void AppWindow::OnConvertPdfToPng() noexcept {
    if (state_ != ViewState::Ready || currentPath_.empty() || !documentInfo_.has_value()) {
        return;
    }
    const int pageCount = documentInfo_->pageCount;
    if (pageCount <= 0) {
        return;
    }

    fastpdf::app::convert::PdfToPngDialog dialog;
    dialog.Show(hwnd_, currentPath_, CurrentPageIndex(), pageCount);
}

void AppWindow::OnConvertImageToPdf(const std::vector<std::wstring>& initialImages) noexcept {
    fastpdf::app::convert::ImageToPdfDialog dialog;
    dialog.Show(hwnd_, initialImages);
}

void AppWindow::OnPrint() noexcept {
    if (state_ != ViewState::Ready || currentPath_.empty() || !documentInfo_.has_value()) {
        return;
    }
    const int pageCount = documentInfo_->pageCount;
    if (pageCount <= 0) {
        return;
    }

    fastpdf::pdfium::OpenError err = fastpdf::pdfium::OpenError::None;
    fastpdf::pdfium::PdfSource src = fastpdf::pdfium::PdfSource::Load(currentPath_, err);
    if (!src.isValid()) {
        MessageBoxW(hwnd_, L"Failed to load PDF source for printing.", L"Print Error", MB_OK | MB_ICONERROR);
        return;
    }

    fastpdf::app::print::PrintDialog dialog;
    dialog.Show(hwnd_, currentPath_, src, CurrentPageIndex(), pageCount);
}

void AppWindow::Log(const std::wstring& message) const noexcept {
    // Diagnostic logging without any sensitive document contents
    fastpdf::platform::win::AppendDiagnosticLog(message);
}

// ---------------------------------------------------------------------------
// Search Implementation (Phase 8)
// ---------------------------------------------------------------------------

namespace {

WNDPROC g_originalEditProc = nullptr;

LRESULT CALLBACK SearchEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            HWND parent = GetParent(hwnd);
            HWND mainWin = GetParent(parent);
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            SendMessageW(mainWin, WM_COMMAND, MAKEWPARAM(shift ? kIdSearchPrev : kIdSearchNext, 0), 0);
            return 0;
        } else if (wParam == VK_ESCAPE) {
            HWND parent = GetParent(hwnd);
            HWND mainWin = GetParent(parent);
            SendMessageW(mainWin, WM_COMMAND, MAKEWPARAM(kIdSearchClose, 0), 0);
            return 0;
        }
    }
    return CallWindowProcW(g_originalEditProc, hwnd, msg, wParam, lParam);
}

} // namespace

void AppWindow::ShowSearchUI() noexcept {
    if (presenting_ || state_ != ViewState::Ready) {
        return;
    }

    if (hwndSearchPanel_ == nullptr) {
        // Create search panel window
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        const int panelW = fastpdf::platform::win::ScaleForDpi(360, dpi_);
        const int panelH = fastpdf::platform::win::ScaleForDpi(36, dpi_);
        const int vpW = static_cast<int>(ViewportWidth());
        const int panelX = std::max(10, vpW - panelW - 30);
        const int panelY = 10;

        hwndSearchPanel_ = CreateWindowExW(
            WS_EX_TOPMOST, L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | SS_NOTIFY,
            panelX, panelY, panelW, panelH,
            hwnd_, reinterpret_cast<HMENU>(10100), hInst, nullptr);

        const int pad = fastpdf::platform::win::ScaleForDpi(4, dpi_);
        const int btnW = fastpdf::platform::win::ScaleForDpi(26, dpi_);
        const int editW = fastpdf::platform::win::ScaleForDpi(160, dpi_);
        const int countW = fastpdf::platform::win::ScaleForDpi(70, dpi_);
        const int ctrlH = panelH - pad * 2 - 2;

        int curX = pad;
        hwndSearchEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            curX, pad, editW, ctrlH,
            hwnd_, reinterpret_cast<HMENU>(10101), hInst, nullptr);
        SetParent(hwndSearchEdit_, hwndSearchPanel_);
        curX += editW + pad;

        hwndSearchCount_ = CreateWindowExW(
            0, L"STATIC", L"0 / 0",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            curX, pad, countW, ctrlH,
            hwnd_, reinterpret_cast<HMENU>(10102), hInst, nullptr);
        SetParent(hwndSearchCount_, hwndSearchPanel_);
        curX += countW + pad;

        hwndSearchPrev_ = CreateWindowExW(
            0, L"BUTTON", L"<",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            curX, pad, btnW, ctrlH,
            hwnd_, reinterpret_cast<HMENU>(kIdSearchPrev), hInst, nullptr);
        SetParent(hwndSearchPrev_, hwndSearchPanel_);
        curX += btnW + pad;

        hwndSearchNext_ = CreateWindowExW(
            0, L"BUTTON", L">",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            curX, pad, btnW, ctrlH,
            hwnd_, reinterpret_cast<HMENU>(kIdSearchNext), hInst, nullptr);
        SetParent(hwndSearchNext_, hwndSearchPanel_);
        curX += btnW + pad;

        hwndSearchClose_ = CreateWindowExW(
            0, L"BUTTON", L"X",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            curX, pad, btnW, ctrlH,
            hwnd_, reinterpret_cast<HMENU>(kIdSearchClose), hInst, nullptr);
        SetParent(hwndSearchClose_, hwndSearchPanel_);

        // Subclass Edit control to handle Enter and Esc
        g_originalEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwndSearchEdit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SearchEditSubclassProc)));

        // Set standard GUI font
        if (searchFont_ == nullptr) {
            searchFont_ = CreateFontW(-fastpdf::platform::win::ScaleForDpi(13, dpi_), 0, 0, 0,
                                      FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
        SendMessageW(hwndSearchEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(searchFont_), TRUE);
        SendMessageW(hwndSearchCount_, WM_SETFONT, reinterpret_cast<WPARAM>(searchFont_), TRUE);
        SendMessageW(hwndSearchPrev_, WM_SETFONT, reinterpret_cast<WPARAM>(searchFont_), TRUE);
        SendMessageW(hwndSearchNext_, WM_SETFONT, reinterpret_cast<WPARAM>(searchFont_), TRUE);
        SendMessageW(hwndSearchClose_, WM_SETFONT, reinterpret_cast<WPARAM>(searchFont_), TRUE);
    } else {
        ShowWindow(hwndSearchPanel_, SW_SHOW);
    }

    searchVisible_ = true;
    SetFocus(hwndSearchEdit_);
    SendMessageW(hwndSearchEdit_, EM_SETSEL, 0, -1);
}

void AppWindow::HideSearchUI() noexcept {
    if (!searchVisible_) {
        return;
    }
    searchVisible_ = false;
    if (hwndSearchPanel_ != nullptr) {
        ShowWindow(hwndSearchPanel_, SW_HIDE);
    }
    worker_.CancelSearch();
    searchMatches_.clear();
    searchCurrentMatchIndex_ = -1;
    currentMatchRects_.clear();
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OnSearchTextChanged() noexcept {
    if (hwndSearchEdit_ == nullptr) {
        return;
    }
    wchar_t buf[256]{};
    GetWindowTextW(hwndSearchEdit_, buf, 256);
    std::wstring query = fastpdf::core::search::SanitizeSearchQuery(buf);

    if (query == searchCurrentQuery_) {
        return;
    }

    searchCurrentQuery_ = query;
    ++searchEpoch_;
    searchMatches_.clear();
    searchCurrentMatchIndex_ = -1;
    currentMatchRects_.clear();

    if (searchCurrentQuery_.empty()) {
        worker_.CancelSearch();
        searchInProgress_ = false;
        UpdateSearchStatus();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    searchInProgress_ = true;
    UpdateSearchStatus();
    worker_.StartSearch(searchEpoch_, searchCurrentQuery_, /*matchCase=*/false);
}

void AppWindow::SearchNext() noexcept {
    if (searchMatches_.empty()) {
        return;
    }
    int next = fastpdf::core::search::NextMatchIndex(searchCurrentMatchIndex_, static_cast<int>(searchMatches_.size()), true);
    if (next >= 0) {
        GoToMatch(static_cast<size_t>(next));
    }
}

void AppWindow::SearchPrev() noexcept {
    if (searchMatches_.empty()) {
        return;
    }
    int prev = fastpdf::core::search::NextMatchIndex(searchCurrentMatchIndex_, static_cast<int>(searchMatches_.size()), false);
    if (prev >= 0) {
        GoToMatch(static_cast<size_t>(prev));
    }
}

void AppWindow::GoToMatch(size_t matchIndex) noexcept {
    if (matchIndex >= searchMatches_.size()) {
        return;
    }
    searchCurrentMatchIndex_ = static_cast<int>(matchIndex);
    const auto& match = searchMatches_[matchIndex];

    // Scroll to page if not visible
    if (layout_.has_value()) {
        const int targetPage = match.pageIndex;
        auto [first, last] = layout_->visiblePageRange(scrollY_, ViewportHeight());
        if (targetPage < first || targetPage > last) {
            GoToPage(targetPage);
        }
    }

    // Request highlight rects for this match if not yet cached
    bool alreadyCached = false;
    for (const auto& c : currentMatchRects_) {
        if (c.pageIndex == match.pageIndex && c.charIndex == match.charIndex && c.charCount == match.charCount) {
            alreadyCached = true;
            break;
        }
    }
    if (!alreadyCached) {
        worker_.RequestHighlightRects(searchEpoch_, match.pageIndex, match.charIndex, match.charCount);
    }

    UpdateSearchStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::UpdateSearchStatus() noexcept {
    if (hwndSearchCount_ == nullptr) {
        return;
    }
    size_t cur = (searchCurrentMatchIndex_ >= 0) ? static_cast<size_t>(searchCurrentMatchIndex_) : 0;
    size_t total = searchMatches_.size();
    std::wstring countStr = fastpdf::core::search::FormatSearchCount(
        (searchCurrentMatchIndex_ >= 0) ? cur : 0,
        total);
    if (searchCurrentMatchIndex_ < 0 && total > 0) {
        countStr = L"- / " + std::to_wstring(total);
    }
    SetWindowTextW(hwndSearchCount_, countStr.c_str());
}

void AppWindow::DrawSearchHighlights(ID2D1HwndRenderTarget& target) noexcept {
    if (!searchVisible_ || !layout_.has_value() || !documentInfo_.has_value()) {
        return;
    }
    if (searchHighlightBrush_ == nullptr || searchActiveHighlightBrush_ == nullptr) {
        return;
    }

    const auto [first, last] = layout_->visiblePageRange(scrollY_, ViewportHeight());
    if (first < 0) {
        return;
    }

    // Identify visible matches and request rects if missing
    for (size_t i = 0; i < searchMatches_.size(); ++i) {
        const auto& match = searchMatches_[i];
        if (match.pageIndex < first || match.pageIndex > last) {
            continue;
        }

        // Check if cached
        const HighlightRectCache* cached = nullptr;
        for (const auto& c : currentMatchRects_) {
            if (c.pageIndex == match.pageIndex && c.charIndex == match.charIndex && c.charCount == match.charCount) {
                cached = &c;
                break;
            }
        }

        if (cached == nullptr) {
            // Request rects for visible match
            worker_.RequestHighlightRects(searchEpoch_, match.pageIndex, match.charIndex, match.charCount);
            continue;
        }

        // Draw rects
        const auto& pageInfo = documentInfo_->pages[static_cast<size_t>(match.pageIndex)];
        const fastpdf::core::layout::PageRect pageRect = layout_->pageRect(match.pageIndex);
        const double pagePtsW = pageInfo.widthPoints;
        const double pagePtsH = pageInfo.heightPoints;
        if (pagePtsW <= 0.0 || pagePtsH <= 0.0) {
            continue;
        }

        const double scaleX = pageRect.width / pagePtsW;
        const double scaleY = pageRect.height / pagePtsH;
        const bool isActive = (static_cast<int>(i) == searchCurrentMatchIndex_);
        ID2D1SolidColorBrush* brush = isActive ? searchActiveHighlightBrush_.Get() : searchHighlightBrush_.Get();

        for (const auto& r : cached->rects) {
            // PDF points to layout space (PDF coordinate origin is bottom-left)
            const double rectLeft = pageRect.left + (r.left * scaleX);
            const double rectRight = pageRect.left + (r.right * scaleX);
            const double rectTop = pageRect.top + ((pagePtsH - r.top) * scaleY);
            const double rectBottom = pageRect.top + ((pagePtsH - r.bottom) * scaleY);

            const float vx1 = static_cast<float>(rectLeft - scrollX_);
            const float vy1 = static_cast<float>(std::min(rectTop, rectBottom) - scrollY_);
            const float vx2 = static_cast<float>(rectRight - scrollX_);
            const float vy2 = static_cast<float>(std::max(rectTop, rectBottom) - scrollY_);

            target.FillRectangle(D2D1::RectF(vx1, vy1, vx2, vy2), brush);
        }
    }
}

// ---------------------------------------------------------------------------
// Recent Files Implementation (Phase 8)
// ---------------------------------------------------------------------------

void AppWindow::LoadRecentFilesState() noexcept {
    fastpdf::platform::win::LoadRecentFiles(recentState_);
}

void AppWindow::SaveCurrentToRecentFiles() noexcept {
    if (currentPath_.empty() || !layout_.has_value()) {
        return;
    }

    fastpdf::core::recent::RecentEntry entry;
    entry.path = currentPath_;
    entry.lastOpenedEpochSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    entry.lastPage = CurrentPageIndex();
    entry.zoomMode = fitMode_;
    entry.zoomPercent = zoomPercent_;
    entry.anchor = fastpdf::core::layout::CaptureAnchor(
        *layout_, scrollX_, scrollY_, ViewportWidth(), ViewportHeight(),
        ViewportWidth() * 0.5, ViewportHeight() * 0.5);

    fastpdf::core::recent::AddRecentEntry(recentState_, std::move(entry));
    fastpdf::platform::win::SaveRecentFiles(recentState_);
    UpdateRecentMenu();
}

void AppWindow::UpdateRecentMenu() noexcept {
    if (recentMenu_ == nullptr) {
        return;
    }

    // Clear existing items in submenu
    while (GetMenuItemCount(recentMenu_) > 0) {
        DeleteMenu(recentMenu_, 0, MF_BYPOSITION);
    }

    if (recentState_.entries.empty()) {
        AppendMenuW(recentMenu_, MF_GRAYED | MF_STRING, 0, L"(No Recent Files)");
        return;
    }

    for (size_t i = 0; i < recentState_.entries.size() && i < fastpdf::core::recent::kMaxRecentFiles; ++i) {
        const auto& entry = recentState_.entries[i];
        // Build menu display label: "1: filename.pdf"
        size_t slash = entry.path.find_last_of(L"\\/");
        std::wstring fileName = (slash != std::wstring::npos) ? entry.path.substr(slash + 1) : entry.path;
        std::wstring label = L"&" + std::to_wstring(i + 1) + L": " + fileName;
        AppendMenuW(recentMenu_, MF_STRING, kIdRecentBase + i, label.c_str());
    }

    AppendMenuW(recentMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(recentMenu_, MF_STRING, kIdRecentClear, L"Clear Recent Files");
}

void AppWindow::OnOpenRecent(size_t index) noexcept {
    if (index >= recentState_.entries.size()) {
        return;
    }

    const auto entry = recentState_.entries[index];
    // Check if file exists; if missing, handle cleanly and remove from recent
    DWORD attribs = GetFileAttributesW(entry.path.c_str());
    if (attribs == INVALID_FILE_ATTRIBUTES || (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(hwnd_, (L"The file could not be found:\n" + entry.path).c_str(),
                    L"Recent Files", MB_OK | MB_ICONWARNING);
        fastpdf::core::recent::RemoveRecentEntry(recentState_, entry.path);
        fastpdf::platform::win::SaveRecentFiles(recentState_);
        UpdateRecentMenu();
        return;
    }

    OpenPath(entry.path);

    // After opening, apply remembered zoom mode / zoom percent
    if (entry.zoomMode != fastpdf::core::layout::FitMode::Custom) {
        fitMode_ = entry.zoomMode;
    } else if (entry.zoomPercent > 0.0) {
        zoomPercent_ = entry.zoomPercent;
        fitMode_ = fastpdf::core::layout::FitMode::Custom;
    }
}

} // namespace fastpdf::app
