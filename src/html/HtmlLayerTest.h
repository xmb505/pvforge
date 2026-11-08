#pragma once

namespace pvf {
namespace html {

// End-to-end project tests for M6 HTML layers. They skip cleanly when no
// supported browser is installed; the export test also skips without ffmpeg.
int htmlLayerTest();
int htmlExportTest();

} // namespace html
} // namespace pvf
