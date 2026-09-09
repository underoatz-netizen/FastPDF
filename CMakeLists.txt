cmake_minimum_required(VERSION 3.24)

# FindPDFium.cmake resolves PDFIUM_ROOT itself; opt into the modern
# find_package <PACKAGENAME>_ROOT behavior to avoid a dev warning.
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(FastPDF
    VERSION 0.1.1
    DESCRIPTION "Fast PDF Utility Viewer - persistent async renderer (Handoff 4)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Module path (FindPDFium.cmake lives here).
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Global requirements: x64 only, C++20, MSVC-friendly defaults.
# ---------------------------------------------------------------------------
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "FastPDF requires a 64-bit (x64) build. "
        "Configure with the x64 architecture (e.g. cmake --preset debug).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(FASTPDF_WITH_PDFIUM
    "Build against the pinned PDFium artifact (requires PDFIUM_ROOT)" ON)
option(FASTPDF_BUILD_TESTS "Build the test executables" ON)
option(FASTPDF_BUILD_SMOKE
    "Build the PDFium smoke-test executable (requires FASTPDF_WITH_PDFIUM)" ON)

if(FASTPDF_WITH_PDFIUM)
    find_package(PDFium REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Output layout: build/<preset>/bin/<Config>/
# ---------------------------------------------------------------------------
foreach(kind RUNTIME LIBRARY ARCHIVE)
    set(CMAKE_${kind}_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel")
endforeach()

# ---------------------------------------------------------------------------
# Build boundaries (see README.md "Project layout and boundaries"):
#   fastpdf_core          - platform-independent core (version, error codes)
#   fastpdf_renderer      - PDFium-free render pipeline (RenderKey/Quality,
#                           byte-bounded LRU CPU cache, bounded coalescing
#                           priority scheduler, preview/final idle state)
#   fastpdf_pdfium        - PDFium RAII adapter; the ONLY boundary that may
#                           reference raw PDFium headers/symbols
#   fastpdf_platform_win  - Win32/Direct2D/DirectWrite platform services
#   FastPDF               - application shell; owns the HWND and all
#                           Direct2D device resources
# ---------------------------------------------------------------------------
# enable_testing() must run before add_subdirectory() so that add_test()
# calls inside the subdirectories (e.g. fastpdf_pdfium_smoke) are registered.
if(FASTPDF_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src/core)
add_subdirectory(src/renderer)
add_subdirectory(src/pdfium)
add_subdirectory(src/platform_win)
add_subdirectory(src/app)

if(FASTPDF_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Release packaging target
# ---------------------------------------------------------------------------
add_custom_target(fastpdf_package_release
    COMMAND powershell.exe -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_release.ps1"
        -BuildDir "${CMAKE_CURRENT_BINARY_DIR}"
        -DistDir "${CMAKE_CURRENT_SOURCE_DIR}/dist"
        -PdfiumRoot "${PDFIUM_ROOT}"
    DEPENDS FastPDF fastpdf_third_party_notices
    COMMENT "Assembling FastPDF portable release package into dist/"
    VERBATIM)