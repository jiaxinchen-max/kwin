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

# Note: termux-render is linked by kwin main program, available via transitive dependency

install(TARGETS KWinAndroidBackend
    LIBRARY DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kwin.backends
)
