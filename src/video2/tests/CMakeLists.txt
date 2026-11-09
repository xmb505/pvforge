add_executable(pvf_video2_tests
    VideoFramesTests.cpp
)

target_link_libraries(pvf_video2_tests PRIVATE
    pvf_video2
    Qt6::Test
)

set(PVF_VIDEO2_TEST_CASES
    frameZero
    laterFrameDiffers
    frameBoundary
    fpsControlsTime
    proxySize
    sourceSize
    cacheHit
    lruEvicts
    missingFile
    noVideoStream
)

foreach(test_case IN LISTS PVF_VIDEO2_TEST_CASES)
    add_test(NAME video2.${test_case} COMMAND pvf_video2_tests ${test_case})
    set_tests_properties(video2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
