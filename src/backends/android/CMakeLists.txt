# Android Backend for KWin

set(ANDROID_BACKEND_SOURCES
    android_backend.cpp
    android_output.cpp
    android_egl_backend.cpp
)

add_library(KWinAndroidBackend MODULE ${ANDROID_BACKEND_SOURCES})

target_link_libraries(KWinAndroidBackend
    kwin
    Qt::Core
    Qt::Gui
    EGL::EGL
    epoxy::epoxy
)

# Link termux-render library
find_library(TERMUX_RENDER_LIB NAMES termux-render)

if(TERMUX_RENDER_LIB)
    message(STATUS "Found termux-render library: ${TERMUX_RENDER_LIB}")
    target_link_libraries(KWinAndroidBackend ${TERMUX_RENDER_LIB})
else()
    message(WARNING "termux-render library not found, assuming it will be available at runtime")
    target_link_libraries(KWinAndroidBackend termux-render)
endif()

install(TARGETS KWinAndroidBackend
    LIBRARY DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kwin.backends
)
