#include "AudioDecoder.h"

#include <QColor>
#include <QPainter>
#include <QtMath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;

struct FfmpegCleanup
{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* swr = nullptr;

    ~FfmpegCleanup()
    {
        if (swr)
            swr_free(&swr);
        if (frame)
            av_frame_free(&frame);
        if (packet)
            av_packet_free(&packet);
        if (codec)
            avcodec_free_context(&codec);
        if (format)
            avformat_close_input(&format);
    }
};

} // namespace

AudioDecodeResult decodeAudioFile(const QString& path)
{
    AudioDecodeResult result;
    FfmpegCleanup ff;
    const QByteArray utf8 = path.toUtf8();

    if (avformat_open_input(&ff.format, utf8.constData(), nullptr, nullptr) < 0)
    {
        result.error = QStringLiteral("无法打开音频文件");
        return result;
    }
    if (avformat_find_stream_info(ff.format, nullptr) < 0)
    {
        result.error = QStringLiteral("无法读取音频信息");
        return result;
    }

    const int streamIndex =
        av_find_best_stream(ff.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0)
    {
        result.error = QStringLiteral("文件中没有音频流");
        return result;
    }

    const AVCodecParameters* params = ff.format->streams[streamIndex]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(params->codec_id);
    if (!decoder)
    {
        result.error = QStringLiteral("找不到音频解码器");
        return result;
    }

    ff.codec = avcodec_alloc_context3(decoder);
    if (!ff.codec || avcodec_parameters_to_context(ff.codec, params) < 0
        || avcodec_open2(ff.codec, decoder, nullptr) < 0)
    {
        result.error = QStringLiteral("无法初始化音频解码器");
        return result;
    }

    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, kChannels);
    if (swr_alloc_set_opts2(&ff.swr, &outLayout, AV_SAMPLE_FMT_S16, kSampleRate,
                            &ff.codec->ch_layout, ff.codec->sample_fmt,
                            ff.codec->sample_rate, 0, nullptr)
            < 0
        || swr_init(ff.swr) < 0)
    {
        av_channel_layout_uninit(&outLayout);
        result.error = QStringLiteral("无法初始化音频重采样");
        return result;
    }
    av_channel_layout_uninit(&outLayout);

    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = kSampleRate;
    clip->channels = kChannels;
    if (ff.format->duration > 0)
    {
        const qint64 estimate =
            qint64(ff.format->duration / double(AV_TIME_BASE) * kSampleRate) * kChannels;
        if (estimate > 0 && estimate < (qint64(1) << 32))
            clip->samples.reserve(int(estimate));
    }

    ff.packet = av_packet_alloc();
    ff.frame = av_frame_alloc();
    if (!ff.packet || !ff.frame)
    {
        result.error = QStringLiteral("内存不足");
        return result;
    }

    auto receive = [&](AVFrame* frame) -> bool {
        const int outSamples = swr_get_out_samples(ff.swr, frame->nb_samples);
        if (outSamples <= 0)
            return true;
        const int oldSize = int(clip->samples.size());
        clip->samples.resize(oldSize + outSamples * kChannels);
        uint8_t* output[1] = {
            reinterpret_cast<uint8_t*>(clip->samples.data() + oldSize)};
        const int converted = swr_convert(ff.swr, output, outSamples, frame->data,
                                          frame->nb_samples);
        if (converted < 0)
            return false;
        clip->samples.resize(oldSize + converted * kChannels);
        return true;
    };

    while (av_read_frame(ff.format, ff.packet) >= 0)
    {
        if (ff.packet->stream_index == streamIndex)
        {
            if (avcodec_send_packet(ff.codec, ff.packet) >= 0)
            {
                while (avcodec_receive_frame(ff.codec, ff.frame) == 0)
                {
                    if (!receive(ff.frame))
                    {
                        av_packet_unref(ff.packet);
                        result.error = QStringLiteral("音频重采样失败");
                        return result;
                    }
                }
            }
        }
        av_packet_unref(ff.packet);
    }

    // Flush the decoder and resampler.
    avcodec_send_packet(ff.codec, nullptr);
    while (avcodec_receive_frame(ff.codec, ff.frame) == 0)
    {
        if (!receive(ff.frame))
        {
            result.error = QStringLiteral("音频重采样失败");
            return result;
        }
    }

    if (clip->samples.isEmpty())
    {
        result.error = QStringLiteral("没有解码出音频样本");
        return result;
    }

    clip->duration = clip->samples.size() / double(kChannels * kSampleRate);

    // One peak per millisecond, taking the maximum absolute sample of both
    // channels. This is the data the timeline waveform reads.
    const int framesPerPeak = kSampleRate / 1000;
    const int valuesPerPeak = framesPerPeak * kChannels;
    clip->peaks.reserve(int(std::ceil(clip->duration * 1000.0)));
    for (int i = 0; i < clip->samples.size(); i += valuesPerPeak)
    {
        int peak = 0;
        const int end = std::min(i + valuesPerPeak, int(clip->samples.size()));
        for (int j = i; j < end; ++j)
            peak = std::max(peak, std::abs(int(clip->samples[j])));
        clip->peaks.append(peak / 32768.0f);
    }

    result.clip = clip;
    return result;
}

QImage renderAudioThumbnail(const AudioClip& clip, const QSize& size,
                            const QColor& accent, const QColor& background)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(background);
    if (size.isEmpty() || clip.peaks.isEmpty())
        return image;

    QPainter painter(&image);
    const int center = size.height() / 2;
    const int half = std::max(1, size.height() / 2 - 4);
    painter.setPen(QPen(accent, 1.0));
    for (int x = 0; x < size.width(); ++x)
    {
        const double t0 = x / double(std::max(1, size.width())) * clip.peaks.size();
        const double t1 =
            (x + 1) / double(std::max(1, size.width())) * clip.peaks.size();
        const int i0 = std::clamp(int(t0), 0, int(clip.peaks.size()) - 1);
        const int i1 = std::clamp(int(std::ceil(t1)), 0, int(clip.peaks.size()));
        float peak = 0.0f;
        for (int i = i0; i < i1; ++i)
            peak = std::max(peak, clip.peaks[i]);
        const int amplitude = int(std::lround(peak * half));
        painter.drawLine(x, center - amplitude, x, center + amplitude);
    }
    return image;
}

} // namespace pvf
