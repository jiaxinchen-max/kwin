target_sources(kwin PRIVATE
    android_backend.cpp
    android_output.cpp
    android_egl_backend.cpp
    android_qpainter_backend.cpp
)

find_library(TERMUX_RENDER_LIB NAMES termux-render REQUIRED)
add_library(KWinTermuxRender INTERFACE)
target_link_libraries(KWinTermuxRender INTERFACE ${TERMUX_RENDER_LIB})
target_link_libraries(kwin PRIVATE KWinTermuxRender)

# Install Plasma startup script
install(PROGRAMS start-plasma.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME start-plasma-termux
)
