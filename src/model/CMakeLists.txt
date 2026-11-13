cmake_minimum_required(VERSION 3.21)

project(PVForgeModel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

add_library(pvf_model STATIC
    Content.cpp
    Easing.cpp
    Entity.cpp
    Keyframe.cpp
    Migrations.cpp
    Project.cpp
    Query.cpp
    Uuid.cpp
)

target_include_directories(pvf_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_model PUBLIC Qt6::Core)
target_compile_features(pvf_model PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_model_tests
        tests/model_tests.cpp
        tests/entity_tree_tests.cpp
    )
    target_link_libraries(pvf_model_tests PRIVATE pvf_model)
    add_test(NAME pvf_model_tests COMMAND pvf_model_tests)
endif()
