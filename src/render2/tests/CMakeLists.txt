add_executable(pvf_render2_tests
    Render2Tests.cpp
)

target_link_libraries(pvf_render2_tests PRIVATE
    pvf_render2
    Qt6::Gui
)

set(PVF_RENDER2_TEST_CASES
    redVectorCenterPixel
    imageAssetOnCanvas
    textIsNotBlank
    childFollowsParent
    hiddenAndInvisible
    opacityHalfBlend
    missingAssetCheckerboard
    missingImageCard
    missingVideoAndHtmlCards
    presentAssetKeepsPendingPlaceholder
    corruptImageShowsMissingCard
    smallMissingRectDoesNotOverflow
    reimportSameHashRestoresImage
    cameraCrop
    cameraKeepsCanvasCoordinates
    cameraNegativeScaleFlips
    worldRectRendersOutsideCanvas
    cameraIgnoresWorldRect
    transparentOutsideCanvas
    transparentWebpOverheadIsSmall
    worldPoseAndShape
    solverSelfShapeTerminates
    fractionalProjectFrame
    frameProviderReceivesFractionalFrame
    animatedContentFieldMapping
    contentFallback
    textPixelSizeRenders
    vectorContentAnimationRenders
)

foreach(test_case IN LISTS PVF_RENDER2_TEST_CASES)
    add_test(NAME render2.${test_case} COMMAND pvf_render2_tests ${test_case})
    set_tests_properties(render2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none")
endforeach()

add_executable(pvf_render2_asset_tests AssetSizingTests.cpp)
target_link_libraries(pvf_render2_asset_tests PRIVATE pvf_render2 Qt6::Gui)
foreach(test_case IN ITEMS assetModelSizeIgnoresFramePixels html4xExportIsNotCropped)
    add_test(NAME render2.${test_case} COMMAND pvf_render2_asset_tests ${test_case})
    set_tests_properties(render2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none")
endforeach()

add_executable(pvf_render2_hidden_tests HiddenVisibilityTests.cpp)
target_link_libraries(pvf_render2_hidden_tests PRIVATE pvf_render2 Qt6::Gui)
add_test(NAME render2.hiddenVisibility COMMAND pvf_render2_hidden_tests)
set_tests_properties(render2.hiddenVisibility PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none")

add_executable(pvf_render2_canvas_tests CanvasSpaceTests.cpp)
target_link_libraries(pvf_render2_canvas_tests PRIVATE pvf_render2 Qt6::Gui)
add_test(NAME render2.canvasSpace COMMAND pvf_render2_canvas_tests)
set_tests_properties(render2.canvasSpace PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none")

add_executable(pvf_render2_cache_tests ImageCacheTests.cpp)
target_link_libraries(pvf_render2_cache_tests PRIVATE pvf_render2 Qt6::Gui)
add_test(NAME render2.imageCache COMMAND pvf_render2_cache_tests)
set_tests_properties(render2.imageCache PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none")
