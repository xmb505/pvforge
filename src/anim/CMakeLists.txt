cmake_minimum_required(VERSION 3.21)

project(PVForgeAnim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)

# Standalone build: pull in the sibling modules when the top-level project did
# not already provide them.  The anim module is also usable via
# add_subdirectory(src/anim) from the root build.
if(NOT TARGET pvf_model)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../model
                     ${CMAKE_CURRENT_BINARY_DIR}/model_dep)
endif()

if(NOT TARGET pvf_solver)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../solver
                     ${CMAKE_CURRENT_BINARY_DIR}/solver_dep)
endif()

add_library(pvf_anim STATIC
    Anim.cpp
)

target_include_directories(pvf_anim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(pvf_anim PUBLIC
    pvf_model
    pvf_solver
    Qt6::Core
    Qt6::Gui
)

target_compile_features(pvf_anim PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
