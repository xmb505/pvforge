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

if(NOT TARGET pvf_next_settings)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../settings
                     ${CMAKE_CURRENT_BINARY_DIR}/settings_dep)
endif()

if(NOT TARGET pvf_html2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../html2
                     ${CMAKE_CURRENT_BINARY_DIR}/html2_dep)
endif()

if(NOT TARGET pvf_video2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../video2
                     ${CMAKE_CURRENT_BINARY_DIR}/video2_dep)
endif()

if(NOT TARGET pvf_audio2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../audio2
                     ${CMAKE_CURRENT_BINARY_DIR}/audio2_dep)
endif()

if(NOT TARGET pvf_workbench)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../workbench
                     ${CMAKE_CURRENT_BINARY_DIR}/workbench_dep)
endif()

if(NOT TARGET pvf_perf)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../perf
                     ${CMAKE_CURRENT_BINARY_DIR}/perf_dep)
endif()

# Document slice shared by the legacy delivery shell (P1) and the services
# below.  Keeping it separate means `pvforge` can link the document/core/store
# without pulling in any src/next UI panel.
add_library(pvf_next_document STATIC
    ProjectDocument.cpp
    ProjectDocument.h
)

target_include_directories(pvf_next_document PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_document PUBLIC
    pvf_next_core
    pvf_store
    Qt6::Core
)

target_compile_features(pvf_next_document PUBLIC cxx_std_20)

# Delivery services: media providers, playback watchdog and workbench cache
# behind the legacy panels.  `pvforge` links these.
add_library(pvf_next_services STATIC
    MediaServices.cpp
    MediaServices.h
    WorkbenchServices.cpp
    WorkbenchServices.h
    PlaybackController.cpp
    PlaybackController.h
)

target_include_directories(pvf_next_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_services PUBLIC
    pvf_next_document
    pvf_next_core
    pvf_store
    pvf_next_assets
    pvf_render2
    pvf_next_settings
    pvf_html2
    pvf_video2
    pvf_audio2
    pvf_workbench
    pvf_perf
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_compile_features(pvf_next_services PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_document_tests tests/ProjectDocumentTests.cpp)
    target_link_libraries(pvf_next_document_tests PRIVATE pvf_next_document Qt6::Test)
    add_test(NAME next.project_document COMMAND pvf_next_document_tests)
    set_tests_properties(next.project_document PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")

    add_executable(pvf_media_services_tests tests/MediaServicesTests.cpp)
    target_link_libraries(pvf_media_services_tests PRIVATE pvf_next_services Qt6::Test)
    add_test(NAME next.media_services COMMAND pvf_media_services_tests)
    # Drives html2's HtmlFrameService, i.e. real Chrome/DevTools processes;
    # share the lock with html2.* so a full `ctest -j4` does not race them.
    set_tests_properties(next.media_services PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config"
        RESOURCE_LOCK pvforge_chrome)

    add_executable(pvf_playback_controller_tests tests/PlaybackControllerTests.cpp)
    target_link_libraries(pvf_playback_controller_tests PRIVATE pvf_next_services Qt6::Test)
    add_test(NAME next.playback_controller COMMAND pvf_playback_controller_tests)
    set_tests_properties(next.playback_controller PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")

    add_executable(pvf_workbench_services_tests tests/WorkbenchIntegrationTests.cpp)
    target_link_libraries(pvf_workbench_services_tests PRIVATE pvf_next_services Qt6::Test)
    add_test(NAME next.workbench_integration COMMAND pvf_workbench_services_tests)
    set_tests_properties(next.workbench_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")
endif()
