#include "audio2/Audio2.h"
#include "audio2/Peaks.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QMediaDevices>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace pvf;
using namespace pvf::model;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    ++gChecks;
    if (condition)
        return;

    ++gFailures;
    QTextStream(stderr) << QStringLiteral("FAIL %1:%2  %3")
                                .arg(QString::fromLatin1(file))
                                .arg(line)
                                .arg(QString::fromLatin1(expression))
                        << '\n';
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr double kTwoPi = 6.28318530717958647692;

QString writeWav(const QString& path, int sampleRate, int sampleCount,
                 const std::function<double(int)>& sampleAt)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};

    const quint32 dataSize = quint32(sampleCount * kChannels * 2);
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataSize);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16) << quint16(1) << quint16(kChannels)
           << quint32(sampleRate) << quint32(sampleRate * kChannels * 2)
           << quint16(kChannels * 2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataSize;

    for (int i = 0; i < sampleCount; ++i)
    {
        const double clamped = std::clamp(sampleAt(i), -1.0, 1.0);
        const qint16 sample = qint16(std::lround(clamped * 32767.0));
        stream << sample << sample;
    }
    file.close();
    return path;
}

QString writeToneWav(const QString& path, double frequency, double amplitude,
                     double seconds = 1.0)
{
    const int sampleCount = int(kSampleRate * seconds);
    return writeWav(path, kSampleRate, sampleCount, [=](int i) {
        const double time = double(i) / double(kSampleRate);
        return std::sin(kTwoPi * frequency * time) * amplitude;
    });
}

QString writeSplitToneWav(const QString& path, double firstFrequency,
                          double firstAmplitude, double secondFrequency,
                          double secondAmplitude)
{
    const int sampleCount = kSampleRate;
    return writeWav(path, kSampleRate, sampleCount, [=](int i) {
        const double time = double(i) / double(kSampleRate);
        const bool secondHalf = time >= 0.5;
        const double frequency = secondHalf ? secondFrequency : firstFrequency;
        const double amplitude = secondHalf ? secondAmplitude : firstAmplitude;
        return std::sin(kTwoPi * frequency * time) * amplitude;
    });
}

Project makeProject()
{
    Project project;
    project.settings.fps = 30;
    return project;
}

void addAudioEntity(Project& project, const QString& uuid,
                    const AssetHash& hash, int start, int end,
                    int sourceIn = 0, double volume = 1.0)
{
    Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.start = start;
    entity.end = end;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Audio;
    entity.content.asset.hash = hash;
    entity.content.asset.sourceIn = sourceIn;
    entity.content.asset.volume = volume;
    project.entities.insert(uuid, entity);
}

audio2::MixOptions mixOptions(int startFrame, int endFrame)
{
    audio2::MixOptions options;
    options.startFrame = startFrame;
    options.endFrame = endFrame;
    options.fps = 30;
    options.sampleRate = kSampleRate;
    options.channels = kChannels;
    return options;
}

int outputFrames(const QByteArray& pcm)
{
    return pcm.size() / int(sizeof(float)) / kChannels;
}

double rms(const QByteArray& pcm, int startFrame, int endFrame, int channel = 0)
{
    if (pcm.isEmpty())
        return 0.0;
    const auto* samples = reinterpret_cast<const float*>(pcm.constData());
    const int frames = outputFrames(pcm);
    startFrame = std::clamp(startFrame, 0, frames);
    endFrame = std::clamp(endFrame, startFrame, frames);
    if (endFrame <= startFrame)
        return 0.0;

    double sum = 0.0;
    for (int frame = startFrame; frame < endFrame; ++frame)
    {
        const float value = samples[frame * kChannels + channel];
        sum += double(value) * double(value);
    }
    return std::sqrt(sum / double(endFrame - startFrame));
}

double maxDifference(const QByteArray& first, const QByteArray& second)
{
    if (first.size() != second.size())
        return 1.0e9;
    const auto* a = reinterpret_cast<const float*>(first.constData());
    const auto* b = reinterpret_cast<const float*>(second.constData());
    const int values = first.size() / int(sizeof(float));
    double difference = 0.0;
    for (int i = 0; i < values; ++i)
    {
        difference = std::max(difference,
                              std::abs(double(a[i]) - double(b[i])));
    }
    return difference;
}

double peak(const QByteArray& pcm)
{
    if (pcm.isEmpty())
        return 0.0;
    const auto* samples = reinterpret_cast<const float*>(pcm.constData());
    const int values = pcm.size() / int(sizeof(float));
    double result = 0.0;
    for (int i = 0; i < values; ++i)
        result = std::max(result, std::abs(double(samples[i])));
    return result;
}

void testMixTwoTones()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    QString error;
    const auto first = store.importFile(
        writeToneWav(directory.path() + QStringLiteral("/first.wav"), 440.0,
                     0.5),
        &error);
    const auto second = store.importFile(
        writeToneWav(directory.path() + QStringLiteral("/second.wav"), 660.0,
                     0.25),
        &error);
    CHECK(first.has_value());
    CHECK(second.has_value());
    if (!first || !second)
        return;

    Project project = makeProject();
    addAudioEntity(project, QStringLiteral("first"), first->hash, 0, 30);
    addAudioEntity(project, QStringLiteral("second"), second->hash, 0, 30);

    const audio2::MixOptions options = mixOptions(0, 30);
    const QByteArray both = audio2::mixRange(project, store, options);
    CHECK(!both.isEmpty());
    CHECK(outputFrames(both) == 48000);
    CHECK(both.size()
          == 48000 * kChannels * int(sizeof(float)));
    CHECK(peak(both) > 0.55);
    CHECK(peak(both) <= 1.0);

    Project firstOnly = project;
    firstOnly.entities.remove(QStringLiteral("second"));
    Project secondOnly = project;
    secondOnly.entities.remove(QStringLiteral("first"));
    const QByteArray onlyFirst = audio2::mixRange(firstOnly, store, options);
    const QByteArray onlySecond = audio2::mixRange(secondOnly, store, options);
    CHECK(!onlyFirst.isEmpty());
    CHECK(!onlySecond.isEmpty());
    CHECK(maxDifference(both, onlyFirst) > 0.0);
    CHECK(maxDifference(onlyFirst, onlySecond) > 0.0);

    const auto* all = reinterpret_cast<const float*>(both.constData());
    const auto* a = reinterpret_cast<const float*>(onlyFirst.constData());
    const auto* b = reinterpret_cast<const float*>(onlySecond.constData());
    double sumDifference = 0.0;
    if (onlyFirst.size() == both.size() && onlySecond.size() == both.size())
    {
        for (int i = 0; i < both.size() / int(sizeof(float)); ++i)
        {
            sumDifference =
                std::max(sumDifference,
                         std::abs(double(all[i])
                                  - (double(a[i]) + double(b[i]))));
        }
    }
    else
    {
        sumDifference = 1.0e9;
    }
    CHECK(sumDifference < 1.0e-5);
}

void testVolumeHalf()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    QString error;
    const auto asset = store.importFile(
        writeToneWav(directory.path() + QStringLiteral("/tone.wav"), 440.0,
                     0.5),
        &error);
    CHECK(asset.has_value());
    if (!asset)
        return;

    Project loud = makeProject();
    addAudioEntity(loud, QStringLiteral("tone"), asset->hash, 0, 30, 0, 1.0);
    Project staticHalf = loud;
    staticHalf.entities[QStringLiteral("tone")].content.asset.volume = 0.5;

    Project trackHalf = loud;
    Track volumeTrack;
    Keyframe key;
    key.frame = 0;
    key.value = 0.5;
    volumeTrack.keys.append(key);
    trackHalf.entities[QStringLiteral("tone")].tracks.insert(
        QStringLiteral("volume"), volumeTrack);

    const audio2::MixOptions options = mixOptions(0, 30);
    const QByteArray full = audio2::mixRange(loud, store, options);
    const QByteArray half = audio2::mixRange(staticHalf, store, options);
    const QByteArray tracked = audio2::mixRange(trackHalf, store, options);
    CHECK(!full.isEmpty());
    CHECK(!half.isEmpty());
    CHECK(!tracked.isEmpty());
    CHECK(half.size() == full.size());
    CHECK(tracked.size() == full.size());

    CHECK(maxDifference(half, tracked) < 1.0e-6);
    const auto* fullSamples = reinterpret_cast<const float*>(full.constData());
    const auto* halfSamples = reinterpret_cast<const float*>(half.constData());
    double difference = 0.0;
    for (int i = 0; i < full.size() / int(sizeof(float)); ++i)
    {
        difference = std::max(
            difference,
            std::abs(double(halfSamples[i]) - 0.5 * double(fullSamples[i])));
    }
    CHECK(difference < 1.0e-6);
}

void testSourceOffset()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    QString error;
    const auto asset = store.importFile(
        writeSplitToneWav(directory.path() + QStringLiteral("/split.wav"),
                          440.0, 0.4, 880.0, 0.2),
        &error);
    CHECK(asset.has_value());
    if (!asset)
        return;

    Project project = makeProject();
    // Entity starts at frame 15 and reads the asset from frame 15 (0.5 s).
    // Only the second half of the requested 0..30 range overlaps, and it must
    // carry the 880 Hz / 0.2 half of the source.
    addAudioEntity(project, QStringLiteral("split"), asset->hash, 15, 45, 15);

    const audio2::MixOptions options = mixOptions(0, 30);
    const QByteArray pcm = audio2::mixRange(project, store, options);
    CHECK(!pcm.isEmpty());
    CHECK(outputFrames(pcm) == 48000);
    CHECK(rms(pcm, 0, 24000) < 1.0e-5);
    const double secondRms = rms(pcm, 24000, 48000);
    CHECK(std::abs(secondRms - 0.2 / std::sqrt(2.0)) < 0.01);
}

void testShortAudioPadding()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    QString error;
    const auto shortAsset = store.importFile(
        writeToneWav(directory.path() + QStringLiteral("/short.wav"), 440.0,
                     0.5, 0.25),
        &error);
    CHECK(shortAsset.has_value());
    if (!shortAsset)
        return;

    const audio2::MixOptions options = mixOptions(0, 30);

    {
        // 0.25 s of source inside a 1 s entity: the tail is silence.
        Project project = makeProject();
        addAudioEntity(project, QStringLiteral("short"), shortAsset->hash, 0,
                       30);
        const QByteArray pcm = audio2::mixRange(project, store, options);
        CHECK(!pcm.isEmpty());
        CHECK(outputFrames(pcm) == 48000);
        CHECK(rms(pcm, 0, 12000) > 0.1);
        CHECK(rms(pcm, 12000, 48000) < 1.0e-5);
    }

    {
        // 1 s of source inside a 0.5 s entity: the excess is cropped.
        Project project = makeProject();
        addAudioEntity(project, QStringLiteral("long"), shortAsset->hash, 0,
                       15);
        const QByteArray pcm = audio2::mixRange(project, store, options);
        CHECK(!pcm.isEmpty());
        CHECK(outputFrames(pcm) == 48000);
        CHECK(rms(pcm, 0, 12000) > 0.1);
        CHECK(rms(pcm, 24000, 48000) < 1.0e-5);
    }
}

void testNoAudio()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    Project project = makeProject();
    Entity empty;
    empty.uuid = QStringLiteral("empty");
    empty.start = 0;
    empty.end = 30;
    project.entities.insert(empty.uuid, empty);

    CHECK(audio2::mixRange(project, store, mixOptions(0, 30)).isEmpty());
    CHECK(audio2::mixRange(project, store, mixOptions(10, 10)).isEmpty());
}

void testHiddenAndInvisible()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    QString error;
    const auto asset = store.importFile(
        writeToneWav(directory.path() + QStringLiteral("/tone.wav"), 440.0,
                     0.5),
        &error);
    CHECK(asset.has_value());
    if (!asset)
        return;

    const audio2::MixOptions options = mixOptions(0, 30);

    // `hidden` is editor-only: it never mutes audio.
    {
        Project project = makeProject();
        addAudioEntity(project, QStringLiteral("hidden"), asset->hash, 0, 30);
        project.entities[QStringLiteral("hidden")].hidden = true;
        CHECK(!audio2::mixRange(project, store, options).isEmpty());
    }

    // `hidden` does not override an explicit visible=0.
    {
        Project project = makeProject();
        addAudioEntity(project, QStringLiteral("hidden-invisible"),
                       asset->hash, 0, 30);
        model::Entity& entity =
            project.entities[QStringLiteral("hidden-invisible")];
        entity.hidden = true;
        entity.visible = false;
        CHECK(audio2::mixRange(project, store, options).isEmpty());
    }

    {
        Project project = makeProject();
        addAudioEntity(project, QStringLiteral("invisible"), asset->hash, 0,
                       30);
        project.entities[QStringLiteral("invisible")].visible = false;
        CHECK(audio2::mixRange(project, store, options).isEmpty());
    }

    {
        Project project = makeProject();
        addAudioEntity(project, QStringLiteral("tracked"), asset->hash, 0, 30);
        Track visibleTrack;
        Keyframe key;
        key.frame = 0;
        key.value = 0.0;
        visibleTrack.keys.append(key);
        project.entities[QStringLiteral("tracked")].tracks.insert(
            QStringLiteral("visible"), visibleTrack);
        CHECK(audio2::mixRange(project, store, options).isEmpty());
    }

    {
        Project project = makeProject();
        Entity parent;
        parent.uuid = QStringLiteral("parent");
        parent.start = 0;
        parent.end = 30;
        parent.hidden = true;
        project.entities.insert(parent.uuid, parent);
        addAudioEntity(project, QStringLiteral("child"), asset->hash, 0, 30);
        project.entities[QStringLiteral("child")].parentUuid =
            QStringLiteral("parent");

        // A hidden parent does not mute an inheriting child.
        CHECK(!audio2::mixRange(project, store, options).isEmpty());

        // Parent visibility inheritance still applies to visible=0.
        project.entities[QStringLiteral("parent")].visible = false;
        CHECK(audio2::mixRange(project, store, options).isEmpty());

        project.entities[QStringLiteral("child")].inheritVisibility = false;
        CHECK(!audio2::mixRange(project, store, options).isEmpty());
    }
}

void testPlaybackNoAudio()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();

    audio2::Playback playback(project, store);
    QString error;
    CHECK(!playback.play(0, 30, 30, &error));
    CHECK(!error.isEmpty());
    CHECK(!playback.isPlaying());
    playback.stop();
    CHECK(!playback.isPlaying());
}

void testPlaybackNoDevice()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    QString error;
    const auto asset = store.importFile(
        writeToneWav(directory.path() + QStringLiteral("/tone.wav"), 440.0,
                     0.5),
        &error);
    CHECK(asset.has_value());
    if (!asset)
        return;

    Project project = makeProject();
    addAudioEntity(project, QStringLiteral("tone"), asset->hash, 0, 30);

    CHECK(QMediaDevices::defaultAudioOutput().isNull());

    audio2::Playback playback(project, store);
    error.clear();
    CHECK(!playback.play(0, 30, 30, &error));
    CHECK(!error.isEmpty());
    CHECK(!playback.isPlaying());
    CHECK(playback.startFrame() == 0);
    CHECK(playback.endFrame() == 30);
    playback.stop();
}

bool makePeaksFixture(const QString& path)
{
    QProcess process;
    process.start(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-y"), QStringLiteral("-hide_banner"),
         QStringLiteral("-loglevel"), QStringLiteral("error"),
         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("sine=frequency=440:duration=1"), QStringLiteral("-af"),
         QStringLiteral("volume='if(lt(t,0.5),0.2,6.0)':eval=frame"),
         QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"),
         QStringLiteral("1"), path});
    if (!process.waitForStarted(5000))
        return false;
    if (!process.waitForFinished(30000))
    {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit
           && process.exitCode() == 0 && QFile::exists(path);
}

void testPeaks()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());

    const QString path =
        directory.path() + QStringLiteral("/peaks.wav");
    if (!makePeaksFixture(path))
    {
        QTextStream(stderr) << QStringLiteral("ffmpeg 不可用，跳过 peaks 测试\n");
        return;
    }

    QString error = QStringLiteral("stale");
    const QVector<float> peaks = audio2::audioPeaks(path, 64, &error);
    CHECK(!peaks.isEmpty());
    CHECK(error.isEmpty());
    CHECK(peaks.size() == 64);

    float low = 1.0f;
    float high = 0.0f;
    bool inRange = true;
    for (float value : peaks)
    {
        inRange = inRange && value >= 0.0f && value <= 1.0f;
        low = std::min(low, value);
        high = std::max(high, value);
    }
    CHECK(inRange);
    CHECK(high > 0.5f);
    CHECK(low < 0.3f);
    CHECK(high - low > 0.2f);

    // The second-half window sees only the loud part.
    error.clear();
    const QVector<float> second =
        audio2::audioPeaks(path, 16, 0.5, 0.5, &error);
    CHECK(error.isEmpty());
    CHECK(second.size() == 16);
    float secondLow = 1.0f;
    for (float value : second)
        secondLow = std::min(secondLow, value);
    CHECK(secondLow > 0.5f);

    // Missing file and invalid bucket count fail without crashing.
    error.clear();
    const QVector<float> missing =
        audio2::audioPeaks(directory.path() + QStringLiteral("/missing.wav"),
                           8, &error);
    CHECK(missing.isEmpty());
    CHECK(!error.isEmpty());

    error.clear();
    CHECK(audio2::audioPeaks(path, 0, &error).isEmpty());
    CHECK(!error.isEmpty());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const QString name = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    auto run = [&name](const char* testName) {
        return name.isEmpty() || name == QLatin1String(testName);
    };

    bool known = false;
    if (run("mixTwoTones"))
    {
        known = true;
        testMixTwoTones();
    }
    if (run("volumeHalf"))
    {
        known = true;
        testVolumeHalf();
    }
    if (run("sourceOffset"))
    {
        known = true;
        testSourceOffset();
    }
    if (run("shortAudioPadding"))
    {
        known = true;
        testShortAudioPadding();
    }
    if (run("noAudio"))
    {
        known = true;
        testNoAudio();
    }
    if (run("hiddenAndInvisible"))
    {
        known = true;
        testHiddenAndInvisible();
    }
    if (run("playbackNoAudio"))
    {
        known = true;
        testPlaybackNoAudio();
    }
    if (run("playbackNoDevice"))
    {
        known = true;
        testPlaybackNoDevice();
    }
    if (run("peaks"))
    {
        known = true;
        testPeaks();
    }

    if (!known)
    {
        QTextStream(stderr) << QStringLiteral("Unknown test: %1\n").arg(name);
        return 2;
    }

    QTextStream(stderr) << QStringLiteral("%1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
