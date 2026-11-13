cmake_minimum_required(VERSION 3.21)

project(PVForgeMcp2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network)

if(NOT TARGET pvf_model)
    add_subdirectory(../model ${CMAKE_CURRENT_BINARY_DIR}/pvf-model)
endif()

if(NOT TARGET pvf_assets)
    add_subdirectory(../assets ${CMAKE_CURRENT_BINARY_DIR}/pvf-assets)
endif()

if(NOT TARGET pvf_store)
    add_subdirectory(../store ${CMAKE_CURRENT_BINARY_DIR}/pvf-store)
endif()

if(NOT TARGET pvf_anim)
    add_subdirectory(../anim ${CMAKE_CURRENT_BINARY_DIR}/pvf-anim)
endif()

if(NOT TARGET pvf_render2)
    add_subdirectory(../render2 ${CMAKE_CURRENT_BINARY_DIR}/pvf-render2)
endif()

add_library(pvf_mcp2 STATIC
    Service.cpp
    Support.cpp
    ProjectTools.cpp
    EntityContentSupport.cpp
    EntityCreateTools.cpp
    EntityReadTools.cpp
    EntityEditTools.cpp
    EntityTrimTools.cpp
    EntityDeleteTools.cpp
    EntityToolsInternal.h
    KeyframeTools.cpp
    SolverTools.cpp
    PreviewTools.cpp
    AssetTools.cpp
    SchemaTools.cpp
    HttpServer.h
    HttpServer.cpp
)

target_include_directories(pvf_mcp2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_mcp2 PUBLIC
    pvf_model
    pvf_store
    pvf_assets
    pvf_anim
    pvf_render2
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)
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
        trimSolverCutPose
        moveTrimReparentValidation
        reparentPreserveWorldKeepsPosition
        lockedEntityRejectsWrite
        deleteRequiresConfirm
        assetImportDeduplicates
        assetImportOutsideNeedsConfirm
        markersSetListRemove
        historyUndoRedo
        defaultRowIsHighestPlusOne
        twoCallsTwoCommits
        createManyRollsBack
        keyframesSetManyRollsBack
        assetAssignAndUnassign
        duplicateSubtreeOneCommit
        projectUpdateRejectsFpsAndResolution
        deleteManyMergesHierarchy
        listToolsAndSchema
        solverCreateUpdateDelete
        solverValidationRejects
        viewStateReturnsInjected
        previewRenderFrameWritesPng
        previewRejectsOversize
        solverEvaluateNestedEntity
        moveRowOnlyAndGetDefaults
        followParentRangeUpdateAligns
        entityGetIncludesSolvers
        textualContentCanBecomeEmpty
        specialContentCreateAndUpdate
        specialContentRejectsUnknownFields
        writeFlagsReflectService
        previewDocumentContract
        previewRateLimit
        assetSourceInClamped
        assetMissingContentStillListed
        deleteReportsSolverDependencies
        reparentPreserveWorldRejectsKeyframes
        assetImportRejectsOversize
        stopBlocksWrites
        stopKeepsReadsWorking
        resumeRestoresWrites
        createManyAbortsMidBatch
        writeToolClassification
    )

    foreach(test_case IN LISTS PVF_MCP2_TEST_CASES)
        add_test(NAME mcp2.${test_case} COMMAND pvf_mcp2_tests ${test_case})
        set_tests_properties(mcp2.${test_case} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()

    add_executable(pvf_mcp2_http_tests tests/http_server_tests.cpp)
    target_link_libraries(pvf_mcp2_http_tests PRIVATE
        pvf_mcp2
        Qt6::Test
        Qt6::Network
    )

    set(PVF_MCP2_HTTP_TEST_CASES
        httpInitialize
        httpToolsList
        httpToolsCallProjectGet
        httpSecurityAndLimits
        httpNotificationsAndErrors
        httpCredentialsPersistAcrossRestart
        httpDefaultConfigUsesFixedPort
        httpPortConflictFailsClearly
        httpStopAiBlocksWrites
    )

    foreach(test_case IN LISTS PVF_MCP2_HTTP_TEST_CASES)
        add_test(NAME mcp2.${test_case} COMMAND pvf_mcp2_http_tests ${test_case})
        set_tests_properties(mcp2.${test_case} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
