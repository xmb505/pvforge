#pragma once

// Compatibility layer for tests that still exercise the removed legacy shell
// (src/core/Project.*, src/html/HtmlCaptureTest.*). New tests must not include
// this; it keeps the legacy include set out of TestSupport.h and TestApp.h.

#include "TestSupport.h"
#include "TestApp.h"

#include "core/Project.h"
