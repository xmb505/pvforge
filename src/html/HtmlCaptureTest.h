#pragma once

namespace pvf {
namespace html {

// Offscreen self-test for the CDP renderer. Returns 0 when it passes or when
// no supported browser is installed (the test then prints a skip message).
int htmlCaptureTest();

} // namespace html
} // namespace pvf
