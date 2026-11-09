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
    sourceOffset
    shortAudioPadding
    noAudio
    hiddenAndInvisible
    playbackNoAudio
    playbackNoDevice
)

foreach(test_case IN LISTS PVF_AUDIO2_TEST_CASES)
    add_test(NAME audio2.${test_case} COMMAND pvf_audio2_tests ${test_case})
    set_tests_properties(audio2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# Guarantee that this test really runs without an audio output device.
set_tests_properties(audio2.playbackNoDevice PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=/nonexistent")
