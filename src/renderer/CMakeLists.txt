add_library(fastpdf_renderer STATIC
    include/fastpdf/renderer/bitmap.h
    include/fastpdf/renderer/render_key.h
    include/fastpdf/renderer/lru_cache.h
    include/fastpdf/renderer/priority_scheduler.h
    include/fastpdf/renderer/idle.h
    src/bitmap.cpp
    src/render_key.cpp
    src/lru_cache.cpp
    src/priority_scheduler.cpp
    src/idle.cpp)

target_include_directories(fastpdf_renderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# fastpdf_renderer is PDFium-free and Windows-free by design (see README):
# RenderKey/Quality, the byte-bounded LRU CPU cache, the bounded coalescing
# priority scheduler, and the preview/final idle state have no PDFium or
# platform dependencies so they can be unit-tested without either.
