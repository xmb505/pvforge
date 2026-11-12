cmake_minimum_required(VERSION 3.21)

project(PVForgePerf VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

add_library(pvf_perf STATIC
    FrameProfiler.cpp
    FrameProfiler.h
    SystemStats.cpp
    SystemStats.h
)

target_include_directories(pvf_perf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_perf PUBLIC Qt6::Core)
target_compile_features(pvf_perf PUBLIC cxx_std_20)
