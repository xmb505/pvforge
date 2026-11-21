cmake_minimum_required(VERSION 3.21)

project(PVForge VERSION 0.3.0 DESCRIPTION "2D music PV editor with a Mine-imator style animation workflow" LANGUAGES CXX)

# Debug builds default to tests; a plain Release configure must not compile
# test hooks or test binaries unless the caller explicitly asks for them.
set(_pvf_testing_default OFF)
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_pvf_testing_default ON)
endif()
option(BUILD_TESTING "Build the test suite" ${_pvf_testing_default})
# Enable testing at the top level so `ctest` in the build root discovers the
# add_test() calls registered by the module subdirectories. Enabling it only in
# subdirectories writes their CTestTestfile.cmake but leaves the build root
# without one, so a root-level ctest reports zero tests.
if(BUILD_TESTING)
    enable_testing()
endif()

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

# `*ForTest` accessors are declared in production headers.  Compile them only
# for test-enabled builds; the release preset turns BUILD_TESTING and
# PVFORGE_BUILD_SELF_TESTS off, so the delivered binary carries no test hooks.
# PVFORGE_SELF_TESTS is separate: only pvforge-tests dispatches `--*-test`.
if(BUILD_TESTING OR PVFORGE_BUILD_SELF_TESTS)
    add_compile_definitions(PVFORGE_TEST_HOOKS)
endif()


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

# The stdio bridge is shared by the pvforge-mcp CLI and its self-test; keep it
# in one library so the TU is compiled once.  The editor GUI does not link it.
add_library(pvf_mcp_bridge STATIC
    src/mcp/StdioBridge.cpp
    src/mcp/StdioBridge.h
)

target_include_directories(pvf_mcp_bridge PUBLIC src)
target_link_libraries(pvf_mcp_bridge PUBLIC Qt6::Core Qt6::Network)

add_executable(pvforge-mcp src/mcp/main.cpp)

target_compile_definitions(pvforge-mcp PRIVATE
    PVFORGE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(pvforge-mcp PRIVATE pvf_mcp_bridge)

# Everything the editor needs, minus the entry point. Kept as a static library
# so `pvforge` and `pvforge-tests` share one compilation of the application
# sources instead of building them twice.
set(PVF_APP_SOURCES
    src/core/PerfMonitor.cpp
    src/core/Settings.cpp
    src/core/Lang.cpp
    src/core/TextStyle.cpp
    src/core/TextBlock.cpp
    src/core/VectorShape.cpp
    src/core/Easing.cpp
    src/media/DecoderInfo.cpp
    src/app/AppIcon.cpp
    src/app/SystemInfo.cpp
    src/app/SettingsDialog.cpp
    src/app/SettingsDialogAppPages.cpp
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
    src/ui/WholeFrameEdit.cpp
    src/ui/WholeFrameEdit.h
    src/next/properties/SolverPreviewWidget.cpp
    src/next/properties/SolverPreviewWidget.h
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
    src/ui/McpLogPanel.cpp
    src/ui/PerfMonitorWindow.cpp
    src/ui/TextLayoutDialog.cpp
    src/ui/CollapsibleSection.cpp
    src/ui/EnvironmentPanel.cpp
    src/ui/ProjectPropertiesPanel.cpp
    src/ui/TimelinePanel.cpp
    src/ui/TimelinePanelInternal.h
    src/ui/TimelinePanelSupport.cpp
    src/ui/TimelinePanelLayout.cpp
    src/ui/TimelinePanelPaint.cpp
    src/ui/TimelinePanelMouse.cpp
    src/ui/TimelinePanelInput.cpp
    src/ui/TimelinePanelMenu.cpp
    src/ui/TimelinePanelKeyframes.cpp
    src/ui/TimelinePanelSessionDrag.cpp
    src/ui/TimelinePanelMarkers.cpp
    src/ui/TimelinePanelSolver.cpp
    src/ui/TimelinePanelPopups.cpp
    src/ui/TimelinePanelPopups.h
    src/ui/TimelinePanelTestHooks.cpp
    src/ui/Viewport.cpp
    src/ui/ViewportInternal.h
    src/ui/ViewportSupport.cpp
    src/ui/ViewportSession.cpp
    src/ui/ViewportGizmo.cpp
    src/ui/ViewportGizmoPaint.cpp
    src/ui/ViewportSnap.cpp
    src/ui/ViewportCamera.cpp
    src/ui/ViewportSolverPreview.cpp
    src/ui/ViewportTestHooks.cpp
    src/next/timeline/MarkerDialog.cpp
    src/next/timeline/MarkerDialog.h
    src/ui/WorkbenchPanel.cpp
    src/ui/WorkbenchPanelMedia.cpp
    src/ui/WorkbenchPanelPages.cpp
    src/ui/WorkbenchPanelPreview.cpp
    src/ui/panels/PanelGeometry.cpp
    src/ui/panels/PanelGeometry.h
)

add_library(pvf_app STATIC ${PVF_APP_SOURCES})

target_compile_definitions(pvf_app PRIVATE
    PVFORGE_VERSION="${PROJECT_VERSION}"
    PVFORGE_BUILD_TYPE="$<CONFIG>"
)

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
        src/tests/KeyframeClipboardTests.cpp
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
    target_compile_definitions(pvforge-tests PRIVATE
        PVFORGE_SELF_TESTS
        PVFORGE_MCP_BINARY="$<TARGET_FILE:pvforge-mcp>"
    )
    target_include_directories(pvforge-tests PRIVATE src)
    target_link_libraries(pvforge-tests PRIVATE pvf_app pvf_mcp_bridge)
    pvf_add_lang_resources(pvforge-tests)

    # Every behavioural self-test switch is a CTest case (issue #110). Only
    # the profiling/measurement switches stay manual: they print performance
    # numbers instead of asserting behaviour, and their numbers vary with the
    # machine. They are listed in docs/AI/SELF_TESTS.md.
    if(BUILD_TESTING)
        set(PVF_SELF_TEST_CASES
            self-test
            session-test
            welcome-test
            settings-test
            git-test
            rename-test
            panel-layout-test
            properties-test
            keyframe-easing-group-test
            html-vars-test
            text-layout-test
            export-dialog-test
            export-test
            assets-test
            asset-import-test
            asset-drag-test
            asset-replace-test
            workbench-assets-test
            timeline-test
            timeline-bottomup-test
            timeline-ui-test
            timeline-assets-test
            timeline-edit-test
            timeline-keys-test
            keyframe-clipboard-test
            timeline-split-test
            timeline-marker-test
            timeline-edit-e2e
            solver-ui-test
            solver-mini-preview-test
            solver-preview-freeze-test
            solver-convert-test
            gizmo-test
            inkscape-handles-test
            camera-monitor-test
            mcp-test
            mcp-lock-test
            mcp-bridge-test
            perf-scale-test
        )
        foreach(case IN LISTS PVF_SELF_TEST_CASES)
            add_test(NAME pvf.${case} COMMAND pvforge-tests --${case})
            set_tests_properties(pvf.${case} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none"
                TIMEOUT 300
                SKIP_RETURN_CODE 77
                LABELS "self-test"
                # Several self-tests build a MainWindow, which starts the MCP
                # server on the fixed 47821 port. Serialise them with the two
                # mcp2 fixed-port tests instead of racing under ctest -j.
                RESOURCE_LOCK pvforge_mcp_port
            )
        endforeach()

        set(PVF_SELF_TEST_UI
            session-test welcome-test settings-test rename-test panel-layout-test
            properties-test keyframe-easing-group-test
            html-vars-test text-layout-test
            export-dialog-test
            assets-test asset-import-test asset-drag-test asset-replace-test
            workbench-assets-test
            timeline-test timeline-bottomup-test timeline-ui-test
            timeline-assets-test timeline-edit-test timeline-keys-test
            timeline-split-test timeline-marker-test timeline-edit-e2e
            solver-ui-test solver-mini-preview-test solver-preview-freeze-test
            gizmo-test inkscape-handles-test camera-monitor-test
        )
        foreach(case IN LISTS PVF_SELF_TEST_UI)
            set_tests_properties(pvf.${case} PROPERTIES LABELS "self-test;ui")
        endforeach()
        set(PVF_SELF_TEST_CORE
            self-test git-test solver-convert-test
        )
        foreach(case IN LISTS PVF_SELF_TEST_CORE)
            set_tests_properties(pvf.${case} PROPERTIES LABELS "self-test;core")
        endforeach()
        set(PVF_SELF_TEST_MCP mcp-test mcp-lock-test mcp-bridge-test)
        foreach(case IN LISTS PVF_SELF_TEST_MCP)
            set_tests_properties(pvf.${case} PROPERTIES LABELS "self-test;mcp")
        endforeach()
        set_tests_properties(pvf.export-test PROPERTIES
            LABELS "self-test;media")
        # Heavier cases (full window, media, end-to-end editing) carry the
        # `slow` label so a quick `ctest -LE slow` stays possible.
        set(PVF_SELF_TEST_SLOW
            welcome-test git-test export-test assets-test
            timeline-test timeline-edit-test timeline-edit-e2e
            timeline-ui-test solver-ui-test camera-monitor-test
        )
        foreach(case IN LISTS PVF_SELF_TEST_SLOW)
            set_property(TEST pvf.${case} APPEND PROPERTY LABELS slow)
        endforeach()
        set_tests_properties(pvf.timeline-edit-e2e pvf.export-test
            PROPERTIES TIMEOUT 600)
    endif()
endif()

install(TARGETS pvforge pvforge-mcp RUNTIME DESTINATION bin)
install(FILES README.md LICENSE DESTINATION share/pvforge)

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
add_subdirectory(src/next/timeline)
add_subdirectory(src/next/properties)
# src/next/app carries pvf_next_document/pvf_next_services, which the delivery
# shell links.
add_subdirectory(src/next/app)

# Phase B app-layer tests compile EditActions/TimelineAdapter without the
# Phase C UI shell, so they stay runnable while pvf_app is being migrated.
if(BUILD_TESTING)
    add_subdirectory(src/app/tests)
endif()

# P1/P2 delivery shell: link only the document slice and the shared entity
# range helpers.
target_link_libraries(pvf_app PUBLIC pvf_next_document pvf_next_services
    pvf_mcp2 pvf_entity_range pvf_next_history pvf_export2 pvf_audio2)
