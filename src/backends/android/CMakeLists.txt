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
# Note: Both kwin and AndroidBackend link termux-render, but this should be safe
# as they will share the same library instance in the process
target_link_libraries(KWinAndroidBackend termux-render)

install(TARGETS KWinAndroidBackend
    LIBRARY DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kwin.backends
)
