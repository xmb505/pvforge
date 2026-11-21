add_executable(pvf_audio2_tests
    Audio2Tests.cpp
)

target_link_libraries(pvf_audio2_tests PRIVATE
    pvf_audio2
    Qt6::Multimedia
)

set(PVF_AUDIO2_TEST_CASES
    mixTwoTones
    volumeHalf
    audioFade
    sourceOffset
    shortAudioPadding
    noAudio
    scrubSlice
    scrubPlayback
    hiddenAndInvisible
    playbackNoAudio
    playbackNoDevice
    streamLongRange
    streamSeekOnlyMixesFromPosition
    streamSampleExactAndBounded
    peaks
)

foreach(test_case IN LISTS PVF_AUDIO2_TEST_CASES)
    add_test(NAME audio2.${test_case} COMMAND pvf_audio2_tests ${test_case})
    set_tests_properties(audio2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none")
endforeach()

# Guarantee that this test really runs without an audio output device.
set_tests_properties(audio2.playbackNoDevice audio2.scrubPlayback PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;XDG_RUNTIME_DIR=/nonexistent")
set_tests_properties(audio2.peaks PROPERTIES SKIP_RETURN_CODE 77)
