cmake_minimum_required(VERSION 3.21)

project(PVForgeNextAssets VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test)

if(NOT TARGET pvf_next_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core
                     ${CMAKE_CURRENT_BINARY_DIR}/core_dep)
endif()

if(NOT TARGET pvf_video2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../video2
                     ${CMAKE_CURRENT_BINARY_DIR}/video2_dep)
endif()

add_library(pvf_next_assets STATIC
    AssetPanel.cpp
    AssetPanel.h
    AssetPanelImport.cpp
    AssetPanelInternal.h
    AssetPanelProgress.cpp
)

target_include_directories(pvf_next_assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_assets PUBLIC
    pvf_next_core
    pvf_video2
    Qt6::Widgets
)

target_compile_features(pvf_next_assets PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_assets_tests tests/AssetPanelTest.cpp)
    target_link_libraries(pvf_next_assets_tests PRIVATE
        pvf_next_assets
        Qt6::Test
    )
    add_test(NAME pvf_next_assets.tests COMMAND pvf_next_assets_tests)
    set_tests_properties(pvf_next_assets.tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
