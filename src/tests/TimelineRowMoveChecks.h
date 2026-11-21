#pragma once

#include <QApplication>

namespace pvf::tests {

// `--timeline-edit-test` row-move checks.  Kept in a separate translation unit
// so both the EditActions model rules and the timeline multi-selection drag
// stay under the per-file size budget.
int runTimelineRowMoveChecks(QApplication& app);

} // namespace pvf::tests
