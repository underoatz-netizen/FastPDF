#include "ImageToPdfWorker.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <memory>

#include <fastpdf/platform/win/ComInitializer.h>
#include "ImagePlacement.h"

// Check if building with PDFium
#if defined(FASTPDF_WITH_PDFIUM) && FASTPDF_WITH_PDFIUM
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include "../pdfium/src/pdfium_gate.h"
#define FASTPDF_HAS_PDFIUM_HEADERS 1
#else
#define FASTPDF_HAS_PDFIUM_HEADERS 0
#endif

namespace fastpdf::app::convert {

namespace {

#if FASTPDF_HAS_PDFIUM_HEADERS
// Custom file writer for FPDF_SaveAsCopy
struct PdfFileWriter : public FPDF_FILEWRITE {
    std::ofstream file;
    bool writeError = false;

    PdfFileWriter() {
        version = 1;
        WriteBlock = &WriteBlockCallback;
    }

    static int WriteBlockCallback(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
        auto* writer = static_cast<PdfFileWriter*>(self);
        if (writer->file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size))) {
            return 1;
        }
        writer->writeError = true;
        return 0;
    }
};
#endif

// RAII helper to delete temporary output file if not committed.
class ScopedTempOutputFile {
public:
    explicit ScopedTempOutputFile(std::wstring path) : path_(std::move(path)) {}
    ~ScopedTempOutputFile() {
        if (!committed_ && !path_.empty()) {
            DeleteFileW(path_.c_str());
        }
    }

    void Commit() noexcept { committed_ = true; }
    const std::wstring& Path() const noexcept { return path_; }

private:
    std::wstring path_;
    bool committed_ = false;
};

} // namespace

ImageToPdfResult CreatePdfFromImages(
    const std::vector<std::wstring>& imagePaths,
    const std::wstring& outputPath,
    const std::atomic<bool>& cancelFlag,
    ImageToPdfProgressCallback onProgress) noexcept {

    ImageToPdfResult result;
    result.totalImages = static_cast<int>(imagePaths.size());

    if (imagePaths.empty()) {
        result.status = ImageToPdfStatus::NoImagesProvided;
        result.userErrorMessage = L"No images were selected for conversion.";
        return result;
    }

    if (outputPath.empty()) {
        result.status = ImageToPdfStatus::WriteFailed;
        result.userErrorMessage = L"Output file path was not specified.";
        return result;
    }

    // Never overwrite an existing destination file.
    if (GetFileAttributesW(outputPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        result.status = ImageToPdfStatus::OutputFileAlreadyExists;
        result.failedFile = outputPath;
        result.userErrorMessage =
            L"The file \"" + std::filesystem::path(outputPath).filename().wstring() +
            L"\" already exists. FastPDF will not overwrite existing files.";
        return result;
    }

#if !FASTPDF_HAS_PDFIUM_HEADERS
    (void)cancelFlag;
    (void)onProgress;
    result.status = ImageToPdfStatus::PdfiumUnavailable;
    result.userErrorMessage = L"PDFium library is not available.";
    return result;
#else

    // Initialize COM on this worker thread for WIC decoding.
    fastpdf::platform::win::ComInitializer com;
    if (!com.initialized()) {
        result.status = ImageToPdfStatus::ImageDecodeFailed;
        result.userErrorMessage = L"Failed to initialize COM for image decoding.";
        return result;
    }

    // Create unique temporary file path in the target directory for atomic write & rename.
    std::filesystem::path finalPath(outputPath);
    std::filesystem::path parentDir = finalPath.parent_path();
    if (parentDir.empty()) {
        parentDir = L".";
    }

    std::wstring tempPathStr;
    {
        wchar_t tempFile[MAX_PATH]{};
        if (GetTempFileNameW(parentDir.wstring().c_str(), L"FPDF", 0, tempFile) == 0) {
            result.status = ImageToPdfStatus::WriteFailed;
            result.userErrorMessage = L"Failed to create temporary output file.";
            return result;
        }
        tempPathStr = tempFile;
    }
    ScopedTempOutputFile tempFileScope(tempPathStr);

    // Check cancellation before creating PDFium document
    if (cancelFlag.load(std::memory_order_relaxed)) {
        result.status = ImageToPdfStatus::Cancelled;
        result.userErrorMessage = L"Conversion was cancelled.";
        return result;
    }

    // Create new PDFium document and build pages
    FPDF_DOCUMENT doc = nullptr;
    {
        fastpdf::pdfium::detail::PdfiumCallGuard gate;
        doc = FPDF_CreateNewDocument();
    }

    if (doc == nullptr) {
        result.status = ImageToPdfStatus::PdfCreationFailed;
        result.userErrorMessage = L"Failed to initialize PDF document engine.";
        return result;
    }

    struct DocScope {
        FPDF_DOCUMENT d = nullptr;
        ~DocScope() {
            if (d != nullptr) {
                fastpdf::pdfium::detail::PdfiumCallGuard gate;
                FPDF_CloseDocument(d);
            }
        }
    } docScope{doc};

    for (int i = 0; i < static_cast<int>(imagePaths.size()); ++i) {
        if (cancelFlag.load(std::memory_order_relaxed)) {
            result.status = ImageToPdfStatus::Cancelled;
            result.userErrorMessage = L"Conversion was cancelled.";
            return result;
        }

        const auto& imgPath = imagePaths[static_cast<size_t>(i)];
        std::wstring fileNameOnly = std::filesystem::path(imgPath).filename().wstring();

        if (onProgress) {
            ImageToPdfProgress prog;
            prog.current = i;
            prog.total = static_cast<int>(imagePaths.size());
            prog.currentFileName = fileNameOnly;
            if (!onProgress(prog)) {
                result.status = ImageToPdfStatus::Cancelled;
                result.userErrorMessage = L"Conversion was cancelled.";
                return result;
            }
        }

        // Decode image using WIC
        DecodedImage decoded;
        if (!DecodeImageFileWithWic(imgPath, decoded)) {
            result.status = ImageToPdfStatus::ImageDecodeFailed;
            result.failedFile = imgPath;
            result.userErrorMessage =
                L"Failed to decode image \"" + fileNameOnly + L"\". The format may be unsupported or the file is corrupted.";
            return result;
        }

        const PagePlacement placement = ComputeImagePlacement(decoded.width, decoded.height);

        // Add page to PDF document using serialized PDFium call gate
        {
            fastpdf::pdfium::detail::PdfiumCallGuard gate;

            FPDF_PAGE page = FPDFPage_New(doc, i, placement.pageWidth, placement.pageHeight);
            if (page == nullptr) {
                result.status = ImageToPdfStatus::PdfCreationFailed;
                result.failedFile = imgPath;
                result.userErrorMessage = L"Failed to create PDF page for image \"" + fileNameOnly + L"\".";
                return result;
            }

            struct PageScope {
                FPDF_PAGE p = nullptr;
                ~PageScope() {
                    if (p != nullptr) {
                        FPDF_ClosePage(p);
                    }
                }
            } pageScope{page};

            FPDF_PAGEOBJECT imgObj = FPDFPageObj_NewImageObj(doc);
            if (imgObj == nullptr) {
                result.status = ImageToPdfStatus::PdfCreationFailed;
                result.failedFile = imgPath;
                result.userErrorMessage = L"Failed to create image object for \"" + fileNameOnly + L"\".";
                return result;
            }

            // Create FPDF_BITMAP from decoded BGRA pixels
            // PDFium bitmap formats: FPDFBitmap_BGRx = 3, FPDFBitmap_BGRA = 4
            const int pdfBitmapFormat = decoded.hasAlpha ? FPDFBitmap_BGRA : FPDFBitmap_BGRx;
            const int stride = decoded.width * 4;

            FPDF_BITMAP fpdfBitmap = FPDFBitmap_CreateEx(
                decoded.width, decoded.height, pdfBitmapFormat,
                decoded.bgraPixels.data(), stride);

            if (fpdfBitmap == nullptr) {
                FPDFPageObj_Destroy(imgObj);
                result.status = ImageToPdfStatus::PdfCreationFailed;
                result.failedFile = imgPath;
                result.userErrorMessage = L"Failed to allocate bitmap memory for \"" + fileNameOnly + L"\".";
                return result;
            }

            // Load bitmap into image object
            FPDF_PAGE loadedPages[1] = { page };
            const FPDF_BOOL setBitmapOk = FPDFImageObj_SetBitmap(loadedPages, 1, imgObj, fpdfBitmap);
            FPDFBitmap_Destroy(fpdfBitmap);

            if (!setBitmapOk) {
                FPDFPageObj_Destroy(imgObj);
                result.status = ImageToPdfStatus::PdfCreationFailed;
                result.failedFile = imgPath;
                result.userErrorMessage = L"Failed to set bitmap for image \"" + fileNameOnly + L"\".";
                return result;
            }

            // Set transform matrix for image object:
            // [ a  c  e ]   [ w  0  x ]
            // [ b  d  f ] = [ 0  h  y ]
            // [ 0  0  1 ]   [ 0  0  1 ]
            if (!FPDFImageObj_SetMatrix(imgObj,
                                        placement.imageWidth, 0.0,
                                        0.0, placement.imageHeight,
                                        placement.imageX, placement.imageY)) {
                FPDFPageObj_Destroy(imgObj);
                result.status = ImageToPdfStatus::PdfCreationFailed;
                result.failedFile = imgPath;
                result.userErrorMessage = L"Failed to set image transform for \"" + fileNameOnly + L"\".";
                return result;
            }

            // Insert image object into page (takes ownership of imgObj on success)
            if (!FPDFPage_InsertObject(page, imgObj)) {
                // imgObj is freed on failure by FPDFPage_InsertObject
                result.status = ImageToPdfStatus::PdfCreationFailed;
                result.failedFile = imgPath;
                result.userErrorMessage = L"Failed to insert image object into page.";
                return result;
            }

            // Generate content stream for page
            if (!FPDFPage_GenerateContent(page)) {
                result.status = ImageToPdfStatus::PdfCreationFailed;
                result.failedFile = imgPath;
                result.userErrorMessage = L"Failed to generate content stream for page.";
                return result;
            }
        }

        result.completedImages = i + 1;
    }

    if (cancelFlag.load(std::memory_order_relaxed)) {
        result.status = ImageToPdfStatus::Cancelled;
        result.userErrorMessage = L"Conversion was cancelled.";
        return result;
    }

    // Save document to temporary output file
    PdfFileWriter writer;
    writer.file.open(tempPathStr, std::ios::binary | std::ios::trunc);
    if (!writer.file.is_open()) {
        result.status = ImageToPdfStatus::WriteFailed;
        result.userErrorMessage = L"Failed to open temporary file for writing.";
        return result;
    }

    FPDF_BOOL saveOk = FALSE;
    {
        fastpdf::pdfium::detail::PdfiumCallGuard gate;
        saveOk = FPDF_SaveAsCopy(doc, &writer, 0);
    }
    writer.file.close();

    if (!saveOk || writer.writeError) {
        result.status = ImageToPdfStatus::WriteFailed;
        result.userErrorMessage = L"Failed to save PDF document to disk.";
        return result;
    }

    // Final check before atomic rename: ensure destination wasn't created in the meantime
    if (GetFileAttributesW(outputPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        result.status = ImageToPdfStatus::OutputFileAlreadyExists;
        result.failedFile = outputPath;
        result.userErrorMessage =
            L"The file \"" + std::filesystem::path(outputPath).filename().wstring() +
            L"\" already exists. FastPDF will not overwrite existing files.";
        return result;
    }

    // Atomic rename from temp file to final destination
    if (!MoveFileExW(tempPathStr.c_str(), outputPath.c_str(), 0)) {
        result.status = ImageToPdfStatus::WriteFailed;
        result.userErrorMessage = L"Failed to rename temporary file to destination PDF.";
        return result;
    }

    tempFileScope.Commit();
    result.status = ImageToPdfStatus::Success;
    return result;
#endif
}

} // namespace fastpdf::app::convert
