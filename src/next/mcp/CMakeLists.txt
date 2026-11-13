cmake_minimum_required(VERSION 3.21)

project(PVForgeNextMcpLog VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test)

add_library(pvf_next_mcp_log STATIC
    McpLogPanel.cpp
    McpLogPanel.h
)

target_include_directories(pvf_next_mcp_log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_mcp_log PUBLIC
    Qt6::Widgets
)

target_compile_features(pvf_next_mcp_log PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    # Standalone module builds do not see the top-level test-hook switch.
    add_compile_definitions(PVFORGE_TEST_HOOKS)
    add_executable(pvf_next_mcp_log_tests tests/McpLogPanelTest.cpp)
    target_link_libraries(pvf_next_mcp_log_tests PRIVATE
        pvf_next_mcp_log
        Qt6::Test
    )

    add_test(NAME next.mcp_log COMMAND pvf_next_mcp_log_tests)
    set_tests_properties(next.mcp_log PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
