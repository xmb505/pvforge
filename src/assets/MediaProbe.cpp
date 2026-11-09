#include "assets/MediaProbe.h"

#include <QImageReader>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include <cmath>

namespace pvf::assets {

namespace {

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

struct FormatGuard
{
    AVFormatContext* context = nullptr;

    ~FormatGuard()
    {
        if (context)
            avformat_close_input(&context);
    }
};

double streamDurationSeconds(const AVStream* stream)
{
    if (!stream || stream->duration == AV_NOPTS_VALUE || stream->duration <= 0)
        return 0;
    return stream->duration * av_q2d(stream->time_base);
}

Probe probeAv(const QString& path, AssetType type, QString* error)
{
    Probe result;
    FormatGuard format;
    const QByteArray utf8 = path.toUtf8();

    int status = avformat_open_input(&format.context, utf8.constData(), nullptr, nullptr);
    if (status < 0)
    {
        setError(error, QStringLiteral("无法打开媒体文件: %1").arg(ffmpegError(status)));
        return result;
    }
    status = avformat_find_stream_info(format.context, nullptr);
    if (status < 0)
    {
        setError(error, QStringLiteral("无法读取媒体信息: %1").arg(ffmpegError(status)));
        return result;
    }

    const int videoIndex =
        av_find_best_stream(format.context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audioIndex =
        av_find_best_stream(format.context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (videoIndex >= 0)
    {
        const AVCodecParameters* params = format.context->streams[videoIndex]->codecpar;
        result.size = QSize(params->width, params->height);
    }
    if (audioIndex >= 0)
    {
        const AVCodecParameters* params = format.context->streams[audioIndex]->codecpar;
        result.sampleRate = params->sample_rate;
        result.channels = params->ch_layout.nb_channels;
        if (result.channels == 0 && params->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        {
            quint64 mask = params->ch_layout.u.mask;
            while (mask)
            {
                result.channels += int(mask & 1);
                mask >>= 1;
            }
        }
    }

    if (format.context->duration != AV_NOPTS_VALUE && format.context->duration > 0)
    {
        result.durationSeconds = format.context->duration / double(AV_TIME_BASE);
    }
    else
    {
        const int preferred = type == AssetType::Audio ? audioIndex : videoIndex;
        if (preferred >= 0)
            result.durationSeconds = streamDurationSeconds(format.context->streams[preferred]);
        else if (audioIndex >= 0)
            result.durationSeconds = streamDurationSeconds(format.context->streams[audioIndex]);
    }
    if (result.durationSeconds < 0 || !std::isfinite(result.durationSeconds))
        result.durationSeconds = 0;

    if (type == AssetType::Video && videoIndex < 0)
    {
        setError(error, QStringLiteral("文件中没有视频流"));
        return {};
    }
    if (type == AssetType::Audio && audioIndex < 0)
    {
        setError(error, QStringLiteral("文件中没有音频流"));
        return {};
    }

    return result;
}

} // namespace

Probe probe(const QString& path, AssetType type, QString* error)
{
    switch (type)
    {
    case AssetType::Image:
    {
        QImageReader reader(path);
        if (!reader.canRead())
        {
            setError(error, QStringLiteral("无法读取图片: %1 (%2)")
                                .arg(path, reader.errorString()));
            return {};
        }
        Probe result;
        result.size = reader.size();
        if (!result.size.isValid())
        {
            setError(error, QStringLiteral("无法读取图片尺寸: %1").arg(path));
            return {};
        }
        return result;
    }
    case AssetType::Video:
    case AssetType::Audio:
        return probeAv(path, type, error);
    case AssetType::Html:
        return {};
    }
    return {};
}

} // namespace pvf::assets
