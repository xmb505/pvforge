cmake_minimum_required(VERSION 3.21)

project(PVForgeHtml2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network)

# The Pvhtml renderer reuses the proven Chrome/CDP plumbing from the old HTML
# module without linking the old pvforge target.
add_library(pvf_html2 STATIC
    PvhtmlRenderer.h
    PvhtmlRenderer.cpp
    LocalBundleServer.h
    LocalBundleServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../html/CdpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../html/CdpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../html/PipeCdpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../html/WsClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../html/HtmlBrowser.cpp
)

target_include_directories(pvf_html2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(pvf_html2 PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_compile_features(pvf_html2 PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
