cmake_minimum_required(VERSION 3.21)

project(PVForgeNextApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent Test)

if(NOT TARGET pvf_next_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core
                     ${CMAKE_CURRENT_BINARY_DIR}/core_dep)
endif()

if(NOT TARGET pvf_store)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../store
                     ${CMAKE_CURRENT_BINARY_DIR}/store_dep)
endif()

if(NOT TARGET pvf_next_assets)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../assets
                     ${CMAKE_CURRENT_BINARY_DIR}/assets_dep)
endif()

if(NOT TARGET pvf_next_viewport)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../viewport
                     ${CMAKE_CURRENT_BINARY_DIR}/viewport_dep)
endif()

if(NOT TARGET pvf_next_properties)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../properties
                     ${CMAKE_CURRENT_BINARY_DIR}/properties_dep)
endif()

if(NOT TARGET pvf_next_timeline)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../timeline
                     ${CMAKE_CURRENT_BINARY_DIR}/timeline_dep)
endif()

if(NOT TARGET pvf_mcp2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../mcp2
                     ${CMAKE_CURRENT_BINARY_DIR}/mcp2_dep)
endif()

if(NOT TARGET pvf_export2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../export2
                     ${CMAKE_CURRENT_BINARY_DIR}/export2_dep)
endif()

if(NOT TARGET pvf_audio2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../audio2
                     ${CMAKE_CURRENT_BINARY_DIR}/audio2_dep)
endif()

if(NOT TARGET pvf_next_settings)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../settings
                     ${CMAKE_CURRENT_BINARY_DIR}/settings_dep)
endif()

if(NOT TARGET pvf_next_mcp_log)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mcp
                     ${CMAKE_CURRENT_BINARY_DIR}/mcp_log_dep)
endif()

if(NOT TARGET pvf_next_history)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../history
                     ${CMAKE_CURRENT_BINARY_DIR}/history_dep)
endif()

if(NOT TARGET pvf_html2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../html2
                     ${CMAKE_CURRENT_BINARY_DIR}/html2_dep)
endif()

if(NOT TARGET pvf_video2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../video2
                     ${CMAKE_CURRENT_BINARY_DIR}/video2_dep)
endif()

if(NOT TARGET pvf_workbench)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../workbench
                     ${CMAKE_CURRENT_BINARY_DIR}/workbench_dep)
endif()

add_library(pvf_next_app STATIC
    ProjectDocument.cpp
    ProjectDocument.h
    NextWindow.cpp
    NextWindow.h
    MediaServices.cpp
    MediaServices.h
    WorkbenchServices.cpp
    WorkbenchServices.h
    WorkbenchPanel.cpp
    WorkbenchPanel.h
    ExportDialog.cpp
    ExportDialog.h
    PlaybackController.cpp
    PlaybackController.h
)

target_include_directories(pvf_next_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_app PUBLIC
    pvf_next_core
    pvf_store
    pvf_next_assets
    pvf_next_viewport
    pvf_next_properties
    pvf_next_timeline
    pvf_render2
    pvf_mcp2
    pvf_export2
    pvf_audio2
    pvf_next_settings
    pvf_next_mcp_log
    pvf_next_history
    pvf_html2
    pvf_video2
    pvf_workbench
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
)

target_compile_features(pvf_next_app PUBLIC cxx_std_20)

add_executable(pvforge-next main.cpp)

target_link_libraries(pvforge-next PRIVATE
    pvf_next_app
    pvf_next_core
    pvf_next_assets
    pvf_next_viewport
    pvf_next_properties
    pvf_next_timeline
    pvf_render2
    pvf_store
    pvf_mcp2
    pvf_export2
    pvf_audio2
    pvf_next_settings
    pvf_next_mcp_log
    pvf_next_history
    pvf_html2
    pvf_video2
    pvf_workbench
    Qt6::Widgets
    Qt6::Concurrent
)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_app_tests tests/ProjectDocumentTests.cpp)
    target_link_libraries(pvf_next_app_tests PRIVATE pvf_next_app Qt6::Test)
    add_test(NAME next.project_document COMMAND pvf_next_app_tests)
    set_tests_properties(next.project_document PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")

    add_executable(pvf_next_window_tests tests/NextWindowTests.cpp)
    target_link_libraries(pvf_next_window_tests PRIVATE pvf_next_app Qt6::Test)
    target_compile_definitions(pvf_next_window_tests PRIVATE
        PVF_NEXT_APP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME next.window_shell COMMAND pvf_next_window_tests)
    set_tests_properties(next.window_shell PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")

    add_executable(pvf_media_services_tests tests/MediaServicesTests.cpp)
    target_link_libraries(pvf_media_services_tests PRIVATE pvf_next_app Qt6::Test)
    add_test(NAME next.media_services COMMAND pvf_media_services_tests)
    set_tests_properties(next.media_services PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")

    add_executable(pvf_playback_controller_tests tests/PlaybackControllerTests.cpp)
    target_link_libraries(pvf_playback_controller_tests PRIVATE pvf_next_app Qt6::Test)
    add_test(NAME next.playback_controller COMMAND pvf_playback_controller_tests)
    set_tests_properties(next.playback_controller PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")

    add_executable(pvf_workbench_services_tests tests/WorkbenchIntegrationTests.cpp)
    target_link_libraries(pvf_workbench_services_tests PRIVATE pvf_next_app Qt6::Test)
    add_test(NAME next.workbench_integration COMMAND pvf_workbench_services_tests)
    set_tests_properties(next.workbench_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")
endif()
