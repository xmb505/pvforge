cmake_minimum_required(VERSION 3.21)

project(PVForgeStore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

if(NOT TARGET pvf_model)
    add_subdirectory(../model ${CMAKE_CURRENT_BINARY_DIR}/pvf-model)
endif()

if(NOT TARGET pvf_assets)
    add_subdirectory(../assets ${CMAKE_CURRENT_BINARY_DIR}/pvf-assets)
endif()

add_library(pvf_store STATIC
    ProjectStore.cpp
    ProjectStoreFiles.cpp
    ProjectStoreGit.cpp
    ProjectStoreSupport.h
)

target_include_directories(pvf_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_store PUBLIC pvf_model pvf_assets Qt6::Core)
target_compile_features(pvf_store PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    # Standalone module builds do not see the top-level test-hook switch.
    add_compile_definitions(PVFORGE_TEST_HOOKS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    add_executable(pvf_store_tests
        tests/store_tests.cpp
        tests/store_tree_tests.cpp
        tests/store_tests.h
    )
    target_link_libraries(pvf_store_tests PRIVATE pvf_store Qt6::Test)

    set(PVF_STORE_TEST_CASES
        createOpenRoundTrip
        saveEntityReopen
        saveProjectRoundTrip
        deleteEntityFile
        commitAndHistory
        assetImportCommit
        commitFailureReportsStderr
        undoRedoRestoresText
        undoProjectSettings
        undoNewAndDeletedFiles
        undoStackLimits
        assetsSurviveUndo
        corruptProjectJson
        corruptEntityJson
        nonGitDirectoryRejected
        missingGitCreateFallsBack
        createRejectsInvalidInput
        cleanProjectHasNoDirtyText
        dirtyTextDetectedOnOpen
        commitRecoveredTextCreatesCommit
        discardDirtyTextRestores
        restoreCommitRewindsText
        restoreCommitDeletesNewEntities
        restoreCommitRejectsDirtyText
        restoreCommitRejectsInvalidHash
        restoreCommitClearsStacksAndKeepsAssets
        restoreCommitRollsBackOnCommitFailure
        createWithoutGit
        openWithoutGitRepoAndGitRequested
        enableGitLater
        disableGitThenCommitStillUndoable
        setGitEnabledReportsMissingGit
        openRejectsFutureVersion
        openRejectsUnmigratableVersion
        openRejectsLegacyFlatFormat
        openRejectsFutureEntityVersion
        openRejectsStaleEntityVersion
        openStrictRequiredFields
        unknownFieldsSurviveReopen
        nestedEntityTreeRoundTrip
        entityTreeRejectsInconsistentNesting
        entityTreeRejectsDuplicateUuid
        entityTreeRejectsRootFileNameMismatch
    )

    foreach(test_case IN LISTS PVF_STORE_TEST_CASES)
        add_test(NAME store.${test_case} COMMAND pvf_store_tests ${test_case})
    endforeach()
endif()
