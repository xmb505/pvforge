// End-to-end export checks for the editor-only `hidden` flag: hidden entities
// and audio must still reach the MP4 while `visible` keeps its old meaning.
// Kept in a separate executable so Export2Tests.cpp stays under the code-size
// ratchet.

#include "export2/Export.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QColor>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QScopeGuard>
#include <QSize>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>

#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
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
    project.entities.insert(camera.uuid, camera);

    Entity rect = makeEntity(QStringLiteral("rect"), 0, frames);
    rect.content.kind = ContentKind::Vector;
    rect.content.vector.shape = 0;
    rect.content.vector.width = 100.0;
    rect.content.vector.height = 60.0;
    rect.content.vector.fillColor = QStringLiteral("#ff0000");
    rect.content.vector.strokeWidth = 0.0;
    project.entities.insert(rect.uuid, rect);
    return project;
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
                       int end)
{
    Entity entity = makeEntity(uuid, start, end);
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Audio;
    entity.content.asset.hash = hash;
    entity.content.asset.volume = 1.0;
    return entity;
}

QVector<float> decodeMono(const QString& path)
{
    QVector<float> samples;
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path.toUtf8().constData(), nullptr, nullptr)
        < 0)
        return samples;
    const auto inputGuard = qScopeGuard([&] { avformat_close_input(&input); });
    if (avformat_find_stream_info(input, nullptr) < 0)
        return samples;

    const int index =
        av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (index < 0)
        return samples;

    const AVCodec* codec =
        avcodec_find_decoder(input->streams[index]->codecpar->codec_id);
    if (!codec)
        return samples;
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (!context)
        return samples;
    const auto contextGuard =
        qScopeGuard([&] { avcodec_free_context(&context); });
    if (avcodec_parameters_to_context(context, input->streams[index]->codecpar)
            < 0
        || avcodec_open2(context, codec, nullptr) < 0)
        return samples;

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame)
    {
        av_packet_free(&packet);
        av_frame_free(&frame);
        return samples;
    }
    const auto packetGuard = qScopeGuard([&] { av_packet_free(&packet); });
    const auto frameGuard = qScopeGuard([&] { av_frame_free(&frame); });

    auto append = [&](const AVFrame* decoded) {
        const int channels = decoded->ch_layout.nb_channels;
        if (channels <= 0 || decoded->nb_samples <= 0)
            return;
        for (int i = 0; i < decoded->nb_samples; ++i)
        {
            double sum = 0.0;
            for (int channel = 0; channel < channels; ++channel)
            {
                if (decoded->format == AV_SAMPLE_FMT_FLTP)
                    sum += reinterpret_cast<const float*>(
                        decoded->extended_data[channel])[i];
                else if (decoded->format == AV_SAMPLE_FMT_FLT)
                    sum += reinterpret_cast<const float*>(
                        decoded->extended_data[0])[i * channels + channel];
                else if (decoded->format == AV_SAMPLE_FMT_S16P)
                    sum += reinterpret_cast<const qint16*>(
                               decoded->extended_data[channel])[i]
                           / 32768.0;
            }
            samples.append(float(sum / channels));
        }
    };

    auto receive = [&]() {
        while (true)
        {
            const int status = avcodec_receive_frame(context, frame);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF)
                return;
            if (status < 0)
                return;
            append(frame);
            av_frame_unref(frame);
        }
    };

    while (av_read_frame(input, packet) >= 0)
    {
        if (packet->stream_index == index)
        {
            if (avcodec_send_packet(context, packet) < 0)
                break;
            av_packet_unref(packet);
            receive();
            continue;
        }
        av_packet_unref(packet);
    }
    avcodec_send_packet(context, nullptr);
    receive();
    return samples;
}

QImage decodeFirstVideoFrame(const QString& path)
{
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path.toUtf8().constData(), nullptr, nullptr)
        < 0)
        return {};
    const auto inputGuard = qScopeGuard([&] { avformat_close_input(&input); });
    if (avformat_find_stream_info(input, nullptr) < 0)
        return {};

    const int index =
        av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0)
        return {};

    const AVCodec* codec =
        avcodec_find_decoder(input->streams[index]->codecpar->codec_id);
    if (!codec)
        return {};
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (!context)
        return {};
    const auto contextGuard =
        qScopeGuard([&] { avcodec_free_context(&context); });
    if (avcodec_parameters_to_context(context, input->streams[index]->codecpar)
            < 0
        || avcodec_open2(context, codec, nullptr) < 0)
        return {};

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame)
    {
        av_packet_free(&packet);
        av_frame_free(&frame);
        return {};
    }
    const auto packetGuard = qScopeGuard([&] { av_packet_free(&packet); });
    const auto frameGuard = qScopeGuard([&] { av_frame_free(&frame); });

    QImage result;
    bool inputDone = false;
    while (result.isNull())
    {
        if (!inputDone)
        {
            if (av_read_frame(input, packet) < 0)
            {
                inputDone = true;
                avcodec_send_packet(context, nullptr);
            }
            else
            {
                if (packet->stream_index == index)
                    avcodec_send_packet(context, packet);
                av_packet_unref(packet);
            }
        }

        const int status = avcodec_receive_frame(context, frame);
        if (status == AVERROR(EAGAIN))
        {
            if (inputDone)
                break;
            continue;
        }
        if (status < 0)
            break;

        SwsContext* sws = sws_getContext(
            frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), frame->width,
            frame->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr,
            nullptr);
        if (sws)
        {
            QImage image(frame->width, frame->height, QImage::Format_RGB888);
            uint8_t* destination[4] = {image.bits(), nullptr, nullptr, nullptr};
            int strides[4] = {int(image.bytesPerLine()), 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                      destination, strides);
            sws_freeContext(sws);
            result = image.copy();
        }
        av_frame_unref(frame);
    }
    return result;
}

double peakOf(const QVector<float>& samples)
{
    double peak = 0.0;
    for (const float sample : samples)
        peak = std::max(peak, std::abs(double(sample)));
    return peak;
}

void testHiddenVectorIsExported()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    Project project = makeProject();
    project.entities[QStringLiteral("rect")].hidden = true;
    const QString path = directory.path() + QStringLiteral("/hidden.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));
    CHECK(result.ok);
    CHECK(result.framesWritten == 10);

    const QImage frame = decodeFirstVideoFrame(path);
    CHECK(frame.size() == QSize(160, 90));
    if (!frame.size().isEmpty())
    {
        const QColor centre = frame.pixelColor(80, 45);
        CHECK(centre.red() > 150 && centre.green() < 110 && centre.blue() < 110);
    }
}

void testInvisibleVectorIsNotExported()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    Project project = makeProject();
    Entity& rect = project.entities[QStringLiteral("rect")];
    rect.hidden = true;
    rect.visible = false;
    const QString path = directory.path() + QStringLiteral("/invisible.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));
    CHECK(result.ok);
    CHECK(result.framesWritten == 10);

    const QImage frame = decodeFirstVideoFrame(path);
    CHECK(frame.size() == QSize(160, 90));
    if (!frame.size().isEmpty())
    {
        const QColor centre = frame.pixelColor(80, 45);
        CHECK(centre.red() < 60 && centre.green() < 60 && centre.blue() < 60);
    }
}

void testHiddenAudioIsExported()
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
    Entity audio =
        makeAudioEntity(QStringLiteral("audio"), stored->hash, 0, 30);
    audio.hidden = true;
    project.entities.insert(audio.uuid, audio);

    const QString path = directory.path() + QStringLiteral("/hidden-audio.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));
    CHECK(result.ok);
    CHECK(result.framesWritten == 30);
    CHECK(peakOf(decodeMono(path)) > 0.05);
}

void testHiddenParentDoesNotMuteChild()
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
    Entity parent = makeEntity(QStringLiteral("parent"), 0, 30);
    parent.hidden = true;
    project.entities.insert(parent.uuid, parent);
    Entity audio =
        makeAudioEntity(QStringLiteral("audio"), stored->hash, 0, 30);
    audio.parentUuid = parent.uuid;
    project.entities.insert(audio.uuid, audio);

    const QString path = directory.path() + QStringLiteral("/hidden-parent.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));
    CHECK(result.ok);
    CHECK(result.framesWritten == 30);
    CHECK(peakOf(decodeMono(path)) > 0.05);
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    testHiddenVectorIsExported();
    testInvisibleVectorIsNotExported();
    testHiddenAudioIsExported();
    testHiddenParentDoesNotMuteChild();

    QTextStream(stdout) << QStringLiteral("export2-hidden: %1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
