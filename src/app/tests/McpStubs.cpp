// Test-only MCP backend.
//
// Phase B tests exercise EditActions/EditorSession/TimelineAdapter while the
// real mcp2 sources are still waiting for Phase D.  EditorSession only needs
// the service/server surface below; these definitions keep the test target
// linkable without compiling the (currently blocked) mcp2 tool sources.

#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"

#include <utility>

namespace pvf::mcp2 {

Service::Service(store::ProjectStore* store, assets::AssetStore* assets,
                 QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_assets(assets)
{
}

void Service::setViewStateProvider(ViewStateProvider provider)
{
    m_viewStateProvider = std::move(provider);
}

void Service::setVideoFrameProvider(FrameProvider provider)
{
    m_videoFrameProvider = std::move(provider);
}

void Service::setHtmlFrameProvider(FrameProvider provider)
{
    m_htmlFrameProvider = std::move(provider);
}

void Service::setBatchCheckpoint(BatchCheckpoint checkpoint)
{
    m_batchCheckpoint = std::move(checkpoint);
}

void Service::setExternalImportConfirmer(ExternalImportConfirmer confirmer)
{
    m_externalImportConfirmer = std::move(confirmer);
}

void Service::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

void Service::setWritesEnabled(bool enabled)
{
    m_writesEnabled = enabled;
}

bool Service::writesEnabled() const
{
    return m_writesEnabled;
}

void Service::requestStop()
{
    m_stopped = true;
}

void Service::resumeWrites()
{
    m_stopped = false;
}

bool Service::isStopped() const
{
    return m_stopped;
}

bool Service::isWriteTool(const QString&)
{
    return true;
}

QJsonObject Service::execute(const QString&, const QJsonObject&)
{
    return {};
}

QJsonObject Service::listTools() const
{
    return {};
}

HttpServer::HttpServer(Service* service, QObject* parent)
    : QObject(parent)
    , m_service(service)
{
}

HttpServer::~HttpServer() = default;

void HttpServer::setToken(const QByteArray& token)
{
    m_token = token;
}

void HttpServer::resetCredentials()
{
}

void HttpServer::setEndpointFilePath(const QString& path)
{
    m_endpointFilePath = path;
}

QString HttpServer::endpointFilePath() const
{
    return m_endpointFilePath;
}

void HttpServer::setConfigFilePath(const QString& path)
{
    m_configFilePath = path;
}

QString HttpServer::configFilePath() const
{
    return m_configFilePath;
}

void HttpServer::stopAi()
{
    if (m_service)
        m_service->requestStop();
    emit stoppedChanged(true);
}

void HttpServer::resumeAi()
{
    if (m_service)
        m_service->resumeWrites();
    emit stoppedChanged(false);
}

bool HttpServer::aiStopped() const
{
    return m_service && m_service->isStopped();
}

quint16 HttpServer::start(quint16, QString*)
{
    return 0;
}

void HttpServer::stop()
{
}

bool HttpServer::isRunning() const
{
    return false;
}

QString HttpServer::endpointUrl() const
{
    return {};
}

} // namespace pvf::mcp2
