#include "export2/Export.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QColor>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSize>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>

#include <cmath>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
}

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

Entity makeEntity(const Uuid& uuid, int start, int end)
{
    Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.start = start;
    entity.end = end;
    return entity;
}

Project makeProject(int frames = 10)
{
    Project project;
    project.settings.width = 160;
    project.settings.height = 90;
    project.settings.fps = 30;
    project.settings.background = QStringLiteral("#101014");

    Entity camera = makeEntity(QStringLiteral("camera"), 0, frames);
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 80.0;
    camera.transform.y = 45.0;
    camera.transform.scaleX = 1.0;
    camera.transform.scaleY = 1.0;
    project.entities.insert(camera.uuid, camera);

    Entity rect = makeEntity(QStringLiteral("rect"), 0, frames);
    rect.transform.x = 80.0;
    rect.transform.y = 45.0;
    rect.content.kind = ContentKind::Vector;
    rect.content.vector.shape = 0;
    rect.content.vector.width = 100.0;
    rect.content.vector.height = 60.0;
    rect.content.vector.fillColor = QStringLiteral("#ff0000");
    rect.content.vector.strokeWidth = 0.0;
    project.entities.insert(rect.uuid, rect);

    return project;
}

QString writeToneWav(const QString& path, int sampleRate, int seconds)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};

    const int sampleCount = sampleRate * seconds;
    const quint32 dataSize = quint32(sampleCount * 2 * 2);
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataSize);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(sampleRate)
           << quint32(sampleRate * 4) << quint16(4) << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataSize;

    constexpr double kTwoPi = 6.28318530717958647692;
    for (int i = 0; i < sampleCount; ++i)
    {
        const double time = double(i) / double(sampleRate);
        const qint16 sample =
            qint16(std::sin(kTwoPi * 440.0 * time) * 12000.0);
        stream << sample << sample;
    }
    file.close();
    return path;
}

Entity makeAudioEntity(const Uuid& uuid, const AssetHash& hash, int start,
                       int end, int sourceIn = 0)
{
    Entity entity = makeEntity(uuid, start, end);
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Audio;
    entity.content.asset.hash = hash;
    entity.content.asset.sourceIn = sourceIn;
    entity.content.asset.volume = 1.0;
    return entity;
}

export2::Options makeOptions(const QString& path)
{
    export2::Options options;
    options.outputPath = path;
    options.cameraUuid = QStringLiteral("camera");
    options.outputSize = QSize(160, 90);
    options.outputFps = 30;
    options.crf = 18;
    return options;
}

AVFormatContext* openInput(const QString& path)
{
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path.toUtf8().constData(), nullptr,
                            nullptr)
        < 0)
    {
        CHECK(false);
        return nullptr;
    }
    CHECK(avformat_find_stream_info(input, nullptr) >= 0);
    return input;
}

AVStream* audioStreamOf(AVFormatContext* input)
{
    if (!input)
        return nullptr;
    for (unsigned int i = 0; i < input->nb_streams; ++i)
    {
        if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            return input->streams[i];
    }
    return nullptr;
}

double inputDuration(AVFormatContext* input)
{
    if (!input)
        return 0.0;
    if (input->duration > 0)
        return input->duration / double(AV_TIME_BASE);
    double duration = 0.0;
    for (unsigned int i = 0; i < input->nb_streams; ++i)
    {
        const AVStream* stream = input->streams[i];
        if (stream->duration > 0)
            duration = std::max(
                duration, stream->duration * av_q2d(stream->time_base));
    }
    return duration;
}

void testExportRedVector()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();
    const QString path = directory.path() + QStringLiteral("/out.mp4");

    int progressCalls = 0;
    int lastFrame = 0;
    int lastTotal = 0;
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path),
                             [&](int frame, int total) {
                                 ++progressCalls;
                                 lastFrame = frame;
                                 lastTotal = total;
                                 return true;
                             });

    CHECK(result.ok);
    CHECK(result.error.isEmpty());
    CHECK(result.framesWritten == 10);
    CHECK(progressCalls == 10);
    CHECK(lastFrame == 10);
    CHECK(lastTotal == 10);

    const QFileInfo file(path);
    CHECK(file.exists());
    CHECK(file.size() > 0);

    AVFormatContext* input = nullptr;
    const int openStatus = avformat_open_input(
        &input, path.toUtf8().constData(), nullptr, nullptr);
    CHECK(openStatus >= 0);
    if (openStatus >= 0 && input)
    {
        CHECK(avformat_find_stream_info(input, nullptr) >= 0);
        CHECK(input->nb_streams == 1);
        if (input->nb_streams >= 1)
        {
            const AVStream* video = input->streams[0];
            CHECK(video->codecpar->codec_type == AVMEDIA_TYPE_VIDEO);
            CHECK(video->codecpar->codec_id == AV_CODEC_ID_H264);
            CHECK(video->codecpar->width == 160);
            CHECK(video->codecpar->height == 90);

            double duration = 0.0;
            if (input->duration > 0)
                duration = input->duration / double(AV_TIME_BASE);
            else if (video->duration > 0)
                duration = video->duration * av_q2d(video->time_base);
            CHECK(std::abs(duration - 10.0 / 30.0) < 0.1);
        }
        avformat_close_input(&input);
    }
}

void testMissingCamera()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();

    {
        export2::Options options = makeOptions(
            directory.path() + QStringLiteral("/empty-camera.mp4"));
        options.cameraUuid.clear();
        const export2::Result result =
            export2::exportVideo(project, store, options);
        CHECK(!result.ok);
        CHECK(!result.error.isEmpty());
        CHECK(!QFileInfo::exists(options.outputPath));
    }

    {
        export2::Options options = makeOptions(
            directory.path() + QStringLiteral("/not-a-camera.mp4"));
        options.cameraUuid = QStringLiteral("rect");
        const export2::Result result =
            export2::exportVideo(project, store, options);
        CHECK(!result.ok);
        CHECK(!result.error.isEmpty());
        CHECK(!QFileInfo::exists(options.outputPath));
    }

    {
        export2::Options options = makeOptions(
            directory.path() + QStringLiteral("/missing-camera.mp4"));
        options.cameraUuid = QStringLiteral("missing");
        const export2::Result result =
            export2::exportVideo(project, store, options);
        CHECK(!result.ok);
        CHECK(!result.error.isEmpty());
        CHECK(!QFileInfo::exists(options.outputPath));
    }
}

void testCancelRemovesFile()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();
    const QString path = directory.path() + QStringLiteral("/cancelled.mp4");

    int progressCalls = 0;
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path),
                             [&](int, int) {
                                 ++progressCalls;
                                 return false;
                             });

    CHECK(!result.ok);
    CHECK(result.error == QStringLiteral("cancelled"));
    CHECK(progressCalls == 1);
    CHECK(!QFileInfo::exists(path));
    CHECK(QDir(directory.path())
              .entryList(QStringList{QStringLiteral("*.mp4")}, QDir::Files)
              .isEmpty());
}

void testOversizedOutput()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();
    const QString path = directory.path() + QStringLiteral("/too-big.mp4");

    export2::Options options = makeOptions(path);
    options.outputSize = QSize(4097, 90);
    const export2::Result result =
        export2::exportVideo(project, store, options);

    CHECK(!result.ok);
    CHECK(result.error.contains(QStringLiteral("4096")));
    CHECK(!QFileInfo::exists(path));
}

void testSilentProject()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject(30);
    const QString path = directory.path() + QStringLiteral("/silent.mp4");

    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));

    CHECK(result.ok);
    CHECK(result.framesWritten == 30);
    CHECK(result.warnings.isEmpty());
    CHECK(QFileInfo::exists(path));

    AVFormatContext* input = openInput(path);
    CHECK(input != nullptr);
    if (input)
    {
        CHECK(input->nb_streams == 1);
        CHECK(audioStreamOf(input) == nullptr);
        avformat_close_input(&input);
    }
}

void testAudioMux()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    const QString wav =
        writeToneWav(directory.path() + QStringLiteral("/tone.wav"), 48000, 1);
    CHECK(!wav.isEmpty());
    const auto stored = store.importFile(wav);
    CHECK(stored.has_value());
    if (!stored)
        return;

    Project project = makeProject(30);
    const Entity audio =
        makeAudioEntity(QStringLiteral("audio"), stored->hash, 0, 30);
    project.entities.insert(audio.uuid, audio);

    const QString path = directory.path() + QStringLiteral("/audio.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));

    CHECK(result.ok);
    CHECK(result.framesWritten == 30);
    CHECK(result.warnings.isEmpty());
    CHECK(QFileInfo::exists(path));

    AVFormatContext* input = openInput(path);
    CHECK(input != nullptr);
    if (input)
    {
        CHECK(input->nb_streams == 2);
        const AVStream* audioStream = audioStreamOf(input);
        CHECK(audioStream != nullptr);
        if (audioStream)
        {
            CHECK(audioStream->codecpar->codec_id == AV_CODEC_ID_AAC);
            CHECK(audioStream->codecpar->sample_rate == 48000);
            CHECK(audioStream->codecpar->ch_layout.nb_channels == 2);
        }
        CHECK(std::abs(inputDuration(input) - 1.0) < 0.1);
        avformat_close_input(&input);
    }
}

void testAudioSourceOffset()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    const QString wav =
        writeToneWav(directory.path() + QStringLiteral("/tone.wav"), 48000, 1);
    const auto stored = store.importFile(wav);
    CHECK(stored.has_value());
    if (!stored)
        return;

    Project project = makeProject(30);
    const Entity audio = makeAudioEntity(QStringLiteral("audio"), stored->hash,
                                         10, 30, 5);
    project.entities.insert(audio.uuid, audio);

    const QString path = directory.path() + QStringLiteral("/offset.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));

    CHECK(result.ok);
    CHECK(result.framesWritten == 30);
    CHECK(result.warnings.isEmpty());

    AVFormatContext* input = openInput(path);
    CHECK(input != nullptr);
    if (input)
    {
        CHECK(input->nb_streams == 2);
        CHECK(audioStreamOf(input) != nullptr);
        CHECK(std::abs(inputDuration(input) - 1.0) < 0.1);
        avformat_close_input(&input);
    }
}

void testMissingAudioSkipped()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    Project project = makeProject(30);
    const Entity audio = makeAudioEntity(
        QStringLiteral("audio"), QStringLiteral("missing-hash"), 0, 30);
    project.entities.insert(audio.uuid, audio);

    const QString path = directory.path() + QStringLiteral("/missing.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));

    CHECK(result.ok);
    CHECK(result.framesWritten == 30);
    CHECK(!result.warnings.isEmpty());
    CHECK(QFileInfo::exists(path));

    AVFormatContext* input = openInput(path);
    CHECK(input != nullptr);
    if (input)
    {
        CHECK(input->nb_streams == 1);
        CHECK(audioStreamOf(input) == nullptr);
        avformat_close_input(&input);
    }
}

void testOutputFpsMultipliers()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject(); // 10 frames at 30 fps

    {
        const QString path = directory.path() + QStringLiteral("/fps60.mp4");
        export2::Options options = makeOptions(path);
        options.outputFps = 60;
        const export2::Result result =
            export2::exportVideo(project, store, options);

        CHECK(result.ok);
        CHECK(result.framesWritten == 20);
        AVFormatContext* input = openInput(path);
        CHECK(input != nullptr);
        if (input)
        {
            CHECK(input->nb_streams == 1);
            if (input->nb_streams >= 1)
            {
                const AVStream* video = input->streams[0];
                CHECK(video->codecpar->width == 160);
                CHECK(video->codecpar->height == 90);
                CHECK(std::abs(av_q2d(video->r_frame_rate) - 60.0) < 0.01);
            }
            CHECK(std::abs(inputDuration(input) - 10.0 / 30.0) < 0.05);
            avformat_close_input(&input);
        }
    }

    {
        const QString path = directory.path() + QStringLiteral("/fps120.mp4");
        export2::Options options = makeOptions(path);
        options.outputFps = 120;
        const export2::Result result =
            export2::exportVideo(project, store, options);

        CHECK(result.ok);
        CHECK(result.framesWritten == 40);
        AVFormatContext* input = openInput(path);
        CHECK(input != nullptr);
        if (input)
        {
            CHECK(input->nb_streams == 1);
            if (input->nb_streams >= 1)
            {
                const AVStream* video = input->streams[0];
                CHECK(std::abs(av_q2d(video->r_frame_rate) - 120.0) < 0.01);
            }
            CHECK(std::abs(inputDuration(input) - 10.0 / 30.0) < 0.05);
            avformat_close_input(&input);
        }
    }
}

void testInvalidOutputFps()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();
    const QString path = directory.path() + QStringLiteral("/bad-fps.mp4");

    export2::Options options = makeOptions(path);
    options.outputFps = 45; // not 1x/2x/4x of 30
    const export2::Result result =
        export2::exportVideo(project, store, options);

    CHECK(!result.ok);
    CHECK(result.error.contains(QStringLiteral("帧率")));
    CHECK(!QFileInfo::exists(path));
}

void testResolutionScale()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();
    const QString path = directory.path() + QStringLiteral("/scale2.mp4");

    export2::Options options = makeOptions(path);
    options.outputSize = QSize(); // project size * resolutionScale
    options.resolutionScale = 2;
    const export2::Result result =
        export2::exportVideo(project, store, options);

    CHECK(result.ok);
    CHECK(result.framesWritten == 10);
    AVFormatContext* input = openInput(path);
    CHECK(input != nullptr);
    if (input)
    {
        CHECK(input->nb_streams == 1);
        if (input->nb_streams >= 1)
        {
            CHECK(input->streams[0]->codecpar->width == 320);
            CHECK(input->streams[0]->codecpar->height == 180);
        }
        CHECK(std::abs(inputDuration(input) - 10.0 / 30.0) < 0.05);
        avformat_close_input(&input);
    }
}

void testInvalidResolutionScale()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();
    const QString path = directory.path() + QStringLiteral("/scale3.mp4");

    export2::Options options = makeOptions(path);
    options.outputSize = QSize();
    options.resolutionScale = 3;
    const export2::Result result =
        export2::exportVideo(project, store, options);

    CHECK(!result.ok);
    CHECK(result.error.contains(QStringLiteral("倍率")));
    CHECK(!QFileInfo::exists(path));
}

void testAudioDurationAtDoubleFps()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    const QString wav =
        writeToneWav(directory.path() + QStringLiteral("/tone.wav"), 48000, 1);
    CHECK(!wav.isEmpty());
    const auto stored = store.importFile(wav);
    CHECK(stored.has_value());
    if (!stored)
        return;

    Project project = makeProject(30); // 1 second at 30 fps
    const Entity audio =
        makeAudioEntity(QStringLiteral("audio"), stored->hash, 0, 30);
    project.entities.insert(audio.uuid, audio);

    const QString path = directory.path() + QStringLiteral("/audio60.mp4");
    export2::Options options = makeOptions(path);
    options.outputFps = 60;
    const export2::Result result =
        export2::exportVideo(project, store, options);

    CHECK(result.ok);
    CHECK(result.framesWritten == 60);
    CHECK(result.warnings.isEmpty());

    AVFormatContext* input = openInput(path);
    CHECK(input != nullptr);
    if (input)
    {
        CHECK(input->nb_streams == 2);
        CHECK(audioStreamOf(input) != nullptr);
        // Audio keeps the project duration in seconds, not the output frame
        // count.
        CHECK(std::abs(inputDuration(input) - 1.0) < 0.1);
        avformat_close_input(&input);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const QVector<QPair<QString, void (*)()>> tests = {
        {QStringLiteral("exportRedVector"), testExportRedVector},
        {QStringLiteral("silentProject"), testSilentProject},
        {QStringLiteral("audioMux"), testAudioMux},
        {QStringLiteral("audioSourceOffset"), testAudioSourceOffset},
        {QStringLiteral("missingAudioSkipped"), testMissingAudioSkipped},
        {QStringLiteral("missingCamera"), testMissingCamera},
        {QStringLiteral("cancelRemovesFile"), testCancelRemovesFile},
        {QStringLiteral("oversizedOutput"), testOversizedOutput},
        {QStringLiteral("outputFpsMultipliers"), testOutputFpsMultipliers},
        {QStringLiteral("invalidOutputFps"), testInvalidOutputFps},
        {QStringLiteral("resolutionScale"), testResolutionScale},
        {QStringLiteral("invalidResolutionScale"), testInvalidResolutionScale},
        {QStringLiteral("audioDurationAtDoubleFps"),
         testAudioDurationAtDoubleFps},
    };

    QString requested;
    if (argc > 1)
        requested = QString::fromLocal8Bit(argv[1]);

    int ran = 0;
    for (const auto& entry : tests)
    {
        if (!requested.isEmpty() && entry.first != requested)
            continue;
        entry.second();
        ++ran;
    }

    if (!requested.isEmpty() && ran == 0)
    {
        QTextStream(stderr) << QStringLiteral("Unknown test: %1\n").arg(requested);
        return 2;
    }

    QTextStream(stdout) << QStringLiteral("export2: %1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
