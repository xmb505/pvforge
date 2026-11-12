cmake_minimum_required(VERSION 3.21)

project(PVForgeHtml2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network)

# Standalone builds pull in the sibling cache module. A parent build that
# already defines pvf_htmlcache reuses that target.
if(NOT TARGET pvf_htmlcache)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../htmlcache ${CMAKE_BINARY_DIR}/pvf_htmlcache)
endif()

# The Pvhtml renderer reuses the proven Chrome/CDP plumbing, which now lives in
# src/htmlcommon/ (CdpClient/CdpTransport/WsClient/HtmlBrowser).  It used to sit
# in src/html/ next to the retired html-layer widgets; the shared transport was
# split out so the old module could go away without losing CDP.
add_library(pvf_html2 STATIC
    PvhtmlRenderer.h
    PvhtmlRenderer.cpp
    HtmlFrameService.h
    HtmlFrameService.cpp
    LocalBundleServer.h
    LocalBundleServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../htmlcommon/CdpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../htmlcommon/CdpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../htmlcommon/PipeCdpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../htmlcommon/WsClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../htmlcommon/HtmlBrowser.cpp
)

target_include_directories(pvf_html2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# QThread/QWaitCondition live in Qt6::Core; pvf_htmlcache supplies FrameKey and
# the memory/disk/tiered frame caches.
target_link_libraries(pvf_html2 PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    pvf_htmlcache
)

target_compile_features(pvf_html2 PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
