cmake_minimum_required(VERSION 3.21)

project(PVForgeMcp2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)

if(NOT TARGET pvf_model)
    add_subdirectory(../model ${CMAKE_CURRENT_BINARY_DIR}/pvf-model)
endif()

if(NOT TARGET pvf_assets)
    add_subdirectory(../assets ${CMAKE_CURRENT_BINARY_DIR}/pvf-assets)
endif()

if(NOT TARGET pvf_store)
    add_subdirectory(../store ${CMAKE_CURRENT_BINARY_DIR}/pvf-store)
endif()

add_library(pvf_mcp2 STATIC
    Service.cpp
    Support.cpp
    ProjectTools.cpp
    EntityTools.cpp
    KeyframeTools.cpp
    AssetTools.cpp
    SchemaTools.cpp
)

target_include_directories(pvf_mcp2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_mcp2 PUBLIC pvf_model pvf_store pvf_assets Qt6::Core)
target_compile_features(pvf_mcp2 PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    add_executable(pvf_mcp2_tests tests/mcp2_tests.cpp)
    target_link_libraries(pvf_mcp2_tests PRIVATE pvf_mcp2 Qt6::Test)

    set(PVF_MCP2_TEST_CASES
        createTreeOneCommit
        keyframesSetRespectsDuration
        solverDrivenTrackRejectsNormalKeys
        trimSolverRules
        moveTrimReparentValidation
        reparentPreserveWorldKeepsPosition
        lockedEntityRejectsWrite
        deleteRequiresConfirm
        assetImportDeduplicates
        markersSetListRemove
        historyUndoRedo
        defaultRowIsHighestPlusOne
        twoCallsTwoCommits
        createManyRollsBack
        keyframesSetManyRollsBack
        assetAssignAndUnassign
        duplicateSubtreeOneCommit
        projectUpdateFpsNeedsConfirm
        deleteManyMergesHierarchy
        listToolsAndSchema
    )

    foreach(test_case IN LISTS PVF_MCP2_TEST_CASES)
        add_test(NAME mcp2.${test_case} COMMAND pvf_mcp2_tests ${test_case})
        set_tests_properties(mcp2.${test_case} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
