add_executable(pvf_video2_tests
    VideoFramesTests.cpp
)

target_link_libraries(pvf_video2_tests PRIVATE
    pvf_video2
    Qt6::Test
)

add_executable(pvf_video2_sequence_tests
    VideoSequenceTests.cpp
)

target_link_libraries(pvf_video2_sequence_tests PRIVATE
    pvf_video2
    Qt6::Test
)

add_executable(pvf_video2_queue_tests
    VideoExtractionQueueTests.cpp
)

target_link_libraries(pvf_video2_queue_tests PRIVATE
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

set(PVF_VIDEO2_SEQUENCE_TEST_CASES
    extractCreatesIndexAndImages
    staticFramesDeduplicate
    lowFpsSourceDeduplicates
    frameLookupAndMemoryHit
    restartReadsDiskIndex
    missingWebpFallsBack
    smallDiskBudgetEvicts
    cancelMidwayKeepsProgress
    cancelFlagStops
    missingVideoFails
)

set(PVF_VIDEO2_QUEUE_TEST_CASES
    extractsAndSkipsCompletedJob
    cancelMidwayStops
    cancelImmediatelyEmitsCancelled
    cancelAllDropsPendingAndRunning
    missingVideoFails
)

foreach(test_case IN LISTS PVF_VIDEO2_TEST_CASES)
    add_test(NAME video2.${test_case} COMMAND pvf_video2_tests ${test_case})
    set_tests_properties(video2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

foreach(test_case IN LISTS PVF_VIDEO2_SEQUENCE_TEST_CASES)
    add_test(NAME video2.sequence.${test_case}
             COMMAND pvf_video2_sequence_tests ${test_case})
    set_tests_properties(video2.sequence.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endforeach()

foreach(test_case IN LISTS PVF_VIDEO2_QUEUE_TEST_CASES)
    add_test(NAME video2.queue.${test_case}
             COMMAND pvf_video2_queue_tests ${test_case})
    set_tests_properties(video2.queue.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endforeach()
