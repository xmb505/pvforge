cmake_minimum_required(VERSION 3.21)

project(PVForge VERSION 0.3.0 DESCRIPTION "2D music PV editor with a Mine-imator style animation workflow" LANGUAGES CXX)

# Enable testing at the top level so `ctest` in the build root discovers the
# add_test() calls registered by the module subdirectories. Enabling it only in
# subdirectories writes their CTestTestfile.cmake but leaves the build root
# without one, so a root-level ctest reports zero tests.
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The `--*-test` suite is a development tool: it is compiled into a separate
# `pvforge-tests` executable, never into the shipped `pvforge` binary. Debug
# builds get it by default so `./build/pvforge-tests --self-test` just works;
# release builds must opt in explicitly.
set(_pvf_self_tests_default OFF)
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_pvf_self_tests_default ON)
endif()
option(PVFORGE_BUILD_SELF_TESTS
    "Build pvforge-tests, the executable that answers the --*-test switches"
    ${_pvf_self_tests_default})


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

# Everything the editor needs, minus the entry point. Kept as a static library
# so `pvforge` and `pvforge-tests` share one compilation of the application
# sources instead of building them twice.
set(PVF_APP_SOURCES
    src/core/CheckpointManager.cpp
    src/core/PerfMonitor.cpp
    src/core/Settings.cpp
    src/core/Lang.cpp
    src/core/TextStyle.cpp
    src/core/TextBlock.cpp
    src/core/ComicBox.cpp
    src/core/LineDeco.cpp
    src/core/BrandWall.cpp
    src/core/VectorShape.cpp
    src/core/Easing.cpp
    src/media/VideoDecoder.cpp
    src/media/VideoProxy.cpp
    src/media/AudioDecoder.cpp
    src/media/DecoderInfo.cpp
    src/mcp/StdioBridge.cpp
    src/app/AppIcon.cpp
    src/app/SystemInfo.cpp
    src/app/SettingsDialog.cpp
    src/app/SettingsDialogAppPages.cpp
    src/app/SettingsDialogLegacyPages.cpp
    src/app/SettingsDialogSupport.h
    src/app/ExportDialog.cpp
    src/app/ExportProgressDialog.cpp
    src/app/MainWindow.cpp
    src/app/MainWindowMenus.cpp
    src/app/MainWindowMcp.cpp
    src/app/MainWindowProjectActions.cpp
    src/app/EditorSession.cpp
    src/app/EditorSession.h
    src/app/EditActions.cpp
    src/app/EditActionsContent.cpp
    src/app/EditActionsKeyframes.cpp
    src/app/EditActions.h
    src/app/EditActionsSupport.h
    src/app/TimelineAdapter.cpp
    src/app/TimelineAdapter.h
    src/app/WelcomeWindow.cpp
    src/app/NewProjectDialog.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/PropertiesPanelBuild.cpp
    src/ui/PropertiesPanelContent.cpp
    src/ui/PropertiesPanelHtml.cpp
    src/ui/PropertiesPanelInternal.cpp
    src/ui/PropertiesPanelInternal.h
    src/ui/PropertiesPanelKeyframes.cpp
    src/ui/PropertiesPanelSolver.cpp
    src/next/properties/SolverPreviewWidget.cpp
    src/next/properties/SolverPreviewWidget.h
    src/next/properties/SpecialContentEditor.cpp
    src/next/properties/SpecialContentEditor.h
    src/next/properties/TextLayoutDialog.cpp
    src/next/properties/TextLayoutDialog.h
    src/ui/AssetLibraryPanel.cpp
    src/ui/AssetLibraryPanelHelpers.cpp
    src/ui/AssetLibraryPanelImport.cpp
    src/ui/AssetLibraryPanelInternal.h
    src/ui/AssetLibraryPanelProgress.cpp
    src/ui/AssetLibraryPanelSession.cpp
    src/ui/AssetLibraryPanelThumbs.cpp
    src/ui/BezierEditorDialog.cpp
    src/ui/CameraMonitor.cpp
    src/ui/EasingChooser.cpp
    src/ui/EasingPicker.cpp
    src/ui/McpLogPanel.cpp
    src/ui/PerfMonitorWindow.cpp
    src/ui/TextLayoutDialog.cpp
    src/ui/BrandWallDialog.cpp
    src/ui/CollapsibleSection.cpp
    src/ui/EnvironmentPanel.cpp
    src/ui/ProjectPropertiesPanel.cpp
    src/ui/TimelinePanel.cpp
    src/ui/Viewport.cpp
    src/next/timeline/MarkerDialog.cpp
    src/next/timeline/MarkerDialog.h
    src/ui/WorkbenchPanel.cpp
    src/ui/WorkbenchPanelMedia.cpp
    src/ui/WorkbenchPanelPages.cpp
    src/ui/WorkbenchPanelPreview.cpp
    src/ui/panels/PanelGeometry.cpp
    src/ui/panels/PanelGeometry.h
    src/ui/panels/WorkbenchPanelHost.cpp
    src/ui/panels/WorkbenchPanelHost.h
)

add_library(pvf_app STATIC ${PVF_APP_SOURCES})

target_compile_definitions(pvf_app PRIVATE
    PVFORGE_VERSION="${PROJECT_VERSION}"
    PVFORGE_BUILD_TYPE="$<CONFIG>"
    PVFORGE_MCP_BINARY="$<TARGET_FILE:pvforge-mcp>"
)

add_dependencies(pvf_app pvforge-mcp)

target_include_directories(pvf_app PUBLIC src)
target_link_libraries(pvf_app PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Multimedia Qt6::Network
    PkgConfig::FFMPEG
    pvf_gizmo
    pvf_perf
)

# Both executables embed the lang catalogs themselves: a resource compiled into
# a static library is not reliably pulled in by the linker.
function(pvf_add_lang_resources target)
    qt_add_resources(${target} "lang"
        PREFIX "/"
        BASE "assets"
        FILES
            assets/lang/zh_CN.json
            assets/lang/en_US.json
    )
endfunction()

# Production entry point. src/main.cpp holds only QApplication setup, the
# `--open/--shot` CLI and the welcome window; test dispatch compiles away when
# PVFORGE_BUILD_SELF_TESTS is off.
add_executable(pvforge src/main.cpp)
target_include_directories(pvforge PRIVATE src)
target_link_libraries(pvforge PRIVATE pvf_app)
pvf_add_lang_resources(pvforge)

if(PVFORGE_BUILD_SELF_TESTS)
    set(PVF_TEST_SOURCES
        src/tests/TestSupport.cpp
        src/tests/TimelineEditTests.cpp
        src/tests/TestRegistry.cpp
        src/tests/SessionTests.cpp
        src/tests/PropertiesTests.cpp
        src/tests/SolverTests.cpp
        src/tests/ProfileFixtures.cpp
        src/tests/TimelineTests.cpp
        src/tests/ViewportCameraTests.cpp
        src/tests/EditorTests.cpp
        src/tests/RenderMediaTests.cpp
        src/tests/AssetTests.cpp
        src/tests/McpTests.cpp
        src/tests/PerfTests.cpp
        src/tests/PanelLayoutTests.cpp
        # Test-only helpers live in src/tests.
        src/tests/StdioBridgeTest.cpp
    )

    add_executable(pvforge-tests src/main.cpp ${PVF_TEST_SOURCES})
    target_compile_definitions(pvforge-tests PRIVATE PVFORGE_SELF_TESTS)
    target_include_directories(pvforge-tests PRIVATE src)
    target_link_libraries(pvforge-tests PRIVATE pvf_app)
    pvf_add_lang_resources(pvforge-tests)
endif()

install(TARGETS pvforge pvforge-mcp RUNTIME DESTINATION bin)

# New entity model modules. The legacy pvforge target links only the small
# document/core/store slice (pvf_next_document) plus the services and panels
# it uses.
add_subdirectory(src/model)
add_subdirectory(src/perf)
add_subdirectory(src/assets)
add_subdirectory(src/framecache)
add_subdirectory(src/solver)
add_subdirectory(src/gizmo)
add_subdirectory(src/store)
add_subdirectory(src/anim)
add_subdirectory(src/render2)
add_subdirectory(src/workbench)
add_subdirectory(src/export2)
add_subdirectory(src/audio2)
add_subdirectory(src/htmlcache)
add_subdirectory(src/html2)
add_subdirectory(src/video2)
add_subdirectory(src/mcp2)
add_subdirectory(src/next/core)
add_subdirectory(src/next/settings)
add_subdirectory(src/next/history)
add_subdirectory(src/next/assets)
add_subdirectory(src/next/mcp)
add_subdirectory(src/next/timeline)
add_subdirectory(src/next/properties)
# src/next/app carries pvf_next_document/pvf_next_services, which the delivery
# shell links.
add_subdirectory(src/next/app)

# P1/P2 delivery shell: link only the document slice and the shared entity
# range helpers.
target_link_libraries(pvf_app PUBLIC pvf_next_document pvf_next_services
    pvf_mcp2 pvf_entity_range pvf_next_mcp_log pvf_next_history pvf_export2)
