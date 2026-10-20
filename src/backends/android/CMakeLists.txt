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

# One-command launcher for the GPU-accelerated container Plasma desktop, plus
# the in-container session script it binds into the container.
install(PROGRAMS start-plasma-container.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME start-plasma-container
)

install(PROGRAMS ${CMAKE_SOURCE_DIR}/packaging/container/plasma-container-session.sh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/android
)

install(PROGRAMS android-rendering-env.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME kwin-android-rendering-env
)

install(PROGRAMS start-kwin-x11-zink.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME kwin-x11-zink
)

install(PROGRAMS kwin-glxgears-test.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME kwin-glxgears-test
)

install(PROGRAMS kwin-glxgears-zink-test.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME kwin-glxgears-zink-test
)

install(PROGRAMS kwin-android-detect.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME kwin-android-detect
)
