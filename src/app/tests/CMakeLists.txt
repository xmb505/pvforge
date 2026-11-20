cmake_minimum_required(VERSION 3.21)

# App-layer tests that do not link the legacy UI shell.  During Phase B the
# full pvf_app target is blocked on the Phase C panels, so these compile
# EditActions, EditorSession and TimelineAdapter directly against the new
# model and the document/services libraries.
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(pvf_app_edit_tests
    EditActionsTests.cpp
    McpStubs.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp2/Service.h
    ${CMAKE_SOURCE_DIR}/src/mcp2/HttpServer.h
    ${CMAKE_SOURCE_DIR}/src/app/EditorSession.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EditActions.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EditActionsContent.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EditActionsKeyframes.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TimelineAdapter.cpp
)

target_include_directories(pvf_app_edit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pvf_app_edit_tests PRIVATE
    pvf_next_services
    pvf_entity_range
    pvf_next_timeline
    Qt6::Test
)
target_compile_definitions(pvf_app_edit_tests PRIVATE PVFORGE_TEST_HOOKS)

add_test(NAME app.edit_actions COMMAND pvf_app_edit_tests)
set_tests_properties(app.edit_actions PROPERTIES
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config"
    TIMEOUT 300
    SKIP_RETURN_CODE 77
    LABELS "app;core"
)
