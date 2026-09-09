# Focused tests that run WITHOUT PDFium:
#   fastpdf_core_tests         - pure, platform-independent core tests
#   fastpdf_platform_win_tests - Win32-dependent, PDFium-free platform tests
#   fastpdf_structure_tests    - build configuration/structure (PE x64 check)
# The PDFium smoke test lives in src/pdfium and is only built when a valid
# pinned PDFium artifact is supplied.

add_executable(fastpdf_core_tests core_tests.cpp test_harness.h)
target_link_libraries(fastpdf_core_tests PRIVATE fastpdf_core)
add_test(NAME fastpdf_core_tests COMMAND fastpdf_core_tests)

# Pure continuous-page layout / viewport / anchor math (Phase 2 viewer core).
add_executable(fastpdf_layout_tests layout_tests.cpp test_harness.h)
target_link_libraries(fastpdf_layout_tests PRIVATE fastpdf_core)
add_test(NAME fastpdf_layout_tests COMMAND fastpdf_layout_tests)

# Phase-3 render pipeline primitives (RenderKey/Quality, byte-bounded LRU CPU
# cache, bounded coalescing priority scheduler, preview/final idle state).
# PDFium-free and Windows-free.
add_executable(fastpdf_renderer_tests renderer_tests.cpp test_harness.h)
target_link_libraries(fastpdf_renderer_tests PRIVATE fastpdf_renderer)
add_test(NAME fastpdf_renderer_tests COMMAND fastpdf_renderer_tests)

add_executable(fastpdf_platform_win_tests platform_win_tests.cpp test_harness.h)
target_link_libraries(fastpdf_platform_win_tests PRIVATE fastpdf_platform_win)
add_test(NAME fastpdf_platform_win_tests COMMAND fastpdf_platform_win_tests)

# Drag-and-drop routing predicate (single PDF opens; non-PDF ignored).
add_executable(fastpdf_drop_routing_tests
    drop_routing_tests.cpp test_harness.h
    "${CMAKE_SOURCE_DIR}/src/app/DropRouting.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/DropRouting.h")
target_include_directories(fastpdf_drop_routing_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app")
add_test(NAME fastpdf_drop_routing_tests COMMAND fastpdf_drop_routing_tests)

# Phase-4 presentation decision logic (keyboard shortcut mapping, page-advance
# clamping, and the pre-render page set). PDFium-free and Windows-free.
add_executable(fastpdf_presentation_tests
    presentation_tests.cpp test_harness.h
    "${CMAKE_SOURCE_DIR}/src/app/Presentation.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/Presentation.h")
target_include_directories(fastpdf_presentation_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app")
add_test(NAME fastpdf_presentation_tests COMMAND fastpdf_presentation_tests)

# Phase-5 screenshot geometry and compositor tests (PDFium-free).
add_executable(fastpdf_screenshot_tests
    screenshot_tests.cpp test_harness.h
    "${CMAKE_SOURCE_DIR}/src/app/ScreenshotGeometry.h"
    "${CMAKE_SOURCE_DIR}/src/app/ScreenshotCompositor.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/ScreenshotCompositor.h")
target_include_directories(fastpdf_screenshot_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app")
target_link_libraries(fastpdf_screenshot_tests PRIVATE fastpdf_renderer)
add_test(NAME fastpdf_screenshot_tests COMMAND fastpdf_screenshot_tests)

# Phase-5 WIC PNG roundtrip & clipboard safety tests (PDFium-free).
add_executable(fastpdf_screenshot_clipboard_tests
    screenshot_clipboard_tests.cpp test_harness.h
    "${CMAKE_SOURCE_DIR}/src/app/ScreenshotClipboard.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/ScreenshotClipboard.h")
target_include_directories(fastpdf_screenshot_clipboard_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app")
target_link_libraries(fastpdf_screenshot_clipboard_tests PRIVATE
    fastpdf_renderer
    fastpdf_platform_win)
add_test(NAME fastpdf_screenshot_clipboard_tests COMMAND fastpdf_screenshot_clipboard_tests)

add_executable(fastpdf_structure_tests structure_tests.cpp test_harness.h)
target_compile_definitions(fastpdf_structure_tests PRIVATE
    FASTPDF_EXE_PATH="$<TARGET_FILE:FastPDF>")
add_test(NAME fastpdf_structure_tests COMMAND fastpdf_structure_tests)

# Phase-6A PDF to PNG conversion options and naming tests (PDFium-free).
add_executable(fastpdf_conversion_options_tests
    conversion_options_tests.cpp test_harness.h
    "${CMAKE_SOURCE_DIR}/src/app/PdfToPngOptions.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/PdfToPngOptions.h")
target_include_directories(fastpdf_conversion_options_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app")
add_test(NAME fastpdf_conversion_options_tests COMMAND fastpdf_conversion_options_tests)

# Phase-6B Image-to-PDF placement, auto-orientation, and routing tests (PDFium-free).
add_executable(fastpdf_image_to_pdf_placement_tests
    image_to_pdf_placement_tests.cpp test_harness.h
    "${CMAKE_SOURCE_DIR}/src/app/ImagePlacement.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/ImagePlacement.h"
    "${CMAKE_SOURCE_DIR}/src/app/DropRouting.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/DropRouting.h")
target_include_directories(fastpdf_image_to_pdf_placement_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app")
add_test(NAME fastpdf_image_to_pdf_placement_tests COMMAND fastpdf_image_to_pdf_placement_tests)

# Phase-7 PrintLayout unit tests (layout, ranges, auto-orientation, margins, copies)
add_executable(fastpdf_print_layout_tests
    print_layout_tests.cpp test_harness.h)
target_link_libraries(fastpdf_print_layout_tests PRIVATE fastpdf_core)
add_test(NAME fastpdf_print_layout_tests COMMAND fastpdf_print_layout_tests)

# Phase-7 Print integration tests (requires PDFium & Windows printing)
if(FASTPDF_WITH_PDFIUM)
    add_executable(fastpdf_print_integration_tests
        print_integration_tests.cpp test_harness.h
        "${CMAKE_SOURCE_DIR}/src/app/PrintCommon.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/PrintCommon.h"
        "${CMAKE_SOURCE_DIR}/src/app/PrintWorker.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/PrintWorker.h")
    target_include_directories(fastpdf_print_integration_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/app")
    target_link_libraries(fastpdf_print_integration_tests PRIVATE
        fastpdf_core
        fastpdf_renderer
        fastpdf_pdfium
        fastpdf_platform_win
        PDFium::pdfium)
    fastpdf_copy_pdfium_runtime(fastpdf_print_integration_tests)
    add_test(NAME fastpdf_print_integration_tests COMMAND fastpdf_print_integration_tests)
endif()

# Phase-8 Search & Recent files pure unit tests (core parsing, ordering, limits)
add_executable(fastpdf_search_recent_tests
    search_recent_tests.cpp test_harness.h)
target_link_libraries(fastpdf_search_recent_tests PRIVATE fastpdf_core)
add_test(NAME fastpdf_search_recent_tests COMMAND fastpdf_search_recent_tests)

# Phase-8 Search PDFium integration tests (requires PDFium)
if(FASTPDF_WITH_PDFIUM)
    add_executable(fastpdf_search_pdfium_integration_tests
        search_pdfium_integration_tests.cpp test_harness.h)
    target_link_libraries(fastpdf_search_pdfium_integration_tests PRIVATE
        fastpdf_core
        fastpdf_pdfium
        PDFium::pdfium)
    fastpdf_copy_pdfium_runtime(fastpdf_search_pdfium_integration_tests)
    add_test(NAME fastpdf_search_pdfium_integration_tests COMMAND fastpdf_search_pdfium_integration_tests)
endif()