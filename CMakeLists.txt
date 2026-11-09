cmake_minimum_required(VERSION 3.21)

project(PVForge VERSION 0.2.0 DESCRIPTION "2D music PV editor with a Mine-imator style animation workflow" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Multimedia Network)
# The HTML/CDP renderer only needs QtCore. Prefer the Qt WebSockets module when
# its development package is present; otherwise WsClient implements the small
# RFC 6455 subset CDP needs, so the build works without an extra dependency.
find_package(Qt6 6.5 QUIET COMPONENTS WebSockets)
if(TARGET Qt6::WebSockets)
    message(STATUS "Qt6 WebSockets found; the CDP pipe transport is the default, the loopback WebSocket is a debug fallback")
else()
    message(STATUS "Qt6 WebSockets development files not found; the loopback RFC 6455 fallback uses the built-in client")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)

qt_standard_project_setup()

add_executable(pvforge-mcp
    src/mcp/main.cpp
    src/mcp/StdioBridge.cpp
)

target_compile_definitions(pvforge-mcp PRIVATE
    PVFORGE_VERSION="${PROJECT_VERSION}"
)

target_include_directories(pvforge-mcp PRIVATE src)
target_link_libraries(pvforge-mcp PRIVATE Qt6::Core Qt6::Network)

add_executable(pvforge
    src/main.cpp
    src/core/Project.cpp
    src/core/CheckpointManager.cpp
    src/core/History.cpp
    src/core/PerfMonitor.cpp
    src/core/Settings.cpp
    src/core/Lang.cpp
    src/core/TextStyle.cpp
    src/core/TextBlock.cpp
    src/core/ComicBox.cpp
    src/core/LineDeco.cpp
    src/core/BrandWall.cpp
    src/core/VectorShape.cpp
    src/core/Camera.cpp
    src/core/Easing.cpp
    src/core/Commands.cpp
    src/media/VideoDecoder.cpp
    src/media/VideoPlayer.cpp
    src/media/VideoProxy.cpp
    src/media/AudioDecoder.cpp
    src/media/DecoderInfo.cpp
    src/media/MediaImporter.cpp
    src/mcp/McpServer.cpp
    src/mcp/StdioBridge.cpp
    src/mcp/StdioBridgeTest.cpp
    src/mcp/McpCommandService.cpp
    src/audio/AudioEngine.cpp
    src/render/SceneRenderer.cpp
    src/render/CameraRenderer.cpp
    src/render/VideoExporter.cpp
    src/html/HtmlAsset.cpp
    src/html/WsClient.cpp
    src/html/CdpTransport.cpp
    src/html/PipeCdpTransport.cpp
    src/html/CdpClient.cpp
    src/html/HtmlBrowser.cpp
    src/html/HtmlRenderer.cpp
    src/html/HtmlCaptureTest.cpp
    src/html/HtmlLayerTest.cpp
    src/app/AppIcon.cpp
    src/app/SystemInfo.cpp
    src/app/SettingsDialog.cpp
    src/app/ExportDialog.cpp
    src/app/ExportProgressDialog.cpp
    src/app/MainWindow.cpp
    src/app/WelcomeWindow.cpp
    src/app/NewProjectDialog.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/AssetLibraryPanel.cpp
    src/ui/BezierEditorDialog.cpp
    src/ui/CameraMonitor.cpp
    src/ui/EasingChooser.cpp
    src/ui/EasingPicker.cpp
    src/ui/McpLogPanel.cpp
    src/ui/TextLayoutDialog.cpp
    src/ui/BrandWallDialog.cpp
    src/ui/CollapsibleSection.cpp
    src/ui/CurveEditor.cpp
    src/ui/EnvironmentPanel.cpp
    src/ui/ProjectPropertiesPanel.cpp
    src/ui/TimelinePanel.cpp
    src/ui/Viewport.cpp
    src/ui/WaveformView.cpp
    src/ui/WorkbenchPanel.cpp
)

qt_add_resources(pvforge "lang"
    PREFIX "/"
    BASE "assets"
    FILES
        assets/lang/zh_CN.json
        assets/lang/en_US.json
)

target_compile_definitions(pvforge PRIVATE
    PVFORGE_VERSION="${PROJECT_VERSION}"
    PVFORGE_BUILD_TYPE="$<CONFIG>"
    PVFORGE_MCP_BINARY="$<TARGET_FILE:pvforge-mcp>"
)

add_dependencies(pvforge pvforge-mcp)

target_include_directories(pvforge PRIVATE src)
target_link_libraries(pvforge PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Multimedia Qt6::Network
    PkgConfig::FFMPEG
)

install(TARGETS pvforge pvforge-mcp RUNTIME DESTINATION bin)

# New entity model modules. The old pvforge target stays buildable while
# pvforge-next is assembled; these libraries are not linked into it yet.
add_subdirectory(src/model)
add_subdirectory(src/assets)
add_subdirectory(src/solver)
add_subdirectory(src/gizmo)
add_subdirectory(src/store)
add_subdirectory(src/anim)
add_subdirectory(src/render2)
add_subdirectory(src/next/core)
add_subdirectory(src/next/assets)
add_subdirectory(src/next/timeline)
add_subdirectory(src/next/properties)
add_subdirectory(src/next/viewport)
add_subdirectory(src/next/app)
