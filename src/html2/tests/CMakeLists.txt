add_executable(pvf_html2_tests
    Html2Tests.cpp
)

target_link_libraries(pvf_html2_tests PRIVATE
    pvf_html2
    Qt6::Gui
    Qt6::Network
)

set(PVF_HTML2_TEST_CASES
    serverServesAndRejects
    renderFrameColors
    timeoutDoesNotHang
)

foreach(test_case IN LISTS PVF_HTML2_TEST_CASES)
    add_test(NAME html2.${test_case} COMMAND pvf_html2_tests ${test_case})
    set_tests_properties(html2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180)
endforeach()
