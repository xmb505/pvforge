#pragma once

#include <QApplication>

namespace pvf::tests {

// `--timeline-edit-test`: EditActions model checks plus the timeline panel
// gestures (drag/trim/reparent/undo). Extracted from TimelineTests.cpp so the
// setup, model checks and UI checks each stay navigable.
int runTimelineEditTest(QApplication& app);

// `--timeline-edit-e2e`: self-contained end-to-end edit.  Creates its project
// inside a QTemporaryDir, reopens it from disk and checks the entity JSON the
// timeline drag wrote.
int runTimelineEditE2eTest(QApplication& app);

} // namespace pvf::tests
