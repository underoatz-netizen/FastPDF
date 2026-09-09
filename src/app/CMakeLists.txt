set(FASTPDF_RC_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(FASTPDF_RC_VERSION_STR "${PROJECT_VERSION}")
# Absolute paths so the generated app.rc (placed in the binary dir) resolves the
# icon asset and shared resource header from the source tree.
set(FASTPDF_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/fastpdf.ico")
set(FASTPDF_RESOURCE_H "${CMAKE_CURRENT_SOURCE_DIR}/resource.h")
configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

add_executable(FastPDF WIN32
    main.cpp
    AppWindow.h
    AppWindow.cpp
    CommandLineOpen.h
    CommandLineOpen.cpp
    Benchmark.h
    Benchmark.cpp
    RenderWorker.h
    RenderWorker.cpp
    DropRouting.h
    DropRouting.cpp
    Presentation.h
    Presentation.cpp
    SearchPanelRouting.h
    SearchPanelRouting.cpp
    ScreenshotGeometry.h
    ScreenshotCompositor.h
    ScreenshotCompositor.cpp
    ScreenshotClipboard.h
    ScreenshotClipboard.cpp
    PdfToPngOptions.h
    PdfToPngOptions.cpp
    PdfToPngWorker.h
    PdfToPngWorker.cpp
    PdfToPngDialog.h
    PdfToPngDialog.cpp
    ImagePlacement.h
    ImagePlacement.cpp
    ImageToPdfWorker.h
    ImageToPdfWorker.cpp
    ImageToPdfDialog.h
    ImageToPdfDialog.cpp
    PrintCommon.h
    PrintCommon.cpp
    PrintWorker.h
    PrintWorker.cpp
    PrintDialog.h
    PrintDialog.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc")

target_include_directories(FastPDF PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

if(FASTPDF_WITH_PDFIUM)
    target_link_libraries(FastPDF PRIVATE PDFium::pdfium)
    target_compile_definitions(FastPDF PRIVATE FASTPDF_WITH_PDFIUM=1)
endif()

# The UI boundary owns the HWND and all Direct2D device resources; PDFium
# stays behind fastpdf_pdfium. The render pipeline types (RenderKey, LRU CPU
# cache, priority scheduler, idle state) come from fastpdf_renderer.
target_link_libraries(FastPDF PRIVATE
    fastpdf_core
    fastpdf_renderer
    fastpdf_pdfium
    fastpdf_platform_win)

if(FASTPDF_WITH_PDFIUM)
    fastpdf_copy_pdfium_runtime(FastPDF)
endif()

# Third-party notices are copied next to the executable for packaging.
add_custom_target(fastpdf_third_party_notices ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "$<TARGET_FILE_DIR:FastPDF>/THIRD_PARTY_NOTICES.txt"
    COMMENT "Copying third-party notices next to FastPDF.exe"
    VERBATIM)