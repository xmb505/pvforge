add_executable(pvf_anim_tests
    AnimTests.cpp
)

target_link_libraries(pvf_anim_tests PRIVATE
    pvf_anim
)

add_test(NAME pvf_anim_tests COMMAND pvf_anim_tests)
