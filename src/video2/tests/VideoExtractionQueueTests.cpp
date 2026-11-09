#include "video2/VideoExtractionQueue.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QString runFfmpeg(const QString& output, const QStringList& arguments)
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        return {};

    QProcess process;
    process.start(QStringLiteral("ffmpeg"),
                  QStringList{QStringLiteral("-hide_banner"),
                              QStringLiteral("-loglevel"), QStringLiteral("error"),
                              QStringLiteral("-y")}
                      + arguments + QStringList{output});
    if (!process.waitForFinished(120000))
    {
        process.kill();
        process.waitForFinished();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QFileInfo::exists(output) ? output : QString();
}

QString indexFile(const QString& cacheRoot, const QString& hash, int fps,
                  int maxWidth)
{
    return QDir(cacheRoot).filePath(
        hash + QLatin1Char('/') + QString::number(fps) + QLatin1Char('/')
        + QString::number(maxWidth) + QStringLiteral("/index.json"));
}

QJsonObject readIndex(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

pvf::video2::ExtractionJob makeJob(const QString& hash, const QString& video,
                                   const QString& cacheRoot)
{
    pvf::video2::ExtractionJob job;
    job.assetHash = hash;
    job.videoPath = video;
    job.projectFps = 30;
    job.maxWidth = 160;
    job.cacheRoot = cacheRoot;
    return job;
}

} // namespace

class VideoExtractionQueueTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void extractsAndSkipsCompletedJob();
    void cancelMidwayStops();
    void cancelImmediatelyEmitsCancelled();
    void cancelAllDropsPendingAndRunning();
    void missingVideoFails();

private:
    QTemporaryDir m_directory;
    QString m_shortVideo;
    QString m_longVideo;
};

void VideoExtractionQueueTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    m_shortVideo = runFfmpeg(
        m_directory.filePath(QStringLiteral("short.avi")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc=size=160x90:rate=30:duration=1"),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
         QStringLiteral("-q:v"), QStringLiteral("2"), QStringLiteral("-pix_fmt"),
         QStringLiteral("yuv420p")});

    m_longVideo = runFfmpeg(
        m_directory.filePath(QStringLiteral("long.avi")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc=size=320x180:rate=30:duration=10"),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
         QStringLiteral("-q:v"), QStringLiteral("2"), QStringLiteral("-pix_fmt"),
         QStringLiteral("yuv420p")});

    if (m_shortVideo.isEmpty() || m_longVideo.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成测试视频");
}

void VideoExtractionQueueTest::extractsAndSkipsCompletedJob()
{
    const QString cacheRoot = m_directory.filePath(QStringLiteral("cache-short"));
    const QString hash = QStringLiteral("shorthash");
    pvf::video2::VideoExtractionQueue queue;
    QSignalSpy startedSpy(&queue, &pvf::video2::VideoExtractionQueue::started);
    QSignalSpy progressSpy(&queue, &pvf::video2::VideoExtractionQueue::progress);
    QSignalSpy finishedSpy(&queue, &pvf::video2::VideoExtractionQueue::finished);
    QSignalSpy failedSpy(&queue, &pvf::video2::VideoExtractionQueue::failed);

    const auto job = makeJob(hash, m_shortVideo, cacheRoot);
    queue.enqueue(job);
    queue.enqueue(job); // duplicate must be ignored

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 60000);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(startedSpy.first().at(0).toString(), hash);
    QCOMPARE(startedSpy.first().at(1).toInt(), 30);

    QVERIFY(!progressSpy.isEmpty());
    const QList<QVariant> last = progressSpy.last();
    QCOMPARE(last.at(1).toInt(), 30);
    QCOMPARE(last.at(2).toInt(), 30);
    QVERIFY(!queue.isRunning(hash));
    QVERIFY(!queue.isQueued(hash));

    const QString index = indexFile(cacheRoot, hash, 30, 160);
    QVERIFY2(QFileInfo(index).isFile(), qPrintable(index));
    const QJsonObject root = readIndex(index);
    QCOMPARE(root.value(QStringLiteral("totalFrames")).toInt(), 30);
    QCOMPARE(root.value(QStringLiteral("frames")).toObject().size(), 30);
    QVERIFY(QDir(QFileInfo(index).absolutePath())
                .entryList(QStringList{QStringLiteral("*.webp")}, QDir::Files)
                .size()
            > 0);

    // A completed index is detected synchronously and never queued again.
    queue.enqueue(job);
    QTest::qWait(300);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!queue.isRunning(hash));
    QVERIFY(!queue.isQueued(hash));
}

void VideoExtractionQueueTest::cancelMidwayStops()
{
    const QString cacheRoot = m_directory.filePath(QStringLiteral("cache-cancel"));
    const QString hash = QStringLiteral("cancelhash");
    pvf::video2::VideoExtractionQueue queue;
    QSignalSpy startedSpy(&queue, &pvf::video2::VideoExtractionQueue::started);
    QSignalSpy cancelledSpy(&queue, &pvf::video2::VideoExtractionQueue::cancelled);
    QSignalSpy finishedSpy(&queue, &pvf::video2::VideoExtractionQueue::finished);

    // Cancel from the worker callback so the stop is guaranteed mid-extraction.
    connect(
        &queue, &pvf::video2::VideoExtractionQueue::progress, this,
        [&queue, hash](const QString& current, int done, int) {
            if (current == hash && done >= 2)
                queue.cancel(hash);
        },
        Qt::DirectConnection);

    queue.enqueue(makeJob(hash, m_longVideo, cacheRoot));

    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 60000);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(cancelledSpy.first().at(0).toString(), hash);
    QVERIFY(!queue.isRunning(hash));
    QVERIFY(!queue.isQueued(hash));
    QCOMPARE(queue.pendingCount(), 0);

    const QString index = indexFile(cacheRoot, hash, 30, 160);
    QVERIFY2(QFileInfo(index).isFile(), qPrintable(index));
    const QJsonObject root = readIndex(index);
    const int total = root.value(QStringLiteral("totalFrames")).toInt();
    const int mapped = root.value(QStringLiteral("frames")).toObject().size();
    QVERIFY2(total > 0, qPrintable(QString::number(total)));
    QVERIFY2(mapped > 0 && mapped < total,
             qPrintable(QStringLiteral("mapped=%1 total=%2").arg(mapped).arg(total)));
}

void VideoExtractionQueueTest::cancelImmediatelyEmitsCancelled()
{
    const QString cacheRoot = m_directory.filePath(QStringLiteral("cache-fast"));
    const QString hash = QStringLiteral("fasthash");
    pvf::video2::VideoExtractionQueue queue;
    QSignalSpy cancelledSpy(&queue, &pvf::video2::VideoExtractionQueue::cancelled);
    QSignalSpy finishedSpy(&queue, &pvf::video2::VideoExtractionQueue::finished);

    queue.enqueue(makeJob(hash, m_longVideo, cacheRoot));
    queue.cancel(hash);

    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 60000);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(!queue.isRunning(hash));
    QVERIFY(!queue.isQueued(hash));

    const QJsonObject root = readIndex(indexFile(cacheRoot, hash, 30, 160));
    if (!root.isEmpty())
    {
        const int total = root.value(QStringLiteral("totalFrames")).toInt();
        const int mapped =
            root.value(QStringLiteral("frames")).toObject().size();
        QVERIFY(mapped < total);
    }
}

void VideoExtractionQueueTest::cancelAllDropsPendingAndRunning()
{
    const QString cacheRoot = m_directory.filePath(QStringLiteral("cache-all"));
    pvf::video2::VideoExtractionQueue queue;
    QSignalSpy cancelledSpy(&queue, &pvf::video2::VideoExtractionQueue::cancelled);
    QSignalSpy finishedSpy(&queue, &pvf::video2::VideoExtractionQueue::finished);
    QSignalSpy startedSpy(&queue, &pvf::video2::VideoExtractionQueue::started);

    const QString first = QStringLiteral("allhash1");
    const QString second = QStringLiteral("allhash2");
    queue.enqueue(makeJob(first, m_longVideo, cacheRoot));
    queue.enqueue(makeJob(second, m_longVideo, cacheRoot));
    queue.cancelAll();

    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 2, 60000);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(startedSpy.count() <= 1);
    QVERIFY(!queue.isRunning(first));
    QVERIFY(!queue.isRunning(second));
    QCOMPARE(queue.pendingCount(), 0);
}

void VideoExtractionQueueTest::missingVideoFails()
{
    const QString cacheRoot = m_directory.filePath(QStringLiteral("cache-missing"));
    const QString hash = QStringLiteral("missinghash");
    pvf::video2::VideoExtractionQueue queue;
    QSignalSpy failedSpy(&queue, &pvf::video2::VideoExtractionQueue::failed);
    QSignalSpy finishedSpy(&queue, &pvf::video2::VideoExtractionQueue::finished);

    queue.enqueue(makeJob(
        hash, m_directory.filePath(QStringLiteral("does-not-exist.mp4")),
        cacheRoot));

    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 30000);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(failedSpy.first().at(0).toString(), hash);
    QVERIFY(!failedSpy.first().at(1).toString().isEmpty());
    QVERIFY(!queue.isRunning(hash));
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication application(argc, argv);
    VideoExtractionQueueTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "VideoExtractionQueueTests.moc"
