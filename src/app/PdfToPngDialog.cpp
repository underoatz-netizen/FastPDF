#include "PdfToPngDialog.h"

#include <shlobj.h>
#include <wrl/client.h>
#include <filesystem>

namespace fastpdf::app::convert {

namespace {

// Helper to safely cast int control ID to HMENU for CreateWindowExW
inline HMENU ControlIdToHmenu(int id) noexcept {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

// Control IDs for the conversion dialog
constexpr int IDC_GRP_PAGES      = 1001;
constexpr int IDC_RADIO_ALL      = 1002;
constexpr int IDC_RADIO_CURRENT  = 1003;
constexpr int IDC_RADIO_CUSTOM   = 1004;
constexpr int IDC_EDIT_CUSTOM    = 1005;

constexpr int IDC_GRP_QUALITY    = 1010;
constexpr int IDC_RADIO_STANDARD = 1011;
constexpr int IDC_RADIO_HIGH     = 1012;

constexpr int IDC_GRP_OUTPUT     = 1020;
constexpr int IDC_EDIT_FOLDER    = 1021;
constexpr int IDC_BTN_BROWSE     = 1022;

constexpr int IDC_PROGRESS_BAR   = 1030;
constexpr int IDC_STATUS_TEXT    = 1031;

constexpr int IDC_BTN_CONVERT    = IDOK;
constexpr int IDC_BTN_CANCEL     = IDCANCEL;

// Helper to open modern IFileOpenDialog for folder picking
bool PickFolderModern(HWND hwndParent, std::wstring& outFolder) noexcept {
    Microsoft::WRL::ComPtr<IFileDialog> pfd;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) {
        return false;
    }

    DWORD dwOptions = 0;
    if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
        pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    pfd->SetTitle(L"Select Output Folder for PNG Images");

    hr = pfd->Show(hwndParent);
    if (FAILED(hr)) {
        return false; // Cancelled or failed
    }

    Microsoft::WRL::ComPtr<IShellItem> psi;
    hr = pfd->GetResult(&psi);
    if (SUCCEEDED(hr)) {
        PWSTR pszPath = nullptr;
        hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
        if (SUCCEEDED(hr) && pszPath != nullptr) {
            outFolder = pszPath;
            CoTaskMemFree(pszPath);
            return true;
        }
    }
    return false;
}

// Fallback SHBrowseForFolder if needed
bool PickFolderClassic(HWND hwndParent, std::wstring& outFolder) noexcept {
    BROWSEINFOW bi{};
    bi.hwndOwner = hwndParent;
    bi.lpszTitle = L"Select Output Folder for PNG Images";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(pidl, path)) {
            outFolder = path;
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
    return false;
}

bool PickFolder(HWND hwndParent, std::wstring& outFolder) noexcept {
    if (PickFolderModern(hwndParent, outFolder)) {
        return true;
    }
    return PickFolderClassic(hwndParent, outFolder);
}

} // namespace

PdfToPngDialog::~PdfToPngDialog() {
    cancelFlag_.store(true, std::memory_order_relaxed);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void PdfToPngDialog::Show(HWND hwndParent, const std::wstring& pdfPath,
                          int currentPage, int totalPages) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        return;
    }
    hwndParent_ = hwndParent;
    pdfPath_ = pdfPath;
    currentPage_ = currentPage;
    totalPages_ = totalPages;

    options_.currentPage = currentPage;
    options_.docBaseName = ExtractDocBaseName(pdfPath);
    options_.selectionMode = PageSelectionMode::All;
    options_.quality = QualityPreset::Standard;
    options_.customRangeText = L"";

    // Set default output folder to PDF directory, or desktop/documents
    std::filesystem::path p(pdfPath);
    if (std::filesystem::exists(p) && p.has_parent_path()) {
        options_.outputFolder = p.parent_path().wstring();
    } else {
        wchar_t userFolder[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, userFolder))) {
            options_.outputFolder = userFolder;
        }
    }

    // In-memory dialog template construction (DLGTEMPLATEEX or standard DLGTEMPLATE)
    // To ensure native Win32 dialog without resource script dependencies:
    std::vector<std::uint8_t> dlgBuffer(2048, 0);
    auto* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuffer.data());
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->dwExtendedStyle = 0;
    dlg->cdit = 0; // We create controls dynamically in WM_INITDIALOG for crisp DPI & font handling
    dlg->x = 0;
    dlg->y = 0;
    dlg->cx = 260;
    dlg->cy = 235;

    DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        dlg,
        hwndParent,
        DialogProc,
        reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK PdfToPngDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PdfToPngDialog* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<PdfToPngDialog*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PdfToPngDialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }

    if (self != nullptr) {
        return self->HandleDialogMessage(hwnd, msg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR PdfToPngDialog::HandleDialogMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_INITDIALOG: {
            hwndDialog_ = hwnd;
            OnInitDialog(hwnd);
            return TRUE;
        }
        case WM_CONVERT_PROGRESS: {
            auto* prog = reinterpret_cast<BatchProgress*>(lParam);
            if (prog != nullptr) {
                OnProgress(*prog);
                delete prog;
            }
            return TRUE;
        }
        case WM_CONVERT_FINISHED: {
            auto* res = reinterpret_cast<BatchConversionResult*>(lParam);
            if (res != nullptr) {
                OnFinished(*res);
                delete res;
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (id == IDC_RADIO_ALL || id == IDC_RADIO_CURRENT || id == IDC_RADIO_CUSTOM) {
                if (code == BN_CLICKED) {
                    UpdateControlsState(hwnd);
                }
                return TRUE;
            }

            if (id == IDC_BTN_BROWSE && code == BN_CLICKED) {
                OnBrowseFolder(hwnd);
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

void PdfToPngDialog::OnInitDialog(HWND hwnd) noexcept {
    SetWindowTextW(hwnd, L"Convert PDF to PNG");

    // Get default UI font
    HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Group 1: Pages
    HWND grpPages = CreateWindowExW(0, L"BUTTON", L"Pages",
                                   WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   15, 10, 360, 115, hwnd, ControlIdToHmenu(IDC_GRP_PAGES), hInst, nullptr);
    SendMessageW(grpPages, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND rAll = CreateWindowExW(0, L"BUTTON", L"&All pages",
                               WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                               25, 30, 200, 20, hwnd, ControlIdToHmenu(IDC_RADIO_ALL), hInst, nullptr);
    SendMessageW(rAll, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    std::wstring currentLabel = L"&Current page (" + std::to_wstring(currentPage_ + 1) + L")";
    HWND rCurrent = CreateWindowExW(0, L"BUTTON", currentLabel.c_str(),
                                  WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                  25, 55, 200, 20, hwnd, ControlIdToHmenu(IDC_RADIO_CURRENT), hInst, nullptr);
    SendMessageW(rCurrent, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND rCustom = CreateWindowExW(0, L"BUTTON", L"Cu&stom (e.g. 1-5, 8, 10):",
                                 WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                 25, 80, 200, 20, hwnd, ControlIdToHmenu(IDC_RADIO_CUSTOM), hInst, nullptr);
    SendMessageW(rCustom, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND editCustom = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    230, 80, 135, 22, hwnd, ControlIdToHmenu(IDC_EDIT_CUSTOM), hInst, nullptr);
    SendMessageW(editCustom, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Group 2: Quality
    HWND grpQual = CreateWindowExW(0, L"BUTTON", L"Quality",
                                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                  15, 135, 360, 60, hwnd, ControlIdToHmenu(IDC_GRP_QUALITY), hInst, nullptr);
    SendMessageW(grpQual, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND rStd = CreateWindowExW(0, L"BUTTON", L"&Standard (150 DPI)",
                               WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                               25, 158, 150, 20, hwnd, ControlIdToHmenu(IDC_RADIO_STANDARD), hInst, nullptr);
    SendMessageW(rStd, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND rHigh = CreateWindowExW(0, L"BUTTON", L"&High (300 DPI)",
                                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                200, 158, 150, 20, hwnd, ControlIdToHmenu(IDC_RADIO_HIGH), hInst, nullptr);
    SendMessageW(rHigh, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Group 3: Output Folder
    HWND grpOut = CreateWindowExW(0, L"BUTTON", L"Output Folder",
                                 WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 15, 205, 360, 60, hwnd, ControlIdToHmenu(IDC_GRP_OUTPUT), hInst, nullptr);
    SendMessageW(grpOut, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND editFolder = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", options_.outputFolder.c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                     25, 228, 255, 22, hwnd, ControlIdToHmenu(IDC_EDIT_FOLDER), hInst, nullptr);
    SendMessageW(editFolder, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND btnBrowse = CreateWindowExW(0, L"BUTTON", L"&Browse...",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    290, 227, 75, 24, hwnd, ControlIdToHmenu(IDC_BTN_BROWSE), hInst, nullptr);
    SendMessageW(btnBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Progress Bar & Status Text
    CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                    WS_CHILD | WS_VISIBLE,
                    15, 275, 360, 18, hwnd, ControlIdToHmenu(IDC_PROGRESS_BAR), hInst, nullptr);

    HWND statusText = CreateWindowExW(0, L"STATIC", L"Ready to convert.",
                                     WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                     15, 300, 360, 18, hwnd, ControlIdToHmenu(IDC_STATUS_TEXT), hInst, nullptr);
    SendMessageW(statusText, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Buttons: Convert, Cancel
    HWND btnConvert = CreateWindowExW(0, L"BUTTON", L"&Convert",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                     215, 330, 75, 26, hwnd, ControlIdToHmenu(IDC_BTN_CONVERT), hInst, nullptr);
    SendMessageW(btnConvert, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    300, 330, 75, 26, hwnd, ControlIdToHmenu(IDC_BTN_CANCEL), hInst, nullptr);
    SendMessageW(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Set initial radio selections
    CheckRadioButton(hwnd, IDC_RADIO_ALL, IDC_RADIO_CUSTOM, IDC_RADIO_ALL);
    CheckRadioButton(hwnd, IDC_RADIO_STANDARD, IDC_RADIO_HIGH, IDC_RADIO_STANDARD);

    // Position window nicely with appropriate client size
    RECT rcClient = {0, 0, 390, 370};
    AdjustWindowRectEx(&rcClient, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
    const int w = rcClient.right - rcClient.left;
    const int h = rcClient.bottom - rcClient.top;
    SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);

    UpdateControlsState(hwnd);
}

void PdfToPngDialog::UpdateControlsState(HWND hwnd) noexcept {
    const bool isCustom = (IsDlgButtonChecked(hwnd, IDC_RADIO_CUSTOM) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_CUSTOM), isCustom ? TRUE : FALSE);
}

void PdfToPngDialog::OnBrowseFolder(HWND hwnd) noexcept {
    std::wstring folder;
    if (PickFolder(hwnd, folder)) {
        options_.outputFolder = folder;
        SetDlgItemTextW(hwnd, IDC_EDIT_FOLDER, folder.c_str());
    }
}

void PdfToPngDialog::OnStartConversion(HWND hwnd) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        return;
    }

    // 1. Gather selection mode
    if (IsDlgButtonChecked(hwnd, IDC_RADIO_ALL) == BST_CHECKED) {
        options_.selectionMode = PageSelectionMode::All;
    } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_CURRENT) == BST_CHECKED) {
        options_.selectionMode = PageSelectionMode::Current;
    } else {
        options_.selectionMode = PageSelectionMode::Custom;
    }

    // 2. Gather custom range text if custom mode
    if (options_.selectionMode == PageSelectionMode::Custom) {
        wchar_t buf[256]{};
        GetDlgItemTextW(hwnd, IDC_EDIT_CUSTOM, buf, 256);
        options_.customRangeText = buf;
    }

    // 3. Resolve pages
    if (!ResolvePagesToConvert(options_.selectionMode, currentPage_,
                              options_.customRangeText, totalPages_, resolvedPages_)) {
        MessageBoxW(hwnd, L"Invalid page range specified. Please enter valid page numbers (e.g. 1-5, 8, 10).",
                    L"Convert PDF to PNG", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(hwnd, IDC_EDIT_CUSTOM));
        return;
    }

    // 4. Quality preset
    if (IsDlgButtonChecked(hwnd, IDC_RADIO_HIGH) == BST_CHECKED) {
        options_.quality = QualityPreset::High;
    } else {
        options_.quality = QualityPreset::Standard;
    }

    // 5. Output folder
    wchar_t folderBuf[MAX_PATH]{};
    GetDlgItemTextW(hwnd, IDC_EDIT_FOLDER, folderBuf, MAX_PATH);
    options_.outputFolder = folderBuf;

    if (options_.outputFolder.empty() || !std::filesystem::is_directory(options_.outputFolder)) {
        MessageBoxW(hwnd, L"Please select a valid output folder.",
                    L"Convert PDF to PNG", MB_OK | MB_ICONWARNING);
        return;
    }

    // Disable inputs, enable progress
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_ALL), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_CURRENT), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_CUSTOM), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_CUSTOM), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_STANDARD), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_HIGH), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_CONVERT), FALSE);
    SetDlgItemTextW(hwnd, IDC_BTN_CANCEL, L"Cancel");

    // Init progress bar
    SendDlgItemMessageW(hwnd, IDC_PROGRESS_BAR, PBM_SETRANGE32, 0, static_cast<LPARAM>(resolvedPages_.size()));
    SendDlgItemMessageW(hwnd, IDC_PROGRESS_BAR, PBM_SETPOS, 0, 0);
    SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Converting...");

    // Spawn background worker thread
    cancelFlag_.store(false, std::memory_order_relaxed);
    isRunning_.store(true, std::memory_order_relaxed);

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    workerThread_ = std::thread([this, hwnd]() {
        auto progressCb = [this, hwnd](const BatchProgress& prog) -> bool {
            if (cancelFlag_.load(std::memory_order_relaxed)) {
                return false;
            }
            auto* p = new BatchProgress(prog);
            PostMessageW(hwnd, WM_CONVERT_PROGRESS, 0, reinterpret_cast<LPARAM>(p));
            return true;
        };

        BatchConversionResult res = RunPdfToPngBatch(
            pdfPath_, options_, resolvedPages_, cancelFlag_, progressCb);

        auto* r = new BatchConversionResult(std::move(res));
        PostMessageW(hwnd, WM_CONVERT_FINISHED, 0, reinterpret_cast<LPARAM>(r));
    });
}

void PdfToPngDialog::OnCancelConversion(HWND hwnd) noexcept {
    if (isRunning_.load(std::memory_order_relaxed)) {
        cancelFlag_.store(true, std::memory_order_relaxed);
        SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Cancelling...");
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_CANCEL), FALSE);
    } else {
        EndDialog(hwnd, IDCANCEL);
    }
}

void PdfToPngDialog::OnProgress(const BatchProgress& progress) noexcept {
    SendDlgItemMessageW(hwndDialog_, IDC_PROGRESS_BAR, PBM_SETPOS, progress.current, 0);
    std::wstring status = L"Converting " + std::to_wstring(progress.current) +
                          L" / " + std::to_wstring(progress.total);
    SetDlgItemTextW(hwndDialog_, IDC_STATUS_TEXT, status.c_str());
}

void PdfToPngDialog::OnFinished(const BatchConversionResult& result) noexcept {
    isRunning_.store(false, std::memory_order_relaxed);

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    // Re-enable inputs
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_ALL), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_CURRENT), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_CUSTOM), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_STANDARD), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_RADIO_HIGH), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_BTN_BROWSE), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_BTN_CONVERT), TRUE);
    EnableWindow(GetDlgItem(hwndDialog_, IDC_BTN_CANCEL), TRUE);
    SetDlgItemTextW(hwndDialog_, IDC_BTN_CANCEL, L"Close");
    UpdateControlsState(hwndDialog_);

    switch (result.status) {
        case BatchResultStatus::Success: {
            SendDlgItemMessageW(hwndDialog_, IDC_PROGRESS_BAR, PBM_SETPOS, result.completedPages, 0);
            std::wstring msg = L"Converted " + std::to_wstring(result.completedPages) +
                               L" page(s) successfully.";
            SetDlgItemTextW(hwndDialog_, IDC_STATUS_TEXT, msg.c_str());
            MessageBoxW(hwndDialog_, msg.c_str(), L"Convert PDF to PNG", MB_OK | MB_ICONINFORMATION);
            EndDialog(hwndDialog_, IDOK);
            break;
        }
        case BatchResultStatus::Cancelled: {
            SetDlgItemTextW(hwndDialog_, IDC_STATUS_TEXT, L"Conversion cancelled.");
            MessageBoxW(hwndDialog_, L"Conversion cancelled. Any partially written files have been cleaned up.",
                        L"Convert PDF to PNG", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case BatchResultStatus::FileAlreadyExists: {
            std::wstring msg = L"Conversion stopped because the destination file already exists:\n" +
                               result.failedFile +
                               L"\n\nExisting files are never overwritten.";
            SetDlgItemTextW(hwndDialog_, IDC_STATUS_TEXT, L"File already exists.");
            MessageBoxW(hwndDialog_, msg.c_str(), L"Convert PDF to PNG", MB_OK | MB_ICONWARNING);
            break;
        }
        default: {
            std::wstring msg = result.userErrorMessage.empty()
                                   ? L"An error occurred during conversion."
                                   : result.userErrorMessage;
            SetDlgItemTextW(hwndDialog_, IDC_STATUS_TEXT, L"Conversion failed.");
            MessageBoxW(hwndDialog_, msg.c_str(), L"Convert PDF to PNG", MB_OK | MB_ICONERROR);
            break;
        }
    }
}

} // namespace fastpdf::app::convert
