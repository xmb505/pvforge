cmake_minimum_required(VERSION 3.21)

project(PVForgeGizmo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)

add_library(pvf_gizmo STATIC
    Gizmo.cpp
)

target_include_directories(pvf_gizmo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_gizmo PUBLIC Qt6::Core Qt6::Gui)
target_compile_features(pvf_gizmo PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_gizmo_tests tests/gizmo_tests.cpp)
    target_link_libraries(pvf_gizmo_tests PRIVATE pvf_gizmo)
    add_test(NAME pvf_gizmo_tests COMMAND pvf_gizmo_tests)
endif()
