cmake_minimum_required(VERSION 3.21)

project(pvf-assets VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)

add_library(pvf_assets STATIC
    AssetHash.cpp
    AssetTypeDetect.cpp
    MediaProbe.cpp
    Pvhtml.cpp
    WavExtract.cpp
    AssetStore.cpp
)

target_include_directories(pvf_assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(pvf_assets
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE PkgConfig::FFMPEG
)

add_executable(pvf_assets_tests tests/TestAssets.cpp)
target_link_libraries(pvf_assets_tests PRIVATE pvf_assets Qt6::Test)

enable_testing()

set(PVF_ASSET_TEST_CASES
    sha256KnownFile
    sha256DirectoryStable
    detectTypeByExtensionAndMagic
    probeImage
    probeAudioAndExtractWav
    probeVideoAndExtractWav
    importFileDeduplicates
    storeGetListRemove
    missingAssetReturnsEmpty
    pvhtmlDirectoryImport
    pvhtmlZipImport
    pvhtmlRejectsMissingEntry
    pvhtmlRejectsSymlink
    videoImportWithAudio
)

foreach(test_case IN LISTS PVF_ASSET_TEST_CASES)
    add_test(NAME assets.${test_case} COMMAND pvf_assets_tests ${test_case})
endforeach()
