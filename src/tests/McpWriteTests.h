#pragma once

namespace pvf::tests {

// `--mcp-write-test`: McpCommandService write path (arming, scene scope,
// retire, batches, emergency stop, scene.create and project limits).
int runMcpWriteTest();

} // namespace pvf::tests
