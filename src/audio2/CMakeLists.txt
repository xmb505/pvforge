cmake_minimum_required(VERSION 3.21)

project(PVForgeAudio2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Multimedia)
find_package(PkgConfig REQUIRED)

# Standalone build: pull in sibling modules unless a parent build already
# provides them.
if(NOT TARGET pvf_model)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../model
                     ${CMAKE_CURRENT_BINARY_DIR}/model_dep)
endif()

if(NOT TARGET pvf_assets)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../assets
                     ${CMAKE_CURRENT_BINARY_DIR}/assets_dep)
endif()

if(NOT TARGET PkgConfig::FFMPEG)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswresample)
endif()

add_library(pvf_audio2 STATIC
    Audio2.h
    AudioMix.cpp
    Playback.cpp
)

target_include_directories(pvf_audio2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(pvf_audio2
    PUBLIC
    pvf_model
    pvf_assets
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    PRIVATE
    PkgConfig::FFMPEG
)

target_compile_features(pvf_audio2 PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
