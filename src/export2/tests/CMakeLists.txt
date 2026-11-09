add_executable(pvf_export2_tests
    Export2Tests.cpp
)

target_link_libraries(pvf_export2_tests PRIVATE
    pvf_export2
    Qt6::Gui
    PkgConfig::FFMPEG
)

set(PVF_EXPORT2_TEST_CASES
    exportRedVector
    silentProject
    audioMux
    audioSourceOffset
    missingAudioSkipped
    missingCamera
    cancelRemovesFile
    oversizedOutput
    frameProviders
    noFrameProviderPlaceholder
    resolutionScale4
)

foreach(test_case IN LISTS PVF_EXPORT2_TEST_CASES)
    add_test(NAME export2.${test_case} COMMAND pvf_export2_tests ${test_case})
    set_tests_properties(export2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
