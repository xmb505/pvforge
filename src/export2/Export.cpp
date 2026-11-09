#include "Export.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QScopeGuard>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace pvf::export2 {
namespace {

constexpr int kMaxDimension = 8192;

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
// QImage ARGB32_Premultiplied stores B, G, R, A in memory on little endian.
constexpr AVPixelFormat kSourcePixelFormat = AV_PIX_FMT_BGRA;
#else
constexpr AVPixelFormat kSourcePixelFormat = AV_PIX_FMT_ARGB;
#endif

QString ffmpegError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

QSize evenSize(const QSize& requested)
{
    // H.264 yuv420p requires even dimensions.  Rounding down keeps the
    // requested frame inside the requested box; the minimum is 2 because a
    // 1-pixel 4:2:0 frame does not exist.
    return QSize(std::max(2, requested.width() & ~1),
                 std::max(2, requested.height() & ~1));
}

QSize contentSizeFor(const QSize& output, const QSize& project)
{
    const double scale =
        std::min(double(output.width()) / std::max(1, project.width()),
                 double(output.height()) / std::max(1, project.height()));
    const int width = std::clamp(qRound(project.width() * scale), 1,
                                 output.width());
    const int height = std::clamp(qRound(project.height() * scale), 1,
                                  output.height());
    return QSize(width, height);
}

constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;

QString audioContentFile(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

double audioGainAt(const model::Entity& entity, int projectFrame)
{
    double gain = entity.content.asset.volume;
    const auto track = entity.tracks.constFind(QStringLiteral("volume"));
    if (track != entity.tracks.constEnd())
    {
        const int relativeFrame = projectFrame - entity.start;
        gain = model::evaluateTrack(track.value(), relativeFrame)
                   .value_or(gain);
    }
    if (!std::isfinite(gain) || gain < 0.0)
        return 0.0;
    return std::min(gain, 4.0);
}

// Decodes one audio entity and adds its overlapping part into `mix`, an
// interleaved 48 kHz stereo float buffer whose sample 0 is the first exported
// frame.  Frames before entity.start or outside the export range are ignored;
// frame n of the entity maps to asset time
// (sourceIn + n - entity.start) / fps seconds.
bool mixAudioEntity(const assets::AssetStore& assets,
                    const model::Entity& entity, int exportStart,
                    int totalFrames, int fps, std::vector<float>& mix,
                    QString* error)
{
    const int overlapStart = std::max(entity.start, exportStart);
    const int overlapEnd = std::min(entity.end, exportStart + totalFrames);
    if (overlapEnd <= overlapStart)
        return true;

    const model::AssetContent& content = entity.content.asset;
    const auto stored = assets.get(content.hash);
    if (!stored)
    {
        *error = QStringLiteral("素材缺失");
        return false;
    }

    const QString path = audioContentFile(*stored);
    if (path.isEmpty())
    {
        *error = QStringLiteral("素材内容文件缺失");
        return false;
    }

    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.toUtf8().constData(), nullptr,
                            nullptr)
        < 0)
    {
        *error = QStringLiteral("无法打开音频文件");
        return false;
    }
    const auto formatGuard = qScopeGuard([&] {
        if (format)
            avformat_close_input(&format);
    });

    if (avformat_find_stream_info(format, nullptr) < 0)
    {
        *error = QStringLiteral("无法读取音频信息");
        return false;
    }

    const int audioIndex = av_find_best_stream(
        format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIndex < 0)
    {
        *error = QStringLiteral("没有音频流");
        return false;
    }

    const AVCodecParameters* parameters = format->streams[audioIndex]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
    if (!decoder)
    {
        *error = QStringLiteral("不支持的音频编码");
        return false;
    }

    AVCodecContext* decoderContext = avcodec_alloc_context3(decoder);
    if (!decoderContext)
    {
        *error = QStringLiteral("无法创建音频解码器");
        return false;
    }
    const auto decoderGuard =
        qScopeGuard([&] { avcodec_free_context(&decoderContext); });

    int status = avcodec_parameters_to_context(decoderContext, parameters);
    if (status < 0)
    {
        *error = QStringLiteral("无法复制音频参数：%1").arg(ffmpegError(status));
        return false;
    }
    status = avcodec_open2(decoderContext, decoder, nullptr);
    if (status < 0)
    {
        *error = QStringLiteral("无法打开音频解码器：%1")
                     .arg(ffmpegError(status));
        return false;
    }
    if (decoderContext->sample_rate <= 0
        || decoderContext->ch_layout.nb_channels <= 0)
    {
        *error = QStringLiteral("音频格式无效");
        return false;
    }

    AVChannelLayout outputLayout = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext* swr = nullptr;
    status = swr_alloc_set_opts2(
        &swr, &outputLayout, AV_SAMPLE_FMT_FLT, kAudioSampleRate,
        &decoderContext->ch_layout, decoderContext->sample_fmt,
        decoderContext->sample_rate, 0, nullptr);
    if (status < 0 || !swr)
    {
        *error = QStringLiteral("无法创建音频重采样器：%1")
                     .arg(ffmpegError(status));
        return false;
    }
    const auto swrGuard = qScopeGuard([&] { swr_free(&swr); });

    status = swr_init(swr);
    if (status < 0)
    {
        *error = QStringLiteral("无法初始化音频重采样器：%1")
                     .arg(ffmpegError(status));
        return false;
    }

    const double samplesPerFrame = double(kAudioSampleRate) / double(fps);
    const int64_t mixFrames = int64_t(mix.size() / kAudioChannels);
    const int64_t destinationStart =
        std::max<int64_t>(0, llround((overlapStart - exportStart)
                                     * samplesPerFrame));
    const int64_t sourceFrame = content.sourceIn + (overlapStart - entity.start);
    const int64_t sourceStart =
        std::max<int64_t>(0, llround(sourceFrame * samplesPerFrame));

    int64_t decodedSamples = 0;
    bool enough = false;
    auto mixConverted = [&](const float* data, int count) {
        const int64_t firstOutput =
            destinationStart + decodedSamples - sourceStart;
        const int64_t firstFrameSample =
            std::clamp<int64_t>(firstOutput, 0, std::max<int64_t>(0, mixFrames - 1));
        const int projectFrame =
            exportStart + int(firstFrameSample * fps / kAudioSampleRate);
        const double gain = audioGainAt(entity, projectFrame);

        for (int i = 0; i < count; ++i)
        {
            const int64_t output =
                destinationStart + decodedSamples + i - sourceStart;
            if (output < 0 || output >= mixFrames)
                continue;
            const size_t index = size_t(output) * kAudioChannels;
            mix[index] += data[i * kAudioChannels] * float(gain);
            mix[index + 1] += data[i * kAudioChannels + 1] * float(gain);
        }

        decodedSamples += count;
        if (decodedSamples >= sourceStart
            && destinationStart + (decodedSamples - sourceStart) >= mixFrames)
            enough = true;
    };

    AVFrame* frame = av_frame_alloc();
    if (!frame)
    {
        *error = QStringLiteral("无法创建音频帧");
        return false;
    }
    const auto frameGuard = qScopeGuard([&] { av_frame_free(&frame); });

    AVPacket* packet = av_packet_alloc();
    if (!packet)
    {
        *error = QStringLiteral("无法创建音频包");
        return false;
    }
    const auto packetGuard = qScopeGuard([&] { av_packet_free(&packet); });

    std::vector<float> converted;
    auto convertFrame = [&](const AVFrame* decoded) -> bool {
        const int capacity = int(av_rescale_rnd(
            swr_get_delay(swr, kAudioSampleRate) + decoded->nb_samples,
            kAudioSampleRate, decoded->sample_rate, AV_ROUND_UP));
        if (capacity <= 0)
            return true;

        converted.resize(size_t(capacity) * kAudioChannels);
        uint8_t* outputData[1] = {
            reinterpret_cast<uint8_t*>(converted.data())};
        const int received = swr_convert(
            swr, outputData, capacity,
            reinterpret_cast<const uint8_t* const*>(decoded->extended_data),
            decoded->nb_samples);
        if (received < 0)
        {
            *error = QStringLiteral("音频重采样失败：%1")
                         .arg(ffmpegError(received));
            return false;
        }
        if (received > 0)
            mixConverted(converted.data(), received);
        return true;
    };

    auto receiveDecoded = [&]() -> bool {
        while (true)
        {
            const int receiveStatus =
                avcodec_receive_frame(decoderContext, frame);
            if (receiveStatus == AVERROR(EAGAIN)
                || receiveStatus == AVERROR_EOF)
                return true;
            if (receiveStatus < 0)
            {
                *error = QStringLiteral("音频解码失败：%1")
                             .arg(ffmpegError(receiveStatus));
                return false;
            }
            const bool ok = convertFrame(frame);
            av_frame_unref(frame);
            if (!ok || enough)
                return ok;
        }
    };

    while (!enough && av_read_frame(format, packet) >= 0)
    {
        if (packet->stream_index == audioIndex)
        {
            const int sendStatus = avcodec_send_packet(decoderContext, packet);
            av_packet_unref(packet);
            if (sendStatus < 0)
            {
                *error = QStringLiteral("音频解码失败：%1")
                             .arg(ffmpegError(sendStatus));
                return false;
            }
            if (!receiveDecoded())
                return false;
        }
        else
        {
            av_packet_unref(packet);
        }
    }

    if (!enough)
    {
        avcodec_send_packet(decoderContext, nullptr);
        if (!receiveDecoded())
            return false;

        while (true)
        {
            const int64_t delay = swr_get_delay(swr, kAudioSampleRate);
            if (delay <= 0)
                break;
            const int capacity = int(delay);
            converted.resize(size_t(capacity) * kAudioChannels);
            uint8_t* outputData[1] = {
                reinterpret_cast<uint8_t*>(converted.data())};
            const int received =
                swr_convert(swr, outputData, capacity, nullptr, 0);
            if (received < 0)
            {
                *error = QStringLiteral("音频重采样失败：%1")
                             .arg(ffmpegError(received));
                return false;
            }
            if (received == 0)
                break;
            mixConverted(converted.data(), received);
        }
    }

    return true;
}

struct AudioMix
{
    std::vector<float> samples; // interleaved stereo at 48 kHz
    bool hasAudio = false;
    QStringList warnings;
};

AudioMix mixProjectAudio(const model::Project& project,
                         const assets::AssetStore& assets, int exportStart,
                         int totalFrames, int fps)
{
    AudioMix mix;
    const int64_t totalSamples = std::max<int64_t>(
        0, llround(double(totalFrames) * kAudioSampleRate / double(fps)));
    mix.samples.assign(size_t(totalSamples) * kAudioChannels, 0.0f);

    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.content.kind != ContentKind::Asset
            || entity.content.asset.type != AssetType::Audio)
            continue;
        if (entity.end <= entity.start)
            continue;

        const int overlapStart = std::max(entity.start, exportStart);
        const int overlapEnd = std::min(entity.end, exportStart + totalFrames);
        if (overlapEnd <= overlapStart)
            continue;

        QString error;
        if (mixAudioEntity(assets, entity, exportStart, totalFrames, fps,
                           mix.samples, &error))
        {
            mix.hasAudio = true;
        }
        else
        {
            const QString name =
                entity.name.isEmpty() ? entity.uuid : entity.name;
            mix.warnings.append(
                QStringLiteral("音频实体 \"%1\" 已跳过：%2").arg(name, error));
        }
    }

    for (float& sample : mix.samples)
        sample = std::clamp(sample, -1.0f, 1.0f);

    return mix;
}

} // namespace

Result exportVideo(const model::Project& project,
                   const assets::AssetStore& assets, const Options& options,
                   Progress progress)
{
    Result result;
    auto fail = [&result](const QString& error) {
        result.ok = false;
        result.error = error;
        return result;
    };

    if (options.outputPath.trimmed().isEmpty())
        return fail(QStringLiteral("输出路径为空"));

    const int projectFps = project.settings.fps;
    if (projectFps <= 0)
        return fail(QStringLiteral("工程帧率无效"));

    // Output fps: project fps by default.  Only integer 1x/2x/4x multiples are
    // supported so every output frame maps onto a stable time grid.
    const int requestedFps =
        options.outputFps > 0 ? options.outputFps : options.fps;
    const int outputFps = requestedFps > 0 ? requestedFps : projectFps;
    const int fpsScale = outputFps % projectFps == 0
                             ? outputFps / projectFps
                             : 0;
    if (fpsScale != 1 && fpsScale != 2 && fpsScale != 4)
        return fail(QStringLiteral("输出帧率必须是工程帧率的 1x/2x/4x"));

    if (options.resolutionScale != 1 && options.resolutionScale != 2
        && options.resolutionScale != 4)
        return fail(QStringLiteral("分辨率倍率必须是 1x/2x/4x"));

    if (options.cameraUuid.isEmpty())
        return fail(QStringLiteral("没有选择摄像机"));
    const auto cameraIt = project.entities.constFind(options.cameraUuid);
    if (cameraIt == project.entities.constEnd())
        return fail(QStringLiteral("摄像机实体不存在"));
    const model::Entity& camera = cameraIt.value();
    if (camera.content.kind != ContentKind::Camera)
        return fail(QStringLiteral("指定实体不是摄像机"));
    if (camera.end <= camera.start)
        return fail(QStringLiteral("摄像机时间范围无效"));

    int start = options.startFrame;
    int end = options.endFrame;
    if (end <= start)
    {
        start = camera.start;
        end = camera.end;
    }
    if (start < camera.start || end > camera.end || end <= start)
        return fail(QStringLiteral("导出范围必须位于摄像机范围内"));

    const int crf = std::clamp(options.crf, 0, 51);
    const int projectFrames = end - start;
    // The export keeps the camera range's duration in seconds, not its project
    // frame count: 10 project frames at 30 fps become 20 output frames at 60.
    const int total = std::max(
        1, int(std::llround(double(projectFrames) * double(outputFps)
                            / double(projectFps))));
    const double projectFrameStep = double(projectFps) / double(outputFps);

    const QSize projectSize(std::max(1, project.settings.width),
                            std::max(1, project.settings.height));
    QSize requested = options.outputSize;
    if (requested.isEmpty())
        requested = QSize(projectSize.width() * options.resolutionScale,
                          projectSize.height() * options.resolutionScale);
    if (requested.width() < 1 || requested.height() < 1)
        return fail(QStringLiteral("输出尺寸无效"));
    if (requested.width() > kMaxDimension || requested.height() > kMaxDimension)
        return fail(QStringLiteral("输出尺寸超过 8192 像素"));

    const QSize outputSize = evenSize(requested);
    const QSize contentSize = contentSizeFor(outputSize, projectSize);

    QColor background(project.settings.background);
    if (!background.isValid())
        background = Qt::black;

    AudioMix audioMix;
    if (options.includeAudio)
        audioMix =
            mixProjectAudio(project, assets, start, projectFrames, projectFps);
    result.warnings = audioMix.warnings;

    const QFileInfo outputInfo(options.outputPath);
    if (!outputInfo.absoluteDir().exists())
        return fail(QStringLiteral("输出目录不存在"));
    const QString outputPath = outputInfo.absoluteFilePath();
    const QString partPath = outputPath + QStringLiteral(".part.mp4");
    QFile::remove(partPath);

    bool committed = false;
    const auto tempGuard = qScopeGuard([&] {
        if (!committed)
            QFile::remove(partPath);
    });

    AVFormatContext* format = nullptr;
    int status = avformat_alloc_output_context2(
        &format, nullptr, "mp4", partPath.toUtf8().constData());
    if (status < 0 || !format)
        return fail(QStringLiteral("无法创建 MP4 输出：%1")
                        .arg(ffmpegError(status)));
    const auto formatGuard = qScopeGuard([&] {
        if (!format)
            return;
        if (format->pb)
            avio_closep(&format->pb);
        avformat_free_context(format);
        format = nullptr;
    });

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        return fail(QStringLiteral("找不到 H.264 编码器"));

    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
        return fail(QStringLiteral("无法创建编码器"));
    const auto codecGuard =
        qScopeGuard([&] { avcodec_free_context(&codecContext); });

    codecContext->width = outputSize.width();
    codecContext->height = outputSize.height();
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext->time_base = AVRational{1, outputFps};
    codecContext->framerate = AVRational{outputFps, 1};
    codecContext->gop_size = std::max(1, outputFps * 2);
    codecContext->max_b_frames = 0;
    if (format->oformat->flags & AVFMT_GLOBALHEADER)
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (QByteArray(codec->name) == QByteArrayLiteral("libx264"))
    {
        av_opt_set(codecContext->priv_data, "preset", "medium", 0);
        av_opt_set_int(codecContext->priv_data, "crf", crf, 0);
    }
    else
    {
        codecContext->bit_rate = std::max<qint64>(
            1, qint64(outputSize.width()) * outputSize.height() * outputFps
                   / 8);
    }

    status = avcodec_open2(codecContext, codec, nullptr);
    if (status < 0)
        return fail(QStringLiteral("无法打开 H.264 编码器：%1")
                        .arg(ffmpegError(status)));

    AVStream* stream = avformat_new_stream(format, nullptr);
    if (!stream)
        return fail(QStringLiteral("无法创建视频流"));
    status = avcodec_parameters_from_context(stream->codecpar, codecContext);
    if (status < 0)
        return fail(QStringLiteral("无法复制编码参数：%1")
                        .arg(ffmpegError(status)));
    stream->time_base = codecContext->time_base;

    AVCodecContext* audioContext = nullptr;
    AVStream* audioStream = nullptr;
    AVFrame* audioFrame = nullptr;
    SwrContext* audioSwr = nullptr;
    int audioFrameSize = 1024;
    bool audioEnabled = false;

    const auto audioCodecGuard =
        qScopeGuard([&] { avcodec_free_context(&audioContext); });
    const auto audioSwrGuard = qScopeGuard([&] { swr_free(&audioSwr); });
    const auto audioFrameGuard =
        qScopeGuard([&] { av_frame_free(&audioFrame); });

    auto audioWarning = [&](const QString& message) {
        result.warnings.append(QStringLiteral("音频：%1").arg(message));
    };

    auto setupAudio = [&]() -> bool {
        const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audioCodec)
        {
            audioWarning(QStringLiteral("找不到 AAC 编码器，导出无声视频"));
            return false;
        }

        audioContext = avcodec_alloc_context3(audioCodec);
        if (!audioContext)
        {
            audioWarning(QStringLiteral("无法创建 AAC 编码器，导出无声视频"));
            return false;
        }

        audioContext->sample_rate = kAudioSampleRate;
        av_channel_layout_default(&audioContext->ch_layout, kAudioChannels);
        audioContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audioContext->time_base = AVRational{1, kAudioSampleRate};
        audioContext->bit_rate = 192000;
        if (format->oformat->flags & AVFMT_GLOBALHEADER)
            audioContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        status = avcodec_open2(audioContext, audioCodec, nullptr);
        if (status < 0)
        {
            audioWarning(QStringLiteral(
                             "无法打开 AAC 编码器，导出无声视频：%1")
                             .arg(ffmpegError(status)));
            return false;
        }

        AVChannelLayout inputLayout = AV_CHANNEL_LAYOUT_STEREO;
        status = swr_alloc_set_opts2(
            &audioSwr, &audioContext->ch_layout, audioContext->sample_fmt,
            audioContext->sample_rate, &inputLayout, AV_SAMPLE_FMT_FLT,
            kAudioSampleRate, 0, nullptr);
        if (status < 0 || !audioSwr)
        {
            audioWarning(QStringLiteral("无法创建 AAC 重采样器，导出无声视频：%1")
                             .arg(ffmpegError(status)));
            return false;
        }
        status = swr_init(audioSwr);
        if (status < 0)
        {
            audioWarning(QStringLiteral("无法初始化 AAC 重采样器，导出无声视频：%1")
                             .arg(ffmpegError(status)));
            return false;
        }

        audioFrameSize =
            audioContext->frame_size > 0 ? audioContext->frame_size : 1024;
        audioFrame = av_frame_alloc();
        if (!audioFrame)
        {
            audioWarning(QStringLiteral("无法创建 AAC 音频帧，导出无声视频"));
            return false;
        }
        audioFrame->format = audioContext->sample_fmt;
        audioFrame->sample_rate = audioContext->sample_rate;
        audioFrame->nb_samples = audioFrameSize;
        if (av_channel_layout_copy(&audioFrame->ch_layout,
                                   &audioContext->ch_layout)
            < 0)
        {
            audioWarning(QStringLiteral("无法配置 AAC 声道，导出无声视频"));
            return false;
        }
        status = av_frame_get_buffer(audioFrame, 0);
        if (status < 0)
        {
            audioWarning(QStringLiteral("无法分配 AAC 音频帧，导出无声视频：%1")
                             .arg(ffmpegError(status)));
            return false;
        }

        audioStream = avformat_new_stream(format, nullptr);
        if (!audioStream)
        {
            audioWarning(QStringLiteral("无法创建音频流，导出无声视频"));
            return false;
        }
        status =
            avcodec_parameters_from_context(audioStream->codecpar, audioContext);
        if (status < 0)
        {
            audioStream = nullptr;
            audioWarning(QStringLiteral("无法复制 AAC 参数，导出无声视频：%1")
                             .arg(ffmpegError(status)));
            return false;
        }
        audioStream->time_base = audioContext->time_base;
        return true;
    };

    if (audioMix.hasAudio && !audioMix.samples.empty())
        audioEnabled = setupAudio();

    status =
        avio_open(&format->pb, partPath.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (status < 0)
        return fail(QStringLiteral("无法写入输出文件：%1")
                        .arg(ffmpegError(status)));

    AVDictionary* headerOptions = nullptr;
    av_dict_set(&headerOptions, "movflags", "+faststart", 0);
    status = avformat_write_header(format, &headerOptions);
    av_dict_free(&headerOptions);
    if (status < 0)
        return fail(QStringLiteral("无法写入 MP4 头部：%1")
                        .arg(ffmpegError(status)));

    SwsContext* sws = sws_getContext(
        outputSize.width(), outputSize.height(), kSourcePixelFormat,
        outputSize.width(), outputSize.height(), AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws)
        return fail(QStringLiteral("无法创建色彩转换上下文"));
    const auto swsGuard = qScopeGuard([&] { sws_freeContext(sws); });

    AVFrame* frame = av_frame_alloc();
    if (!frame)
        return fail(QStringLiteral("无法创建视频帧"));
    const auto frameGuard = qScopeGuard([&] { av_frame_free(&frame); });
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = outputSize.width();
    frame->height = outputSize.height();
    status = av_frame_get_buffer(frame, 0);
    if (status < 0)
        return fail(QStringLiteral("无法分配视频帧：%1")
                        .arg(ffmpegError(status)));

    AVPacket* packet = av_packet_alloc();
    if (!packet)
        return fail(QStringLiteral("无法创建编码包"));
    const auto packetGuard = qScopeGuard([&] { av_packet_free(&packet); });

    auto drainPackets = [&]() -> int {
        while (true)
        {
            const int code = avcodec_receive_packet(codecContext, packet);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF)
                return 0;
            if (code < 0)
                return code;

            av_packet_rescale_ts(packet, codecContext->time_base,
                                 stream->time_base);
            packet->stream_index = stream->index;
            const int writeStatus =
                av_interleaved_write_frame(format, packet);
            av_packet_unref(packet);
            if (writeStatus < 0)
                return writeStatus;
        }
    };

    auto drainAudioPackets = [&]() -> int {
        if (!audioEnabled)
            return 0;
        while (true)
        {
            const int code = avcodec_receive_packet(audioContext, packet);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF)
                return 0;
            if (code < 0)
                return code;

            av_packet_rescale_ts(packet, audioContext->time_base,
                                 audioStream->time_base);
            packet->stream_index = audioStream->index;
            const int writeStatus =
                av_interleaved_write_frame(format, packet);
            av_packet_unref(packet);
            if (writeStatus < 0)
                return writeStatus;
        }
    };

    std::vector<float> audioChunk;
    int64_t audioCursor = 0;
    auto encodeAudioUntil = [&](int64_t targetSamples) -> int {
        if (!audioEnabled)
            return 0;
        audioChunk.resize(size_t(audioFrameSize) * kAudioChannels);
        const int64_t mixFrames =
            int64_t(audioMix.samples.size() / kAudioChannels);
        while (audioCursor < targetSamples)
        {
            std::fill(audioChunk.begin(), audioChunk.end(), 0.0f);
            const int64_t available =
                std::max<int64_t>(0, mixFrames - audioCursor);
            const int copyCount =
                int(std::min<int64_t>(audioFrameSize, available));
            if (copyCount > 0)
            {
                std::copy_n(
                    audioMix.samples.data()
                        + size_t(audioCursor) * kAudioChannels,
                    size_t(copyCount) * kAudioChannels, audioChunk.data());
            }

            status = av_frame_make_writable(audioFrame);
            if (status < 0)
                return status;
            const uint8_t* inputData[1] = {
                reinterpret_cast<const uint8_t*>(audioChunk.data())};
            const int received =
                swr_convert(audioSwr, audioFrame->data, audioFrameSize,
                            inputData, audioFrameSize);
            if (received < 0)
                return received;
            if (received == 0)
                return AVERROR(EIO);

            audioFrame->nb_samples = received;
            audioFrame->pts = audioCursor;
            status = avcodec_send_frame(audioContext, audioFrame);
            if (status < 0)
                return status;
            status = drainAudioPackets();
            if (status < 0)
                return status;
            audioCursor += received;
        }
        return 0;
    };

    render2::SceneWorld world(project, assets);
    anim::Cache cache;
    QImage canvas(outputSize, QImage::Format_ARGB32_Premultiplied);
    render2::Options renderOptions;
    renderOptions.cameraUuid = options.cameraUuid;
    renderOptions.outputSize = contentSize;
    renderOptions.background = background;
    renderOptions.videoFrame = options.videoFrame;
    renderOptions.htmlFrame = options.htmlFrame;

    const QPoint contentOffset((outputSize.width() - contentSize.width()) / 2,
                               (outputSize.height() - contentSize.height()) / 2);

    int framesWritten = 0;
    for (int frameIndex = 0; frameIndex < total; ++frameIndex)
    {
        // Time sampling: output frame -> project frame (fractional at 2x/4x).
        renderOptions.projectFrame = start + frameIndex * projectFrameStep;
        QImage rendered = render2::renderFrame(project, assets, renderOptions,
                                               world, cache);
        if (rendered.isNull())
            return fail(QStringLiteral("渲染第 %1 帧失败")
                            .arg(renderOptions.projectFrame));
        if (rendered.format() != QImage::Format_ARGB32_Premultiplied)
            rendered = rendered.convertToFormat(
                QImage::Format_ARGB32_Premultiplied);

        QImage image;
        if (rendered.size() == outputSize)
        {
            image = rendered;
        }
        else
        {
            canvas.fill(background);
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(contentOffset, rendered);
            painter.end();
            image = canvas;
        }

        status = av_frame_make_writable(frame);
        if (status < 0)
            return fail(QStringLiteral("无法写入视频帧：%1")
                            .arg(ffmpegError(status)));

        const uint8_t* sourceData[4] = {image.constBits(), nullptr, nullptr,
                                        nullptr};
        const int sourceStride[4] = {int(image.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, sourceData, sourceStride, 0, outputSize.height(),
                  frame->data, frame->linesize);

        frame->pts = frameIndex;
        status = avcodec_send_frame(codecContext, frame);
        if (status < 0)
            return fail(QStringLiteral("编码第 %1 帧失败：%2")
                            .arg(renderOptions.projectFrame)
                            .arg(ffmpegError(status)));

        status = drainPackets();
        if (status < 0)
            return fail(QStringLiteral("写入第 %1 帧失败：%2")
                            .arg(renderOptions.projectFrame)
                            .arg(ffmpegError(status)));

        if (audioEnabled)
        {
            const int64_t target = llround(
                double(frameIndex + 1) * kAudioSampleRate / outputFps);
            status = encodeAudioUntil(target);
            if (status < 0)
            {
                result.warnings.append(
                    QStringLiteral("音频编码在第 %1 帧失败，后续无声：%2")
                        .arg(renderOptions.projectFrame)
                        .arg(ffmpegError(status)));
                audioEnabled = false;
            }
        }

        ++framesWritten;
        if (progress && !progress(framesWritten, total))
            return fail(QStringLiteral("cancelled"));
    }

    if (audioEnabled)
    {
        const int64_t totalSamples =
            int64_t(audioMix.samples.size() / kAudioChannels);
        status = encodeAudioUntil(totalSamples);
        if (status >= 0)
        {
            status = avcodec_send_frame(audioContext, nullptr);
            if (status >= 0)
                status = drainAudioPackets();
        }
        if (status < 0)
        {
            result.warnings.append(QStringLiteral("音频编码收尾失败：%1")
                                       .arg(ffmpegError(status)));
            audioEnabled = false;
        }
    }

    status = avcodec_send_frame(codecContext, nullptr);
    if (status < 0)
        return fail(QStringLiteral("刷新编码器失败：%1")
                        .arg(ffmpegError(status)));
    status = drainPackets();
    if (status < 0)
        return fail(
            QStringLiteral("写入剩余帧失败：%1").arg(ffmpegError(status)));

    status = av_write_trailer(format);
    if (status < 0)
        return fail(QStringLiteral("无法结束 MP4 文件：%1")
                        .arg(ffmpegError(status)));
    if (format->pb)
        avio_closep(&format->pb);

    if (QFileInfo(partPath).size() <= 0)
        return fail(QStringLiteral("导出的视频为空"));
    QFile::remove(outputPath);
    if (!QFile::rename(partPath, outputPath))
        return fail(QStringLiteral("无法写入输出文件"));

    committed = true;
    result.ok = true;
    result.error.clear();
    result.framesWritten = framesWritten;
    return result;
}

} // namespace pvf::export2
