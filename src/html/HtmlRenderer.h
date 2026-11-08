#pragma once

#include "html/HtmlAsset.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>

class QJsonObject;
class QTimer;

namespace pvf {
namespace html {

class HtmlBrowser;

// Everything needed to render one deterministic frame. `vars` uses the
// canonical `component.variable` keys; unknown keys are ignored and missing
// ones fall back to the manifest default.
struct FrameRequest
{
    HtmlAsset asset;
    QSize pageSize;                 // capture pixels; empty: asset.logicalSize
    QHash<QString, double> vars;
    int frame = 0;
    double time = 0.0;              // seconds
    bool deterministic = true;      // pin Date/performance/rAF and CSS motion
    int timeoutMs = 1000;           // wait budget for pvf.ready/rendered
};

// Owns one CDP page. Per the M6 plan a layer maps to exactly one page and one
// HTML asset. Requests are asynchronous; the callback is always invoked once,
// from this object's thread.
//
// Backpressure policy: at most one request is in flight. A newer request
// replaces any queued request, and marks the in-flight request superseded.
// The superseded callback receives an error immediately; the newest request
// starts as soon as the browser has been told to abort the old wait. Late CDP
// responses and timers are ignored by generation number.
// The browser must outlive every renderer created from it.
class HtmlRenderer : public QObject
{
    Q_OBJECT

public:
    using FrameCallback = std::function<void(const QImage& image, const QString& error)>;

    explicit HtmlRenderer(HtmlBrowser* browser, QObject* parent = nullptr);
    ~HtmlRenderer() override;

    bool hasPage() const { return !m_sessionId.isEmpty(); }
    void requestFrame(const FrameRequest& request, FrameCallback done);
    // Fails the active and queued requests, and aborts the page-side wait.
    void cancel(const QString& reason = QStringLiteral("HTML render cancelled"));
    QString lastError() const { return m_lastError; }

private:
    bool current(quint64 generation) const;
    void startRequest(const FrameRequest& request, FrameCallback done);
    void ensureSession(quint64 generation, std::function<void()> ready);
    void prepareAndCapture(quint64 generation, const FrameRequest& request);
    void finish(quint64 generation, const QImage& image, const QString& error);
    void abortPage(quint64 generation);
    void resetSession(bool closeTarget);
    void startPending();
    QString validate(const FrameRequest& request) const;
    QHash<QString, double> effectiveVariables(const FrameRequest& request) const;
    QString buildScript(const FrameRequest& request, const QHash<QString, double>& vars,
                        quint64 token) const;
    QString buildHostDocument() const;
    QString buildIsolatedDocument(const QString& html, const QString& bootScript) const;
    QString buildWaitExpression(int timeoutMs) const;
    static QString exceptionText(const QJsonObject& result);

    HtmlBrowser* m_browser = nullptr;
    QTimer* m_deadline = nullptr;
    QString m_sessionId;
    QString m_targetId;
    QString m_frameId;
    QString m_lastError;

    bool m_inFlight = false;
    bool m_finishing = false;
    quint64 m_generation = 0;
    quint64 m_activeGeneration = 0;
    FrameRequest m_activeRequest;
    FrameCallback m_activeDone;

    bool m_hasPending = false;
    FrameRequest m_pendingRequest;
    FrameCallback m_pendingDone;
};

} // namespace html
} // namespace pvf
