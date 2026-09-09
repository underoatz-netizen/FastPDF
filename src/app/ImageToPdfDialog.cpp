#include "ImageToPdfDialog.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>

#include "DropRouting.h"

namespace fastpdf::app::convert {

namespace {

inline HMENU ControlIdToHmenu(int id) noexcept {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

// Control IDs
constexpr int IDC_IMG_LIST       = 2001;
constexpr int IDC_BTN_ADD        = 2002;
constexpr int IDC_BTN_REMOVE     = 2003;
constexpr int IDC_BTN_MOVE_UP    = 2004;
constexpr int IDC_BTN_MOVE_DOWN  = 2005;
constexpr int IDC_PROGRESS_BAR   = 2006;
constexpr int IDC_STATUS_TEXT    = 2007;

constexpr int IDC_BTN_CONVERT    = IDOK;
constexpr int IDC_BTN_CANCEL     = IDCANCEL;

// Helper to open file open dialog allowing multiple images selection
std::vector<std::wstring> PickImageFiles(HWND hwndParent) noexcept {
    std::vector<std::wstring> selected;

    Microsoft::WRL::ComPtr<IFileOpenDialog> pfd;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr) && pfd) {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        }

        COMDLG_FILTERSPEC filterSpecs[] = {
            { L"All Supported Images (*.png;*.jpg;*.jpeg;*.bmp)", L"*.png;*.jpg;*.jpeg;*.bmp" },
            { L"PNG Images (*.png)", L"*.png" },
            { L"JPEG Images (*.jpg;*.jpeg)", L"*.jpg;*.jpeg" },
            { L"BMP Images (*.bmp)", L"*.bmp" },
            { L"All Files (*.*)", L"*.*" }
        };
        pfd->SetFileTypes(ARRAYSIZE(filterSpecs), filterSpecs);
        pfd->SetTitle(L"Select Images to Convert to PDF");

        hr = pfd->Show(hwndParent);
        if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IShellItemArray> items;
            hr = pfd->GetResults(&items);
            if (SUCCEEDED(hr) && items) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    Microsoft::WRL::ComPtr<IShellItem> item;
                    if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
                        PWSTR pszPath = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath != nullptr) {
                            if (IsSupportedImagePath(pszPath)) {
                                selected.push_back(pszPath);
                            }
                            CoTaskMemFree(pszPath);
                        }
                    }
                }
            }
        }
        return selected;
    }

    // Classic OPENFILENAME fallback
    std::vector<wchar_t> buffer(65536, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndParent;
    ofn.lpstrFilter = L"Supported Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select Images to Convert to PDF";

    if (GetOpenFileNameW(&ofn)) {
        const wchar_t* ptr = buffer.data();
        std::wstring dir = ptr;
        ptr += dir.size() + 1;
        if (*ptr == L'\0') {
            // Single file selected
            if (IsSupportedImagePath(dir)) {
                selected.push_back(dir);
            }
        } else {
            // Multiple files selected
            while (*ptr != L'\0') {
                std::wstring file = ptr;
                std::wstring fullPath = dir + L"\\" + file;
                if (IsSupportedImagePath(fullPath)) {
                    selected.push_back(fullPath);
                }
                ptr += file.size() + 1;
            }
        }
    }
    return selected;
}

// Native save file dialog for output PDF path
bool PickOutputPdfFile(HWND hwndParent, std::wstring& outPath) noexcept {
    Microsoft::WRL::ComPtr<IFileSaveDialog> psd;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psd));
    if (SUCCEEDED(hr) && psd) {
        COMDLG_FILTERSPEC filterSpecs[] = {
            { L"PDF Document (*.pdf)", L"*.pdf" }
        };
        psd->SetFileTypes(1, filterSpecs);
        psd->SetDefaultExtension(L"pdf");
        psd->SetFileName(L"Output.pdf");
        psd->SetTitle(L"Save PDF As");

        DWORD dwOptions = 0;
        if (SUCCEEDED(psd->GetOptions(&dwOptions))) {
            psd->SetOptions(dwOptions | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT);
        }

        hr = psd->Show(hwndParent);
        if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IShellItem> item;
            if (SUCCEEDED(psd->GetResult(&item)) && item) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath != nullptr) {
                    outPath = pszPath;
                    CoTaskMemFree(pszPath);
                    return true;
                }
            }
        }
        return false;
    }

    // Classic GetSaveFileName fallback
    wchar_t filename[MAX_PATH] = L"Output.pdf";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndParent;
    ofn.lpstrFilter = L"PDF Document (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Save PDF As";

    if (GetSaveFileNameW(&ofn)) {
        outPath = filename;
        return true;
    }
    return false;
}

} // namespace

ImageToPdfDialog::~ImageToPdfDialog() {
    cancelFlag_.store(true, std::memory_order_relaxed);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void ImageToPdfDialog::Show(HWND hwndParent, const std::vector<std::wstring>& initialImages) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        return;
    }
    hwndParent_ = hwndParent;
    imagePaths_ = initialImages;

    std::vector<std::uint8_t> dlgBuffer(2048, 0);
    auto* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuffer.data());
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->dwExtendedStyle = 0;
    dlg->cdit = 0;
    dlg->x = 0;
    dlg->y = 0;
    dlg->cx = 320;
    dlg->cy = 240;

    DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        dlg,
        hwndParent,
        DialogProc,
        reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK ImageToPdfDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImageToPdfDialog* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<ImageToPdfDialog*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ImageToPdfDialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }

    if (self != nullptr) {
        return self->HandleDialogMessage(hwnd, msg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR ImageToPdfDialog::HandleDialogMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_INITDIALOG: {
            hwndDialog_ = hwnd;
            OnInitDialog(hwnd);
            return TRUE;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            bool added = false;
            for (UINT i = 0; i < count; ++i) {
                wchar_t path[MAX_PATH]{};
                if (DragQueryFileW(drop, i, path, MAX_PATH) > 0) {
                    if (IsSupportedImagePath(path)) {
                        imagePaths_.push_back(path);
                        added = true;
                    }
                }
            }
            DragFinish(drop);
            if (added) {
                RefreshList(hwnd);
                UpdateControlsState(hwnd);
            }
            return 0;
        }
        case WM_IMAGE_TO_PDF_PROGRESS: {
            auto* prog = reinterpret_cast<ImageToPdfProgress*>(lParam);
            if (prog != nullptr) {
                OnProgress(*prog);
                delete prog;
            }
            return TRUE;
        }
        case WM_IMAGE_TO_PDF_FINISHED: {
            auto* res = reinterpret_cast<ImageToPdfResult*>(lParam);
            if (res != nullptr) {
                OnFinished(*res);
                delete res;
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (id == IDC_IMG_LIST && (code == LBN_SELCHANGE || code == LBN_SELCANCEL)) {
                UpdateControlsState(hwnd);
                return TRUE;
            }

            if (id == IDC_BTN_ADD && code == BN_CLICKED) {
                OnAddImages(hwnd);
                return TRUE;
            }

            if (id == IDC_BTN_REMOVE && code == BN_CLICKED) {
                OnRemoveSelected(hwnd);
                return TRUE;
            }

            if (id == IDC_BTN_MOVE_UP && code == BN_CLICKED) {
                OnMoveUp(hwnd);
                return TRUE;
            }

            if (id == IDC_BTN_MOVE_DOWN && code == BN_CLICKED) {
                OnMoveDown(hwnd);
                return TRUE;
            }

            if (id == IDC_BTN_CONVERT && code == BN_CLICKED) {
                OnStartConversion(hwnd);
                return TRUE;
            }

            if (id == IDC_BTN_CANCEL && code == BN_CLICKED) {
                OnCancelConversion(hwnd);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE: {
            OnCancelConversion(hwnd);
            return TRUE;
        }
    }
    return FALSE;
}

void ImageToPdfDialog::OnInitDialog(HWND hwnd) noexcept {
    SetWindowTextW(hwnd, L"Convert Images to PDF");

    HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Listbox for images
    hwndList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        15, 15, 340, 240, hwnd, ControlIdToHmenu(IDC_IMG_LIST), hInst, nullptr);
    SendMessageW(hwndList_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Buttons on the right side
    HWND btnAdd = CreateWindowExW(
        0, L"BUTTON", L"&Add Images...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        370, 15, 100, 26, hwnd, ControlIdToHmenu(IDC_BTN_ADD), hInst, nullptr);
    SendMessageW(btnAdd, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND btnRemove = CreateWindowExW(
        0, L"BUTTON", L"&Remove",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        370, 50, 100, 26, hwnd, ControlIdToHmenu(IDC_BTN_REMOVE), hInst, nullptr);
    SendMessageW(btnRemove, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND btnUp = CreateWindowExW(
        0, L"BUTTON", L"Move &Up",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        370, 95, 100, 26, hwnd, ControlIdToHmenu(IDC_BTN_MOVE_UP), hInst, nullptr);
    SendMessageW(btnUp, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND btnDown = CreateWindowExW(
        0, L"BUTTON", L"Move &Down",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        370, 130, 100, 26, hwnd, ControlIdToHmenu(IDC_BTN_MOVE_DOWN), hInst, nullptr);
    SendMessageW(btnDown, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Progress bar and status label
    HWND progressBar = CreateWindowExW(
        0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE,
        15, 270, 455, 18, hwnd, ControlIdToHmenu(IDC_PROGRESS_BAR), hInst, nullptr);
    SendMessageW(progressBar, PBM_SETRANGE32, 0, 100);
    SendMessageW(progressBar, PBM_SETPOS, 0, 0);

    HWND statusText = CreateWindowExW(
        0, L"STATIC", L"Add or drop images to convert to PDF. Use Move Up/Down to reorder.",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        15, 295, 455, 20, hwnd, ControlIdToHmenu(IDC_STATUS_TEXT), hInst, nullptr);
    SendMessageW(statusText, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Convert and Cancel buttons
    HWND btnConvert = CreateWindowExW(
        0, L"BUTTON", L"&Create PDF...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        275, 325, 95, 28, hwnd, ControlIdToHmenu(IDC_BTN_CONVERT), hInst, nullptr);
    SendMessageW(btnConvert, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND btnCancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        375, 325, 95, 28, hwnd, ControlIdToHmenu(IDC_BTN_CANCEL), hInst, nullptr);
    SendMessageW(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Resize dialog to fit nicely
    RECT rc = { 0, 0, 485, 365 };
    AdjustWindowRectEx(&rc, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);

    // Enable drag-and-drop onto the dialog
    DragAcceptFiles(hwnd, TRUE);

    RefreshList(hwnd);
    UpdateControlsState(hwnd);
}

void ImageToPdfDialog::RefreshList(HWND /*hwnd*/) noexcept {
    if (hwndList_ == nullptr) return;
    const int sel = static_cast<int>(SendMessageW(hwndList_, LB_GETCURSEL, 0, 0));
    SendMessageW(hwndList_, LB_RESETCONTENT, 0, 0);

    for (size_t i = 0; i < imagePaths_.size(); ++i) {
        std::wstring itemText = std::to_wstring(i + 1) + L". " +
                                std::filesystem::path(imagePaths_[i]).filename().wstring();
        SendMessageW(hwndList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(itemText.c_str()));
    }

    if (sel >= 0 && sel < static_cast<int>(imagePaths_.size())) {
        SendMessageW(hwndList_, LB_SETCURSEL, sel, 0);
    } else if (!imagePaths_.empty()) {
        SendMessageW(hwndList_, LB_SETCURSEL, static_cast<WPARAM>(imagePaths_.size() - 1), 0);
    }
}

void ImageToPdfDialog::UpdateControlsState(HWND hwnd) noexcept {
    const bool running = isRunning_.load(std::memory_order_relaxed);
    const int count = static_cast<int>(imagePaths_.size());
    const int sel = (hwndList_ != nullptr) ? static_cast<int>(SendMessageW(hwndList_, LB_GETCURSEL, 0, 0)) : -1;

    EnableWindow(GetDlgItem(hwnd, IDC_IMG_LIST), !running);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_ADD), !running);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_REMOVE), !running && (sel >= 0 && sel < count));
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE_UP), !running && (sel > 0 && sel < count));
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE_DOWN), !running && (sel >= 0 && sel < count - 1));
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_CONVERT), !running && (count > 0));

    if (running) {
        SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_CANCEL), L"Cancel");
    } else {
        SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_CANCEL), L"Close");
    }
}

void ImageToPdfDialog::OnAddImages(HWND hwnd) noexcept {
    std::vector<std::wstring> chosen = PickImageFiles(hwnd);
    if (!chosen.empty()) {
        imagePaths_.insert(imagePaths_.end(), chosen.begin(), chosen.end());
        RefreshList(hwnd);
        UpdateControlsState(hwnd);
    }
}

void ImageToPdfDialog::OnRemoveSelected(HWND hwnd) noexcept {
    if (hwndList_ == nullptr) return;
    const int sel = static_cast<int>(SendMessageW(hwndList_, LB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(imagePaths_.size())) {
        imagePaths_.erase(imagePaths_.begin() + sel);
        RefreshList(hwnd);
        UpdateControlsState(hwnd);
    }
}

void ImageToPdfDialog::OnMoveUp(HWND hwnd) noexcept {
    if (hwndList_ == nullptr) return;
    const int sel = static_cast<int>(SendMessageW(hwndList_, LB_GETCURSEL, 0, 0));
    if (sel > 0 && sel < static_cast<int>(imagePaths_.size())) {
        std::swap(imagePaths_[sel], imagePaths_[sel - 1]);
        RefreshList(hwnd);
        SendMessageW(hwndList_, LB_SETCURSEL, sel - 1, 0);
        UpdateControlsState(hwnd);
    }
}

void ImageToPdfDialog::OnMoveDown(HWND hwnd) noexcept {
    if (hwndList_ == nullptr) return;
    const int sel = static_cast<int>(SendMessageW(hwndList_, LB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel + 1 < static_cast<int>(imagePaths_.size())) {
        std::swap(imagePaths_[sel], imagePaths_[sel + 1]);
        RefreshList(hwnd);
        SendMessageW(hwndList_, LB_SETCURSEL, sel + 1, 0);
        UpdateControlsState(hwnd);
    }
}

void ImageToPdfDialog::OnStartConversion(HWND hwnd) noexcept {
    if (isRunning_.load(std::memory_order_relaxed) || imagePaths_.empty()) {
        return;
    }

    std::wstring outputPath;
    if (!PickOutputPdfFile(hwnd, outputPath)) {
        return; // User cancelled save dialog
    }

    if (GetFileAttributesW(outputPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwnd,
            L"The selected output file already exists. Please choose a different file name.",
            L"File Already Exists", MB_OK | MB_ICONWARNING);
        return;
    }

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    isRunning_.store(true, std::memory_order_relaxed);
    cancelFlag_.store(false, std::memory_order_relaxed);
    UpdateControlsState(hwnd);

    HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS_BAR);
    SendMessageW(hProgress, PBM_SETRANGE32, 0, static_cast<LPARAM>(imagePaths_.size()));
    SendMessageW(hProgress, PBM_SETPOS, 0, 0);
    SetWindowTextW(GetDlgItem(hwnd, IDC_STATUS_TEXT), L"Preparing PDF creation...");

    const std::vector<std::wstring> paths = imagePaths_;
    const HWND targetHwnd = hwnd;

    workerThread_ = std::thread([this, paths, outputPath, targetHwnd]() {
        ImageToPdfResult res = CreatePdfFromImages(
            paths, outputPath, cancelFlag_,
            [targetHwnd](const ImageToPdfProgress& prog) -> bool {
                auto* p = new ImageToPdfProgress(prog);
                PostMessageW(targetHwnd, WM_IMAGE_TO_PDF_PROGRESS, 0, reinterpret_cast<LPARAM>(p));
                return true;
            });

        auto* finalRes = new ImageToPdfResult(std::move(res));
        PostMessageW(targetHwnd, WM_IMAGE_TO_PDF_FINISHED, 0, reinterpret_cast<LPARAM>(finalRes));
    });
}

void ImageToPdfDialog::OnCancelConversion(HWND hwnd) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        cancelFlag_.store(true, std::memory_order_relaxed);
        SetWindowTextW(GetDlgItem(hwnd, IDC_STATUS_TEXT), L"Cancelling...");
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_CANCEL), FALSE);
    } else {
        EndDialog(hwnd, IDCANCEL);
    }
}

void ImageToPdfDialog::OnProgress(const ImageToPdfProgress& progress) noexcept {
    if (!hwndDialog_) return;
    HWND hProgress = GetDlgItem(hwndDialog_, IDC_PROGRESS_BAR);
    SendMessageW(hProgress, PBM_SETPOS, progress.current + 1, 0);

    std::wstring status = L"Processing image " + std::to_wstring(progress.current + 1) +
                          L" of " + std::to_wstring(progress.total) + L": " + progress.currentFileName;
    SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), status.c_str());
}

void ImageToPdfDialog::OnFinished(const ImageToPdfResult& result) noexcept {
    isRunning_.store(false, std::memory_order_relaxed);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    if (!hwndDialog_) return;
    UpdateControlsState(hwndDialog_);

    HWND hProgress = GetDlgItem(hwndDialog_, IDC_PROGRESS_BAR);
    if (result.status == ImageToPdfStatus::Success) {
        SendMessageW(hProgress, PBM_SETPOS, result.totalImages, 0);
        SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), L"PDF created successfully.");
        MessageBoxW(hwndDialog_,
            (L"Successfully created PDF with " + std::to_wstring(result.completedImages) + L" pages.").c_str(),
            L"Conversion Complete", MB_OK | MB_ICONINFORMATION);
        EndDialog(hwndDialog_, IDOK);
    } else if (result.status == ImageToPdfStatus::Cancelled) {
        SendMessageW(hProgress, PBM_SETPOS, 0, 0);
        SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), L"Conversion cancelled.");
    } else {
        SendMessageW(hProgress, PBM_SETPOS, 0, 0);
        SetWindowTextW(GetDlgItem(hwndDialog_, IDC_STATUS_TEXT), L"Conversion failed.");
        MessageBoxW(hwndDialog_, result.userErrorMessage.c_str(), L"Conversion Error", MB_OK | MB_ICONERROR);
    }
}

} // namespace fastpdf::app::convert
