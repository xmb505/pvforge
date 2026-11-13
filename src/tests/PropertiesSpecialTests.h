#pragma once

#include <QApplication>

namespace pvf::tests {

// `--special-test`: comic shapes/vertical text, scene annotations, brand wall,
// perf monitor and the workbench-facing UI flows. Extracted from
// PropertiesTests.cpp so the orchestration and the per-feature checks stay
// small enough to navigate.
int runPropertiesSpecialTest(QApplication& app);

} // namespace pvf::tests
