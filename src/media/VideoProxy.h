#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QProcess;

namespace pvf {

// Deterministic cache path for a video preview proxy. Empty when the source
// is missing or the cache directory cannot be created.
QString videoProxyCachePath(const QString& sourcePath);

// True when a completed preview proxy exists in the cache.
bool videoProxyReady(const QString& sourcePath);

// Path the frame decoder should use: the proxy when ready, else the source.
QString videoDecodePath(const QString& sourcePath);

// Generates low-resolution, short-GOP preview proxies in the background.
// One ffmpeg process runs at a time; extra jobs are queued. Uses the system
// ffmpeg executable when available and silently falls back to the source.
class VideoProxyGenerator : public QObject
{
    Q_OBJECT

public:
    explicit VideoProxyGenerator(QObject* parent = nullptr);
    ~VideoProxyGenerator() override;

    void ensure(const QString& sourcePath);
    bool isGenerating(const QString& sourcePath) const;

signals:
    void started(const QString& sourcePath);
    void finished(const QString& sourcePath, bool success);

private:
    struct Job
    {
        QString source;
        QString output;
        QString temporary;
    };

    void startNext();
    void finishCurrent(bool ffmpegOk);

    QProcess* m_process = nullptr;
    QList<Job> m_queue;
    QSet<QString> m_known;
    QString m_currentSource;
    QString m_currentOutput;
    QString m_currentTemporary;
    QString m_ffmpeg;
};

} // namespace pvf
