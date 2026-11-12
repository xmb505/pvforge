#include "Peaks.h"

#include <QScopeGuard>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace pvf::audio2 {
namespace {

constexpr int kPeakSampleRate = 48000;

QString ffmpegError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

bool streamDurationSeconds(const AVFormatContext* format, const AVStream* stream,
                           double* seconds)
{
    if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
    {
        *seconds = double(format->duration) / double(AV_TIME_BASE);
        return true;
    }
    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0
        && stream->time_base.den > 0)
    {
        *seconds = double(stream->duration) * double(stream->time_base.num)
                   / double(stream->time_base.den);
        return true;
    }
    return false;
}

QVector<float> bucketSamples(const QVector<float>& samples, int buckets)
{
    QVector<float> peaks(buckets, 0.0f);
    const int count = samples.size();
    for (int i = 0; i < count; ++i)
    {
        const int bucket = std::min(
            buckets - 1, int(int64_t(i) * buckets / std::max(1, count)));
        peaks[bucket] = std::max(peaks[bucket], samples.at(i));
    }
    return peaks;
}

} // namespace

QVector<float> audioPeaks(const QString& path, int buckets, QString* error)
{
    return audioPeaks(path, buckets, 0.0, 0.0, error);
}

QVector<float> audioPeaks(const QString& path, int buckets, double startSeconds,
                          double durationSeconds, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QVector<float>();
    };

    if (error)
        error->clear();
    if (buckets <= 0)
        return fail(QStringLiteral("峰值分桶数必须大于 0"));
    if (path.isEmpty())
        return fail(QStringLiteral("音频路径为空"));
    if (startSeconds < 0.0 || durationSeconds < 0.0)
        return fail(QStringLiteral("音频窗口不能为负数"));

    const QByteArray pathUtf8 = path.toUtf8();
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, pathUtf8.constData(), nullptr, nullptr) < 0)
        return fail(QStringLiteral("无法打开音频文件"));
    const auto formatGuard = qScopeGuard([&] {
        if (format)
            avformat_close_input(&format);
    });

    if (avformat_find_stream_info(format, nullptr) < 0)
        return fail(QStringLiteral("无法读取音频信息"));

    const int audioIndex =
        av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIndex < 0)
        return fail(QStringLiteral("没有音频流"));

    const AVCodecParameters* parameters = format->streams[audioIndex]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
    if (!decoder)
        return fail(QStringLiteral("不支持的音频编码"));

    AVCodecContext* decoderContext = avcodec_alloc_context3(decoder);
    if (!decoderContext)
        return fail(QStringLiteral("无法创建音频解码器"));
    const auto decoderGuard =
        qScopeGuard([&] { avcodec_free_context(&decoderContext); });

    int status = avcodec_parameters_to_context(decoderContext, parameters);
    if (status < 0)
        return fail(QStringLiteral("无法复制音频参数：%1").arg(ffmpegError(status)));
    status = avcodec_open2(decoderContext, decoder, nullptr);
    if (status < 0)
        return fail(QStringLiteral("无法打开音频解码器：%1")
                        .arg(ffmpegError(status)));
    if (decoderContext->sample_rate <= 0
        || decoderContext->ch_layout.nb_channels <= 0)
        return fail(QStringLiteral("音频格式无效"));

    AVChannelLayout monoLayout;
    av_channel_layout_default(&monoLayout, 1);
    const auto layoutGuard =
        qScopeGuard([&] { av_channel_layout_uninit(&monoLayout); });

    SwrContext* swr = nullptr;
    status = swr_alloc_set_opts2(
        &swr, &monoLayout, AV_SAMPLE_FMT_FLT, kPeakSampleRate,
        &decoderContext->ch_layout, decoderContext->sample_fmt,
        decoderContext->sample_rate, 0, nullptr);
    if (status < 0 || !swr)
        return fail(QStringLiteral("无法创建音频重采样器：%1")
                        .arg(ffmpegError(status)));
    const auto swrGuard = qScopeGuard([&] { swr_free(&swr); });

    status = swr_init(swr);
    if (status < 0)
        return fail(QStringLiteral("无法初始化音频重采样器：%1")
                        .arg(ffmpegError(status)));
    double fileSeconds = 0.0;
    const bool knownDuration = streamDurationSeconds(
        format, format->streams[audioIndex], &fileSeconds);

    const double windowStart = startSeconds;
    double windowDuration = durationSeconds;
    if (windowDuration <= 0.0 && knownDuration)
        windowDuration = std::max(0.0, fileSeconds - windowStart);
    // Duration still unknown: decode to EOF and bucket the collected envelope.
    const bool collectAll = windowDuration <= 0.0;

    const int64_t windowStartSample =
        llround(windowStart * double(kPeakSampleRate));
    const int64_t windowSamples =
        collectAll ? -1
                   : llround(windowDuration * double(kPeakSampleRate));
    if (!collectAll && windowSamples <= 0)
        return fail(QStringLiteral("音频窗口为空"));

    QVector<float> peaks(buckets, 0.0f);
    QVector<float> collected;
    int64_t decodedSamples = 0;
    bool enough = false;

    auto addConverted = [&](const float* data, int count) {
        for (int i = 0; i < count; ++i)
        {
            const int64_t index = decodedSamples + i;
            const int64_t relative = index - windowStartSample;
            if (relative < 0)
                continue;

            const float value = std::min(1.0f, std::abs(data[i]));
            if (collectAll)
            {
                collected.append(value);
                continue;
            }
            if (relative >= windowSamples)
            {
                enough = true;
                break;
            }
            const int bucket = std::min(
                buckets - 1,
                int(relative * int64_t(buckets) / windowSamples));
            peaks[bucket] = std::max(peaks[bucket], value);
        }
        decodedSamples += count;
        if (!collectAll && decodedSamples >= windowStartSample + windowSamples)
            enough = true;
    };

    AVFrame* frame = av_frame_alloc();
    if (!frame)
        return fail(QStringLiteral("无法创建音频帧"));
    const auto frameGuard = qScopeGuard([&] { av_frame_free(&frame); });

    AVPacket* packet = av_packet_alloc();
    if (!packet)
        return fail(QStringLiteral("无法创建音频包"));
    const auto packetGuard = qScopeGuard([&] { av_packet_free(&packet); });

    QByteArray converted;
    auto convertFrame = [&](const AVFrame* decoded) -> bool {
        const int capacity = int(av_rescale_rnd(
            swr_get_delay(swr, kPeakSampleRate) + decoded->nb_samples,
            kPeakSampleRate, decoded->sample_rate, AV_ROUND_UP));
        if (capacity <= 0)
            return true;

        converted.resize(int(size_t(capacity) * sizeof(float)));
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
            if (error)
                *error = QStringLiteral("音频重采样失败：%1")
                             .arg(ffmpegError(received));
            return false;
        }
        if (received > 0)
            addConverted(reinterpret_cast<const float*>(converted.constData()),
                         received);
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
                if (error)
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
                if (error)
                    *error = QStringLiteral("音频解码失败：%1")
                                 .arg(ffmpegError(sendStatus));
                return {};
            }
            if (!receiveDecoded())
                return {};
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
            return {};

        while (!collectAll)
        {
            const int64_t delay = swr_get_delay(swr, kPeakSampleRate);
            if (delay <= 0)
                break;
            const int capacity = int(delay);
            converted.resize(int(size_t(capacity) * sizeof(float)));
            uint8_t* outputData[1] = {
                reinterpret_cast<uint8_t*>(converted.data())};
            const int received =
                swr_convert(swr, outputData, capacity, nullptr, 0);
            if (received < 0)
            {
                if (error)
                    *error = QStringLiteral("音频重采样失败：%1")
                                 .arg(ffmpegError(received));
                return {};
            }
            if (received == 0)
                break;
            addConverted(reinterpret_cast<const float*>(converted.constData()),
                         received);
            if (enough)
                break;
        }
    }

    if (collectAll)
    {
        if (collected.isEmpty())
            return fail(QStringLiteral("没有解码出音频数据"));
        peaks = bucketSamples(collected, buckets);
    }

    return peaks;
}

} // namespace pvf::audio2
