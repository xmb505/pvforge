add_executable(pvf_anim_tests
    AnimTests.cpp
)

target_link_libraries(pvf_anim_tests PRIVATE
    pvf_anim
)

add_test(NAME pvf_anim_tests COMMAND pvf_anim_tests)
set_tests_properties(pvf_anim_tests PROPERTIES
    ENVIRONMENT "QT_IM_MODULE=none")
