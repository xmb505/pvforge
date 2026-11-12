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
    frameCacheHits
    staticFramesShareCache
    prefetchFillsCache
    finalFrameUncached
    frameKeyVaries
    asyncRequestLowRes
    lowResWaitTimeout
    lowResPreemption
    cancelPendingLowRes
    customCacheBudgets
    chromeExecutableOverride
    finalFrameSizeLimits
    finalRendererRestart
    finalRendererCrossThreadStop
    finalRendererRepeatedStartStop
    finalRendererStopWhileRendering
)

# Every case runs its own pvf_html2_tests process, and most of them launch a
# Chrome/DevTools-pipe renderer. Under `ctest -j4` several Chrome startups at
# once starve each other on CI runners (6.6.3 debug: "Browser.getVersion
# timed out"). RESOURCE_LOCK serialises only the tests that share this lock, so
# the rest of the suite keeps running in parallel.
foreach(test_case IN LISTS PVF_HTML2_TEST_CASES)
    add_test(NAME html2.${test_case} COMMAND pvf_html2_tests ${test_case})
    set_tests_properties(html2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180
        RESOURCE_LOCK pvforge_chrome)
endforeach()
