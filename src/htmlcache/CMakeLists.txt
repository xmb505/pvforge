cmake_minimum_required(VERSION 3.21)

project(PVForgeHtmlCache VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)
find_package(Qt6 6.5 QUIET COMPONENTS Sql)

include(CTest)

# Standalone builds pull in the sibling assets module. When htmlcache is added
# by a parent project that already defines pvf_assets, reuse that target.
if(NOT TARGET pvf_assets)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../assets ${CMAKE_BINARY_DIR}/pvf_assets)
endif()

add_library(pvf_htmlcache STATIC
    DiskCache.cpp
    FrameCache.cpp
    FrameKey.cpp
    MemoryCache.cpp
    TieredCache.cpp
)

target_include_directories(pvf_htmlcache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pvf_htmlcache
    PUBLIC pvf_assets Qt6::Core Qt6::Gui
)
target_compile_features(pvf_htmlcache PUBLIC cxx_std_20)

if(TARGET Qt6::Sql)
    target_link_libraries(pvf_htmlcache PRIVATE Qt6::Sql)
    target_compile_definitions(pvf_htmlcache PRIVATE PVF_HTMLCACHE_HAVE_SQLITE=1)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    add_executable(pvf_htmlcache_tests tests/htmlcache_tests.cpp)
    target_link_libraries(pvf_htmlcache_tests PRIVATE pvf_htmlcache Qt6::Test)

    set(PVF_HTMLCACHE_TEST_CASES
        canonicalKeyStable
        canonicalKeyDiffers
        webpRoundtrip
        memoryPutGet
        memoryLruEviction
        memoryClearAsset
        diskPutGet
        diskLruEviction
        diskClearAsset
        diskCorruptFileIsMiss
        diskIndexSurvivesRestart
        tieredColdHitBackfillsHot
        tieredPutAndClear
    )

    foreach(test_case IN LISTS PVF_HTMLCACHE_TEST_CASES)
        add_test(NAME htmlcache.${test_case} COMMAND pvf_htmlcache_tests ${test_case})
        set_tests_properties(htmlcache.${test_case} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 60
        )
    endforeach()
endif()
