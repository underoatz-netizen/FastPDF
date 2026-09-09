#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_harness.h"
#include <fastpdf/platform/win/ComInitializer.h>
#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/pdfium/PdfDocument.h>

#include "ImagePlacement.h"
#include "ImageToPdfWorker.h"

#pragma comment(lib, "windowscodecs.lib")

namespace {

// Scoped temp folder for test files
class ScopedTempDir {
public:
    ScopedTempDir() {
        wchar_t tempBase[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempBase);
        wchar_t tempDir[MAX_PATH]{};
        GetTempFileNameW(tempBase, L"I2P", 0, tempDir);
        DeleteFileW(tempDir);
        CreateDirectoryW(tempDir, nullptr);
        path_ = tempDir;
    }

    ~ScopedTempDir() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }

    const std::wstring& Path() const noexcept { return path_; }

private:
    std::wstring path_;
};

// Helper to save BGRA pixel buffer as PNG or BMP using WIC
bool SaveTestImage(const std::wstring& filePath, const GUID& containerFormat,
                   int width, int height, const std::vector<std::uint8_t>& bgra) {
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) return false;

    hr = stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (FAILED(hr) || !encoder) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr) || !frame) return false;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return false;

    hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    if (FAILED(hr)) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return false;

    const UINT stride = static_cast<UINT>(width * 4);
    const UINT cbBufferSize = static_cast<UINT>(bgra.size());
    hr = frame->WritePixels(static_cast<UINT>(height), stride, cbBufferSize,
                            const_cast<BYTE*>(bgra.data()));
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    return SUCCEEDED(hr);
}

// Generate simple colored pattern bitmap
std::vector<std::uint8_t> MakeColorPattern(int width, int height, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    std::vector<std::uint8_t> data(static_cast<size_t>(width * height * 4));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>((y * width + x) * 4);
            data[idx + 0] = b;
            data[idx + 1] = g;
            data[idx + 2] = r;
            data[idx + 3] = a;
        }
    }
    return data;
}

} // namespace

FASTPDF_TEST(image_to_pdf_multi_page_roundtrip_verification) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    fastpdf::pdfium::PdfiumLibrary library;
    if (!library.isAvailable()) {
        std::printf("fastpdf_image_to_pdf_integration_tests: PDFium is not available, skipping.\n");
        return;
    }

    ScopedTempDir tempDir;
    const std::wstring dir = tempDir.Path();

    // Image 1: 400 x 200 (Landscape) PNG with alpha
    const std::wstring img1Path = dir + L"\\img1_landscape.png";
    std::vector<std::uint8_t> px1 = MakeColorPattern(400, 200, 255, 0, 0, 200); // semi-transparent blue
    FASTPDF_CHECK(SaveTestImage(img1Path, GUID_ContainerFormatPng, 400, 200, px1));

    // Image 2: 200 x 400 (Portrait) BMP opaque
    const std::wstring img2Path = dir + L"\\img2_portrait.bmp";
    std::vector<std::uint8_t> px2 = MakeColorPattern(200, 400, 0, 255, 0, 255); // opaque green
    FASTPDF_CHECK(SaveTestImage(img2Path, GUID_ContainerFormatBmp, 200, 400, px2));

    // Image 3: 300 x 300 (Square -> Portrait) JPEG
    const std::wstring img3Path = dir + L"\\img3_square.jpg";
    std::vector<std::uint8_t> px3 = MakeColorPattern(300, 300, 0, 0, 255, 255); // opaque red
    FASTPDF_CHECK(SaveTestImage(img3Path, GUID_ContainerFormatJpeg, 300, 300, px3));

    const std::wstring outPdfPath = dir + L"\\combined_output.pdf";
    std::atomic<bool> cancelFlag{false};

    std::vector<std::wstring> imagePaths = { img1Path, img2Path, img3Path };
    int progressCalls = 0;

    auto result = fastpdf::app::convert::CreatePdfFromImages(
        imagePaths, outPdfPath, cancelFlag,
        [&progressCalls](const fastpdf::app::convert::ImageToPdfProgress& /*prog*/) -> bool {
            ++progressCalls;
            return true;
        });

    if (result.status != fastpdf::app::convert::ImageToPdfStatus::Success) {
        std::wprintf(L"image_to_pdf_multi_page_roundtrip_verification failed status=%d msg=%s failedFile=%s\n",
            static_cast<int>(result.status), result.userErrorMessage.c_str(), result.failedFile.c_str());
    }

    FASTPDF_CHECK(result.status == fastpdf::app::convert::ImageToPdfStatus::Success);
    FASTPDF_CHECK_EQ(result.completedImages, 3);
    FASTPDF_CHECK_EQ(result.totalImages, 3);
    FASTPDF_CHECK(progressCalls >= 3);
    FASTPDF_CHECK(std::filesystem::exists(outPdfPath));
    FASTPDF_CHECK(std::filesystem::file_size(outPdfPath) > 500);

    // Reopen generated PDF via PdfDocument to inspect pages, dimensions, orientations
    fastpdf::pdfium::OpenError openError = fastpdf::pdfium::OpenError::None;
    auto source = fastpdf::pdfium::PdfSource::Load(outPdfPath, openError);
    FASTPDF_CHECK(source.isValid());
    FASTPDF_CHECK(openError == fastpdf::pdfium::OpenError::None);

    fastpdf::pdfium::PdfDocument doc(source);
    FASTPDF_CHECK(doc.isOpen());
    FASTPDF_CHECK_EQ(doc.pageCount(), 3);

    // Page 0 (from Image 1: 400x200 landscape)
    double w0 = 0.0, h0 = 0.0;
    FASTPDF_CHECK(doc.pageSize(0, w0, h0));
    // Must be A4 Landscape: width ~ 841.89 pt, height ~ 595.28 pt
    FASTPDF_CHECK(w0 > h0); // Landscape
    FASTPDF_CHECK(std::abs(w0 - fastpdf::app::convert::kA4HeightPoints) < 1.0);
    FASTPDF_CHECK(std::abs(h0 - fastpdf::app::convert::kA4WidthPoints) < 1.0);

    // Page 1 (from Image 2: 200x400 portrait)
    double w1 = 0.0, h1 = 0.0;
    FASTPDF_CHECK(doc.pageSize(1, w1, h1));
    // Must be A4 Portrait: width ~ 595.28 pt, height ~ 841.89 pt
    FASTPDF_CHECK(w1 < h1); // Portrait
    FASTPDF_CHECK(std::abs(w1 - fastpdf::app::convert::kA4WidthPoints) < 1.0);
    FASTPDF_CHECK(std::abs(h1 - fastpdf::app::convert::kA4HeightPoints) < 1.0);

    // Page 2 (from Image 3: 300x300 square -> portrait)
    double w2 = 0.0, h2 = 0.0;
    FASTPDF_CHECK(doc.pageSize(2, w2, h2));
    FASTPDF_CHECK(w2 < h2); // Portrait
    FASTPDF_CHECK(std::abs(w2 - fastpdf::app::convert::kA4WidthPoints) < 1.0);
    FASTPDF_CHECK(std::abs(h2 - fastpdf::app::convert::kA4HeightPoints) < 1.0);

    // Verify rasterization of rendered page from generated PDF works properly
    fastpdf::renderer::Bitmap rendered;
    double duration = 0.0;
    FASTPDF_CHECK(doc.renderPage(0, 420, 297, 0, rendered, duration));
    FASTPDF_CHECK(!rendered.empty());
    FASTPDF_CHECK_EQ(rendered.width, 420);
    FASTPDF_CHECK_EQ(rendered.height, 297);
}

FASTPDF_TEST(image_to_pdf_never_overwrites_existing_output) {
    fastpdf::platform::win::ComInitializer com;
    ScopedTempDir tempDir;
    const std::wstring dir = tempDir.Path();

    const std::wstring imgPath = dir + L"\\test.png";
    std::vector<std::uint8_t> px = MakeColorPattern(100, 100, 128, 128, 128, 255);
    FASTPDF_CHECK(SaveTestImage(imgPath, GUID_ContainerFormatPng, 100, 100, px));

    const std::wstring outPdfPath = dir + L"\\existing.pdf";
    {
        std::ofstream existing(outPdfPath);
        existing << "PRE-EXISTING CONTENT";
        existing.close();
    }

    std::atomic<bool> cancelFlag{false};
    auto result = fastpdf::app::convert::CreatePdfFromImages(
        { imgPath }, outPdfPath, cancelFlag, nullptr);

    FASTPDF_CHECK(result.status == fastpdf::app::convert::ImageToPdfStatus::OutputFileAlreadyExists);
    FASTPDF_CHECK(result.failedFile == outPdfPath);

    // File must NOT have been modified
    std::ifstream in(outPdfPath);
    std::string content;
    in >> content;
    FASTPDF_CHECK_EQ(content, std::string("PRE-EXISTING"));
}

FASTPDF_TEST(image_to_pdf_cancellation_and_cleanup) {
    fastpdf::platform::win::ComInitializer com;
    ScopedTempDir tempDir;
    const std::wstring dir = tempDir.Path();

    const std::wstring imgPath = dir + L"\\test.png";
    std::vector<std::uint8_t> px = MakeColorPattern(100, 100, 128, 128, 128, 255);
    FASTPDF_CHECK(SaveTestImage(imgPath, GUID_ContainerFormatPng, 100, 100, px));

    const std::wstring outPdfPath = dir + L"\\cancelled.pdf";
    std::atomic<bool> cancelFlag{true}; // Immediately cancelled

    auto result = fastpdf::app::convert::CreatePdfFromImages(
        { imgPath }, outPdfPath, cancelFlag, nullptr);

    FASTPDF_CHECK(result.status == fastpdf::app::convert::ImageToPdfStatus::Cancelled);
    FASTPDF_CHECK(!std::filesystem::exists(outPdfPath));

    // Verify no stray temp files left behind in directory
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        FASTPDF_CHECK(entry.path() == std::filesystem::path(imgPath));
    }
}

int main() {
    return fastpdf::test::RunAll();
}
