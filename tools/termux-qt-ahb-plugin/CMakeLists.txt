cmake_minimum_required(VERSION 3.21)

project(termux-qt-ahb-plugin LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGL WaylandClient)
find_package(Qt6GuiPrivate REQUIRED)
find_package(Qt6WaylandClientPrivate REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)

add_library(qt-plugin-termux-ahardware-buffer MODULE
    main.cpp
    termuxahardwarebuffer.cpp
    termuxahardwarebuffer.h
)

target_link_libraries(qt-plugin-termux-ahardware-buffer PRIVATE
    dl
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::WaylandClientPrivate
    Wayland::Client
)

set_target_properties(qt-plugin-termux-ahardware-buffer PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "qt-plugin-termux-ahardware-buffer"
)

install(TARGETS qt-plugin-termux-ahardware-buffer
    LIBRARY DESTINATION lib/qt6/plugins/wayland-graphics-integration-client
)
install(PROGRAMS qt-system-gles-run DESTINATION bin)

add_library(termux-system-egl SHARED system-egl.cpp)
find_library(TERMUX_PLATFORM_NAMESPACE termux-platform-ns REQUIRED)
target_link_libraries(termux-system-egl PRIVATE dl ${TERMUX_PLATFORM_NAMESPACE})
set_target_properties(termux-system-egl PROPERTIES OUTPUT_NAME EGL SOVERSION 1 VERSION 1)
install(TARGETS termux-system-egl LIBRARY DESTINATION lib/qt6/termux-system-egl)

add_executable(qt-client-renderer-probe qt-client-probe.cpp)
target_link_libraries(qt-client-renderer-probe PRIVATE Qt6::Core Qt6::Gui)

set_target_properties(qt-client-renderer-probe PROPERTIES OUTPUT_NAME kwin-qt-client-renderer-probe)
install(TARGETS qt-client-renderer-probe RUNTIME DESTINATION bin)
