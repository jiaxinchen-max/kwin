add_library(kwin_fake_udev STATIC
    libudev/libudev.c
    libudev/libudev-device.c
    libudev/libudev-enumerate.c
    libudev/libudev-hwdb.c
    libudev/libudev-list.c
    libudev/libudev-monitor.c
    libudev/libudev-queue.c
    libudev/libudev-util.c
)
add_library(UDev::UDev ALIAS kwin_fake_udev)
set_property(TARGET kwin_fake_udev PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(kwin_fake_udev
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libudev
)

add_library(kwin_fake_libinput STATIC
    libinput/libinput.c
    libinput/libinput-config.c
    libinput/libinput-mock-all.c
    libinput/evdev.c
    libinput/evdev-mt-touchpad.c
    libinput/path-seat.c
    libinput/udev-seat.c
    libinput/filter.c
    libinput/timer.c
    libinput/termux-input-bridge.c
)
add_library(Libinput::Libinput ALIAS kwin_fake_libinput)
set_property(TARGET kwin_fake_libinput PROPERTY POSITION_INDEPENDENT_CODE ON)
find_path(TERMUX_RENDER_INCLUDE_DIR
    NAMES termux/render/render.h
    HINTS
        ${CMAKE_INSTALL_PREFIX}/include
        /data/data/com.termux/files/usr/include
)
target_include_directories(kwin_fake_libinput
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libinput
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libudev
)
if (TERMUX_RENDER_INCLUDE_DIR)
    target_include_directories(kwin_fake_libinput PRIVATE ${TERMUX_RENDER_INCLUDE_DIR})
endif()
target_link_libraries(kwin_fake_libinput
    PUBLIC
        UDev::UDev
        Threads::Threads
)
