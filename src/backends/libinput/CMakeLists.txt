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
# Add Termux support - libinput backend needs termux-render for get_conn_fd()
find_library(TERMUX_RENDER_LIB NAMES termux-render REQUIRED)
message(STATUS "Found termux-render library for libinput: ${TERMUX_RENDER_LIB}")
target_link_libraries(kwin PRIVATE ${TERMUX_RENDER_LIB})

target_link_libraries(kwin PRIVATE Libinput::Libinput)
