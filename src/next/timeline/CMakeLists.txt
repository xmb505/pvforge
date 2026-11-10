cmake_minimum_required(VERSION 3.21)

project(PVForgeNextTimeline VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Test)

if(NOT TARGET pvf_next_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core
                     ${CMAKE_CURRENT_BINARY_DIR}/core_dep)
endif()

if(NOT TARGET pvf_audio2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../audio2
                     ${CMAKE_CURRENT_BINARY_DIR}/audio2_dep)
endif()

if(NOT TARGET pvf_render2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../render2
                     ${CMAKE_CURRENT_BINARY_DIR}/render2_dep)
endif()

add_library(pvf_next_timeline STATIC
    MarkerDialog.cpp
    MarkerDialog.h
    TimelineWidget.cpp
    TimelineWidget.h
)

# Range/split helpers shared by the new timeline, the properties panel and the
# legacy delivery shell.  Kept out of pvf_next_timeline so pvforge does not pull
# in the reference UI or Qt Test.
add_library(pvf_entity_range STATIC
    EntityRange.cpp
    EntityRange.h
)

target_include_directories(pvf_entity_range PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_entity_range PUBLIC
    pvf_next_core
    pvf_render2
    Qt6::Core
    Qt6::Gui
)

target_compile_features(pvf_entity_range PUBLIC cxx_std_20)

target_include_directories(pvf_next_timeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(pvf_next_timeline PUBLIC
    pvf_next_core
    pvf_entity_range
    pvf_audio2
    pvf_render2
    Qt6::Widgets
    Qt6::Test
)

target_compile_features(pvf_next_timeline PUBLIC cxx_std_20)

include(CTest)

if(BUILD_TESTING)
    add_executable(pvf_next_timeline_tests tests/timeline_tests.cpp)
    target_link_libraries(pvf_next_timeline_tests PRIVATE
        pvf_next_timeline
        Qt6::Test
    )

    set(PVF_TIMELINE_TEST_CASES
        rowsAreBottomUp
        clickEntityBarSelects
        rulerClickSetsPlayhead
        dragMiddleMovesBoth
        dragParentMovesDescendants
        dragParentClampsAtZero
        dragLeftEdgeTrimsStart
        trimStartShiftsKeys
        trimEndDropsKeys
        trimParentClipsChild
        trimParentDeletesChild
        trimExpandShiftsKeysAndSourceIn
        trimStartRejectsNegativeSourceIn
        trimEndRejectsMissingMaterial
        trimSolverStartCutsSegment
        trimSolverEndClamps
        trimSolverOpenEndPins
        trimFailureRestoresSnapshot
        solverBandPaints
        privateTimelineShowsOnlyChildren
        plusButtonRequestsPrivateTimeline
        doubleClickInsertsGenericKeys
        doubleClickKeepsExistingTracks
        keyframeDiamondDragMovesAllTracks
        keyframeShiftMultiSelectDrag
        keyframeDeleteRemovesSelection
        keyframeCopyPasteSameEntity
        lockedEntityRejectsKeyframeEdits
        lockedAncestorRejectsChildEdits
        solverStartKeyDeleteRejected
        solverSegmentRejectsKeyframeMove
        dropAssetCreatesEntity
        dropOntoOccupiedRowUsesNextRow
        dropVideoCreatesAudioEntity
        dropAssetOutsidePrivateParentRejected
        dragEnterRejectsUnknownMime
        markersAddEditDelete
        markerDragChangesFrame
        markerValidationRejects
        markerShortcutAdds
        waveformPaintsForAudioEntity
        waveformMissingAssetPaintsFlat
        playheadAutoScrollFollowsBothWays
        rowSlotsStayFixedAfterDelete
        dropDeepRowKeepsRequestedSlot
        deepRowsScrollIntoView
        oneGestureCommitsOnceAndEscRestores
        rulerLabelsCenterOnTicks
        moveSnapsToNeighborEdges
        trimSnapsToNeighborEdge
        splitEntityCreatesRightHalf
        splitParentSplitsChildren
        splitVideoShiftsSourceIn
        splitRejectsBoundary
    )

    foreach(test_case IN LISTS PVF_TIMELINE_TEST_CASES)
        add_test(NAME timeline.${test_case}
                 COMMAND pvf_next_timeline_tests ${test_case})
        set_tests_properties(timeline.${test_case} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
