#pragma once

// Small shared helpers for the pvforge self-tests.
//
// This header deliberately carries no application/UI headers: those live in
// TestApp.h (current model/shell) and LegacyTestSupport.h (removed legacy
// shell). Keeping the helper layer narrow lets a test translation unit pull in
// only the headers it actually exercises.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace pvf::tests {

// Minimal HTTP client used by the MCP tests. The MCP transport is plain
// HTTP/1.1 over loopback, so a socket + QEventLoop is enough; pulling in a
// test-only HTTP dependency is not worth it.
struct McpHttpResponse
{
    int status = -1;
    int expectedBody = 0;
    QByteArray headers;
    QByteArray body;
    bool timedOut = false;
};

McpHttpResponse mcpExchange(quint16 port, const QByteArray& request,
                            int timeoutMs = 5000);
QByteArray mcpPostRequest(quint16 port, const QByteArray& payload,
                          const QByteArray& token,
                          const QByteArray& contentType, qint64 declaredLength,
                          const QByteArray& extraHeaders,
                          const QByteArray& hostHeader);
McpHttpResponse mcpHttpPost(quint16 port, const QByteArray& payload,
                            const QByteArray& token,
                            const QByteArray& contentType =
                                QByteArrayLiteral("application/json"),
                            qint64 declaredLength = -1,
                            const QByteArray& extraHeaders = QByteArray(),
                            const QByteArray& hostHeader = QByteArray());
bool mcpWaitUntil(const std::function<bool()>& predicate, int timeoutMs);
int mcpStatusFromRaw(const QByteArray& received);
QJsonObject mcpRpc(quint16 port, const QByteArray& token,
                   const QJsonObject& request, int* statusOut = nullptr);
QJsonObject mcpStructuredResult(const McpHttpResponse& response);
McpHttpResponse mcpCallTool(quint16 port, const QByteArray& token,
                            const QString& tool, const QJsonObject& args);

} // namespace pvf::tests
