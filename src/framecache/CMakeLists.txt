cmake_minimum_required(VERSION 3.21)

project(PVForgeFrameCache VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Sql)

include(CTest)

# Shared frame-cache infrastructure: one SQLite schema, one directory LRU and
# one decoded-image LRU.  The workbench and htmlcache facades inject their key
# generation, codec choice and per-file validation.
add_library(pvf_framecache STATIC
    CacheUsage.cpp
    ImageLru.cpp
    LegacyImport.cpp
    Schema.cpp
    Store.cpp
)
target_include_directories(pvf_framecache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_framecache PUBLIC Qt6::Core Qt6::Gui Qt6::Sql)
target_compile_features(pvf_framecache PUBLIC cxx_std_20)

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    add_executable(pvf_framecache_tests tests/framecache_tests.cpp)
    target_link_libraries(pvf_framecache_tests PRIVATE pvf_framecache Qt6::Test)

    set(PVF_FRAMECACHE_TEST_CASES
        schemaIsShared
        schemaMigratesWorkbenchLegacy
        schemaMigratesHtmlcacheLegacy
        putGetSurvivesRestart
        lruEvictsOldest
        orphanFilesSwept
        statsCountersShared
        updateRepairsSize
        rejectsUnsafeKeys
        memoryLruOrderAndBudget
        memoryLruTagClear
        usageSeparatesFramesFromIndex
        clearReportsFreedBytes
        clearReportsUndeletableFiles
        budgetShrinkEvictsOldestFirst
        budgetShrinkScansOnce
    )

    foreach(test_case IN LISTS PVF_FRAMECACHE_TEST_CASES)
        add_test(NAME framecache.${test_case}
                 COMMAND pvf_framecache_tests ${test_case})
        set_tests_properties(framecache.${test_case} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 60
        )
    endforeach()
endif()
