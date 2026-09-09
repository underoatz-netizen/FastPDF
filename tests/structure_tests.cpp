// Build configuration/structure test: verifies that the built FastPDF.exe
// exists, is an x64 (AMD64) PE image, and is a Windows GUI-subsystem binary.
// No PDFium required.

#include <windows.h>

#include <cstdint>
#include <string>

#include "test_harness.h"

#ifndef FASTPDF_EXE_PATH
#error "FASTPDF_EXE_PATH must be defined to the built FastPDF.exe path"
#endif

namespace {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), size);
    return wide;
}

struct PeInfo {
    bool fileExists = false;
    bool isPe = false;
    bool isX64 = false;
    bool isGuiSubsystem = false;
};

PeInfo ReadPeInfo(const std::wstring& path) {
    PeInfo info{};

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return info;
    }
    info.fileExists = true;

    IMAGE_DOS_HEADER dos{};
    DWORD bytesRead = 0;
    if (!ReadFile(file, &dos, sizeof(dos), &bytesRead, nullptr) ||
        bytesRead != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(file);
        return info;
    }

    if (SetFilePointer(file, static_cast<LONG>(dos.e_lfanew), nullptr, FILE_BEGIN) ==
        INVALID_SET_FILE_POINTER) {
        CloseHandle(file);
        return info;
    }

    DWORD signature = 0;
    if (!ReadFile(file, &signature, sizeof(signature), &bytesRead, nullptr) ||
        bytesRead != sizeof(signature) || signature != IMAGE_NT_SIGNATURE) {
        CloseHandle(file);
        return info;
    }
    info.isPe = true;

    IMAGE_FILE_HEADER coff{};
    if (!ReadFile(file, &coff, sizeof(coff), &bytesRead, nullptr) ||
        bytesRead != sizeof(coff)) {
        CloseHandle(file);
        return info;
    }
    info.isX64 = (coff.Machine == IMAGE_FILE_MACHINE_AMD64);

    IMAGE_OPTIONAL_HEADER64 optional{};
    if (!ReadFile(file, &optional, sizeof(optional), &bytesRead, nullptr) ||
        bytesRead != sizeof(optional) ||
        optional.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        CloseHandle(file);
        return info;
    }
    info.isGuiSubsystem = (optional.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI);

    CloseHandle(file);
    return info;
}

} // namespace

FASTPDF_TEST(built_exe_exists) {
    const PeInfo info = ReadPeInfo(Utf8ToWide(FASTPDF_EXE_PATH));
    FASTPDF_CHECK(info.fileExists);
}

FASTPDF_TEST(built_exe_is_x64_pe) {
    const PeInfo info = ReadPeInfo(Utf8ToWide(FASTPDF_EXE_PATH));
    FASTPDF_CHECK(info.isPe);
    FASTPDF_CHECK(info.isX64);
}

FASTPDF_TEST(built_exe_is_gui_subsystem) {
    const PeInfo info = ReadPeInfo(Utf8ToWide(FASTPDF_EXE_PATH));
    FASTPDF_CHECK(info.isPe);
    FASTPDF_CHECK(info.isGuiSubsystem);
}

FASTPDF_TEST(built_exe_directory_has_notices) {
    const std::wstring exePath = Utf8ToWide(FASTPDF_EXE_PATH);
    const size_t slash = exePath.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const std::wstring dir = exePath.substr(0, slash);
        const std::wstring noticePath = dir + L"\\THIRD_PARTY_NOTICES.txt";
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        FASTPDF_CHECK(GetFileAttributesExW(noticePath.c_str(), GetFileExInfoStandard, &fad));
    }
}

FASTPDF_TEST(dist_packaging_layout_smoke) {
    // If a dist release package was generated, verify its structure:
    // FastPDF.exe, pdfium.dll, THIRD_PARTY_NOTICES.txt, LICENSE.txt, README.txt, MANIFEST.txt
    const std::wstring distDir = L"dist\\FastPDF-0.1.0-win-x64";
    WIN32_FILE_ATTRIBUTE_DATA dirFad{};
    if (GetFileAttributesExW(distDir.c_str(), GetFileExInfoStandard, &dirFad)) {
        const std::wstring requiredFiles[] = {
            distDir + L"\\FastPDF.exe",
            distDir + L"\\pdfium.dll",
            distDir + L"\\THIRD_PARTY_NOTICES.txt",
            distDir + L"\\LICENSE.txt",
            distDir + L"\\README.txt",
            distDir + L"\\MANIFEST.txt"
        };
        for (const auto& f : requiredFiles) {
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            FASTPDF_CHECK(GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &fad));
        }
    }
}

int main() { return fastpdf::test::RunAll(); }