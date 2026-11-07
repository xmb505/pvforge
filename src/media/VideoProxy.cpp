#include "VideoProxy.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace pvf {

namespace {

// Preview resolution and keyframe interval. Short GOPs are the important part:
// a 140 s 1080p H.264 source has keyframes about every 8 s, so seeking can
// decode ~250 frames; the proxy needs at most 15.
constexpr int kProxyWidth = 960;
constexpr int kProxyGop = 15;

QString proxyDirectory()
{
    const QString cache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty())
        return QString();
    QDir directory(cache + QStringLiteral("/proxies"));
    if (!directory.exists() && !QDir().mkpath(directory.path()))
        return QString();
    return directory.path();
}

} // namespace

QString videoProxyCachePath(const QString& sourcePath)
{
    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile())
        return QString();
    const QString directory = proxyDirectory();
    if (directory.isEmpty())
        return QString();

    const QByteArray key =
        QStringLiteral("pvforge-proxy-v1|%1|%2|%3|%4|%5")
            .arg(kProxyWidth)
            .arg(kProxyGop)
            .arg(info.absoluteFilePath())
            .arg(info.size())
            .arg(info.lastModified().toMSecsSinceEpoch())
            .toUtf8();
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return QDir(directory).filePath(hash + QStringLiteral(".mp4"));
}

bool videoProxyReady(const QString& sourcePath)
{
    const QString path = videoProxyCachePath(sourcePath);
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    return info.isFile() && info.size() > 0;
}

QString videoDecodePath(const QString& sourcePath)
{
    const QString path = videoProxyCachePath(sourcePath);
    if (!path.isEmpty())
    {
        const QFileInfo info(path);
        if (info.isFile() && info.size() > 0)
            return path;
    }
    return sourcePath;
}

VideoProxyGenerator::VideoProxyGenerator(QObject* parent)
    : QObject(parent)
    , m_ffmpeg(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")))
{
}

VideoProxyGenerator::~VideoProxyGenerator()
{
    if (m_process)
    {
        m_process->kill();
        m_process->waitForFinished(1500);
        m_process = nullptr;
    }
    if (!m_currentTemporary.isEmpty())
        QFile::remove(m_currentTemporary);
}

void VideoProxyGenerator::ensure(const QString& sourcePath)
{
    if (sourcePath.isEmpty() || m_known.contains(sourcePath))
        return;
    m_known.insert(sourcePath);

    if (videoProxyReady(sourcePath))
        return;
    if (m_ffmpeg.isEmpty())
    {
        emit finished(sourcePath, false);
        return;
    }

    const QString output = videoProxyCachePath(sourcePath);
    if (output.isEmpty())
    {
        emit finished(sourcePath, false);
        return;
    }

    Job job;
    job.source = sourcePath;
    job.output = output;
    job.temporary = output + QStringLiteral(".part.mp4");
    m_queue.append(job);
    startNext();
}

bool VideoProxyGenerator::isGenerating(const QString& sourcePath) const
{
    return m_currentSource == sourcePath;
}

void VideoProxyGenerator::startNext()
{
    if (m_process || m_queue.isEmpty())
        return;

    const Job job = m_queue.takeFirst();
    m_currentSource = job.source;
    m_currentOutput = job.output;
    m_currentTemporary = job.temporary;
    QFile::remove(m_currentTemporary);

    auto* process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                finishCurrent(status == QProcess::NormalExit && exitCode == 0);
            });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // FailedToStart does not emit finished().
        if (error == QProcess::FailedToStart)
            finishCurrent(false);
    });

    const QStringList arguments{
        QStringLiteral("-y"),
        QStringLiteral("-v"),
        QStringLiteral("error"),
        QStringLiteral("-i"),
        job.source,
        QStringLiteral("-map"),
        QStringLiteral("0:v:0"),
        QStringLiteral("-an"),
        QStringLiteral("-vf"),
        QStringLiteral("scale=%1:-2").arg(kProxyWidth),
        QStringLiteral("-c:v"),
        QStringLiteral("libx264"),
        QStringLiteral("-preset"),
        QStringLiteral("ultrafast"),
        QStringLiteral("-crf"),
        QStringLiteral("27"),
        QStringLiteral("-g"),
        QString::number(kProxyGop),
        QStringLiteral("-keyint_min"),
        QString::number(kProxyGop),
        QStringLiteral("-sc_threshold"),
        QStringLiteral("0"),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-movflags"),
        QStringLiteral("+faststart"),
        QStringLiteral("-f"),
        QStringLiteral("mp4"),
        QStringLiteral("-threads"),
        QStringLiteral("4"),
        job.temporary};

    emit started(job.source);
    process->start(m_ffmpeg, arguments);
}

void VideoProxyGenerator::finishCurrent(bool ffmpegOk)
{
    if (!m_process)
        return;

    QProcess* process = m_process;
    m_process = nullptr;
    process->deleteLater();

    const QString source = m_currentSource;
    const QString output = m_currentOutput;
    const QString temporary = m_currentTemporary;
    m_currentSource.clear();
    m_currentOutput.clear();
    m_currentTemporary.clear();

    bool success = ffmpegOk;
    const QFileInfo temporaryInfo(temporary);
    if (success && (!temporaryInfo.isFile() || temporaryInfo.size() <= 0))
        success = false;
    if (success)
    {
        QFile::remove(output);
        success = QFile::rename(temporary, output);
    }
    if (!success)
        QFile::remove(temporary);

    emit finished(source, success);
    startNext();
}

} // namespace pvf
