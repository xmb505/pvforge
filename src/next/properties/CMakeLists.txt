cmake_minimum_required(VERSION 3.21)

project(PVForgeNextProperties VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Test)

if(NOT TARGET pvf_next_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core
                     ${CMAKE_CURRENT_BINARY_DIR}/core_dep)
endif()

if(NOT TARGET pvf_solver)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../solver
                     ${CMAKE_CURRENT_BINARY_DIR}/solver_dep)
endif()

if(NOT TARGET pvf_anim)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../anim
                     ${CMAKE_CURRENT_BINARY_DIR}/anim_dep)
endif()

if(NOT TARGET pvf_render2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../render2
                     ${CMAKE_CURRENT_BINARY_DIR}/render2_dep)
endif()

add_library(pvf_next_properties STATIC
    PropertiesPanel.cpp
    PropertiesPanel.h
    SolverPreviewWidget.cpp
    SolverPreviewWidget.h
    TextLayoutDialog.cpp
    TextLayoutDialog.h
)

target_include_directories(pvf_next_properties PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_properties PUBLIC
    pvf_next_core
    pvf_solver
    pvf_anim
    pvf_render2
    Qt6::Widgets
    Qt6::Test
)

target_compile_features(pvf_next_properties PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_properties_tests tests/PropertiesPanelTest.cpp)
    target_link_libraries(pvf_next_properties_tests PRIVATE
        pvf_next_properties
        Qt6::Test
    )

    add_test(NAME pvf_next_properties_tests COMMAND pvf_next_properties_tests)
    set_tests_properties(pvf_next_properties_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(pvf_next_solver_preview_tests tests/SolverPreviewWidgetTest.cpp)
    target_link_libraries(pvf_next_solver_preview_tests PRIVATE
        pvf_next_properties
        Qt6::Test
    )

    add_test(NAME pvf_next_solver_preview_tests COMMAND pvf_next_solver_preview_tests)
    set_tests_properties(pvf_next_solver_preview_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
