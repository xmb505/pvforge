cmake_minimum_required(VERSION 3.21)

project(PVForgeNextApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)

if(NOT TARGET pvf_next_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core
                     ${CMAKE_CURRENT_BINARY_DIR}/core_dep)
endif()

if(NOT TARGET pvf_store)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../store
                     ${CMAKE_CURRENT_BINARY_DIR}/store_dep)
endif()

add_library(pvf_next_app STATIC
    ProjectDocument.cpp
    ProjectDocument.h
)

target_include_directories(pvf_next_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_app PUBLIC
    pvf_next_core
    pvf_store
    Qt6::Core
    Qt6::Widgets
)

target_compile_features(pvf_next_app PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_app_tests tests/ProjectDocumentTests.cpp)
    target_link_libraries(pvf_next_app_tests PRIVATE pvf_next_app Qt6::Test)
    add_test(NAME next.project_document COMMAND pvf_next_app_tests)
    set_tests_properties(next.project_document PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=/tmp/opencode/gitlfs/root/usr/bin:$ENV{PATH}")
endif()
