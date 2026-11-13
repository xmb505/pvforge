#pragma once

#include <QApplication>

namespace pvf::tests {

// `--timeline-edit-test`: EditActions model checks plus the timeline panel
// gestures (drag/trim/reparent/undo). Extracted from TimelineTests.cpp so the
// setup, model checks and UI checks each stay navigable.
int runTimelineEditTest(QApplication& app);

} // namespace pvf::tests
