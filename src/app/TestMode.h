#pragma once

// Single source of truth for "this process is an in-binary test / profiling
// run".
//
// main() decides once, from the exact switch names registered in
// src/tests/TestRegistry.cpp, and stores the answer here.  App code asks this
// flag instead of scanning argv itself: a project path that merely ends in
// `-test` (e.g. `--open /tmp/project-test`) is not a test switch, and a
// release build never enters test mode at all (it has no test registry).
//
// Only main() in the `pvforge-tests` target calls setTestMode(); in the
// shipping binary the flag stays false.

namespace pvf {

inline bool& testModeFlag()
{
    // Inline function with a function-local static: one instance per program,
    // shared between src/main.cpp and the pvf_app static library TUs.
    static bool flag = false;
    return flag;
}

inline bool testMode()
{
    return testModeFlag();
}

inline void setTestMode(bool value)
{
    testModeFlag() = value;
}

} // namespace pvf
