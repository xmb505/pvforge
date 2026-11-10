cmake_minimum_required(VERSION 3.21)

project(PVForgeNextSettings VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)

add_library(pvf_next_settings STATIC
    SettingsStore.cpp
    SettingsStore.h
    GitDetect.cpp
    GitDetect.h
    SettingsDialog.cpp
    SettingsDialog.h
    OnboardingDialog.cpp
    OnboardingDialog.h
)

target_include_directories(pvf_next_settings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_settings PUBLIC
    Qt6::Core
    Qt6::Widgets
)

target_compile_features(pvf_next_settings PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_settings_tests tests/SettingsTests.cpp)
    target_link_libraries(pvf_next_settings_tests PRIVATE
        pvf_next_settings
        Qt6::Test
    )

    add_test(NAME pvf_next_settings_tests COMMAND pvf_next_settings_tests)
    set_tests_properties(pvf_next_settings_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config"
    )
endif()
