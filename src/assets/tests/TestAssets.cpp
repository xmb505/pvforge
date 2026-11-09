#include "assets/Assets.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

bool writeText(const QString& path, const QByteArray& text)
{
    return writeBytes(path, text);
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJsonObject(const QString& path, const QJsonObject& object)
{
    return writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

bool isFile(const QString& path)
{
    return QFileInfo(path).isFile();
}

QByteArray makeWavBytes(double seconds, int sampleRate, int channels,
                        double frequency = 440.0)
{
    const int frameCount = int(sampleRate * seconds);
    const quint32 dataSize = quint32(frameCount * channels * 2);
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataSize);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16);
    stream << quint16(1);
    stream << quint16(channels);
    stream << quint32(sampleRate);
    stream << quint32(sampleRate * channels * 2);
    stream << quint16(channels * 2);
    stream << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataSize;
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const double t = frame / double(sampleRate);
        const auto sample = qint16(std::sin(2.0 * kPi * frequency * t) * 0.25 * 32767.0);
        for (int channel = 0; channel < channels; ++channel)
            stream << sample;
    }
    return bytes;
}

bool writeWav(const QString& path, double seconds, int sampleRate, int channels)
{
    return writeBytes(path, makeWavBytes(seconds, sampleRate, channels));
}

bool runProcess(const QString& program, const QStringList& arguments,
                const QString& workingDirectory = QString())
{
    QProcess process;
    if (!workingDirectory.isEmpty())
        process.setWorkingDirectory(workingDirectory);
    process.start(program, arguments);
    return process.waitForFinished(120000) && process.exitStatus() == QProcess::NormalExit
           && process.exitCode() == 0;
}

bool makeTestVideo(const QString& path)
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        return false;
    return runProcess(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
         QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
         QStringLiteral("-i"), QStringLiteral("testsrc=size=160x90:rate=10:duration=0.6"),
         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("sine=frequency=440:sample_rate=44100:duration=0.6"),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
         QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
         QStringLiteral("-shortest"), path});
}

struct WavInfo
{
    bool ok = false;
    quint16 format = 0;
    quint16 channels = 0;
    quint32 sampleRate = 0;
    quint16 bits = 0;
    quint32 dataSize = 0;
};

WavInfo readWavInfo(const QString& path)
{
    WavInfo info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return info;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    char riff[4] = {};
    char wave[4] = {};
    char fmt[4] = {};
    char data[4] = {};
    quint32 riffSize = 0;
    quint32 fmtSize = 0;
    quint16 blockAlign = 0;
    quint32 byteRate = 0;

    stream.readRawData(riff, 4);
    stream >> riffSize;
    stream.readRawData(wave, 4);
    stream.readRawData(fmt, 4);
    stream >> fmtSize;
    stream >> info.format;
    stream >> info.channels;
    stream >> info.sampleRate;
    stream >> byteRate;
    stream >> blockAlign;
    stream >> info.bits;
    stream.readRawData(data, 4);
    stream >> info.dataSize;

    if (stream.status() != QDataStream::Ok)
        return info;
    if (QByteArray(riff, 4) != "RIFF" || QByteArray(wave, 4) != "WAVE"
        || QByteArray(fmt, 4) != "fmt " || QByteArray(data, 4) != "data"
        || fmtSize != 16 || blockAlign != info.channels * info.bits / 8)
    {
        return info;
    }
    if (riffSize != 36 + info.dataSize || file.size() != 44 + qint64(info.dataSize))
        return info;
    if (info.channels == 0 || info.bits == 0 || info.dataSize == 0
        || info.dataSize % blockAlign != 0)
    {
        return info;
    }
    info.ok = true;
    return info;
}

} // namespace

class AssetsTest : public QObject
{
    Q_OBJECT

private slots:
    void sha256KnownFile();
    void sha256DirectoryStable();
    void detectTypeByExtensionAndMagic();
    void probeImage();
    void probeAudioAndExtractWav();
    void probeVideoAndExtractWav();
    void importFileDeduplicates();
    void storeGetListRemove();
    void missingAssetReturnsEmpty();
    void assetTagsRoundTrip();
    void assetTagsRejectInvalid();
    void legacyMetadataWithoutTags();
    void pvhtmlDirectoryImport();
    void pvhtmlZipImport();
    void pvhtmlRejectsMissingEntry();
    void pvhtmlRejectsSymlink();
    void videoImportWithAudio();
};

void AssetsTest::sha256KnownFile()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString abcPath = temporary.filePath(QStringLiteral("abc.txt"));
    QVERIFY(writeText(abcPath, "abc"));
    QString error;
    QCOMPARE(pvf::assets::sha256File(abcPath, &error),
             QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    QVERIFY(error.isEmpty());

    const QString emptyPath = temporary.filePath(QStringLiteral("empty.bin"));
    QVERIFY(writeBytes(emptyPath, QByteArray()));
    QCOMPARE(pvf::assets::sha256File(emptyPath),
             QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    error.clear();
    QVERIFY(pvf::assets::sha256File(temporary.filePath(QStringLiteral("missing.bin")), &error)
                .isEmpty());
    QVERIFY(!error.isEmpty());
}

void AssetsTest::sha256DirectoryStable()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString first = temporary.filePath(QStringLiteral("first"));
    const QString second = temporary.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(first + QStringLiteral("/sub")));
    QVERIFY(QDir().mkpath(second + QStringLiteral("/sub")));
    QVERIFY(QDir().mkpath(second + QStringLiteral("/empty")));

    QVERIFY(writeText(first + QStringLiteral("/a.txt"), "alpha"));
    QVERIFY(writeText(first + QStringLiteral("/b.txt"), "beta"));
    QVERIFY(writeBytes(first + QStringLiteral("/sub/c.bin"), QByteArray("\x01\x02\x03", 3)));

    // Same contents, reverse creation order, plus an ignored empty directory.
    QVERIFY(writeBytes(second + QStringLiteral("/sub/c.bin"), QByteArray("\x01\x02\x03", 3)));
    QVERIFY(writeText(second + QStringLiteral("/b.txt"), "beta"));
    QVERIFY(writeText(second + QStringLiteral("/a.txt"), "alpha"));

    QString error;
    const QString firstHash = pvf::assets::sha256Directory(first, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!firstHash.isEmpty());
    QCOMPARE(pvf::assets::sha256Directory(second), firstHash);

    const QString empty = temporary.filePath(QStringLiteral("empty-dir"));
    QVERIFY(QDir().mkpath(empty));
    QCOMPARE(pvf::assets::sha256Directory(empty),
             QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    QVERIFY(writeText(first + QStringLiteral("/b.txt"), "changed"));
    QVERIFY(pvf::assets::sha256Directory(first) != firstHash);

    error.clear();
    QVERIFY(pvf::assets::sha256Directory(temporary.filePath(QStringLiteral("missing")), &error)
                .isEmpty());
    QVERIFY(!error.isEmpty());
}

void AssetsTest::detectTypeByExtensionAndMagic()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QByteArray pngMagic("\x89PNG\r\n\x1a\n", 8);
    const QString pngPath = temporary.filePath(QStringLiteral("picture.png"));
    QVERIFY(writeBytes(pngPath, pngMagic + QByteArray(16, 0)));
    auto type = pvf::assets::detectType(pngPath);
    QVERIFY(type.has_value());
    QVERIFY(*type == pvf::AssetType::Image);

    const QString disguisedPng = temporary.filePath(QStringLiteral("picture.dat"));
    QVERIFY(writeBytes(disguisedPng, pngMagic + QByteArray(16, 0)));
    type = pvf::assets::detectType(disguisedPng);
    QVERIFY(type.has_value());
    QVERIFY(*type == pvf::AssetType::Image);

    const QString wavPath = temporary.filePath(QStringLiteral("sound.wav"));
    QVERIFY(writeWav(wavPath, 0.1, 44100, 1));
    type = pvf::assets::detectType(wavPath);
    QVERIFY(type.has_value());
    QVERIFY(*type == pvf::AssetType::Audio);

    const QString disguisedWav = temporary.filePath(QStringLiteral("sound.dat"));
    QVERIFY(QFile::copy(wavPath, disguisedWav));
    type = pvf::assets::detectType(disguisedWav);
    QVERIFY(type.has_value());
    QVERIFY(*type == pvf::AssetType::Audio);

    const QByteArray mp4Magic = QByteArray("\x00\x00\x00\x18"
                                           "ftypisom\x00\x00\x00\x00",
                                           16);
    const QString mp4Path = temporary.filePath(QStringLiteral("clip.mp4"));
    QVERIFY(writeBytes(mp4Path, mp4Magic));
    type = pvf::assets::detectType(mp4Path);
    QVERIFY(type.has_value());
    QVERIFY(*type == pvf::AssetType::Video);

    const QString m4aPath = temporary.filePath(QStringLiteral("clip.m4a"));
    QVERIFY(writeBytes(m4aPath, mp4Magic));
    type = pvf::assets::detectType(m4aPath);
    QVERIFY(type.has_value());
    QVERIFY(*type == pvf::AssetType::Audio);

    const QString unknown = temporary.filePath(QStringLiteral("notes.txt"));
    QVERIFY(writeText(unknown, "hello"));
    QVERIFY(!pvf::assets::detectType(unknown).has_value());

    const QString bundle = temporary.filePath(QStringLiteral("bundle"));
    QVERIFY(QDir().mkpath(bundle));
    QVERIFY(writeText(bundle + QStringLiteral("/index.html"), "<html></html>"));
    type = pvf::assets::detectType(bundle);
    QVERIFY(type.has_value());
    QVERIFY(*type == pvf::AssetType::Html);

    QVERIFY(!pvf::assets::detectType(temporary.filePath(QStringLiteral("nope"))).has_value());
}

void AssetsTest::probeImage()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString path = temporary.filePath(QStringLiteral("image.png"));
    QImage image(320, 180, QImage::Format_ARGB32);
    image.fill(Qt::darkCyan);
    QVERIFY(image.save(path));

    QString error;
    const auto result = pvf::assets::probe(path, pvf::AssetType::Image, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(result.size, QSize(320, 180));

    error.clear();
    const auto missing =
        pvf::assets::probe(temporary.filePath(QStringLiteral("missing.png")),
                           pvf::AssetType::Image, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(!missing.size.isValid());
}

void AssetsTest::probeAudioAndExtractWav()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString path = temporary.filePath(QStringLiteral("tone.wav"));
    QVERIFY(writeWav(path, 0.25, 44100, 1));

    QString error;
    const auto result = pvf::assets::probe(path, pvf::AssetType::Audio, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(result.sampleRate, 44100);
    QCOMPARE(result.channels, 1);
    QVERIFY(std::abs(result.durationSeconds - 0.25) < 0.05);

    const QString out = temporary.filePath(QStringLiteral("extracted.wav"));
    QVERIFY(pvf::assets::extractWav(path, out, &error));
    QVERIFY(error.isEmpty());

    const WavInfo info = readWavInfo(out);
    QVERIFY(info.ok);
    QCOMPARE(info.format, quint16(1));
    QCOMPARE(info.channels, quint16(2));
    QCOMPARE(info.sampleRate, quint32(48000));
    QCOMPARE(info.bits, quint16(16));
    QVERIFY(info.dataSize % (2 * 2) == 0);
    const double duration = info.dataSize / double(48000 * 2 * 2);
    QVERIFY(std::abs(duration - 0.25) < 0.05);

    error.clear();
    QVERIFY(!pvf::assets::extractWav(temporary.filePath(QStringLiteral("missing.wav")), out,
                                    &error));
    QVERIFY(!error.isEmpty());
}

void AssetsTest::probeVideoAndExtractWav()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString video = temporary.filePath(QStringLiteral("sample.avi"));
    if (!makeTestVideo(video))
        QSKIP("ffmpeg 不可用或无法生成测试视频");

    QString error;
    const auto result = pvf::assets::probe(video, pvf::AssetType::Video, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(result.size, QSize(160, 90));
    QCOMPARE(result.sampleRate, 44100);
    QCOMPARE(result.channels, 1);
    QVERIFY(result.durationSeconds > 0.4);
    QVERIFY(result.durationSeconds < 0.9);

    const QString out = temporary.filePath(QStringLiteral("video-audio.wav"));
    QVERIFY(pvf::assets::extractWav(video, out, &error));
    QVERIFY(error.isEmpty());

    const WavInfo info = readWavInfo(out);
    QVERIFY(info.ok);
    QCOMPARE(info.channels, quint16(2));
    QCOMPARE(info.sampleRate, quint32(48000));
    const double duration = info.dataSize / double(48000 * 2 * 2);
    QVERIFY(std::abs(duration - 0.6) < 0.08);
}

void AssetsTest::importFileDeduplicates()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));

    const QString first = temporary.filePath(QStringLiteral("first.png"));
    QImage image(64, 48, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(first));

    QString error;
    const auto asset = store.importFile(first, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(asset.has_value());
    QVERIFY(asset->type == pvf::AssetType::Image);
    QCOMPARE(asset->name, QStringLiteral("first.png"));
    QVERIFY(asset->hash.size() == 64);

    const QString second = temporary.filePath(QStringLiteral("second.png"));
    QVERIFY(QFile::copy(first, second));
    const auto duplicate = store.importFile(second, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(duplicate.has_value());
    QCOMPARE(duplicate->hash, asset->hash);
    QCOMPARE(duplicate->name, QStringLiteral("first.png"));
    QCOMPARE(store.list().size(), 1);

    const QString content = asset->directory + QStringLiteral("/content.png");
    QVERIFY(isFile(content));
    QVERIFY(isFile(asset->directory + QStringLiteral("/asset.json")));
}

void AssetsTest::storeGetListRemove()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));

    const QString imagePath = temporary.filePath(QStringLiteral("a.png"));
    QImage image(10, 20, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));

    const QString audioPath = temporary.filePath(QStringLiteral("sound.wav"));
    QVERIFY(writeWav(audioPath, 0.1, 44100, 1));

    QString error;
    const auto imageAsset = store.importFile(imagePath, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(imageAsset.has_value());
    const auto audioAsset = store.importFile(audioPath, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(audioAsset.has_value());
    QVERIFY(imageAsset->hash != audioAsset->hash);

    const auto list = store.list();
    QCOMPARE(list.size(), 2);

    const auto fetched = store.get(imageAsset->hash);
    QVERIFY(fetched.has_value());
    QCOMPARE(fetched->name, QStringLiteral("a.png"));
    QVERIFY(fetched->type == pvf::AssetType::Image);
    QCOMPARE(fetched->metadata.value(QStringLiteral("width")).toInt(), 10);
    QCOMPARE(fetched->metadata.value(QStringLiteral("height")).toInt(), 20);

    const auto fetchedAudio = store.get(audioAsset->hash);
    QVERIFY(fetchedAudio.has_value());
    QCOMPARE(fetchedAudio->metadata.value(QStringLiteral("sampleRate")).toInt(), 44100);

    QVERIFY(store.remove(imageAsset->hash, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!store.get(imageAsset->hash).has_value());
    QCOMPARE(store.list().size(), 1);

    error.clear();
    QVERIFY(!store.remove(imageAsset->hash, &error));
    QVERIFY(!error.isEmpty());
}

void AssetsTest::missingAssetReturnsEmpty()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));
    QVERIFY(!store.get(QString(64, QLatin1Char('0'))).has_value());
    QVERIFY(!store.get(QStringLiteral("../escape")).has_value());
    QVERIFY(!store.get(QString()).has_value());
    QVERIFY(store.list().isEmpty());
}

void AssetsTest::assetTagsRoundTrip()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));

    const QString path = temporary.filePath(QStringLiteral("tagged.png"));
    QImage image(20, 10, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    QVERIFY(image.save(path));

    QString error;
    const auto imported = store.importFile(path, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(imported.has_value());
    QVERIFY(imported->tags.isEmpty());

    const QString metadataPath = imported->directory + QStringLiteral("/asset.json");
    QVERIFY(readJsonObject(metadataPath).value(QStringLiteral("tags")).toArray().isEmpty());

    QVERIFY2(store.setTags(imported->hash,
                           {QStringLiteral("  hero "), QStringLiteral("背景"),
                            QStringLiteral("hero"), QStringLiteral("   "),
                            QStringLiteral("背景")},
                           &error),
             qPrintable(error));
    QVERIFY(error.isEmpty());

    const QStringList expected{QStringLiteral("hero"), QStringLiteral("背景")};
    const auto fetched = store.get(imported->hash);
    QVERIFY(fetched.has_value());
    QCOMPARE(fetched->tags, expected);
    QCOMPARE(readJsonObject(metadataPath).value(QStringLiteral("tags")).toArray(),
             QJsonArray::fromStringList(expected));
    QCOMPARE(store.list().first().tags, expected);

    pvf::assets::AssetStore reopened(temporary.filePath(QStringLiteral("project")));
    const auto reloaded = reopened.get(imported->hash);
    QVERIFY(reloaded.has_value());
    QCOMPARE(reloaded->tags, expected);

    // Replacing with an empty list clears tags.
    QVERIFY2(store.setTags(imported->hash, {}, &error), qPrintable(error));
    const auto cleared = store.get(imported->hash);
    QVERIFY(cleared.has_value());
    QVERIFY(cleared->tags.isEmpty());
}

void AssetsTest::assetTagsRejectInvalid()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));

    const QString path = temporary.filePath(QStringLiteral("tagged.png"));
    QImage image(10, 10, QImage::Format_ARGB32);
    image.fill(Qt::darkGreen);
    QVERIFY(image.save(path));

    QString error;
    const auto imported = store.importFile(path, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(imported.has_value());
    const pvf::AssetHash hash = imported->hash;

    const QString tooLong(65, QLatin1Char('x'));
    error.clear();
    QVERIFY(!store.setTags(hash, {QStringLiteral("ok"), tooLong}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.get(hash)->tags.isEmpty());

    QStringList many;
    for (int i = 0; i < 33; ++i)
        many.append(QStringLiteral("tag%1").arg(i));
    error.clear();
    QVERIFY(!store.setTags(hash, many, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.get(hash)->tags.isEmpty());

    // Exactly 32 tags is allowed.
    error.clear();
    QVERIFY2(store.setTags(hash, many.mid(0, 32), &error), qPrintable(error));
    const auto filled = store.get(hash);
    QVERIFY(filled.has_value());
    QCOMPARE(filled->tags.size(), 32);

    // Unknown or malformed hashes are rejected.
    error.clear();
    QVERIFY(!store.setTags(QString(64, QLatin1Char('0')), {QStringLiteral("x")}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!store.setTags(QStringLiteral("../escape"), {QStringLiteral("x")}, &error));
    QVERIFY(!error.isEmpty());
}

void AssetsTest::legacyMetadataWithoutTags()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));

    const QString path = temporary.filePath(QStringLiteral("legacy.png"));
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::gray);
    QVERIFY(image.save(path));

    QString error;
    const auto imported = store.importFile(path, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(imported.has_value());
    const QString metadataPath = imported->directory + QStringLiteral("/asset.json");

    QJsonObject metadata = readJsonObject(metadataPath);
    QVERIFY(metadata.contains(QStringLiteral("tags")));
    metadata.remove(QStringLiteral("tags"));
    QVERIFY(writeJsonObject(metadataPath, metadata));

    const auto legacy = store.get(imported->hash);
    QVERIFY(legacy.has_value());
    QVERIFY(legacy->tags.isEmpty());
    QVERIFY(store.list().first().tags.isEmpty());

    QVERIFY2(store.setTags(imported->hash, {QStringLiteral("legacy")}, &error),
             qPrintable(error));
    const auto upgraded = store.get(imported->hash);
    QVERIFY(upgraded.has_value());
    QCOMPARE(upgraded->tags, QStringList{QStringLiteral("legacy")});
}

void AssetsTest::pvhtmlDirectoryImport()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString bundle = temporary.filePath(QStringLiteral("my-bundle"));
    QVERIFY(QDir().mkpath(bundle));
    QVERIFY(writeText(bundle + QStringLiteral("/index.html"), "<html>hi</html>"));
    QVERIFY(writeText(bundle + QStringLiteral("/style.css"), "body{}"));
    QVERIFY(writeText(bundle + QStringLiteral("/pvhtml.json"),
                      R"({"entry":"index.html","logicalSize":{"width":640,"height":360},"variables":{"speed":1.5,"label":"x"}})"));

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));
    QString error;
    const auto asset = store.importPvhtml(bundle, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(asset.has_value());
    QVERIFY(asset->type == pvf::AssetType::Html);
    QCOMPARE(asset->name, QStringLiteral("my-bundle"));
    QCOMPARE(asset->metadata.value(QStringLiteral("entry")).toString(),
             QStringLiteral("index.html"));
    QCOMPARE(asset->metadata.value(QStringLiteral("width")).toInt(), 640);
    QCOMPARE(asset->metadata.value(QStringLiteral("height")).toInt(), 360);
    QCOMPARE(asset->metadata.value(QStringLiteral("variables")).toObject()
                 .value(QStringLiteral("speed"))
                 .toDouble(),
             1.5);

    QVERIFY(isFile(asset->directory + QStringLiteral("/bundle/index.html")));
    QVERIFY(isFile(asset->directory + QStringLiteral("/bundle/style.css")));
    QVERIFY(isFile(asset->directory + QStringLiteral("/bundle/pvhtml.json")));

    const auto duplicate = store.importPvhtml(bundle, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(duplicate.has_value());
    QCOMPARE(duplicate->hash, asset->hash);
    QCOMPARE(duplicate->name, QStringLiteral("my-bundle"));
    QCOMPARE(store.list().size(), 1);
}

void AssetsTest::pvhtmlZipImport()
{
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty())
        QSKIP("zip 不可用");

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString source = temporary.filePath(QStringLiteral("source"));
    const QString wrapper = source + QStringLiteral("/wrapped");
    QVERIFY(QDir().mkpath(wrapper));
    QVERIFY(writeText(wrapper + QStringLiteral("/index.html"), "<html>zip</html>"));
    QVERIFY(writeText(wrapper + QStringLiteral("/app.js"), "console.log(1)"));

    const QString zipPath = temporary.filePath(QStringLiteral("bundle.zip"));
    QVERIFY(runProcess(QStringLiteral("zip"),
                       {QStringLiteral("-q"), QStringLiteral("-r"), zipPath,
                        QStringLiteral("wrapped")},
                       source));

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));
    QString error;
    const auto asset = store.importPvhtml(zipPath, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(asset.has_value());
    QVERIFY(asset->type == pvf::AssetType::Html);
    QCOMPARE(asset->name, QStringLiteral("bundle"));
    QVERIFY(isFile(asset->directory + QStringLiteral("/bundle/index.html")));
    QVERIFY(isFile(asset->directory + QStringLiteral("/bundle/app.js")));
    QVERIFY(!QFileInfo::exists(asset->directory + QStringLiteral("/bundle/wrapped")));
}

void AssetsTest::pvhtmlRejectsMissingEntry()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString bundle = temporary.filePath(QStringLiteral("broken"));
    QVERIFY(QDir().mkpath(bundle));
    QVERIFY(writeText(bundle + QStringLiteral("/style.css"), "body{}"));

    const QString destination = temporary.filePath(QStringLiteral("destination"));
    pvf::assets::PvhtmlManifest manifest;
    QString error;
    QVERIFY(!pvf::assets::importPvhtml(bundle, destination, &manifest, &error));
    QVERIFY(!error.isEmpty());

    const QString wrongManifest = temporary.filePath(QStringLiteral("wrong-manifest"));
    QVERIFY(QDir().mkpath(wrongManifest));
    QVERIFY(writeText(wrongManifest + QStringLiteral("/index.html"), "<html></html>"));
    QVERIFY(writeText(wrongManifest + QStringLiteral("/pvhtml.json"),
                      R"({"entry":"missing.html"})"));

    error.clear();
    QVERIFY(!pvf::assets::importPvhtml(wrongManifest,
                                      temporary.filePath(QStringLiteral("destination2")),
                                      &manifest, &error));
    QVERIFY(!error.isEmpty());
}

void AssetsTest::pvhtmlRejectsSymlink()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString bundle = temporary.filePath(QStringLiteral("linked"));
    QVERIFY(QDir().mkpath(bundle));
    QVERIFY(writeText(bundle + QStringLiteral("/index.html"), "<html></html>"));

    const QString outside = temporary.filePath(QStringLiteral("outside.txt"));
    QVERIFY(writeText(outside, "secret"));
    if (!QFile::link(outside, bundle + QStringLiteral("/leak.txt")))
        QSKIP("当前文件系统不支持符号链接");
    QVERIFY(QFileInfo(bundle + QStringLiteral("/leak.txt")).isSymLink());

    pvf::assets::PvhtmlManifest manifest;
    QString error;
    QVERIFY(!pvf::assets::importPvhtml(bundle,
                                      temporary.filePath(QStringLiteral("destination")),
                                      &manifest, &error));
    QVERIFY(!error.isEmpty());
}

void AssetsTest::videoImportWithAudio()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString video = temporary.filePath(QStringLiteral("sample.avi"));
    if (!makeTestVideo(video))
        QSKIP("ffmpeg 不可用或无法生成测试视频");

    pvf::assets::AssetStore store(temporary.filePath(QStringLiteral("project")));
    QString error;
    const auto imported = store.importVideoWithAudio(video, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(imported.has_value());
    QVERIFY(imported->audioError.isEmpty());
    QVERIFY(imported->video.type == pvf::AssetType::Video);
    QVERIFY(imported->audio.has_value());
    QVERIFY(imported->audio->type == pvf::AssetType::Audio);
    QCOMPARE(imported->audio->name, QStringLiteral("sample.wav"));
    QCOMPARE(imported->video.metadata.value(QStringLiteral("audioHash")).toString(),
             imported->audio->hash);
    QVERIFY(isFile(imported->audio->directory + QStringLiteral("/content.wav")));

    const auto again = store.importVideoWithAudio(video, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(again.has_value());
    QCOMPARE(again->video.hash, imported->video.hash);
    QVERIFY(again->audio.has_value());
    QCOMPARE(again->audio->hash, imported->audio->hash);
    QCOMPARE(store.list().size(), 2);
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    AssetsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestAssets.moc"
