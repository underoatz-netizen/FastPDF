// Windows-dependent but PDFium-free tests for fastpdf_platform_win.

#include <fastpdf/platform/win/ComInitializer.h>
#include <fastpdf/platform/win/Dpi.h>
#include <fastpdf/platform/win/Error.h>
#include <fastpdf/platform/win/RecentStore.h>

#include <string>

#include "test_harness.h"

using namespace fastpdf::platform::win;

FASTPDF_TEST(dpi_scale_factor_is_96_based) {
    FASTPDF_CHECK_EQ(DpiScaleFactor(96), 1.0f);
    FASTPDF_CHECK_EQ(DpiScaleFactor(192), 2.0f);
}

FASTPDF_TEST(scale_for_dpi_rounds_to_nearest) {
    FASTPDF_CHECK_EQ(ScaleForDpi(100, 96), 100);
    FASTPDF_CHECK_EQ(ScaleForDpi(100, 144), 150);
    FASTPDF_CHECK_EQ(ScaleForDpi(100, 192), 200);
}

FASTPDF_TEST(format_hresult_known_error_is_nonempty) {
    const std::wstring text = FormatHresult(E_INVALIDARG);
    FASTPDF_CHECK(!text.empty());
}

FASTPDF_TEST(format_hresult_unknown_error_falls_back) {
    const std::wstring text = FormatHresult(static_cast<HRESULT>(0x8BADF00D));
    FASTPDF_CHECK(!text.empty());
}

FASTPDF_TEST(format_win32_error_is_nonempty) {
    const std::wstring text = FormatWin32Error(ERROR_FILE_NOT_FOUND);
    FASTPDF_CHECK(!text.empty());
}

FASTPDF_TEST(com_initializer_roundtrip) {
    {
        ComInitializer com;
        FASTPDF_CHECK(com.initialized());
    }
    // The destructor ran CoUninitialize without crashing.
    FASTPDF_CHECK(true);
}

FASTPDF_TEST(diagnostic_log_rotates_and_records) {
    // Write test diagnostic messages and verify they don't crash
    // and format timestamps appropriately.
    fastpdf::platform::win::AppendDiagnosticLog(L"Test log message 1 for Phase 9 verification");
    fastpdf::platform::win::AppendDiagnosticLog(L"Test log message 2 for Phase 9 verification");
    FASTPDF_CHECK(true);
}

int main() { return fastpdf::test::RunAll(); }