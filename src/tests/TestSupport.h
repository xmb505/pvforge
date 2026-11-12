#pragma once

// Shared harness for the pvforge self-tests.
//
// The tests used to live in one 27k-line src/main.cpp. They are now split into
// per-module translation units under src/tests/; this header carries the include
// set they all rely on plus the helpers that more than one test file uses.
//
// Nothing here belongs in the release `pvforge` binary: src/tests/ is compiled
// only into `pvforge-tests` (see PVFORGE_BUILD_SELF_TESTS in CMakeLists.txt).

#include "app/AppIcon.h"
#include "core/Commands.h"
#include "core/Camera.h"
#include "core/History.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "media/MediaImporter.h"
#include "media/AudioDecoder.h"
#include "media/VideoPlayer.h"
#include "media/VideoProxy.h"
#include "media/DecoderInfo.h"
#include "render/CameraRenderer.h"
#include "render/SceneRenderer.h"
#include "audio/AudioEngine.h"
#include "app/MainWindow.h"
#include "app/EditorSession.h"
#include "app/EditActions.h"
#include "app/NewProjectDialog.h"
#include "app/ExportDialog.h"
#include "app/ExportProgressDialog.h"
#include "app/SettingsDialog.h"
#include "app/SystemInfo.h"
#include "app/WelcomeWindow.h"
#include "anim/Anim.h"
#include "assets/AssetStore.h"
#include "assets/AssetTypeDetect.h"
#include "gizmo/Gizmo.h"
#include "html/HtmlCaptureTest.h"
#include "html/HtmlLayerTest.h"
#include "core/Project.h"
#include "export2/Export.h"
#include "mcp2/Service.h"
#include "mcp2/HttpServer.h"
#include "model/Project.h"
#include "model/Uuid.h"
#include "render2/Render.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "next/history/HistoryPanel.h"
#include "next/properties/SolverPreviewWidget.h"
#include "next/settings/SettingsStore.h"
#include "workbench/Qoi.h"
#include "core/CheckpointManager.h"
#include "core/ComicBox.h"
#include "core/PerfMonitor.h"
#include "perf/FrameProfiler.h"
#include "perf/SystemStats.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "ui/BrandWallDialog.h"
#include "ui/PropertiesPanel.h"
#include "ui/TextLayoutDialog.h"
#include "ui/WorkbenchPanel.h"
#include "media/VideoDecoder.h"
#include "mcp/McpServer.h"
#include "mcp/StdioBridgeTest.h"
#include "mcp/McpCommandService.h"
#include "ui/BezierEditorDialog.h"
#include "ui/CameraMonitor.h"
#include "ui/EasingPicker.h"
#include "ui/CollapsibleSection.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CurveEditor.h"
#include "ui/McpLogPanel.h"
#include "ui/PerfMonitorWindow.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WaveformView.h"
#include "ui/WorkbenchPanel.h"

#include <QApplication>
#include <QAction>
#include <QBuffer>
#include <QComboBox>
#include <QCryptographicHash>
#include <QClipboard>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QCheckBox>
#include <QFocusEvent>
#include <QDataStream>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFont>
#include <QHostAddress>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QLineEdit>
#include <QJsonDocument>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QEventLoop>
#include <QProgressBar>
#include <QPushButton>
#include <QQueue>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QWidgetAction>
#include <QWheelEvent>
#include <cmath>
#include <functional>
#include <thread>
#include <memory>

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
