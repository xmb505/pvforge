cmake_minimum_required(VERSION 3.21)

project(PVForgeNextHistory VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)

if(NOT TARGET pvf_store)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../store
                     ${CMAKE_CURRENT_BINARY_DIR}/store_dep)
endif()

add_library(pvf_next_history STATIC
    HistoryPanel.cpp
    HistoryPanel.h
)

target_include_directories(pvf_next_history PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_history PUBLIC
    pvf_store
    Qt6::Widgets
)

target_compile_features(pvf_next_history PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    # Standalone module builds do not see the top-level test-hook switch.
    add_compile_definitions(PVFORGE_TEST_HOOKS)
    add_executable(pvf_next_history_tests tests/HistoryPanelTest.cpp)
    target_link_libraries(pvf_next_history_tests PRIVATE
        pvf_next_history
        Qt6::Test
    )

    add_test(NAME next.history COMMAND pvf_next_history_tests)
    set_tests_properties(next.history PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
