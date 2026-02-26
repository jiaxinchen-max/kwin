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

# Link termux-wayland library
# Adjust the path to match your termux-wayland installation
find_library(TERMUX_WAYLAND_LIB
    NAMES termux-wayland wayland
    PATHS
        /data/data/com.termux/files/usr/lib
        ${CMAKE_PREFIX_PATH}/lib
)

if(TERMUX_WAYLAND_LIB)
    message(STATUS "Found termux-wayland library: ${TERMUX_WAYLAND_LIB}")
    target_link_libraries(KWinAndroidBackend ${TERMUX_WAYLAND_LIB})
else()
    message(WARNING "termux-wayland library not found, assuming it will be available at runtime")
    # Add library name for linking even if not found during configure
    target_link_libraries(KWinAndroidBackend wayland)
endif()

# Include directories for termux-wayland headers
# Adjust the path to match your termux-wayland installation
target_include_directories(KWinAndroidBackend PRIVATE
    /data/data/com.termux/files/usr/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../termux-display-client/include
)

install(TARGETS KWinAndroidBackend
    LIBRARY DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kwin.backends
)
