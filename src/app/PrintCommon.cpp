#include "PrintCommon.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winspool.h>
#include <vector>
#include <string>
#include <algorithm>

namespace fastpdf::app::print {

std::wstring GetDefaultPrinterName() noexcept {
    DWORD size = 0;
    GetDefaultPrinterW(nullptr, &size);
    if (size == 0) {
        return L"";
    }
    std::wstring name(size, L'\0');
    if (GetDefaultPrinterW(name.data(), &size)) {
        while (!name.empty() && name.back() == L'\0') {
            name.pop_back();
        }
        return name;
    }
    return L"";
}

std::vector<PrinterInfo> EnumeratePrinters() noexcept {
    std::vector<PrinterInfo> result;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    DWORD bytesNeeded = 0;
    DWORD count = 0;

    EnumPrintersW(flags, nullptr, 2, nullptr, 0, &bytesNeeded, &count);
    if (bytesNeeded == 0) {
        return result;
    }

    std::vector<BYTE> buffer(bytesNeeded);
    if (!EnumPrintersW(flags, nullptr, 2, buffer.data(), bytesNeeded, &bytesNeeded, &count)) {
        return result;
    }

    const std::wstring defaultPrinter = GetDefaultPrinterName();
    auto* pInfo = reinterpret_cast<PRINTER_INFO_2W*>(buffer.data());

    result.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        if (pInfo[i].pPrinterName != nullptr) {
            PrinterInfo info;
            info.name = pInfo[i].pPrinterName;
            info.isDefault = (info.name == defaultPrinter);
            result.push_back(std::move(info));
        }
    }

    // Sort default printer first, then alphabetically
    std::sort(result.begin(), result.end(), [](const PrinterInfo& a, const PrinterInfo& b) {
        if (a.isDefault != b.isDefault) {
            return a.isDefault > b.isDefault;
        }
        return a.name < b.name;
    });

    return result;
}

std::vector<PaperInfo> EnumeratePapers(const std::wstring& printerName) noexcept {
    std::vector<PaperInfo> papers;
    if (printerName.empty()) {
        return papers;
    }

    // DC_PAPERS gives array of WORD (DMPAPER_*)
    const int count = DeviceCapabilitiesW(printerName.c_str(), nullptr, DC_PAPERS, nullptr, nullptr);
    if (count <= 0) {
        return papers;
    }

    std::vector<WORD> paperCodes(count);
    DeviceCapabilitiesW(printerName.c_str(), nullptr, DC_PAPERS, reinterpret_cast<LPWSTR>(paperCodes.data()), nullptr);

    // DC_PAPERNAMES gives array of wchar_t[64]
    std::vector<wchar_t> namesBuffer(static_cast<std::size_t>(count) * 64);
    DeviceCapabilitiesW(printerName.c_str(), nullptr, DC_PAPERNAMES, namesBuffer.data(), nullptr);

    // DC_PAPERSIZE gives array of POINT (tenths of a mm)
    std::vector<POINT> sizes(count);
    DeviceCapabilitiesW(printerName.c_str(), nullptr, DC_PAPERSIZE, reinterpret_cast<LPWSTR>(sizes.data()), nullptr);

    papers.reserve(count);
    for (int i = 0; i < count; ++i) {
        PaperInfo pi;
        pi.paperSize = static_cast<short>(paperCodes[i]);
        const wchar_t* pName = &namesBuffer[static_cast<std::size_t>(i) * 64];
        pi.name = pName;
        pi.widthMm = sizes[i].x / 10;
        pi.heightMm = sizes[i].y / 10;
        papers.push_back(std::move(pi));
    }

    return papers;
}

} // namespace fastpdf::app::print
