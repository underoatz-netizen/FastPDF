# FindPDFium.cmake - pinned PDFium integration contract.
#
# Creates the imported target `PDFium::pdfium` from a developer-supplied,
# PINNED PDFium artifact. This project NEVER downloads PDFium and never falls
# back to an unknown system copy. Configuration fails with an actionable
# message when PDFIUM_ROOT is unset or the artifact layout is invalid.
#
# Required layout of $PDFIUM_ROOT (matches the official pdfium-win-x64.tgz
# from the pdfium-binaries releases; see third_party/pdfium/PDFIUM_LOCK.md):
#   <PDFIUM_ROOT>/include/fpdfview.h   (plus sibling headers)
#   <PDFIUM_ROOT>/lib/pdfium.dll.lib   (import library)
#   <PDFIUM_ROOT>/bin/pdfium.dll       (runtime DLL)
#
# Optional enforcement: set the cache variable PDFIUM_EXPECTED_SHA256 to the
# SHA-256 of the pinned pdfium.dll (see scripts/verify_pdfium.ps1). When set,
# configure fails on mismatch.

if(TARGET PDFium::pdfium)
    return()
endif()

set(_pdfium_root "")

# 1. Resolve PDFIUM_ROOT: cache variable takes precedence over environment.
if(DEFINED CACHE{PDFIUM_ROOT} AND NOT PDFIUM_ROOT STREQUAL "")
    set(_pdfium_root "${PDFIUM_ROOT}")
elseif(DEFINED ENV{PDFIUM_ROOT} AND NOT "$ENV{PDFIUM_ROOT}" STREQUAL "")
    set(_pdfium_root "$ENV{PDFIUM_ROOT}")
endif()

if(_pdfium_root STREQUAL "")
    message(FATAL_ERROR
        "PDFIUM_ROOT is not set.\n"
        "FastPDF builds against a PINNED PDFium artifact and never uses an unknown "
        "system copy. To build with PDFium:\n"
        "  1. Obtain the pinned artifact (see third_party/pdfium/PDFIUM_LOCK.md).\n"
        "  2. Verify it (see scripts/verify_pdfium.ps1).\n"
        "  3. Set PDFIUM_ROOT to the extracted artifact directory (contains include/, "
        "lib/, bin/), e.g.:\n"
        "       cmake --preset debug -DPDFIUM_ROOT=C:/deps/pdfium\n"
        "To build the application shell WITHOUT PDFium, configure with "
        "-DFASTPDF_WITH_PDFIUM=OFF.")
endif()

get_filename_component(_pdfium_root "${_pdfium_root}" ABSOLUTE)
set(PDFIUM_ROOT "${_pdfium_root}" CACHE PATH
    "Path to the pinned PDFium artifact (include/, lib/, bin/)" FORCE)

set(_pdfium_include "${_pdfium_root}/include")
set(_pdfium_lib "${_pdfium_root}/lib/pdfium.dll.lib")
set(_pdfium_dll "${_pdfium_root}/bin/pdfium.dll")

set(_pdfium_missing "")
if(NOT EXISTS "${_pdfium_include}/fpdfview.h")
    string(APPEND _pdfium_missing "  - missing header: ${_pdfium_include}/fpdfview.h\n")
endif()
if(NOT EXISTS "${_pdfium_lib}")
    string(APPEND _pdfium_missing "  - missing import library: ${_pdfium_lib}\n")
endif()
if(NOT EXISTS "${_pdfium_dll}")
    string(APPEND _pdfium_missing "  - missing runtime DLL: ${_pdfium_dll}\n")
endif()

if(NOT _pdfium_missing STREQUAL "")
    message(FATAL_ERROR
        "PDFIUM_ROOT '${_pdfium_root}' does not contain a valid pinned PDFium "
        "artifact.\n"
        "Expected layout (matches the official pdfium-win-x64.tgz):\n"
        "  <PDFIUM_ROOT>/include/fpdfview.h\n"
        "  <PDFIUM_ROOT>/lib/pdfium.dll.lib\n"
        "  <PDFIUM_ROOT>/bin/pdfium.dll\n"
        "Problems found:\n${_pdfium_missing}"
        "See third_party/pdfium/PDFIUM_LOCK.md for the pinned revision and the "
        "verification procedure.")
endif()

# 2. Optional checksum enforcement at configure time.
if(DEFINED CACHE{PDFIUM_EXPECTED_SHA256} AND NOT PDFIUM_EXPECTED_SHA256 STREQUAL "")
    file(SHA256 "${_pdfium_dll}" _pdfium_actual_sha256)
    string(TOLOWER "${PDFIUM_EXPECTED_SHA256}" _pdfium_expected_sha256)
    if(NOT _pdfium_actual_sha256 STREQUAL _pdfium_expected_sha256)
        message(FATAL_ERROR
            "PDFium DLL checksum mismatch - refusing to build against an unverified "
            "artifact.\n"
            "  expected (PDFIUM_EXPECTED_SHA256): ${_pdfium_expected_sha256}\n"
            "  actual:                           ${_pdfium_actual_sha256}\n"
            "See third_party/pdfium/PDFIUM_LOCK.md.")
    endif()
    message(STATUS "PDFium DLL SHA-256 verified: ${_pdfium_actual_sha256}")
else()
    message(WARNING
        "PDFIUM_EXPECTED_SHA256 is not set; the PDFium DLL checksum is NOT enforced "
        "at configure time. Verify the artifact manually with "
        "scripts/verify_pdfium.ps1 before shipping (see "
        "third_party/pdfium/PDFIUM_LOCK.md).")
endif()

# 3. Imported target.
add_library(PDFium::pdfium SHARED IMPORTED)
set_target_properties(PDFium::pdfium PROPERTIES
    IMPORTED_IMPLIB "${_pdfium_lib}"
    IMPORTED_LOCATION "${_pdfium_dll}"
    INTERFACE_INCLUDE_DIRECTORIES "${_pdfium_include}")

# 4. Copies the pinned pdfium.dll next to <target>'s output so the executable
#    and the smoke test can run.
function(fastpdf_copy_pdfium_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_pdfium_dll}"
            "$<TARGET_FILE_DIR:${target}>/pdfium.dll"
        COMMENT "Copying pinned pdfium.dll next to ${target}")
endfunction()

message(STATUS "PDFium: pinned artifact at ${_pdfium_root} (x64)")