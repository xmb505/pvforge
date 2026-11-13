#pragma once

namespace pvf {

// Self-test for the stdio -> Streamable HTTP bridge. It exercises the
// forwarding class against a local QTcpServer and, when CMake provides the
// binary path, the pvforge-mcp CLI end to end.
int mcpBridgeTest();

} // namespace pvf
