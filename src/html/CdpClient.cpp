#include "html/CdpClient.h"

#include "html/CdpTransport.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QTimer>
#include <QUrl>

namespace pvf {
namespace html {

CdpClient::CdpClient(QObject* parent)
    : QObject(parent)
{
    // P0: replace the factory with a pipe transport when the fd 3/4 backend
    // lands. CdpClient does not know which transport it is using.
    m_transport = createLoopbackCdpTransport(this);
    connect(m_transport, &CdpTransport::messageReceived, this, &CdpClient::onMessage);
    connect(m_transport, &CdpTransport::disconnected, this, &CdpClient::onDisconnected);
    connect(m_transport, &CdpTransport::errorOccurred, this, [this](const QString& error) {
        if (!m_transport->isConnected() && !error.isEmpty())
            failAll(error);
    });
}

CdpClient::~CdpClient()
{
    close();
}

bool CdpClient::connectTo(const QUrl& endpoint, int timeoutMs, QString* error)
{
    failAll(QStringLiteral("reconnecting"));
    m_pending.clear();
    if (!m_transport->connectTo(endpoint, timeoutMs, error))
        return false;
    return true;
}

void CdpClient::close()
{
    m_transport->close();
    failAll(QStringLiteral("CDP client closed"));
}

bool CdpClient::isConnected() const
{
    return m_transport && m_transport->isConnected();
}

void CdpClient::call(const QString& method, const QJsonObject& params,
                     const QString& sessionId, int timeoutMs, Callback done)
{
    if (!isConnected())
    {
        if (done)
            done(QJsonObject{}, QStringLiteral("%1: CDP is not connected").arg(method));
        return;
    }

    const int id = m_nextId++;
    QJsonObject message;
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        message.insert(QStringLiteral("params"), params);
    if (!sessionId.isEmpty())
        message.insert(QStringLiteral("sessionId"), sessionId);

    m_pending.insert(id, Pending{method, std::move(done)});
    m_transport->sendMessage(
        QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));

    QTimer::singleShot(qMax(1, timeoutMs), this, [this, id, method]() {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        Pending pending = it.value();
        m_pending.erase(it);
        if (pending.done)
            pending.done(QJsonObject{},
                         QStringLiteral("%1 timed out after its CDP deadline").arg(method));
    });
}

void CdpClient::onMessage(const QString& message)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        emit eventReceived(QStringLiteral("invalid-json"), QJsonObject{}, QString());
        return;
    }

    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("id")))
    {
        const int id = object.value(QStringLiteral("id")).toInt();
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;   // late response after a timeout
        const Pending pending = it.value();
        m_pending.erase(it);

        const QJsonValue errorValue = object.value(QStringLiteral("error"));
        if (errorValue.isObject())
        {
            const QJsonObject errorObject = errorValue.toObject();
            const QString errorMessage =
                errorObject.value(QStringLiteral("message")).toString();
            const int code = errorObject.value(QStringLiteral("code")).toInt();
            const QString text = code != 0
                                     ? QStringLiteral("%1: %2 (code %3)")
                                           .arg(pending.method, errorMessage)
                                           .arg(code)
                                     : QStringLiteral("%1: %2").arg(pending.method, errorMessage);
            if (pending.done)
                pending.done(QJsonObject{}, text);
            return;
        }

        const QJsonValue resultValue = object.value(QStringLiteral("result"));
        const QJsonObject result = resultValue.isObject() ? resultValue.toObject()
                                                          : QJsonObject{};
        if (pending.done)
            pending.done(result, QString());
        return;
    }

    const QString method = object.value(QStringLiteral("method")).toString();
    if (!method.isEmpty())
    {
        emit eventReceived(method, object.value(QStringLiteral("params")).toObject(),
                           object.value(QStringLiteral("sessionId")).toString());
    }
}

void CdpClient::onDisconnected(const QString& reason)
{
    failAll(reason.isEmpty() ? QStringLiteral("DevTools connection closed") : reason);
    emit disconnected(reason);
}

void CdpClient::failAll(const QString& reason)
{
    const QHash<int, Pending> pending = m_pending;
    m_pending.clear();
    for (const Pending& item : pending)
        if (item.done)
            item.done(QJsonObject{}, QStringLiteral("%1: %2").arg(item.method, reason));
}

} // namespace html
} // namespace pvf
