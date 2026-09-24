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
