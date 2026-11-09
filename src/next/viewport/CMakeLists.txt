cmake_minimum_required(VERSION 3.21)

project(PVForgeNextViewport VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Test)

if(NOT TARGET pvf_next_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core
                     ${CMAKE_CURRENT_BINARY_DIR}/core_dep)
endif()

if(NOT TARGET pvf_render2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../render2
                     ${CMAKE_CURRENT_BINARY_DIR}/render2_dep)
endif()

if(NOT TARGET pvf_gizmo)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../gizmo
                     ${CMAKE_CURRENT_BINARY_DIR}/gizmo_dep)
endif()

if(NOT TARGET pvf_anim)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../anim
                     ${CMAKE_CURRENT_BINARY_DIR}/anim_dep)
endif()

add_library(pvf_next_viewport STATIC
    ViewportWidget.cpp
    ViewportWidget.h
)

target_include_directories(pvf_next_viewport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_viewport PUBLIC
    pvf_next_core
    pvf_render2
    pvf_gizmo
    pvf_anim
    Qt6::Widgets
    Qt6::Test
)

target_compile_features(pvf_next_viewport PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_viewport_tests tests/ViewportWidgetTest.cpp)
    target_link_libraries(pvf_next_viewport_tests PRIVATE
        pvf_next_viewport
        Qt6::Test
    )

    add_test(NAME viewport.tests COMMAND pvf_next_viewport_tests)
    set_tests_properties(viewport.tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
