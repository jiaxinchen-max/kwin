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
target_link_libraries(kwin PRIVATE Libinput::Libinput)

# Add Termux support - libinput backend needs termux-render for get_conn_fd()
find_library(TERMUX_RENDER_LIB NAMES termux-render REQUIRED)
message(STATUS "Found termux-render library for libinput: ${TERMUX_RENDER_LIB}")

# Add verbose linking to debug symbol resolution
target_link_options(kwin PRIVATE "LINKER:--verbose")
target_link_options(kwin PRIVATE "LINKER:--no-as-needed")

# Force link the entire library (not just needed symbols)
target_link_libraries(kwin PRIVATE -Wl,--whole-archive ${TERMUX_RENDER_LIB} -Wl,--no-whole-archive)
