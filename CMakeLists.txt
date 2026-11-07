cmake_minimum_required(VERSION 3.21)

project(PVForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)

qt_standard_project_setup()

add_executable(pvforge
    src/main.cpp
    src/core/Project.cpp
    src/core/History.cpp
    src/core/Settings.cpp
    src/core/Lang.cpp
    src/core/VectorShape.cpp
    src/core/Camera.cpp
    src/core/Commands.cpp
    src/media/VideoDecoder.cpp
    src/media/MediaImporter.cpp
    src/render/SceneRenderer.cpp
    src/render/CameraRenderer.cpp
    src/app/AppIcon.cpp
    src/app/SettingsDialog.cpp
    src/app/MainWindow.cpp
    src/app/WelcomeWindow.cpp
    src/app/NewProjectDialog.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/AssetLibraryPanel.cpp
    src/ui/CameraMonitor.cpp
    src/ui/CollapsibleSection.cpp
    src/ui/EnvironmentPanel.cpp
    src/ui/ProjectPropertiesPanel.cpp
    src/ui/TimelinePanel.cpp
    src/ui/Viewport.cpp
    src/ui/WorkbenchPanel.cpp
)

qt_add_resources(pvforge "lang"
    PREFIX "/"
    BASE "assets"
    FILES
        assets/lang/zh_CN.json
        assets/lang/en_US.json
)

target_include_directories(pvforge PRIVATE src)
target_link_libraries(pvforge PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent
    PkgConfig::FFMPEG
)

install(TARGETS pvforge RUNTIME DESTINATION bin)
