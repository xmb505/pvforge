cmake_minimum_required(VERSION 3.21)

project(PVForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

add_executable(pvforge
    src/main.cpp
    src/core/Project.cpp
    src/app/MainWindow.cpp
    src/ui/ObjectTreePanel.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/TimelinePanel.cpp
    src/ui/Viewport.cpp
)

target_include_directories(pvforge PRIVATE src)
target_link_libraries(pvforge PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

install(TARGETS pvforge RUNTIME DESTINATION bin)
