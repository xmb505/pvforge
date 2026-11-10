cmake_minimum_required(VERSION 3.21)

project(PVForgeWorkbench VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Sql)

include(CTest)

# Standalone builds pull in render2 (and through it model/assets/anim/solver).
# Keep dependency tests disabled; this module only runs its own suite.
if(NOT TARGET pvf_render2)
    set(PVF_WORKBENCH_BUILD_TESTING ${BUILD_TESTING})
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../render2
                     ${CMAKE_CURRENT_BINARY_DIR}/render2_dep)
    set(BUILD_TESTING ${PVF_WORKBENCH_BUILD_TESTING} CACHE BOOL "" FORCE)
endif()

add_library(pvf_workbench STATIC
    Cache.cpp
    MetaBuilder.cpp
    PrewarmService.cpp
)

target_include_directories(pvf_workbench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(pvf_workbench
    PUBLIC pvf_render2 Qt6::Core Qt6::Gui Qt6::Sql
)

target_compile_features(pvf_workbench PUBLIC cxx_std_20)

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    add_executable(pvf_workbench_tests tests/WorkbenchCacheTests.cpp)
    target_link_libraries(pvf_workbench_tests PRIVATE pvf_workbench Qt6::Test)

    set(PVF_WORKBENCH_TEST_CASES
        metaIsStable
        metaTracksActiveEntity
        metaIgnoresInactiveEntity
        metaTracksVisibilityAndContent
        metaTracksMissingAsset
        metaTracksCameraBackgroundAndTarget
        metaTracksHtmlVariables
        metaTracksGenerativeContent
        signatureAndSize
        cacheStoreLookup
        cacheMetaMismatch
        cacheSurvivesRestart
        cacheDiskLru
        cacheMemoryBudget
        cacheClear
        cachePerCameraDirectories
        prewarmInitialScan
        prewarmSkipsCachedFrames
        prewarmRerendersChangedProject
        prewarmCancel
        prewarmPauseResume
        prewarmProgressIsMonotonic
        prewarmFailedFrameContinues
    )

    foreach(test_case IN LISTS PVF_WORKBENCH_TEST_CASES)
        add_test(NAME workbench.${test_case}
                 COMMAND pvf_workbench_tests ${test_case})
        set_tests_properties(workbench.${test_case} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 120
        )
    endforeach()
endif()
