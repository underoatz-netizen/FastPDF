add_library(fastpdf_core STATIC
    include/fastpdf/core/version.h
    include/fastpdf/core/error.h
    include/fastpdf/core/layout.h
    include/fastpdf/core/print_layout.h
    include/fastpdf/core/search_types.h
    include/fastpdf/core/recent_files.h
    src/version.cpp
    src/error.cpp
    src/layout.cpp
    src/print_layout.cpp
    src/search_types.cpp
    src/recent_files.cpp)

target_include_directories(fastpdf_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# The version string is generated from the CMake project version so the
# compiled value can never drift from the build metadata.
target_compile_definitions(fastpdf_core PUBLIC
    FASTPDF_VERSION_STRING="${PROJECT_VERSION}")

# fastpdf_core is platform-independent by design: no Windows headers, no
# external dependencies.