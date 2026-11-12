#include "Audio2.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QDir>
#include <QScopeGuard>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace pvf::audio2 {
namespace {

QString ffmpegError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

QString audioContentFile(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

// Mirrors the Anim visibility rule: hidden always hides, the visible track is
// step-evaluated, and a parent hiding its child is controlled by
// inheritVisibility.
bool entityVisibleAt(const model::Project& project, const Uuid& uuid,
                     int projectFrame, QSet<Uuid>& visiting)
{
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return true;
    if (visiting.contains(uuid))
        return true;

    visiting.insert(uuid);
    const model::Entity& entity = it.value();

    bool visible = !entity.hidden;
    if (visible)
    {
        double value = entity.visible ? 1.0 : 0.0;
        const auto track = entity.tracks.constFind(QStringLiteral("visible"));
        if (track != entity.tracks.constEnd())
        {
            value = model::evaluateTrack(track.value(),
                                         projectFrame - entity.start, true)
                        .value_or(value);
        }
        visible = value != 0.0;
    }

    if (visible && !entity.parentUuid.isEmpty() && entity.inheritVisibility)
        visible = entityVisibleAt(project, entity.parentUuid, projectFrame,
                                  visiting);

    visiting.remove(uuid);
    return visible;
}

bool entityVisibleAt(const model::Project& project, const model::Entity& entity,
                     int projectFrame)
{
    QSet<Uuid> visiting;
    return entityVisibleAt(project, entity.uuid, projectFrame, visiting);
}

double audioGainAt(const model::Entity& entity, int projectFrame)
{
    double gain = entity.content.asset.volume;
    const auto track = entity.tracks.constFind(QStringLiteral("volume"));
    if (track != entity.tracks.constEnd())
    {
        gain = model::evaluateTrack(track.value(), projectFrame - entity.start)
                   .value_or(gain);
    }
    if (!std::isfinite(gain) || gain < 0.0)
        return 0.0;
    return gain;
}

int projectFrameForSample(const MixOptions& options, int64_t sampleIndex)
{
    return options.startFrame
           + int(std::floor(double(sampleIndex) * double(options.fps)
                            / double(options.sampleRate)));
}

// Decodes one audio entity and adds its overlapping part into `mix`, an
// interleaved float buffer whose sample 0 is options.startFrame. Frames before
// entity.start or outside the requested range are ignored; entity frame n maps
// to asset time (sourceIn + n - entity.start) / fps seconds.
bool mixAudioEntity(const model::Project& project,
                    const assets::AssetStore& assets,
                    const model::Entity& entity, const MixOptions& options,
                    int64_t totalSamples, float* mix, bool* audible,
                    QString* error)
{
    const model::AssetContent& content = entity.content.asset;
    const int64_t overlapStart =
        std::max<int64_t>(entity.start, options.startFrame);
    const int64_t overlapEnd =
        std::min<int64_t>(entity.end, options.endFrame);
    if (overlapEnd <= overlapStart)
        return true;

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

    const QByteArray pathUtf8 = path.toUtf8();
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, pathUtf8.constData(), nullptr, nullptr)
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

    AVChannelLayout outputLayout;
    av_channel_layout_default(&outputLayout, options.channels);
    const auto layoutGuard =
        qScopeGuard([&] { av_channel_layout_uninit(&outputLayout); });

    SwrContext* swr = nullptr;
    status = swr_alloc_set_opts2(
        &swr, &outputLayout, AV_SAMPLE_FMT_FLT, options.sampleRate,
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

    const double samplesPerFrame =
        double(options.sampleRate) / double(options.fps);
    const int64_t destinationStart = std::max<int64_t>(
        0, llround((overlapStart - options.startFrame) * samplesPerFrame));
    const int64_t destinationEnd = std::min<int64_t>(
        totalSamples,
        llround((overlapEnd - options.startFrame) * samplesPerFrame));
    if (destinationEnd <= destinationStart)
        return true;

    const int64_t sourceFrame = std::max<int64_t>(
        0, content.sourceIn + (overlapStart - entity.start));
    const int64_t sourceStart = llround(sourceFrame * samplesPerFrame);

    int64_t decodedSamples = 0;
    bool enough = false;
    auto mixConverted = [&](const float* data, int count) {
        const int64_t blockStart =
            destinationStart + decodedSamples - sourceStart;
        const int64_t firstOutput =
            std::max<int64_t>(blockStart, destinationStart);
        if (firstOutput < destinationEnd)
        {
            const int projectFrame =
                projectFrameForSample(options, firstOutput);
            const double gain =
                entityVisibleAt(project, entity, projectFrame)
                    ? audioGainAt(entity, projectFrame)
                    : 0.0;

            if (gain > 0.0)
            {
                const int channels = options.channels;
                for (int i = 0; i < count; ++i)
                {
                    const int64_t output = blockStart + i;
                    if (output < destinationStart || output >= destinationEnd)
                        continue;
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        const size_t index =
                            size_t(output) * size_t(channels)
                            + size_t(channel);
                        const float value =
                            data[size_t(i) * size_t(channels)
                                 + size_t(channel)]
                            * float(gain);
                        mix[index] += value;
                        if (value != 0.0f)
                            *audible = true;
                    }
                }
            }
        }

        decodedSamples += count;
        if (blockStart >= destinationEnd)
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

    const int channels = options.channels;
    QByteArray converted;
    auto convertFrame = [&](const AVFrame* decoded) -> bool {
        const int capacity = int(av_rescale_rnd(
            swr_get_delay(swr, options.sampleRate) + decoded->nb_samples,
            options.sampleRate, decoded->sample_rate, AV_ROUND_UP));
        if (capacity <= 0)
            return true;

        converted.resize(int(size_t(capacity) * size_t(channels)
                             * sizeof(float)));
        uint8_t* outputData[1] = {
            reinterpret_cast<uint8_t*>(converted.data())};
        // FFmpeg 6.x expects `const uint8_t**`; FFmpeg 8's extended_data is
        // `uint8_t* const*`, so copy the channel pointers into a const array.
        const int inputChannels =
            std::max(1, decoderContext->ch_layout.nb_channels);
        std::vector<const uint8_t*> inputData(
            static_cast<size_t>(inputChannels));
        for (int channel = 0; channel < inputChannels; ++channel)
            inputData[size_t(channel)] = decoded->extended_data[channel];
        const int received = swr_convert(
            swr, outputData, capacity, inputData.data(),
            decoded->nb_samples);
        if (received < 0)
        {
            *error = QStringLiteral("音频重采样失败：%1")
                         .arg(ffmpegError(received));
            return false;
        }
        if (received > 0)
        {
            mixConverted(reinterpret_cast<const float*>(converted.constData()),
                         received);
        }
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
            const int64_t delay = swr_get_delay(swr, options.sampleRate);
            if (delay <= 0)
                break;
            const int capacity = int(delay);
            converted.resize(int(size_t(capacity) * size_t(channels)
                                 * sizeof(float)));
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
            mixConverted(reinterpret_cast<const float*>(converted.constData()),
                         received);
        }
    }

    return true;
}

} // namespace

QByteArray mixRange(const model::Project& project,
                    const assets::AssetStore& assets,
                    const MixOptions& options, QStringList* warnings)
{
    if (options.endFrame <= options.startFrame || options.fps <= 0
        || options.sampleRate <= 0 || options.channels <= 0)
    {
        return {};
    }

    const int64_t totalFrames =
        int64_t(options.endFrame) - int64_t(options.startFrame);
    const int64_t totalSamples = std::max<int64_t>(
        0, llround(double(totalFrames) * double(options.sampleRate)
                   / double(options.fps)));
    const int64_t totalValues = totalSamples * int64_t(options.channels);
    if (totalSamples <= 0
        || totalValues > std::numeric_limits<int>::max() / int64_t(sizeof(float)))
    {
        return {};
    }

    QByteArray pcm;
    pcm.resize(int(totalValues * int64_t(sizeof(float))));
    float* mix = reinterpret_cast<float*>(pcm.data());
    std::fill(mix, mix + totalValues, 0.0f);

    bool audible = false;
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.content.kind != ContentKind::Asset
            || entity.content.asset.type != AssetType::Audio
            || entity.end <= entity.start)
        {
            continue;
        }

        QString error;
        if (!mixAudioEntity(project, assets, entity, options, totalSamples,
                            mix, &audible, &error))
        {
            if (warnings)
            {
                const QString name =
                    entity.name.isEmpty() ? entity.uuid : entity.name;
                warnings->append(
                    QStringLiteral("音频实体 \"%1\" 已跳过：%2")
                        .arg(name, error));
            }
        }
    }

    if (!audible)
        return {};

    bool nonzero = false;
    for (int64_t i = 0; i < totalValues; ++i)
    {
        mix[i] = std::clamp(mix[i], -1.0f, 1.0f);
        if (mix[i] != 0.0f)
            nonzero = true;
    }
    if (!nonzero)
        return {};

    return pcm;
}

} // namespace pvf::audio2
