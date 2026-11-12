cmake_minimum_required(VERSION 3.21)

project(pvf-solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)

if(NOT TARGET pvf_perf)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../perf
                     ${CMAKE_CURRENT_BINARY_DIR}/perf_dep)
endif()

add_library(pvf_solver STATIC
    Solver.cpp
    GravitySolver.cpp
    OrbitSolver.cpp
    SpinSolver.cpp
    BlackHoleSolver.cpp
    SpiralSolver.cpp
)

target_include_directories(pvf_solver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(pvf_solver PUBLIC
    Qt6::Core
    Qt6::Gui
    pvf_perf
)

enable_testing()
add_subdirectory(tests)
