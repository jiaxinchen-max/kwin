target_sources(kwin PRIVATE
    android_backend.cpp
    android_output.cpp
    android_egl_backend.cpp
    android_qpainter_backend.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TERMUX_RENDER REQUIRED termux-render)
target_include_directories(kwin PRIVATE ${TERMUX_RENDER_INCLUDE_DIRS})
target_link_directories(kwin PRIVATE ${TERMUX_RENDER_LIBRARY_DIRS})
target_link_libraries(kwin PRIVATE ${TERMUX_RENDER_LIBRARIES})
target_compile_options(kwin PRIVATE ${TERMUX_RENDER_CFLAGS_OTHER})

# Install Plasma startup script
install(PROGRAMS start-plasma.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME start-plasma
)
