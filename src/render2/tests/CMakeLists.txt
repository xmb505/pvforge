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
    specialContentRender
    specialContentOpacity
    specialContentFollowsParent
)

foreach(test_case IN LISTS PVF_RENDER2_TEST_CASES)
    add_test(NAME render2.${test_case} COMMAND pvf_render2_tests ${test_case})
    set_tests_properties(render2.${test_case} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
