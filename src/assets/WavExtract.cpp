#include "assets/WavExtract.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

#include <cstdint>
#include <limits>

namespace pvf::assets {

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kBytesPerSample = 2;

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QString ffmpegError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

struct FfmpegGuard
{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* swr = nullptr;

    ~FfmpegGuard()
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

bool writeHeader(QDataStream& stream, quint32 dataBytes)
{
    const quint32 byteRate = kSampleRate * kChannels * kBytesPerSample;
    const quint16 blockAlign = kChannels * kBytesPerSample;

    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataBytes);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16);
    stream << quint16(1);                  // PCM
    stream << quint16(kChannels);
    stream << quint32(kSampleRate);
    stream << byteRate;
    stream << blockAlign;
    stream << quint16(8 * kBytesPerSample);
    stream.writeRawData("data", 4);
    stream << dataBytes;
    return stream.status() == QDataStream::Ok;
}

bool writeSamples(QFile& file, quint32& dataBytes, const uint8_t* data, int frameCount)
{
    if (frameCount <= 0)
        return true;
    const qint64 bytes = qint64(frameCount) * kChannels * kBytesPerSample;
    const quint32 newSize = dataBytes + quint32(bytes);
    if (newSize < dataBytes || newSize > std::numeric_limits<quint32>::max() - 36)
    {
        return false;
    }
    if (file.write(reinterpret_cast<const char*>(data), bytes) != bytes)
        return false;
    dataBytes = newSize;
    return true;
}

} // namespace

bool extractWav(const QString& videoPath, const QString& outPath, QString* error)
{
    FfmpegGuard ff;
    const QByteArray utf8 = videoPath.toUtf8();

    int status = avformat_open_input(&ff.format, utf8.constData(), nullptr, nullptr);
    if (status < 0)
    {
        setError(error, QStringLiteral("无法打开媒体文件: %1").arg(ffmpegError(status)));
        return false;
    }
    status = avformat_find_stream_info(ff.format, nullptr);
    if (status < 0)
    {
        setError(error, QStringLiteral("无法读取媒体信息: %1").arg(ffmpegError(status)));
        return false;
    }

    const int streamIndex =
        av_find_best_stream(ff.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0)
    {
        setError(error, QStringLiteral("文件中没有音频流"));
        return false;
    }

    const AVCodecParameters* params = ff.format->streams[streamIndex]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(params->codec_id);
    if (!decoder)
    {
        setError(error, QStringLiteral("找不到音频解码器"));
        return false;
    }

    ff.codec = avcodec_alloc_context3(decoder);
    if (!ff.codec || avcodec_parameters_to_context(ff.codec, params) < 0
        || avcodec_open2(ff.codec, decoder, nullptr) < 0)
    {
        setError(error, QStringLiteral("无法初始化音频解码器"));
        return false;
    }
    if (ff.codec->sample_rate <= 0 || ff.codec->ch_layout.nb_channels <= 0)
    {
        setError(error, QStringLiteral("音频流缺少采样率或声道信息"));
        return false;
    }

    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, kChannels);
    AVChannelLayout inputLayout = ff.codec->ch_layout;
    const bool defaultedInput = inputLayout.order == AV_CHANNEL_ORDER_UNSPEC
                                && inputLayout.nb_channels > 0;
    if (defaultedInput)
        av_channel_layout_default(&inputLayout, ff.codec->ch_layout.nb_channels);
    status = swr_alloc_set_opts2(&ff.swr, &outLayout, AV_SAMPLE_FMT_S16, kSampleRate,
                                 &inputLayout, ff.codec->sample_fmt,
                                 ff.codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outLayout);
    if (defaultedInput)
        av_channel_layout_uninit(&inputLayout);
    if (status < 0 || !ff.swr || swr_init(ff.swr) < 0)
    {
        setError(error, QStringLiteral("无法初始化音频重采样"));
        return false;
    }

    ff.packet = av_packet_alloc();
    ff.frame = av_frame_alloc();
    if (!ff.packet || !ff.frame)
    {
        setError(error, QStringLiteral("内存不足"));
        return false;
    }

    const QFileInfo outInfo(outPath);
    if (!QDir().mkpath(outInfo.absolutePath()))
    {
        setError(error, QStringLiteral("无法创建输出目录: %1").arg(outInfo.absolutePath()));
        return false;
    }

    QTemporaryFile output(outPath + QStringLiteral(".XXXXXX.wav"));
    output.setAutoRemove(false);
    if (!output.open())
    {
        setError(error, QStringLiteral("无法创建输出文件: %1").arg(output.errorString()));
        return false;
    }

    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    if (!writeHeader(stream, 0))
    {
        setError(error, QStringLiteral("无法写入 WAV 头"));
        return false;
    }

    quint32 dataBytes = 0;
    bool failed = false;
    QString failure;

    auto receiveFrame = [&](const AVFrame* frame) -> bool {
        int outSamples = swr_get_out_samples(ff.swr, frame->nb_samples);
        if (outSamples <= 0)
            return true;
        QByteArray buffer(outSamples * kChannels * kBytesPerSample, Qt::Uninitialized);
        uint8_t* outputData[1] = {reinterpret_cast<uint8_t*>(buffer.data())};
        const int converted = swr_convert(ff.swr, outputData, outSamples, frame->data,
                                          frame->nb_samples);
        if (converted < 0)
        {
            failure = QStringLiteral("音频重采样失败: %1").arg(ffmpegError(converted));
            return false;
        }
        if (!writeSamples(output, dataBytes, outputData[0], converted))
        {
            failure = QStringLiteral("写入 WAV 数据失败");
            return false;
        }
        return true;
    };

    while (!failed && av_read_frame(ff.format, ff.packet) >= 0)
    {
        if (ff.packet->stream_index == streamIndex)
        {
            if (avcodec_send_packet(ff.codec, ff.packet) >= 0)
            {
                while (true)
                {
                    const int received = avcodec_receive_frame(ff.codec, ff.frame);
                    if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                        break;
                    if (received < 0)
                    {
                        failure = QStringLiteral("音频解码失败: %1")
                                      .arg(ffmpegError(received));
                        failed = true;
                        break;
                    }
                    if (!receiveFrame(ff.frame))
                    {
                        failed = true;
                        break;
                    }
                }
            }
        }
        av_packet_unref(ff.packet);
    }

    if (!failed)
    {
        avcodec_send_packet(ff.codec, nullptr);
        while (true)
        {
            const int received = avcodec_receive_frame(ff.codec, ff.frame);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                break;
            if (received < 0)
            {
                failure = QStringLiteral("音频解码失败: %1").arg(ffmpegError(received));
                failed = true;
                break;
            }
            if (!receiveFrame(ff.frame))
            {
                failed = true;
                break;
            }
        }
    }

    // Drain samples still buffered inside the resampler.
    if (!failed)
    {
        while (true)
        {
            const int outSamples = 4096;
            QByteArray buffer(outSamples * kChannels * kBytesPerSample, Qt::Uninitialized);
            uint8_t* outputData[1] = {reinterpret_cast<uint8_t*>(buffer.data())};
            const int converted =
                swr_convert(ff.swr, outputData, outSamples, nullptr, 0);
            if (converted < 0)
            {
                failure = QStringLiteral("音频重采样失败: %1").arg(ffmpegError(converted));
                failed = true;
                break;
            }
            if (converted == 0)
                break;
            if (!writeSamples(output, dataBytes, outputData[0], converted))
            {
                failure = QStringLiteral("写入 WAV 数据失败");
                failed = true;
                break;
            }
        }
    }

    if (!failed && dataBytes == 0)
    {
        failure = QStringLiteral("没有解码出音频样本");
        failed = true;
    }

    if (!failed)
    {
        if (!output.seek(4))
            failed = true;
        else
        {
            stream << quint32(36 + dataBytes);
            if (stream.status() != QDataStream::Ok)
                failed = true;
        }
    }
    if (!failed)
    {
        if (!output.seek(40))
            failed = true;
        else
        {
            stream << dataBytes;
            if (stream.status() != QDataStream::Ok)
                failed = true;
        }
    }
    if (!failed && !output.flush())
        failed = true;

    const QString temporaryPath = output.fileName();
    output.close();

    if (failed)
    {
        QFile::remove(temporaryPath);
        setError(error, failure.isEmpty() ? QStringLiteral("WAV 提取失败") : failure);
        return false;
    }

    QFile::remove(outPath);
    if (!QFile::rename(temporaryPath, outPath))
    {
        QFile::remove(temporaryPath);
        setError(error, QStringLiteral("无法写入输出文件: %1").arg(outPath));
        return false;
    }
    return true;
}

} // namespace pvf::assets
