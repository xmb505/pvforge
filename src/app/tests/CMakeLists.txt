cmake_minimum_required(VERSION 3.21)

# App-layer tests that do not link the legacy UI shell.  The real mcp2 service
# is linked directly: the old test-only stubs were removed in Phase D.
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(pvf_app_edit_tests
    EditActionsTests.cpp
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
    pvf_mcp2
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

# §1/§2: EditorSession's HTML prefetch hooks and the asset bin's HTML
# thumbnails.  The thumbnail renderer is injected (no Chrome needed); the
# prefetch assertions only count the calls EditorSession issues, because
# MediaServices::prefetchHtml() is a no-op while no browser is running.
#
# Both suites share the session and asset-bin translation units.  The bin is
# compiled in directly instead of linking pvf_app: it only needs the session
# plus the asset store, and pulling the whole UI shell in would drag every
# panel it references along with it.
set(PVF_APP_HTML_TEST_SUPPORT
    ${CMAKE_SOURCE_DIR}/src/mcp2/Service.h
    ${CMAKE_SOURCE_DIR}/src/mcp2/HttpServer.h
    ${CMAKE_SOURCE_DIR}/src/app/EditorSession.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EditActions.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EditActionsContent.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EditActionsKeyframes.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TimelineAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Lang.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanelDrop.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanelHelpers.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanelImport.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanelInternal.h
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanelProgress.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanelSession.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AssetLibraryPanelThumbs.cpp
)

function(pvf_add_app_html_test name source)
    add_executable(${name} ${source} ${PVF_APP_HTML_TEST_SUPPORT})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE
        pvf_next_services
        pvf_entity_range
        pvf_next_timeline
        pvf_next_properties
        pvf_mcp2
        pvf_perf
        Qt6::Test
        Qt6::Widgets
    )
    target_compile_definitions(${name} PRIVATE PVFORGE_TEST_HOOKS)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config"
        TIMEOUT 300
        SKIP_RETURN_CODE 77
        LABELS "app;core"
    )
endfunction()

pvf_add_app_html_test(app.html_prefetch HtmlPrefetchTests.cpp)
pvf_add_app_html_test(app.asset_thumbnails AssetLibraryThumbTests.cpp)
