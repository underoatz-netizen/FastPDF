add_library(fastpdf_platform_win STATIC
    include/fastpdf/platform/win/ComInitializer.h
    include/fastpdf/platform/win/Dpi.h
    include/fastpdf/platform/win/Error.h
    include/fastpdf/platform/win/D2d.h
    include/fastpdf/platform/win/PrintTypes.h
    include/fastpdf/platform/win/RecentStore.h
    src/ComInitializer.cpp
    src/Dpi.cpp
    src/Error.cpp
    src/D2d.cpp
    src/RecentStore.cpp)

target_include_directories(fastpdf_platform_win PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(fastpdf_platform_win PUBLIC fastpdf_core)

# Windows platform services. The UI layer owns the actual Direct2D device
# resources; this boundary only provides creation helpers and RAII wrappers.
target_link_libraries(fastpdf_platform_win PUBLIC
    d2d1
    dwrite
    ole32
    winspool)