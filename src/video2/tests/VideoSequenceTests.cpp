#include "video2/VideoSequenceService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
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

QJsonObject readIndex(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

QString frameHash(const QJsonObject& index, int frame)
{
    return index.value(QStringLiteral("frames"))
        .toObject()
        .value(QString::number(frame))
        .toString();
}

int webpFiles(const QString& directory)
{
    return QDir(directory)
        .entryList(QStringList{QStringLiteral("*.webp")}, QDir::Files)
        .size();
}

} // namespace

class VideoSequenceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void extractCreatesIndexAndImages();
    void staticFramesDeduplicate();
    void lowFpsSourceDeduplicates();
    void frameLookupAndMemoryHit();
    void restartReadsDiskIndex();
    void missingWebpFallsBack();
    void smallDiskBudgetEvicts();
    void cancelMidwayKeepsProgress();
    void cancelFlagStops();
    void missingVideoFails();

private:
    pvf::video2::SequenceOptions options(const QString& hash,
                                         const QString& video,
                                         int fps = 30,
                                         int maxWidth = 320,
                                         qint64 budget = 2LL * 1024 * 1024 * 1024) const;

    QTemporaryDir m_directory;
    QString m_testsrc;
    QString m_static;
    QString m_lowFps;
};

pvf::video2::SequenceOptions
VideoSequenceTest::options(const QString& hash, const QString& video, int fps,
                           int maxWidth, qint64 budget) const
{
    pvf::video2::SequenceOptions result;
    result.cacheRoot = m_directory.filePath(QStringLiteral("cache"));
    result.assetHash = hash;
    result.videoPath = video;
    result.projectFps = fps;
    result.maxWidth = maxWidth;
    result.diskBudgetBytes = budget;
    result.memoryEntries = 64;
    return result;
}

void VideoSequenceTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    m_testsrc = runFfmpeg(
        m_directory.filePath(QStringLiteral("testsrc.avi")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc=size=320x180:rate=30:duration=2"),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
         QStringLiteral("-q:v"), QStringLiteral("2"), QStringLiteral("-g"),
         QStringLiteral("30"), QStringLiteral("-pix_fmt"),
         QStringLiteral("yuv420p")});

    m_static = runFfmpeg(
        m_directory.filePath(QStringLiteral("static.mkv")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("color=c=teal:size=320x180:rate=30:duration=2"),
         QStringLiteral("-c:v"), QStringLiteral("ffv1"),
         QStringLiteral("-level"), QStringLiteral("3")});

    m_lowFps = runFfmpeg(
        m_directory.filePath(QStringLiteral("lowfps.avi")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc=size=320x180:rate=10:duration=2"),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
         QStringLiteral("-q:v"), QStringLiteral("2"), QStringLiteral("-g"),
         QStringLiteral("10"), QStringLiteral("-pix_fmt"),
         QStringLiteral("yuv420p")});

    if (m_testsrc.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成测试视频");
}

void VideoSequenceTest::extractCreatesIndexAndImages()
{
    pvf::video2::VideoSequenceService service(
        options(QStringLiteral("extract"), m_testsrc, 30, 160));

    QVERIFY(service.extract());
    QCOMPARE(service.totalFrames(), 60);
    QVERIFY(QFileInfo(service.indexFilePath()).isFile());
    QVERIFY(service.cacheDirectory().endsWith(
        QStringLiteral("/extract/30/160")));

    QVERIFY(service.hasFrame(0));
    QVERIFY(service.hasFrame(30));
    QVERIFY(service.hasFrame(59));
    QVERIFY(!service.hasFrame(60));

    const QJsonObject index = readIndex(service.indexFilePath());
    QCOMPARE(index.value(QStringLiteral("totalFrames")).toInt(), 60);
    QCOMPARE(index.value(QStringLiteral("frames")).toObject().size(), 60);
    QCOMPARE(service.stats().uniqueImages, 60);
    QCOMPARE(webpFiles(service.cacheDirectory()), 60);

    QString error;
    const QImage first = service.frame(0, &error);
    QVERIFY2(!first.isNull(), qPrintable(error));
    QCOMPARE(first.size(), QSize(160, 90));

    error.clear();
    const QImage middle = service.frame(30, &error);
    QVERIFY2(!middle.isNull(), qPrintable(error));
    QCOMPARE(middle.size(), QSize(160, 90));

    error.clear();
    QVERIFY(service.frame(60, &error).isNull());
    QVERIFY(!error.isEmpty());
}

void VideoSequenceTest::staticFramesDeduplicate()
{
    if (m_static.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成静态测试视频");

    pvf::video2::VideoSequenceService service(
        options(QStringLiteral("static"), m_static));

    QVERIFY(service.extract());
    QCOMPARE(service.totalFrames(), 60);
    QVERIFY2(service.stats().uniqueImages < 10,
             qPrintable(QStringLiteral("unique=%1")
                            .arg(service.stats().uniqueImages)));
    QCOMPARE(webpFiles(service.cacheDirectory()),
             service.stats().uniqueImages);

    const QJsonObject index = readIndex(service.indexFilePath());
    const QString firstHash = frameHash(index, 0);
    QVERIFY(!firstHash.isEmpty());
    QCOMPARE(frameHash(index, 30), firstHash);
    QCOMPARE(frameHash(index, 59), firstHash);

    QVERIFY(!service.frame(0).isNull());
    QCOMPARE(service.frame(30).size(), service.frame(0).size());
}

void VideoSequenceTest::lowFpsSourceDeduplicates()
{
    if (m_lowFps.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成低帧率测试视频");

    pvf::video2::VideoSequenceService service(
        options(QStringLiteral("lowfps"), m_lowFps, 30));

    QVERIFY(service.extract());
    QCOMPARE(service.totalFrames(), 60);
    QVERIFY2(service.stats().uniqueImages <= 22,
             qPrintable(QStringLiteral("unique=%1")
                            .arg(service.stats().uniqueImages)));
    QVERIFY(service.stats().uniqueImages < 60);
    QCOMPARE(webpFiles(service.cacheDirectory()),
             service.stats().uniqueImages);

    // 10 fps source sampled at 30 fps: three project frames share one image.
    const QJsonObject index = readIndex(service.indexFilePath());
    const QString firstHash = frameHash(index, 0);
    QVERIFY(!firstHash.isEmpty());
    QCOMPARE(frameHash(index, 1), firstHash);
    QCOMPARE(frameHash(index, 2), firstHash);
    QVERIFY(!frameHash(index, 3).isEmpty());

    const QImage a = service.frame(0);
    const QImage b = service.frame(1);
    QVERIFY(!a.isNull());
    QVERIFY(!b.isNull());
    QCOMPARE(a.size(), b.size());
    QVERIFY(a == b); // shared memory QImage (same content hash)
}

void VideoSequenceTest::frameLookupAndMemoryHit()
{
    pvf::video2::VideoSequenceService service(
        options(QStringLiteral("lookup"), m_testsrc));
    QVERIFY(service.extract());

    QString error;
    const QImage first = service.frame(0, &error);
    QVERIFY2(!first.isNull(), qPrintable(error));

    const int before = service.stats().memoryHits;
    const QImage again = service.frame(0, &error);
    QVERIFY2(!again.isNull(), qPrintable(error));
    QVERIFY(service.stats().memoryHits >= before + 1);
    QCOMPARE(first.size(), again.size());

    error.clear();
    const QImage middle = service.frame(30, &error);
    QVERIFY2(!middle.isNull(), qPrintable(error));
    QCOMPARE(middle.size(), QSize(320, 180));
}

void VideoSequenceTest::restartReadsDiskIndex()
{
    int unique = 0;
    QString index;
    {
        pvf::video2::VideoSequenceService service(
            options(QStringLiteral("restart"), m_testsrc));
        QVERIFY(service.extract());
        unique = service.stats().uniqueImages;
        index = service.indexFilePath();
    }
    QVERIFY(QFileInfo(index).isFile());

    pvf::video2::VideoSequenceService restarted(
        options(QStringLiteral("restart"), m_testsrc));
    QCOMPARE(restarted.totalFrames(), 60);
    QCOMPARE(restarted.stats().uniqueImages, unique);
    QVERIFY(restarted.hasFrame(0));
    QVERIFY(restarted.hasFrame(30));

    QString error;
    const QImage image = restarted.frame(30, &error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.size(), QSize(320, 180));
    QCOMPARE(restarted.stats().diskHits, 1);
}

void VideoSequenceTest::missingWebpFallsBack()
{
    pvf::video2::VideoSequenceService service(
        options(QStringLiteral("missingwebp"), m_testsrc));
    QVERIFY(service.extract());

    const QJsonObject index = readIndex(service.indexFilePath());
    const QString hash = frameHash(index, 0);
    QVERIFY(!hash.isEmpty());
    const QString path =
        QDir(service.cacheDirectory()).filePath(hash + QStringLiteral(".webp"));
    QVERIFY(QFileInfo(path).isFile());
    QVERIFY(QFile::remove(path));

    QVERIFY(!service.hasFrame(0));
    const int before = service.stats().fallbacks;
    QString error;
    const QImage image = service.frame(0, &error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.size(), QSize(320, 180));
    QCOMPARE(service.stats().fallbacks, before + 1);
    QVERIFY(!service.hasFrame(0));
}

void VideoSequenceTest::smallDiskBudgetEvicts()
{
    qint64 average = 0;
    {
        pvf::video2::VideoSequenceService probe(
            options(QStringLiteral("budget-probe"), m_testsrc));
        QVERIFY(probe.extract());
        const QJsonObject index = readIndex(probe.indexFilePath());
        const QJsonObject images =
            index.value(QStringLiteral("images")).toObject();
        QVERIFY(!images.isEmpty());
        qint64 total = 0;
        for (auto it = images.constBegin(); it != images.constEnd(); ++it)
            total += static_cast<qint64>(
                it.value().toObject().value(QStringLiteral("size")).toDouble());
        average = total / images.size();
    }
    QVERIFY(average > 0);

    pvf::video2::VideoSequenceService service(options(
        QStringLiteral("budget-small"), m_testsrc, 30, 320, average * 2));
    QVERIFY(service.extract());

    QVERIFY2(service.stats().uniqueImages > 0,
             "budget must keep at least the newest image");
    QVERIFY(service.stats().uniqueImages < 60);
    // LRU keeps the most recently written frame and drops the oldest one.
    QVERIFY(service.hasFrame(59));
    QVERIFY(!service.hasFrame(0));
    QCOMPARE(webpFiles(service.cacheDirectory()),
             service.stats().uniqueImages);
}

void VideoSequenceTest::cancelMidwayKeepsProgress()
{
    pvf::video2::VideoSequenceService service(
        options(QStringLiteral("cancel"), m_testsrc));
    int lastDone = 0;
    int lastTotal = 0;
    const bool ok = service.extract(
        [&](const pvf::video2::SequenceProgress& progress) {
            lastDone = progress.done;
            lastTotal = progress.total;
            return progress.done < 10;
        });

    QVERIFY(!ok);
    QCOMPARE(lastDone, 10);
    QCOMPARE(lastTotal, 60);
    QVERIFY(QFileInfo(service.indexFilePath()).isFile());
    QVERIFY(service.stats().uniqueImages >= 5);

    pvf::video2::VideoSequenceService restarted(
        options(QStringLiteral("cancel"), m_testsrc));
    QCOMPARE(restarted.totalFrames(), 60);
    QVERIFY(restarted.hasFrame(0));
    QVERIFY(!restarted.hasFrame(59));

    // Resuming fills only the missing frames and keeps the existing ones.
    QVERIFY(restarted.extract());
    QVERIFY(restarted.hasFrame(59));
    QCOMPARE(restarted.stats().uniqueImages, 60);
}

void VideoSequenceTest::cancelFlagStops()
{
    pvf::video2::VideoSequenceService service(
        options(QStringLiteral("cancelflag"), m_testsrc));
    int lastDone = 0;
    const bool ok = service.extract(
        [&](const pvf::video2::SequenceProgress& progress) {
            lastDone = progress.done;
            if (progress.done >= 7)
                const_cast<pvf::video2::SequenceProgress&>(progress).cancelled =
                    true;
            return true;
        });

    QVERIFY(!ok);
    QCOMPARE(lastDone, 7);
    QVERIFY(service.stats().uniqueImages >= 5);
}

void VideoSequenceTest::missingVideoFails()
{
    pvf::video2::VideoSequenceService service(options(
        QStringLiteral("missingvideo"),
        m_directory.filePath(QStringLiteral("does-not-exist.mp4"))));

    QVERIFY(!service.extract());
    QVERIFY(!service.lastError().isEmpty());
    QVERIFY(!service.hasFrame(0));

    QString error;
    QVERIFY(service.frame(0, &error).isNull());
    QVERIFY(!error.isEmpty());
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication application(argc, argv);
    VideoSequenceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "VideoSequenceTests.moc"
