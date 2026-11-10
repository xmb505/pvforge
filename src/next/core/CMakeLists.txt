cmake_minimum_required(VERSION 3.21)

project(PVForgeNextCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

if(NOT TARGET pvf_model)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../model
                     ${CMAKE_CURRENT_BINARY_DIR}/model_dep)
endif()

if(NOT TARGET pvf_assets)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../assets
                     ${CMAKE_CURRENT_BINARY_DIR}/assets_dep)
endif()

add_library(pvf_next_core STATIC
    Document.cpp
    Document.h
    EntityLock.cpp
    EntityLock.h
)

target_include_directories(pvf_next_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_core PUBLIC
    pvf_model
    pvf_assets
    Qt6::Core
)

target_compile_features(pvf_next_core PUBLIC cxx_std_20)
