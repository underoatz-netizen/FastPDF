#include "ScreenshotClipboard.h"

#include <wrl/client.h>
#include <wincodec.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

namespace fastpdf::app::screenshot {

namespace {

// RAII helper to ensure clipboard is closed on exit.
struct ClipboardCloser {
    ~ClipboardCloser() {
        CloseClipboard();
    }
};

// RAII helper for HGLOBAL handle that has not yet been given to the clipboard.
struct GlobalMemScope {
    HGLOBAL handle = nullptr;
    ~GlobalMemScope() {
        if (handle != nullptr) {
            GlobalFree(handle);
        }
    }
    HGLOBAL release() noexcept {
        HGLOBAL h = handle;
        handle = nullptr;
        return h;
    }
};

} // namespace

std::vector<std::uint8_t> EncodePngWithWic(const fastpdf::renderer::Bitmap& bitmap) noexcept {
    if (bitmap.empty() || bitmap.width <= 0 || bitmap.height <= 0 || bitmap.stride <= 0) {
        return {};
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        return {};
    }

    Microsoft::WRL::ComPtr<IStream> stream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr) || !stream) {
        return {};
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr) || !encoder) {
        return {};
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return {};
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr) || !frame) {
        return {};
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        return {};
    }

    hr = frame->SetSize(static_cast<UINT>(bitmap.width), static_cast<UINT>(bitmap.height));
    if (FAILED(hr)) {
        return {};
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        return {};
    }

    const UINT cbBufferSize = static_cast<UINT>(bitmap.stride) * static_cast<UINT>(bitmap.height);
    hr = frame->WritePixels(
        static_cast<UINT>(bitmap.height),
        static_cast<UINT>(bitmap.stride),
        cbBufferSize,
        const_cast<BYTE*>(bitmap.data.data()));
    if (FAILED(hr)) {
        return {};
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        return {};
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        return {};
    }

    STATSTG stat{};
    hr = stream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr) || stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > 100 * 1024 * 1024) {
        return {};
    }

    std::vector<std::uint8_t> result(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG readBytes = 0;
    hr = stream->Read(result.data(), static_cast<ULONG>(result.size()), &readBytes);
    if (FAILED(hr) || readBytes != result.size()) {
        return {};
    }

    return result;
}

bool DecodePngWithWic(const std::vector<std::uint8_t>& pngBytes,
                     int& outWidth, int& outHeight,
                     std::vector<std::uint8_t>& outBgra) noexcept {
    outWidth = 0;
    outHeight = 0;
    outBgra.clear();

    if (pngBytes.empty()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) {
        return false;
    }

    hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(pngBytes.data()),
        static_cast<DWORD>(pngBytes.size()));
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0 || width > 16384 || height > 16384) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        return false;
    }

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return false;
    }

    const UINT stride = width * 4;
    const std::size_t totalBytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> pixels(totalBytes);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(totalBytes), pixels.data());
    if (FAILED(hr)) {
        return false;
    }

    outWidth = static_cast<int>(width);
    outHeight = static_cast<int>(height);
    outBgra = std::move(pixels);
    return true;
}

bool SavePngToFile(const fastpdf::renderer::Bitmap& bitmap, const std::wstring& filePath) noexcept {
    const std::vector<std::uint8_t> bytes = EncodePngWithWic(bitmap);
    if (bytes.empty()) {
        return false;
    }

    HANDLE file = CreateFileW(
        filePath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(
        file, bytes.data(), static_cast<DWORD>(bytes.size()),
        &written, nullptr);
    CloseHandle(file);

    return (ok && written == bytes.size());
}

bool CopyBitmapToClipboard(HWND hwndOwner, const fastpdf::renderer::Bitmap& bitmap) noexcept {
    if (bitmap.empty() || bitmap.width <= 0 || bitmap.height <= 0) {
        return false;
    }

    const int width = bitmap.width;
    const int height = bitmap.height;
    const int srcStride = bitmap.stride;

    // 1. Prepare CF_DIBV5 data:
    // DIBV5 expects a BITMAPV5HEADER followed by raw bottom-up pixel bits (or top-down if bV5Height is negative).
    // Bottom-up (positive height) has the widest compatibility across Windows apps (Office, Paint, Photoshop, browsers).
    const std::size_t dibRowBytes = static_cast<std::size_t>(width) * 4;
    const std::size_t dibImageBytes = dibRowBytes * static_cast<std::size_t>(height);
    const std::size_t totalDibBytes = sizeof(BITMAPV5HEADER) + dibImageBytes;

    GlobalMemScope dibMem;
    dibMem.handle = GlobalAlloc(GMEM_MOVEABLE, totalDibBytes);
    if (dibMem.handle == nullptr) {
        return false;
    }

    auto* dibPtr = static_cast<std::uint8_t*>(GlobalLock(dibMem.handle));
    if (dibPtr == nullptr) {
        return false;
    }

    auto* header = reinterpret_cast<BITMAPV5HEADER*>(dibPtr);
    std::memset(header, 0, sizeof(BITMAPV5HEADER));
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = width;
    header->bV5Height = height; // Positive height => bottom-up DIB
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(dibImageBytes);
    header->bV5RedMask   = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask  = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    header->bV5Intent = LCS_GM_IMAGES;

    std::uint8_t* dstPixels = dibPtr + sizeof(BITMAPV5HEADER);
    // Copy rows bottom-up from the top-down input bitmap
    for (int y = 0; y < height; ++y) {
        const int srcY = (height - 1) - y;
        const std::uint8_t* srcRow = bitmap.data.data() + static_cast<std::size_t>(srcY) * srcStride;
        std::uint8_t* dstRow = dstPixels + static_cast<std::size_t>(y) * dibRowBytes;
        std::memcpy(dstRow, srcRow, dibRowBytes);
    }
    GlobalUnlock(dibMem.handle);

    // 2. Prepare PNG clipboard format data if practical
    const UINT pngFormatId = RegisterClipboardFormatW(L"PNG");
    GlobalMemScope pngMem;
    if (pngFormatId != 0) {
        const std::vector<std::uint8_t> pngBytes = EncodePngWithWic(bitmap);
        if (!pngBytes.empty()) {
            pngMem.handle = GlobalAlloc(GMEM_MOVEABLE, pngBytes.size());
            if (pngMem.handle != nullptr) {
                void* pngPtr = GlobalLock(pngMem.handle);
                if (pngPtr != nullptr) {
                    std::memcpy(pngPtr, pngBytes.data(), pngBytes.size());
                    GlobalUnlock(pngMem.handle);
                } else {
                    GlobalFree(pngMem.release());
                }
            }
        }
    }

    // 3. Open clipboard and set data
    if (!OpenClipboard(hwndOwner)) {
        return false;
    }
    ClipboardCloser closer;

    if (!EmptyClipboard()) {
        return false;
    }

    bool success = false;

    // Set CF_DIBV5
    if (SetClipboardData(CF_DIBV5, dibMem.handle) != nullptr) {
        dibMem.release(); // System now owns the HGLOBAL
        success = true;
    }

    // Set PNG format if available
    if (pngFormatId != 0 && pngMem.handle != nullptr) {
        if (SetClipboardData(pngFormatId, pngMem.handle) != nullptr) {
            pngMem.release(); // System now owns the HGLOBAL
            success = true;
        }
    }

    return success;
}

} // namespace fastpdf::app::screenshot
