#pragma once

#include <QByteArray>
#include <QString>

namespace pvf::mcp2::detail {

// Default discovery locations. The macOS endpoint path deliberately matches
// defaultMcpEndpointPath() in src/mcp/StdioBridge.cpp.
QString configDirectory();
QString defaultConfigPath();
QString defaultEndpointPath();

// Writes a token-bearing file atomically at mode 0600. The parent directory is
// created and tightened to 0700. Refuses to follow a symlink, overwrite a file
// owned by another user, or write into a directory owned by another user.
bool writePrivateFile(const QString& path, const QByteArray& data, QString* error);

// True when the pid recorded in an endpoint file cannot belong to a live
// PVForge instance of this user (dead pid, reused pid, or another user).
bool endpointPidIsStale(qint64 pid);

} // namespace pvf::mcp2::detail
