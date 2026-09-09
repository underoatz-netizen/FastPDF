// fastpdf_pdfium_render_tests - exercises the Phase-1 open+render path
// (OpenAndRenderFirstPage) against deterministically generated, minimal,
// non-sensitive PDF fixtures. Requires a valid pinned PDFium artifact
// (FASTPDF_WITH_PDFIUM=ON). Verifies that a PDF opens, reports its page
// count, and renders a first-page bitmap whose dimensions match the expected
// fit-to-box result, and that the missing / corrupted / unreadable /
// password-required error paths map onto the user-facing OpenError outcomes.

#include <fastpdf/pdfium/PdfDocument.h>
#include <fastpdf/pdfium/PdfiumLibrary.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "test_harness.h"

namespace {

using fastpdf::pdfium::Bitmap;
using fastpdf::pdfium::DocumentInfoRequest;
using fastpdf::pdfium::DocumentInfoResult;
using fastpdf::pdfium::OpenAndRenderFirstPage;
using fastpdf::pdfium::OpenDocumentInfo;
using fastpdf::pdfium::OpenError;
using fastpdf::pdfium::PageRenderRequest;
using fastpdf::pdfium::PageRenderResult;
using fastpdf::pdfium::PdfDocument;
using fastpdf::pdfium::PdfSource;
using fastpdf::pdfium::RenderPage;
using fastpdf::pdfium::RenderRequest;
using fastpdf::pdfium::RenderResult;

// Builds a minimal, valid single-page PDF (612x792 pt, US Letter) with a
// correct xref table. Deterministic and non-sensitive: it contains no text
// or content stream beyond an empty page.
std::vector<std::uint8_t> MakeMinimalPdf() {
    const std::string objects[] = {
        // 1: Catalog
        "<< /Type /Catalog /Pages 2 0 R >>",
        // 2: Pages
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        // 3: Page
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << >> /Contents 4 0 R >>",
        // 4: Empty content stream
        "<< /Length 0 >>\nstream\nendstream",
    };

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    offsets.reserve(std::size(objects) + 1);
    offsets.push_back(0);  // placeholder for object 0 (unused)

    for (size_t i = 0; i < std::size(objects); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const size_t xrefOffset = pdf.size();
    pdf += "xref\n0 " + std::to_string(std::size(objects) + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t i = 1; i <= std::size(objects); ++i) {
        char entry[32]{};
        std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", offsets[i]);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(std::size(objects) + 1) +
           " /Root 1 0 R >>\n";
    pdf += "startxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";

    return std::vector<std::uint8_t>(pdf.begin(), pdf.end());
}

// Builds a minimal, valid multi-page PDF with MIXED page sizes (portrait,
// landscape, small, large) so the continuous layout and per-page dimension
// reporting can be exercised against real PDFium. Deterministic and
// non-sensitive: empty pages, no content streams.
std::vector<std::uint8_t> MakeMixedSizePdf() {
    // (width, height) in points for each page.
    const std::pair<double, double> sizes[] = {
        {612.0, 792.0},    // US Letter portrait
        {792.0, 612.0},    // US Letter landscape
        {300.0, 400.0},    // small
        {1224.0, 1584.0},  // large (2x letter)
    };
    const size_t pageCount = std::size(sizes);

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    offsets.reserve(pageCount + 3);
    offsets.push_back(0);  // placeholder for object 0

    // Object 1: Catalog.
    offsets.push_back(pdf.size());
    pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    // Object 2: Pages tree.
    offsets.push_back(pdf.size());
    std::string kids = "[";
    for (size_t i = 0; i < pageCount; ++i) {
        kids += std::to_string(3 + i) + " 0 R ";
    }
    kids += "]";
    pdf += "2 0 obj\n<< /Type /Pages /Kids " + kids + " /Count " +
           std::to_string(pageCount) + " >>\nendobj\n";

    // Objects 3..3+pageCount-1: Page objects with distinct MediaBoxes.
    for (size_t i = 0; i < pageCount; ++i) {
        offsets.push_back(pdf.size());
        char box[64]{};
        std::snprintf(box, sizeof(box), "[0 0 %.0f %.0f]", sizes[i].first,
                      sizes[i].second);
        pdf += std::to_string(3 + i) + " 0 obj\n<< /Type /Page /Parent 2 0 R " +
               "/MediaBox " + box + " /Resources << >> >>\nendobj\n";
    }

    const size_t xrefOffset = pdf.size();
    pdf += "xref\n0 " + std::to_string(offsets.size()) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t i = 1; i < offsets.size(); ++i) {
        char entry[32]{};
        std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", offsets[i]);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(offsets.size()) +
           " /Root 1 0 R >>\n";
    pdf += "startxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";

    return std::vector<std::uint8_t>(pdf.begin(), pdf.end());
}
std::wstring WriteBytesToTempFile(const std::vector<std::uint8_t>& bytes) {
    wchar_t tempDir[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) {
        return {};
    }
    wchar_t tempFile[MAX_PATH]{};
    if (GetTempFileNameW(tempDir, L"fpdf", 0, tempFile) == 0) {
        return {};
    }
    HANDLE file = CreateFileW(tempFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
              nullptr);
    CloseHandle(file);
    return std::wstring(tempFile);
}

std::wstring WriteFixtureToTempFile() {
    return WriteBytesToTempFile(MakeMinimalPdf());
}

// Creates a fresh temporary directory whose name contains non-ASCII
// characters and writes |bytes| to a non-ASCII-named file inside it. Returns
// the full path to the written file, or an empty string on failure. Proves
// the Unicode-safe open path: the old FPDF_LoadDocument (ANSI/fopen) path
// could not open such a path.
std::wstring WriteFixtureToNonAsciiTempDir(
    const std::vector<std::uint8_t>& bytes) {
    wchar_t baseTempDir[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, baseTempDir) == 0) {
        return {};
    }
    // Non-ASCII directory and file names (Cyrillic + CJK + accented Latin).
    const std::wstring dirName = L"fpdf_тест_目录_é";
    const std::wstring fileName = L"документ_文件_é.pdf";
    const std::wstring dirPath = std::wstring(baseTempDir) + dirName;

    if (!CreateDirectoryW(dirPath.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }

    const std::wstring filePath = dirPath + L"\\" + fileName;
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
              nullptr);
    CloseHandle(file);
    return filePath;
}

// ---------------------------------------------------------------------------
// Encrypted-fixture support (test-local, deterministic, non-sensitive).
//
// To prove the password-required error path against real PDFium we need a
// genuinely encrypted PDF. The Standard security handler V1/R2 (40-bit RC4)
// key derivation uses only MD5 and RC4, so the fixture is built with a
// compact self-contained implementation of both - no new dependencies and no
// network fetch. The password is a fixed test string.
// ---------------------------------------------------------------------------

// Compact one-shot MD5 (RFC 1321). Input is tiny; clarity over speed.
std::array<std::uint8_t, 16> Md5(const std::vector<std::uint8_t>& message) {
    static const auto kTable = [] {
        std::array<std::uint32_t, 64> k{};
        for (int i = 0; i < 64; ++i) {
            k[i] = static_cast<std::uint32_t>(
                std::fabs(std::sin(i + 1.0)) * 4294967296.0);
        }
        return k;
    }();
    static constexpr int kShift[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe,
                  d0 = 0x10325476;

    std::vector<std::uint8_t> msg = message;
    const std::uint64_t bitLength =
        static_cast<std::uint64_t>(message.size()) * 8U;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 0; i < 8; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((bitLength >> (8 * i)) & 0xFF));
    }

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        std::uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = static_cast<std::uint32_t>(msg[offset + 4 * i]) |
                   (static_cast<std::uint32_t>(msg[offset + 4 * i + 1]) << 8) |
                   (static_cast<std::uint32_t>(msg[offset + 4 * i + 2]) << 16) |
                   (static_cast<std::uint32_t>(msg[offset + 4 * i + 3]) << 24);
        }
        std::uint32_t a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            f = f + a + kTable[i] + m[g];
            a = d;
            d = c;
            c = b;
            const int s = kShift[i];
            b = b + ((f << s) | (f >> (32 - s)));
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<std::uint8_t, 16> digest{};
    for (int i = 0; i < 4; ++i) {
        digest[i] = static_cast<std::uint8_t>((a0 >> (8 * i)) & 0xFF);
        digest[4 + i] = static_cast<std::uint8_t>((b0 >> (8 * i)) & 0xFF);
        digest[8 + i] = static_cast<std::uint8_t>((c0 >> (8 * i)) & 0xFF);
        digest[12 + i] = static_cast<std::uint8_t>((d0 >> (8 * i)) & 0xFF);
    }
    return digest;
}

// Plain RC4 (used only for the fixed-size fixture strings below).
std::vector<std::uint8_t> Rc4(const std::uint8_t* key, size_t keyLength,
                              const std::uint8_t* data, size_t length) {
    std::array<std::uint8_t, 256> s{};
    for (int i = 0; i < 256; ++i) {
        s[static_cast<size_t>(i)] = static_cast<std::uint8_t>(i);
    }
    std::uint8_t j = 0;
    for (int i = 0; i < 256; ++i) {
        j = static_cast<std::uint8_t>(
            j + s[static_cast<size_t>(i)] +
            key[static_cast<size_t>(i) % keyLength]);
        std::swap(s[static_cast<size_t>(i)], s[static_cast<size_t>(j)]);
    }
    std::vector<std::uint8_t> out(length);
    std::uint8_t x = 0, y = 0;
    for (size_t k = 0; k < length; ++k) {
        x = static_cast<std::uint8_t>(x + 1);
        y = static_cast<std::uint8_t>(y + s[static_cast<size_t>(x)]);
        std::swap(s[static_cast<size_t>(x)], s[static_cast<size_t>(y)]);
        out[k] = static_cast<std::uint8_t>(
            data[k] ^ s[static_cast<size_t>(
                          (s[static_cast<size_t>(x)] +
                           s[static_cast<size_t>(y)]) &
                          0xFF)]);
    }
    return out;
}

// The PDF 32000-1 Table 210 padding string used by the Standard handler.
std::vector<std::uint8_t> PadPassword(const std::string& password) {
    static constexpr std::uint8_t kPad[32] = {
        0x28, 0xBF, 0xFF, 0xE6, 0xD8, 0x4A, 0xB7, 0x60, 0x8A, 0x8B, 0x9A,
        0x3C, 0x45, 0x2D, 0x9E, 0x29, 0xB1, 0x23, 0x46, 0x31, 0xD3, 0x87,
        0x16, 0x55, 0x9E, 0x23, 0x1C, 0xBC, 0x83, 0x40, 0x9C, 0xE6};
    std::vector<std::uint8_t> padded(std::begin(kPad), std::end(kPad));
    const size_t copyLength =
        std::min<size_t>(password.size(), padded.size());
    std::copy_n(reinterpret_cast<const std::uint8_t*>(password.data()),
                copyLength, padded.begin());
    return padded;
}

std::string ToHex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        hex += kDigits[byte >> 4];
        hex += kDigits[byte & 0x0F];
    }
    return hex;
}

// Builds a minimal, valid, single-page PDF (612x792 pt) encrypted with the
// Standard security handler V1/R2 (40-bit RC4) and a NON-EMPTY user password,
// so opening it without the password must fail with "password required".
// Same structure as MakeMinimalPdf; the only strings (/O, /U) live inside the
// /Encrypt dictionary, which the spec exempts from encryption, and the
// content stream is empty (RC4 over zero bytes is a no-op).
std::vector<std::uint8_t> MakeEncryptedPdf(const std::string& userPassword) {
    const std::vector<std::uint8_t> paddedUser = PadPassword(userPassword);

    // Algorithm 2 (R2): MD5(paddedUser + P little-endian), first 5 bytes.
    // P = -1 (all permissions) -> FF FF FF FF.
    std::vector<std::uint8_t> keyInput = paddedUser;
    keyInput.insert(keyInput.end(), {0xFF, 0xFF, 0xFF, 0xFF});
    const std::array<std::uint8_t, 16> fileDigest = Md5(keyInput);
    const std::uint8_t fileKey[5] = {fileDigest[0], fileDigest[1],
                                     fileDigest[2], fileDigest[3],
                                     fileDigest[4]};

    // Algorithm 4 (R2): U = RC4(fileKey, paddedUser).
    const std::vector<std::uint8_t> uValue =
        Rc4(fileKey, 5, paddedUser.data(), paddedUser.size());

    // Algorithm 3 (R2), owner password == user password:
    // O = RC4(MD5(paddedOwner)[0..4], paddedUser).
    const std::array<std::uint8_t, 16> ownerDigest = Md5(paddedUser);
    const std::uint8_t ownerKey[5] = {ownerDigest[0], ownerDigest[1],
                                      ownerDigest[2], ownerDigest[3],
                                      ownerDigest[4]};
    const std::vector<std::uint8_t> oValue =
        Rc4(ownerKey, 5, paddedUser.data(), paddedUser.size());

    const std::string objects[] = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << >> /Contents 4 0 R >>",
        "<< /Length 0 >>\nstream\nendstream",
        "<< /Filter /Standard /V 1 /R 2 /Length 40 /P -1 /O <" +
            ToHex(oValue) + "> /U <" + ToHex(uValue) + "> >>",
    };

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    offsets.reserve(std::size(objects) + 1);
    offsets.push_back(0);

    for (size_t i = 0; i < std::size(objects); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const size_t xrefOffset = pdf.size();
    pdf += "xref\n0 " + std::to_string(std::size(objects) + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t i = 1; i <= std::size(objects); ++i) {
        char entry[32]{};
        std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", offsets[i]);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(std::size(objects) + 1) +
           " /Root 1 0 R /Encrypt 5 0 R >>\n";
    pdf += "startxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";

    return std::vector<std::uint8_t>(pdf.begin(), pdf.end());
}

} // namespace

FASTPDF_TEST(render_opens_generated_pdf_and_reports_page_count) {
    const std::wstring path = WriteFixtureToTempFile();
    FASTPDF_CHECK(!path.empty());

    RenderRequest request;
    request.path = path;
    request.targetWidth = 800;
    request.targetHeight = 1000;

    const RenderResult result = OpenAndRenderFirstPage(request);
    FASTPDF_CHECK(result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::None));
    FASTPDF_CHECK_EQ(result.pageCount, 1);
    FASTPDF_CHECK(result.openDurationMs >= 0.0);
    FASTPDF_CHECK(result.renderDurationMs >= 0.0);

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(render_opens_pdf_from_non_ascii_path) {
    // A PDF stored in a non-ASCII directory with a non-ASCII filename must
    // open and render. The previous FPDF_LoadDocument (ANSI/fopen) path
    // failed on such paths; the memory-based loader must not.
    const std::wstring path = WriteFixtureToNonAsciiTempDir(MakeMinimalPdf());
    FASTPDF_CHECK(!path.empty());

    RenderRequest request;
    request.path = path;
    request.targetWidth = 800;
    request.targetHeight = 1000;

    const RenderResult result = OpenAndRenderFirstPage(request);
    FASTPDF_CHECK(result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::None));
    FASTPDF_CHECK_EQ(result.pageCount, 1);
    FASTPDF_CHECK(!result.bitmap.data.empty());

    DeleteFileW(path.c_str());
    RemoveDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str());
}

FASTPDF_TEST(render_dimensions_match_fit_to_box) {
    const std::wstring path = WriteFixtureToTempFile();
    FASTPDF_CHECK(!path.empty());

    // Page is 612x792 pt. Fit into 800x1000 px: scale = min(800/612, 1000/792)
    // = min(1.307, 1.263) = 1.263. Rendered size ~ 773 x 1000.
    RenderRequest request;
    request.path = path;
    request.targetWidth = 800;
    request.targetHeight = 1000;

    const RenderResult result = OpenAndRenderFirstPage(request);
    FASTPDF_CHECK(result.ok);
    FASTPDF_CHECK_EQ(result.bitmap.width, 773);
    FASTPDF_CHECK_EQ(result.bitmap.height, 1000);
    FASTPDF_CHECK(result.bitmap.stride >= result.bitmap.width * 4);
    FASTPDF_CHECK_EQ(result.bitmap.data.size(),
                     static_cast<size_t>(result.bitmap.stride) *
                         static_cast<size_t>(result.bitmap.height));

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(render_missing_file_reports_missing) {
    RenderRequest request;
    request.path = L"C:\\definitely\\not\\present\\file.pdf";
    request.targetWidth = 100;
    request.targetHeight = 100;

    const RenderResult result = OpenAndRenderFirstPage(request);
    FASTPDF_CHECK(!result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::MissingFile));
}

FASTPDF_TEST(render_corrupted_file_reports_corrupted) {
    // A file with a PDF header but no parseable structure must map onto the
    // user-facing "corrupted" outcome (never a raw PDFium code).
    const std::string garbage =
        "%PDF-1.4\nthis is not a PDF, it is pure garbage\n";
    const std::vector<std::uint8_t> bytes(garbage.begin(), garbage.end());
    const std::wstring path = WriteBytesToTempFile(bytes);
    FASTPDF_CHECK(!path.empty());

    RenderRequest request;
    request.path = path;
    request.targetWidth = 200;
    request.targetHeight = 200;

    const RenderResult result = OpenAndRenderFirstPage(request);
    FASTPDF_CHECK(!result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::Corrupted));

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(render_locked_file_reports_unreadable) {
    // A file that exists but cannot be opened (held with an exclusive,
    // zero-share handle) must map onto the user-facing "unreadable" outcome.
    const std::wstring path = WriteFixtureToTempFile();
    FASTPDF_CHECK(!path.empty());

    HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0 /* no sharing */, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    FASTPDF_CHECK(lock != INVALID_HANDLE_VALUE);

    RenderRequest request;
    request.path = path;
    request.targetWidth = 200;
    request.targetHeight = 200;

    const RenderResult result = OpenAndRenderFirstPage(request);
    CloseHandle(lock);

    FASTPDF_CHECK(!result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::Unreadable));

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(source_mapping_does_not_lock_file_and_opens_document) {
    // The mapped source must (a) expose the file bytes, (b) not lock the file
    // against other readers/writers/replacers (the old eager read used
    // FILE_SHARE_READ only), and (c) open a working PDFium document.
    const std::wstring path = WriteFixtureToTempFile();
    FASTPDF_CHECK(!path.empty());

    OpenError error = OpenError::None;
    PdfSource source = PdfSource::Load(path, error);
    FASTPDF_CHECK(source.isValid());
    FASTPDF_CHECK_EQ(static_cast<int>(error), static_cast<int>(OpenError::None));
    FASTPDF_CHECK(source.size() > 0);
    FASTPDF_CHECK(source.data() != nullptr);

    // Another handle with write/delete sharing must be able to open the file
    // while the mapping is alive.
    HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE |
                                   FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    FASTPDF_CHECK(probe != INVALID_HANDLE_VALUE);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
    }

    // A document opened from the mapping renders correctly.
    PdfDocument doc(source);
    FASTPDF_CHECK(doc.isOpen());
    FASTPDF_CHECK_EQ(doc.pageCount(), 1);
    double width = 0.0, height = 0.0;
    FASTPDF_CHECK(doc.pageSize(0, width, height));
    FASTPDF_CHECK_EQ(width, 612.0);
    FASTPDF_CHECK_EQ(height, 792.0);

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(render_password_protected_reports_password_required) {
    // Real PDFium must reject the generated V1/R2 40-bit RC4 fixture without
    // the password and report the dedicated PasswordRequired outcome.
    const std::vector<std::uint8_t> bytes = MakeEncryptedPdf("fpdf-test-pw");
    const std::wstring path = WriteBytesToTempFile(bytes);
    FASTPDF_CHECK(!path.empty());

    RenderRequest request;
    request.path = path;
    request.targetWidth = 400;
    request.targetHeight = 400;

    const RenderResult result = OpenAndRenderFirstPage(request);
    FASTPDF_CHECK(!result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::PasswordRequired));
    FASTPDF_CHECK(result.bitmap.data.empty());

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(document_info_reports_page_count_and_dimensions) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    DocumentInfoRequest request;
    request.path = path;
    const DocumentInfoResult result = OpenDocumentInfo(request);
    FASTPDF_CHECK(result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::None));
    FASTPDF_CHECK_EQ(result.pageCount, 4);
    FASTPDF_CHECK_EQ(static_cast<int>(result.pages.size()), 4);
    // Mixed sizes in document order.
    FASTPDF_CHECK_EQ(result.pages[0].widthPoints, 612.0);
    FASTPDF_CHECK_EQ(result.pages[0].heightPoints, 792.0);
    FASTPDF_CHECK_EQ(result.pages[1].widthPoints, 792.0);
    FASTPDF_CHECK_EQ(result.pages[1].heightPoints, 612.0);
    FASTPDF_CHECK_EQ(result.pages[2].widthPoints, 300.0);
    FASTPDF_CHECK_EQ(result.pages[2].heightPoints, 400.0);
    FASTPDF_CHECK_EQ(result.pages[3].widthPoints, 1224.0);
    FASTPDF_CHECK_EQ(result.pages[3].heightPoints, 1584.0);
    FASTPDF_CHECK(result.openDurationMs >= 0.0);

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(document_info_missing_file_reports_missing) {
    DocumentInfoRequest request;
    request.path = L"C:\\definitely\\not\\present\\file.pdf";
    const DocumentInfoResult result = OpenDocumentInfo(request);
    FASTPDF_CHECK(!result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::MissingFile));
}

FASTPDF_TEST(render_page_renders_specific_page_at_exact_size) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    // Render page 2 (the small 300x400 page) at 150x200 px.
    PageRenderRequest request;
    request.path = path;
    request.pageIndex = 2;
    request.width = 150;
    request.height = 200;
    const PageRenderResult result = RenderPage(request);
    FASTPDF_CHECK(result.ok);
    FASTPDF_CHECK_EQ(static_cast<int>(result.error),
                     static_cast<int>(OpenError::None));
    FASTPDF_CHECK_EQ(result.pageIndex, 2);
    FASTPDF_CHECK_EQ(result.bitmap.width, 150);
    FASTPDF_CHECK_EQ(result.bitmap.height, 200);
    FASTPDF_CHECK(result.bitmap.stride >= result.bitmap.width * 4);
    FASTPDF_CHECK_EQ(result.bitmap.data.size(),
                     static_cast<size_t>(result.bitmap.stride) *
                         static_cast<size_t>(result.bitmap.height));
    FASTPDF_CHECK(result.renderDurationMs >= 0.0);

    DeleteFileW(path.c_str());
}

FASTPDF_TEST(render_page_out_of_range_reports_error) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    PageRenderRequest request;
    request.path = path;
    request.pageIndex = 99;  // Out of range (only 4 pages).
    request.width = 100;
    request.height = 100;
    const PageRenderResult result = RenderPage(request);
    FASTPDF_CHECK(!result.ok);
    FASTPDF_CHECK(result.bitmap.data.empty());

    DeleteFileW(path.c_str());
}

int main() {
    // PDFium must be initialized before any document/render call. The RAII
    // adapter owns the process-wide init/shutdown.
    fastpdf::pdfium::PdfiumLibrary library;
    if (!library.isAvailable()) {
        std::printf("fastpdf_pdfium_render_tests: PDFium is not available.\n");
        return 2;
    }
    return fastpdf::test::RunAll();
}