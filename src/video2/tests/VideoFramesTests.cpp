#include "video2/VideoFrames.h"

#include <QColor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <cstring>

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

bool samePixels(const QImage& first, const QImage& second)
{
    return !first.isNull() && first.size() == second.size()
           && first.format() == second.format()
           && std::memcmp(first.constBits(), second.constBits(),
                          size_t(first.sizeInBytes()))
                  == 0;
}

} // namespace

class VideoFramesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void frameZero();
    void laterFrameDiffers();
    void frameBoundary();
    void fpsControlsTime();
    void proxySize();
    void sourceSize();
    void cacheHit();
    void lruEvicts();
    void missingFile();
    void noVideoStream();

private:
    pvf::video2::Request makeRequest(int sourceFrame, int fps = 30) const;

    QTemporaryDir m_directory;
    QString m_video;
    QString m_audio;
    QString m_colors;
};

pvf::video2::Request VideoFramesTest::makeRequest(int sourceFrame, int fps) const
{
    pvf::video2::Request request;
    request.path = m_video;
    request.sourceFrame = sourceFrame;
    request.fps = fps;
    return request;
}

void VideoFramesTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    m_video = runFfmpeg(
        m_directory.filePath(QStringLiteral("testsrc.avi")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc=size=320x180:rate=30:duration=2"),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
         QStringLiteral("-q:v"), QStringLiteral("2"), QStringLiteral("-g"),
         QStringLiteral("30"), QStringLiteral("-pix_fmt"),
         QStringLiteral("yuv420p")});
    if (m_video.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成测试视频");

    m_audio = runFfmpeg(
        m_directory.filePath(QStringLiteral("tone.wav")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("sine=frequency=440:sample_rate=44100:duration=0.5"),
         QStringLiteral("-c:a"), QStringLiteral("pcm_s16le")});

    // One second of red followed by one second of blue at 30 fps. The default
    // GOP does not align with the colour boundary, so reaching frame 30
    // requires decoding forward from an earlier keyframe.
    m_colors = runFfmpeg(
        m_directory.filePath(QStringLiteral("colors.avi")),
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("color=c=red:size=160x90:rate=30:duration=1"),
         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("color=c=blue:size=160x90:rate=30:duration=1"),
         QStringLiteral("-filter_complex"),
         QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0"),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
         QStringLiteral("-q:v"), QStringLiteral("2"),
         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")});
}

void VideoFramesTest::frameZero()
{
    pvf::video2::VideoFrames frames;
    QString error;
    const QImage image = frames.frame(makeRequest(0), &error);

    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(image.size(), QSize(320, 180));
    QCOMPARE(image.format(), QImage::Format_RGBA8888);
    QCOMPARE(frames.stats().misses, 1);
    QCOMPARE(frames.stats().decodes, 1);
}

void VideoFramesTest::laterFrameDiffers()
{
    pvf::video2::VideoFrames frames;
    QString error;
    const QImage first = frames.frame(makeRequest(0), &error);
    QVERIFY2(!first.isNull(), qPrintable(error));

    error.clear();
    const QImage later = frames.frame(makeRequest(30), &error);
    QVERIFY2(!later.isNull(), qPrintable(error));
    QVERIFY(!samePixels(first, later));
}

void VideoFramesTest::frameBoundary()
{
    if (m_colors.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成双色测试视频");

    pvf::video2::VideoFrames frames;
    pvf::video2::Request request;
    request.path = m_colors;
    request.fps = 30;
    QString error;

    request.sourceFrame = 29;
    const QImage lastRed = frames.frame(request, &error);
    QVERIFY2(!lastRed.isNull(), qPrintable(error));
    const QColor lastRedPixel =
        lastRed.pixelColor(lastRed.width() / 2, lastRed.height() / 2);
    QVERIFY2(lastRedPixel.red() > 200 && lastRedPixel.blue() < 60,
             qPrintable(lastRedPixel.name()));

    request.sourceFrame = 30;
    error.clear();
    const QImage firstBlue = frames.frame(request, &error);
    QVERIFY2(!firstBlue.isNull(), qPrintable(error));
    const QColor firstBluePixel =
        firstBlue.pixelColor(firstBlue.width() / 2, firstBlue.height() / 2);
    QVERIFY2(firstBluePixel.blue() > 200 && firstBluePixel.red() < 60,
             qPrintable(firstBluePixel.name()));
}

void VideoFramesTest::fpsControlsTime()
{
    pvf::video2::VideoFrames frames;
    QString error;

    // Both requests ask for 1.0 s, one expressed at 30 fps and one at 60 fps.
    const QImage atThirty = frames.frame(makeRequest(30, 30), &error);
    QVERIFY2(!atThirty.isNull(), qPrintable(error));

    error.clear();
    const QImage atSixty = frames.frame(makeRequest(60, 60), &error);
    QVERIFY2(!atSixty.isNull(), qPrintable(error));

    QVERIFY(samePixels(atThirty, atSixty));
}

void VideoFramesTest::proxySize()
{
    pvf::video2::VideoFrames frames(160);
    QString error;
    const QImage image = frames.frame(makeRequest(0), &error);

    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.size(), QSize(160, 90));
    QVERIFY(image.width() <= 960);

    const double ratio = double(image.width()) / double(image.height());
    QVERIFY(std::abs(ratio - 16.0 / 9.0) < 0.01);
}

void VideoFramesTest::sourceSize()
{
    pvf::video2::VideoFrames frames;
    QString error;

    pvf::video2::Request request = makeRequest(0);
    request.proxy = false;
    request.size = QSize(200, 100);
    QImage image = frames.frame(request, &error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.size(), QSize(200, 100));

    request.size = QSize();
    error.clear();
    image = frames.frame(request, &error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.size(), QSize(320, 180));
}

void VideoFramesTest::cacheHit()
{
    pvf::video2::VideoFrames frames;
    const pvf::video2::Request request = makeRequest(0);
    QString error;

    const QImage first = frames.frame(request, &error);
    QVERIFY2(!first.isNull(), qPrintable(error));
    QCOMPARE(frames.stats().misses, 1);
    QCOMPARE(frames.stats().decodes, 1);

    const QImage second = frames.frame(request, &error);
    QVERIFY(samePixels(first, second));
    QCOMPARE(frames.stats().hits, 1);
    QCOMPARE(frames.stats().misses, 1);
    QCOMPARE(frames.stats().decodes, 1);

    frames.clear();
    QCOMPARE(frames.stats().hits, 0);
    QCOMPARE(frames.stats().misses, 0);
    QCOMPARE(frames.stats().decodes, 0);

    const QImage third = frames.frame(request, &error);
    QVERIFY(!third.isNull());
    QCOMPARE(frames.stats().misses, 1);
    QCOMPARE(frames.stats().decodes, 1);
}

void VideoFramesTest::lruEvicts()
{
    pvf::video2::VideoFrames frames(960, 2);
    QString error;

    QVERIFY(!frames.frame(makeRequest(0), &error).isNull());
    QVERIFY(!frames.frame(makeRequest(15), &error).isNull());
    QVERIFY(!frames.frame(makeRequest(30), &error).isNull());

    // Inserting the third frame evicted frame 0, so it must decode again.
    QVERIFY(!frames.frame(makeRequest(0), &error).isNull());
    QCOMPARE(frames.stats().hits, 0);
    QCOMPARE(frames.stats().misses, 4);
    QCOMPARE(frames.stats().decodes, 4);
}

void VideoFramesTest::missingFile()
{
    pvf::video2::VideoFrames frames;
    pvf::video2::Request request;
    request.path = m_directory.filePath(QStringLiteral("missing.mp4"));
    request.fps = 30;

    QString error;
    const QImage image = frames.frame(request, &error);
    QVERIFY(image.isNull());
    QVERIFY(!error.isEmpty());
}

void VideoFramesTest::noVideoStream()
{
    if (m_audio.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成测试音频");

    pvf::video2::VideoFrames frames;
    pvf::video2::Request request;
    request.path = m_audio;
    request.fps = 30;

    QString error;
    const QImage image = frames.frame(request, &error);
    QVERIFY(image.isNull());
    QVERIFY(!error.isEmpty());
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication application(argc, argv);
    VideoFramesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "VideoFramesTests.moc"
