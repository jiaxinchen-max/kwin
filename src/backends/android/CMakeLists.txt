# Android Backend for KWin

set(ANDROID_BACKEND_SOURCES
    android_backend.cpp
    android_output.cpp
    android_egl_backend.cpp
    android_qpainter_backend.cpp
)

# Create Android backend as a MODULE library
add_library(KWinAndroidBackend MODULE ${ANDROID_BACKEND_SOURCES})

target_link_libraries(KWinAndroidBackend
    kwin
    Qt::Core
    Qt::Gui
    EGL::EGL
    epoxy::epoxy
    dl
)

# Link termux-render library
find_library(TERMUX_RENDER_LIB NAMES termux-render)

if(TERMUX_RENDER_LIB)
    message(STATUS "Found termux-render library: ${TERMUX_RENDER_LIB}")
    target_link_libraries(KWinAndroidBackend ${TERMUX_RENDER_LIB})
else()
    message(FATAL_ERROR "termux-render library not found")
endif()

# termux-render headers are in system include path

install(TARGETS KWinAndroidBackend
    LIBRARY DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kwin.backends
)
