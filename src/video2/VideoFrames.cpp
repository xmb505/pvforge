#include "video2/VideoFrames.h"

#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

namespace pvf::video2 {
namespace {

struct Input
{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* candidate = nullptr;
    SwsContext* sws = nullptr;

    int streamIndex = -1;
    AVRational timeBase{1, 1000};
    int64_t startTime = 0;
    double frameDuration = 1.0 / 30.0;

    ~Input() { close(); }

    void close()
    {
        if (sws)
            sws_freeContext(sws);
        sws = nullptr;
        if (candidate)
            av_frame_free(&candidate);
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

void fail(Input& input, QString* error, const QString& message)
{
    if (error)
        *error = message;
    input.close();
}

bool openInput(Input& input, const QString& path, QString* error)
{
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&input.format, utf8.constData(), nullptr, nullptr)
        < 0)
    {
        fail(input, error, QStringLiteral("无法打开视频文件"));
        return false;
    }

    if (avformat_find_stream_info(input.format, nullptr) < 0)
    {
        fail(input, error, QStringLiteral("无法读取视频信息"));
        return false;
    }

    const int index =
        av_find_best_stream(input.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0)
    {
        fail(input, error, QStringLiteral("文件中没有视频流"));
        return false;
    }

    input.streamIndex = index;
    const AVStream* stream = input.format->streams[index];
    input.timeBase = stream->time_base;
    input.startTime =
        stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;

    AVRational rate = stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        rate = stream->r_frame_rate;
    if (rate.num > 0 && rate.den > 0)
        input.frameDuration = rate.den / double(rate.num);

    const AVCodecParameters* parameters = stream->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
    if (!decoder)
    {
        fail(input, error, QStringLiteral("找不到视频解码器"));
        return false;
    }

    input.codec = avcodec_alloc_context3(decoder);
    if (!input.codec || avcodec_parameters_to_context(input.codec, parameters) < 0
        || avcodec_open2(input.codec, decoder, nullptr) < 0)
    {
        fail(input, error, QStringLiteral("无法初始化解码器"));
        return false;
    }

    input.packet = av_packet_alloc();
    input.frame = av_frame_alloc();
    input.candidate = av_frame_alloc();
    if (!input.packet || !input.frame || !input.candidate)
    {
        fail(input, error, QStringLiteral("内存不足"));
        return false;
    }

    return true;
}

int64_t timestampOf(const AVFrame* frame)
{
    int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE)
        timestamp = frame->pts;
    return timestamp;
}

QImage convert(const AVFrame* frame, const Request& request, int proxyMaxWidth,
               QString* error)
{
    int dstWidth = frame->width;
    int dstHeight = frame->height;

    if (request.proxy)
    {
        if (proxyMaxWidth > 0 && dstWidth > proxyMaxWidth)
        {
            dstWidth = proxyMaxWidth;
            dstHeight = std::max(
                1, int(std::lround(double(frame->height) * double(dstWidth)
                                   / double(frame->width))));
        }
    }
    else if (request.size.isValid())
    {
        dstWidth = request.size.width();
        dstHeight = request.size.height();
    }

    if (dstWidth <= 0 || dstHeight <= 0)
    {
        if (error)
            *error = QStringLiteral("目标尺寸无效");
        return QImage();
    }

    SwsContext* sws = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        dstWidth, dstHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
        nullptr);
    if (!sws)
    {
        if (error)
            *error = QStringLiteral("无法转换视频帧");
        return QImage();
    }

    QImage image(dstWidth, dstHeight, QImage::Format_RGBA8888);
    if (image.isNull())
    {
        sws_freeContext(sws);
        if (error)
            *error = QStringLiteral("内存不足");
        return QImage();
    }

    uint8_t* destination[4] = {image.bits(), nullptr, nullptr, nullptr};
    int destinationLinesize[4] = {int(image.bytesPerLine()), 0, 0, 0};
    const int scaled =
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, destination,
                  destinationLinesize);
    sws_freeContext(sws);

    if (scaled <= 0)
    {
        if (error)
            *error = QStringLiteral("无法转换视频帧");
        return QImage();
    }
    return image;
}

QImage decode(const Request& request, int proxyMaxWidth, QString* error)
{
    if (request.fps <= 0)
    {
        if (error)
            *error = QStringLiteral("帧率必须大于 0");
        return QImage();
    }
    if (request.path.isEmpty() || !QFileInfo(request.path).isFile())
    {
        if (error)
            *error = QStringLiteral("视频文件不存在");
        return QImage();
    }

    Input input;
    if (!openInput(input, request.path, error))
        return QImage();

    const int sourceFrame = std::max(0, request.sourceFrame);
    // Round down: the frame displayed at the requested time is the last frame
    // whose timestamp is <= that time. Rounding up could skip a keyframe and
    // select the following frame when the project fps exceeds the video fps.
    const int64_t targetTimestamp =
        av_rescale_q_rnd(sourceFrame, AVRational{1, request.fps}, input.timeBase,
                         AV_ROUND_DOWN)
        + input.startTime;

    // Seeking is only needed when the target is past the first timestamp;
    // some containers reject a seek to the very start.
    if (targetTimestamp > input.startTime)
    {
        if (av_seek_frame(input.format, input.streamIndex, targetTimestamp,
                          AVSEEK_FLAG_BACKWARD)
            < 0)
        {
            fail(input, error, QStringLiteral("无法定位视频帧"));
            return QImage();
        }
        avcodec_flush_buffers(input.codec);
    }

    bool haveCandidate = false;
    bool done = false;
    bool inputDone = false;
    int64_t lastTimestamp = AV_NOPTS_VALUE;
    const int64_t fallbackStep = std::max<int64_t>(
        1, av_rescale_q(int64_t(input.frameDuration * 1000000.0),
                        AVRational{1, 1000000}, input.timeBase));

    auto accept = [&](AVFrame* frame) {
        int64_t timestamp = timestampOf(frame);
        if (timestamp == AV_NOPTS_VALUE)
        {
            // Streams without timestamps are rare; assume one frame follows
            // the previous one so the loop still terminates.
            timestamp = lastTimestamp == AV_NOPTS_VALUE
                            ? targetTimestamp
                            : lastTimestamp + fallbackStep;
        }
        lastTimestamp = timestamp;

        if (timestamp > targetTimestamp && haveCandidate)
        {
            done = true;
            return;
        }

        av_frame_unref(input.candidate);
        if (av_frame_ref(input.candidate, frame) < 0)
        {
            haveCandidate = false;
            done = true;
            return;
        }
        haveCandidate = true;
        if (timestamp > targetTimestamp)
            done = true;
    };

    // Seek lands on the previous keyframe; decode forward until the frame
    // displayed at the requested time has been reached.
    while (!done)
    {
        const int received = avcodec_receive_frame(input.codec, input.frame);
        if (received == 0)
        {
            accept(input.frame);
            av_frame_unref(input.frame);
            continue;
        }

        if (received != AVERROR(EAGAIN))
            break; // end of stream or decoder error

        if (inputDone)
            break;

        while (!inputDone)
        {
            const int read = av_read_frame(input.format, input.packet);
            if (read < 0)
            {
                avcodec_send_packet(input.codec, nullptr);
                inputDone = true;
                break;
            }
            if (input.packet->stream_index != input.streamIndex)
            {
                av_packet_unref(input.packet);
                continue;
            }

            const int sent =
                avcodec_send_packet(input.codec, input.packet);
            av_packet_unref(input.packet);
            if (sent == AVERROR(EAGAIN))
                break; // drain the decoder before feeding again
            break;     // packet accepted or corrupt: move on
        }
    }

    if (!haveCandidate)
    {
        if (error)
            *error = QStringLiteral("未能解码出视频帧");
        return QImage();
    }

    return convert(input.candidate, request, proxyMaxWidth, error);
}

} // namespace

VideoFrames::VideoFrames(int proxyMaxWidth, int memoryEntries)
    : m_proxyMaxWidth(proxyMaxWidth > 0 ? proxyMaxWidth : 960)
{
    m_cache.setMaxCost(std::max(0, memoryEntries));
}

QString VideoFrames::cacheKey(const Request& request) const
{
    return QStringList{
        request.path,
        QString::number(request.sourceFrame),
        QString::number(request.fps),
        QString::number(request.size.width()),
        QString::number(request.size.height()),
        request.proxy ? QStringLiteral("proxy") : QStringLiteral("full")}
        .join(QChar(0x1f));
}

QImage VideoFrames::frame(const Request& request, QString* error)
{
    if (error)
        error->clear();

    const QString key = cacheKey(request);
    if (QImage* cached = m_cache.object(key))
    {
        ++m_stats.hits;
        return *cached;
    }

    ++m_stats.misses;
    ++m_stats.decodes;

    QString message;
    const QImage image = decode(request, m_proxyMaxWidth, &message);
    if (image.isNull())
    {
        if (error)
            *error = message;
        return QImage();
    }

    if (m_cache.maxCost() > 0)
        m_cache.insert(key, new QImage(image));
    return image;
}

void VideoFrames::clear()
{
    m_cache.clear();
    m_stats = Stats();
}

VideoFrames::Stats VideoFrames::stats() const
{
    return m_stats;
}

} // namespace pvf::video2
