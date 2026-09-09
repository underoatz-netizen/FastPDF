add_library(fastpdf_pdfium STATIC
    include/fastpdf/pdfium/PdfiumLibrary.h
    include/fastpdf/pdfium/PdfDocument.h
    include/fastpdf/pdfium/SearchTypes.h)

target_include_directories(fastpdf_pdfium PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# The public headers re-export fastpdf::renderer::Bitmap, so consumers get the
# renderer include path transitively.
target_link_libraries(fastpdf_pdfium PUBLIC fastpdf_core fastpdf_renderer)

# fastpdf_pdfium is the ONLY boundary allowed to reference raw PDFium headers
# and symbols. With FASTPDF_WITH_PDFIUM=ON it links the pinned imported target
# PDFium::pdfium; with OFF it compiles stubs that report unavailability.
if(FASTPDF_WITH_PDFIUM)
    target_sources(fastpdf_pdfium PRIVATE
        src/PdfiumLibrary.cpp
        src/PdfiumRender.cpp
        src/PdfDocument.cpp
        src/pdfium_gate.cpp)
    target_link_libraries(fastpdf_pdfium PRIVATE PDFium::pdfium)
else()
    target_sources(fastpdf_pdfium PRIVATE
        src/PdfiumLibrary_stub.cpp
        src/PdfiumRender_stub.cpp
        src/PdfDocument_stub.cpp)
endif()

# Smoke test: exercises real PDFium initialization through the RAII adapter.
# Only built when a valid pinned PDFium artifact is supplied.
if(FASTPDF_WITH_PDFIUM AND FASTPDF_BUILD_SMOKE)
    add_executable(fastpdf_pdfium_smoke smoke/main.cpp)
    target_link_libraries(fastpdf_pdfium_smoke PRIVATE fastpdf_pdfium)
    fastpdf_copy_pdfium_runtime(fastpdf_pdfium_smoke)
    add_test(NAME fastpdf_pdfium_smoke COMMAND fastpdf_pdfium_smoke)
    set_tests_properties(fastpdf_pdfium_smoke PROPERTIES LABELS "pdfium")
endif()

# Phase-1 open+render adapter test against a generated PDF fixture. Requires
# the pinned PDFium artifact.
if(FASTPDF_WITH_PDFIUM AND FASTPDF_BUILD_TESTS)
    add_executable(fastpdf_pdfium_render_tests
        tests/pdfium_render_tests.cpp
        "${CMAKE_SOURCE_DIR}/tests/test_harness.h")
    target_include_directories(fastpdf_pdfium_render_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/tests")
    target_link_libraries(fastpdf_pdfium_render_tests PRIVATE fastpdf_pdfium)
    fastpdf_copy_pdfium_runtime(fastpdf_pdfium_render_tests)
    add_test(NAME fastpdf_pdfium_render_tests COMMAND fastpdf_pdfium_render_tests)
    set_tests_properties(fastpdf_pdfium_render_tests PROPERTIES LABELS "pdfium")

    # Phase-2 worker pipeline test: exercises RenderWorker (document info +
    # page render + generation-token staleness) against a real mixed-size
    # fixture through a real window message loop.
    add_executable(fastpdf_worker_pipeline_tests
        "${CMAKE_SOURCE_DIR}/tests/worker_pipeline_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/test_harness.h"
        "${CMAKE_SOURCE_DIR}/src/app/RenderWorker.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/RenderWorker.h")
    target_include_directories(fastpdf_worker_pipeline_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/tests"
        "${CMAKE_SOURCE_DIR}/src/app")
    target_link_libraries(fastpdf_worker_pipeline_tests PRIVATE fastpdf_pdfium)
    fastpdf_copy_pdfium_runtime(fastpdf_worker_pipeline_tests)
    add_test(NAME fastpdf_worker_pipeline_tests COMMAND fastpdf_worker_pipeline_tests)
    set_tests_properties(fastpdf_worker_pipeline_tests PROPERTIES LABELS "pdfium")

    # Phase-4 presentation end-to-end smoke: drives the real RenderWorker +
    # window loop to pre-render the presentation page set (previous/current/
    # next/next+1) for a multi-page fixture, verifying a cached advance is
    # possible and reporting render timing truthfully.
    add_executable(fastpdf_presentation_smoke_tests
        "${CMAKE_SOURCE_DIR}/tests/presentation_smoke_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/test_harness.h"
        "${CMAKE_SOURCE_DIR}/src/app/RenderWorker.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/RenderWorker.h"
        "${CMAKE_SOURCE_DIR}/src/app/Presentation.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/Presentation.h")
    target_include_directories(fastpdf_presentation_smoke_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/tests"
        "${CMAKE_SOURCE_DIR}/src/app")
    target_link_libraries(fastpdf_presentation_smoke_tests PRIVATE fastpdf_pdfium)
    fastpdf_copy_pdfium_runtime(fastpdf_presentation_smoke_tests)
    add_test(NAME fastpdf_presentation_smoke_tests COMMAND fastpdf_presentation_smoke_tests)
    set_tests_properties(fastpdf_presentation_smoke_tests PROPERTIES LABELS "pdfium")

    # Phase-5 screenshot end-to-end smoke: renders multi-page PDF fixture, selects
    # region across pages and viewport, verifies composite offscreen bitmap,
    # clipboard copy, and WIC PNG encoding/decoding.
    add_executable(fastpdf_screenshot_smoke_tests
        "${CMAKE_SOURCE_DIR}/tests/screenshot_smoke_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/test_harness.h"
        "${CMAKE_SOURCE_DIR}/src/app/RenderWorker.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/RenderWorker.h"
        "${CMAKE_SOURCE_DIR}/src/app/ScreenshotGeometry.h"
        "${CMAKE_SOURCE_DIR}/src/app/ScreenshotCompositor.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ScreenshotCompositor.h"
        "${CMAKE_SOURCE_DIR}/src/app/ScreenshotClipboard.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ScreenshotClipboard.h")
    target_include_directories(fastpdf_screenshot_smoke_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/tests"
        "${CMAKE_SOURCE_DIR}/src/app")
    target_link_libraries(fastpdf_screenshot_smoke_tests PRIVATE
        fastpdf_pdfium
        fastpdf_platform_win)
    fastpdf_copy_pdfium_runtime(fastpdf_screenshot_smoke_tests)
    add_test(NAME fastpdf_screenshot_smoke_tests COMMAND fastpdf_screenshot_smoke_tests)
    set_tests_properties(fastpdf_screenshot_smoke_tests PROPERTIES LABELS "pdfium")

    # Phase-6A PDF to PNG conversion integration and cancellation tests:
    # Verifies Standard (150 DPI) / High (300 DPI) conversion, file naming,
    # PNG validity/dimensions via WIC, non-overwriting guarantees, and cancel cleanup.
    add_executable(fastpdf_conversion_integration_tests
        "${CMAKE_SOURCE_DIR}/tests/conversion_integration_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/test_harness.h"
        "${CMAKE_SOURCE_DIR}/src/app/PdfToPngOptions.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/PdfToPngOptions.h"
        "${CMAKE_SOURCE_DIR}/src/app/PdfToPngWorker.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/PdfToPngWorker.h"
        "${CMAKE_SOURCE_DIR}/src/app/ScreenshotClipboard.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ScreenshotClipboard.h")
    target_include_directories(fastpdf_conversion_integration_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/tests"
        "${CMAKE_SOURCE_DIR}/src/app")
    target_link_libraries(fastpdf_conversion_integration_tests PRIVATE
        fastpdf_pdfium
        fastpdf_platform_win)
    fastpdf_copy_pdfium_runtime(fastpdf_conversion_integration_tests)
    add_test(NAME fastpdf_conversion_integration_tests COMMAND fastpdf_conversion_integration_tests)
    set_tests_properties(fastpdf_conversion_integration_tests PROPERTIES LABELS "pdfium")

    # Phase-6B Image to PDF conversion integration and cancellation tests:
    # Verifies multi-page PDF creation from PNG/BMP/JPEG, auto-orientation (portrait/landscape),
    # A4 page dimensions, aspect ratio preservation, no overwriting, and cancellation cleanup.
    add_executable(fastpdf_image_to_pdf_integration_tests
        "${CMAKE_SOURCE_DIR}/tests/image_to_pdf_integration_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/test_harness.h"
        "${CMAKE_SOURCE_DIR}/src/app/ImagePlacement.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ImagePlacement.h"
        "${CMAKE_SOURCE_DIR}/src/app/ImageToPdfWorker.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ImageToPdfWorker.h")
    target_include_directories(fastpdf_image_to_pdf_integration_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/tests"
        "${CMAKE_SOURCE_DIR}/src/app"
        "${CMAKE_SOURCE_DIR}/src/pdfium/src")
    target_compile_definitions(fastpdf_image_to_pdf_integration_tests PRIVATE
        FASTPDF_WITH_PDFIUM=1)
    target_link_libraries(fastpdf_image_to_pdf_integration_tests PRIVATE
        fastpdf_pdfium
        fastpdf_platform_win
        fastpdf_renderer
        PDFium::pdfium)
    fastpdf_copy_pdfium_runtime(fastpdf_image_to_pdf_integration_tests)
    add_test(NAME fastpdf_image_to_pdf_integration_tests COMMAND fastpdf_image_to_pdf_integration_tests)
    set_tests_properties(fastpdf_image_to_pdf_integration_tests PROPERTIES LABELS "pdfium")
endif()