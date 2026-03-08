target_sources(kwin PRIVATE
    android_backend.cpp
    android_output.cpp
    android_egl_backend.cpp
    android_qpainter_backend.cpp
)

find_library(TERMUX_RENDER_LIB NAMES termux-render REQUIRED)
target_link_libraries(kwin PRIVATE ${TERMUX_RENDER_LIB})