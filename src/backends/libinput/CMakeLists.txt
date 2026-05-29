qt_generate_dbus_interface(device.h org.kde.kwin.InputDevice.xml OPTIONS -A)

add_custom_target(
    KWinInputDBusInterfaces
    ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
    DESTINATION
        ${KDE_INSTALL_DBUSINTERFACEDIR}
)

target_sources(kwin PRIVATE
    connection.cpp
    context.cpp
    device.cpp
    events.cpp
    libinput_logging.cpp
    libinputbackend.cpp
)
# Link termux-render for get_conn_fd() function used in context.cpp
find_path(TERMUX_RENDER_INCLUDE_DIR
    NAMES termux/render/render.h
    HINTS
        ${CMAKE_INSTALL_PREFIX}/include
        /data/data/com.termux/files/usr/include
)
find_library(TERMUX_RENDER_LIB NAMES termux-render REQUIRED)
if (TERMUX_RENDER_INCLUDE_DIR)
    target_include_directories(kwin PRIVATE ${TERMUX_RENDER_INCLUDE_DIR})
endif()
target_link_libraries(kwin PRIVATE ${TERMUX_RENDER_LIB})

target_link_libraries(kwin PRIVATE Libinput::Libinput)
