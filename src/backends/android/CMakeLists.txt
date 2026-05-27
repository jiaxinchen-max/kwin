target_sources(kwin PRIVATE
    android_backend.cpp
    android_output.cpp
    android_egl_backend.cpp
    android_qpainter_backend.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TERMUX_RENDER REQUIRED IMPORTED_TARGET termux-render)
target_link_libraries(kwin PRIVATE PkgConfig::TERMUX_RENDER)

# Install Plasma startup script
install(PROGRAMS start-plasma.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME start-plasma
)
