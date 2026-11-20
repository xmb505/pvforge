// Media-focused export tests.  Kept in its own executable (and file) so the
// long-standing Export2Tests.cpp stays inside the code-size ratchet.
//
// Covered here:
//   - audio clips stop at entity.end (#128)
//   - missing/failed visual content makes the export fail (#129)

#include "export2/Export.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSize>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
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
    camera.transform.x = 0.0;
    camera.transform.y = 0.0;
    project.entities.insert(camera.uuid, camera);

    Entity rect = makeEntity(QStringLiteral("rect"), 0, frames);
    rect.transform.x = 0.0;
    rect.transform.y = 0.0;
    rect.content.kind = ContentKind::Vector;
    rect.content.vector.shape = 0;
    rect.content.vector.width = 100.0;
    rect.content.vector.height = 60.0;
    rect.content.vector.fillColor = QStringLiteral("#ff0000");
    rect.content.vector.strokeWidth = 0.0;
    project.entities.insert(rect.uuid, rect);
    return project;
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

Entity makeAssetEntity(const Uuid& uuid, AssetType type, const AssetHash& hash,
                       int start, int end)
{
    Entity entity = makeEntity(uuid, start, end);
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = type;
    entity.content.asset.hash = hash;
    return entity;
}

// Writes a real asset directory (asset.json + content or HTML bundle) so the
// renderer sees the asset as present instead of painting the missing-asset
// card.
AssetHash writeTestAsset(const QString& projectRoot, QChar hashFill,
                         AssetType type, int width = 100, int height = 50)
{
    const AssetHash hash(64, hashFill);
    const QString directory = projectRoot + QStringLiteral("/assets/") + hash;
    if (!QDir().mkpath(directory))
        return {};

    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"),
                    type == AssetType::Html ? QStringLiteral("html")
                                            : QStringLiteral("video"));
    metadata.insert(QStringLiteral("name"), QStringLiteral("test-asset"));
    metadata.insert(QStringLiteral("width"), width);
    metadata.insert(QStringLiteral("height"), height);

    if (type == AssetType::Html)
    {
        if (!QDir().mkpath(directory + QStringLiteral("/bundle")))
            return {};
        metadata.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
        QFile entry(directory + QStringLiteral("/bundle/index.html"));
        if (!entry.open(QIODevice::WriteOnly))
            return {};
        entry.write("<html><body></body></html>");
        entry.close();
    }
    else
    {
        QFile content(directory + QStringLiteral("/content.bin"));
        if (!content.open(QIODevice::WriteOnly))
            return {};
        content.write("asset-bytes");
        content.close();
    }

    QFile json(directory + QStringLiteral("/asset.json"));
    if (!json.open(QIODevice::WriteOnly))
        return {};
    json.write(QJsonDocument(metadata).toJson());
    json.close();
    return hash;
}

QImage redImage()
{
    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    return image;
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

// Decodes the first audio stream into mono float samples at its native rate.
QVector<float> decodeMono(const QString& path)
{
    QVector<float> samples;
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path.toUtf8().constData(), nullptr,
                            nullptr)
        < 0)
        return samples;
    const auto inputGuard =
        qScopeGuard([&] { avformat_close_input(&input); });
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
                else if (decoded->format == AV_SAMPLE_FMT_S16)
                    sum += reinterpret_cast<const qint16*>(
                               decoded->extended_data[0])[i * channels
                                                           + channel]
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

// Issue #128: the mix must stop at entity.end even when the decoded asset is
// longer.  A 10 s tone trimmed to 3 s must be silent after 3 s.
void testAudioStopsAtEntityEnd()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    const QString wav =
        writeToneWav(directory.path() + QStringLiteral("/long.wav"), 48000, 10);
    CHECK(!wav.isEmpty());
    const auto stored = store.importFile(wav);
    CHECK(stored.has_value());
    if (!stored)
        return;

    Project project = makeProject(300); // 10 s at 30 fps
    const Entity audio =
        makeAudioEntity(QStringLiteral("audio"), stored->hash, 0, 90); // 3 s
    project.entities.insert(audio.uuid, audio);

    const QString path = directory.path() + QStringLiteral("/trimmed.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));

    CHECK(result.ok);
    CHECK(result.framesWritten == 300);
    CHECK(QFileInfo::exists(path));

    const QVector<float> samples = decodeMono(path);
    CHECK(samples.size() > 9 * 48000);
    auto rms = [&samples](double from, double to) {
        const int first = std::max(0, int(from * 48000));
        const int last = std::min(int(samples.size()), int(to * 48000));
        if (last <= first)
            return 0.0;
        double sum = 0.0;
        for (int i = first; i < last; ++i)
            sum += double(samples[i]) * double(samples[i]);
        return std::sqrt(sum / double(last - first));
    };

    // Audible inside the clip.
    CHECK(rms(0.5, 2.5) > 0.02);
    // Silent after entity.end (a small AAC tail is tolerated).
    CHECK(rms(3.5, 9.5) < 0.002);
}

// Moved from Export2Tests when placeholder exports became fatal (#129): the
// providers must be called for real, present assets.
void testFrameProviders()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());

    {
        Project project = makeProject();
        const Entity html = makeAssetEntity(
            QStringLiteral("html"), AssetType::Html,
            writeTestAsset(directory.path(), QLatin1Char('a'), AssetType::Html),
            0, 10);
        project.entities.insert(html.uuid, html);

        const QString path = directory.path() + QStringLiteral("/html.mp4");
        export2::Options options = makeOptions(path);
        int calls = 0;
        double lastFrame = -1.0;
        options.htmlFrame = [&](const Uuid&, double projectFrame) {
            ++calls;
            lastFrame = projectFrame;
            return redImage();
        };
        const export2::Result result =
            export2::exportVideo(project, store, options);

        CHECK(result.ok);
        CHECK(QFileInfo::exists(path));
        CHECK(calls == 10);
        CHECK(std::abs(lastFrame - 9.0) < 1e-9);
    }

    {
        Project project = makeProject();
        const Entity video = makeAssetEntity(
            QStringLiteral("video"), AssetType::Video,
            writeTestAsset(directory.path(), QLatin1Char('b'), AssetType::Video),
            0, 10);
        project.entities.insert(video.uuid, video);

        const QString path = directory.path() + QStringLiteral("/video.mp4");
        export2::Options options = makeOptions(path);
        options.outputFps = 60;
        double lastFrame = -1.0;
        options.videoFrame = [&](const Uuid&, double projectFrame) {
            lastFrame = projectFrame;
            return redImage();
        };
        const export2::Result result =
            export2::exportVideo(project, store, options);

        CHECK(result.ok);
        CHECK(QFileInfo::exists(path));
        CHECK(result.framesWritten == 20);
        // 2x output fps samples fractional project frames.
        CHECK(std::abs(lastFrame - 9.5) < 1e-9);
    }
}

// Issue #129: a present asset whose provider returns no frame is a real export
// failure, never a silent "视频" placeholder card.
void testNoFrameProviderFails()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    Project project = makeProject();
    project.entities.insert(
        QStringLiteral("html"),
        makeAssetEntity(QStringLiteral("html"), AssetType::Html,
                        writeTestAsset(directory.path(), QLatin1Char('c'),
                                       AssetType::Html),
                        0, 10));
    project.entities.insert(
        QStringLiteral("video"),
        makeAssetEntity(QStringLiteral("video"), AssetType::Video,
                        writeTestAsset(directory.path(), QLatin1Char('d'),
                                       AssetType::Video),
                        0, 10));

    const QString path = directory.path() + QStringLiteral("/placeholder.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));

    CHECK(!result.ok);
    CHECK(!result.error.isEmpty());
    CHECK(result.error.contains(QStringLiteral("视频"))
          || result.error.contains(QStringLiteral("HTML")));
    CHECK(!QFileInfo::exists(path));
}

// Issue #129: a missing visual asset must fail the export instead of encoding
// the "素材丢失" card.
void testMissingVisualAssetFails()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    Project project = makeProject();
    project.entities.insert(
        QStringLiteral("video"),
        makeAssetEntity(QStringLiteral("video"), AssetType::Video,
                        AssetHash(64, QLatin1Char('e')), 0, 10));

    const QString path = directory.path() + QStringLiteral("/missing.mp4");
    export2::Options options = makeOptions(path);
    options.videoFrame = [](const Uuid&, double) { return redImage(); };
    const export2::Result result = export2::exportVideo(project, store, options);

    CHECK(!result.ok);
    CHECK(result.error.contains(QStringLiteral("视频"))
          && result.error.contains(QStringLiteral("video")));
    CHECK(!QFileInfo::exists(path));
}

// Issue #131: the encoded stream must be tagged BT.709 / TV range so players
// do not reinterpret the BT.601 sws default and shift the colours.  The red
// rectangle also has to survive the RGB->YUV conversion.
void testColorTagsAreBt709TvRange()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    assets::AssetStore store(directory.path());
    const Project project = makeProject();
    const QString path = directory.path() + QStringLiteral("/color.mp4");
    const export2::Result result =
        export2::exportVideo(project, store, makeOptions(path));
    CHECK(result.ok);
    if (!result.ok)
    {
        QTextStream(stderr) << result.error << '\n';
        return;
    }

    AVFormatContext* format = nullptr;
    CHECK(avformat_open_input(&format, path.toUtf8().constData(), nullptr,
                              nullptr)
          >= 0);
    if (!format)
        return;
    const auto closeFormat = qScopeGuard([&] { avformat_close_input(&format); });
    CHECK(avformat_find_stream_info(format, nullptr) >= 0);

    const AVCodecParameters* parameters = nullptr;
    int videoIndex = -1;
    for (unsigned i = 0; i < format->nb_streams; ++i)
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            parameters = format->streams[i]->codecpar;
            videoIndex = int(i);
        }
    CHECK(parameters != nullptr);
    if (!parameters)
        return;
    CHECK(parameters->color_space == AVCOL_SPC_BT709);
    CHECK(parameters->color_range == AVCOL_RANGE_MPEG);
    CHECK(parameters->color_primaries == AVCOL_PRI_BT709);
    CHECK(parameters->color_trc == AVCOL_TRC_BT709);

    // Decode the first frame and check the red rectangle stays red (yuv420p
    // chroma subsampling only softens it, it must not become orange/green).
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    CHECK(codec != nullptr);
    if (!codec)
        return;
    AVCodecContext* context = avcodec_alloc_context3(codec);
    CHECK(context != nullptr);
    if (!context)
        return;
    const auto freeContext = qScopeGuard([&] { avcodec_free_context(&context); });
    CHECK(avcodec_parameters_to_context(context, parameters) >= 0);
    CHECK(avcodec_open2(context, codec, nullptr) >= 0);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    CHECK(packet && frame);
    if (!packet || !frame)
    {
        av_packet_free(&packet);
        av_frame_free(&frame);
        return;
    }
    bool decoded = false;
    while (!decoded && av_read_frame(format, packet) >= 0)
    {
        if (packet->stream_index != videoIndex)
        {
            av_packet_unref(packet);
            continue;
        }
        if (avcodec_send_packet(context, packet) >= 0
            && avcodec_receive_frame(context, frame) == 0)
            decoded = true;
        av_packet_unref(packet);
    }
    CHECK(decoded);
    if (decoded)
    {
        // Use sws to get RGB and sample the frame centre (the rectangle).
        SwsContext* sws = sws_getContext(
            frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), frame->width,
            frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
            nullptr);
        CHECK(sws != nullptr);
        if (sws)
        {
            QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
            uint8_t* destination[4] = {image.bits(), nullptr, nullptr, nullptr};
            int linesize[4] = {int(image.bytesPerLine()), 0, 0, 0};
            CHECK(sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                            destination, linesize)
                  > 0);
            sws_freeContext(sws);
            const QColor centre = image.pixelColor(image.width() / 2,
                                                   image.height() / 2);
            CHECK(centre.red() > 180);
            CHECK(centre.green() < 90 && centre.blue() < 90);
        }
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    const QVector<QPair<QString, void (*)()>> tests = {
        {QStringLiteral("audioStopsAtEntityEnd"), testAudioStopsAtEntityEnd},
        {QStringLiteral("frameProviders"), testFrameProviders},
        {QStringLiteral("noFrameProviderFails"), testNoFrameProviderFails},
        {QStringLiteral("missingVisualAssetFails"), testMissingVisualAssetFails},
        {QStringLiteral("colorTagsAreBt709TvRange"),
         testColorTagsAreBt709TvRange},
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

    QTextStream(stdout) << QStringLiteral("media-export: %1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
