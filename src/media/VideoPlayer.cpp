#include "VideoPlayer.h"

#include "VideoProxy.h"
#include "core/Project.h"

#include <QSet>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace pvf {

struct VideoPlayer::Stream
{
    QString path;
    QString sourcePath;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* candidate = nullptr;   // last decoded frame before the target
    SwsContext* sws = nullptr;

    int streamIndex = -1;
    AVRational timeBase{1, 1000};
    double duration = 0.0;
    double frameDuration = 1.0 / 30.0;

    bool haveDecoded = false;
    bool sentFlush = false;
    double lastDecodedSeconds = -1.0;
    double lastRequested = -1.0;
    QImage image;
};

namespace {

double frameSeconds(const VideoPlayer::Stream& stream, const AVFrame* frame)
{
    int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE)
        timestamp = frame->pts;
    if (timestamp == AV_NOPTS_VALUE)
        return -1.0;
    return timestamp * av_q2d(stream.timeBase);
}

QImage convertFrame(VideoPlayer::Stream& stream, const AVFrame* frame)
{
    stream.sws = sws_getCachedContext(
        stream.sws, frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!stream.sws)
        return QImage();

    QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
    uint8_t* destination[4] = {image.bits(), nullptr, nullptr, nullptr};
    int destinationLinesize[4] = {int(image.bytesPerLine()), 0, 0, 0};
    sws_scale(stream.sws, frame->data, frame->linesize, 0, frame->height,
              destination, destinationLinesize);
    return image;
}

} // namespace

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer()
{
    clear();
}

void VideoPlayer::clear()
{
    for (Stream* stream : m_streams)
    {
        closeStream(*stream);
        delete stream;
    }
    m_streams.clear();
}

void VideoPlayer::closeStream(Stream& stream)
{
    if (stream.sws)
        sws_freeContext(stream.sws);
    if (stream.candidate)
        av_frame_free(&stream.candidate);
    if (stream.frame)
        av_frame_free(&stream.frame);
    if (stream.packet)
        av_packet_free(&stream.packet);
    if (stream.codec)
        avcodec_free_context(&stream.codec);
    if (stream.format)
        avformat_close_input(&stream.format);
    stream = Stream();
}

bool VideoPlayer::openStream(Stream& stream, const QString& path)
{
    closeStream(stream);
    stream.path = path;

    const QByteArray utf8 = path.toUtf8();
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, utf8.constData(), nullptr, nullptr) < 0)
        return false;
    stream.format = format;

    if (avformat_find_stream_info(format, nullptr) < 0)
    {
        closeStream(stream);
        return false;
    }

    const int index =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0)
    {
        closeStream(stream);
        return false;
    }
    stream.streamIndex = index;
    stream.timeBase = format->streams[index]->time_base;

    AVRational rate = format->streams[index]->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        rate = format->streams[index]->r_frame_rate;
    if (rate.num > 0 && rate.den > 0)
        stream.frameDuration = rate.den / double(rate.num);
    else
        stream.frameDuration = 1.0 / 30.0;

    const AVCodecParameters* parameters = format->streams[index]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
    if (!decoder)
    {
        closeStream(stream);
        return false;
    }
    stream.codec = avcodec_alloc_context3(decoder);
    if (!stream.codec
        || avcodec_parameters_to_context(stream.codec, parameters) < 0
        || avcodec_open2(stream.codec, decoder, nullptr) < 0)
    {
        closeStream(stream);
        return false;
    }

    stream.packet = av_packet_alloc();
    stream.frame = av_frame_alloc();
    stream.candidate = av_frame_alloc();
    if (!stream.packet || !stream.frame || !stream.candidate)
    {
        closeStream(stream);
        return false;
    }

    stream.duration =
        format->duration > 0 ? format->duration / double(AV_TIME_BASE) : 0.0;
    return true;
}

QImage VideoPlayer::frameAt(Stream& stream, double seconds, bool* changed)
{
    *changed = false;
    if (!stream.format || !stream.codec)
        return QImage();

    seconds = std::max(0.0, seconds);
    if (stream.haveDecoded && std::abs(seconds - stream.lastRequested) < 1e-6)
        return stream.image;

    // When the project fps is higher than the video fps, several play ticks
    // fall between two decoded frames. Keep the current frame until its
    // display interval ends instead of seeking backwards for each tick.
    if (stream.haveDecoded
        && seconds < stream.lastDecodedSeconds + stream.frameDuration - 1e-4
        && seconds > stream.lastDecodedSeconds - stream.frameDuration - 1e-4)
    {
        return stream.image;
    }

    const bool backwards =
        stream.haveDecoded && seconds + 1e-4 < stream.lastDecodedSeconds;
    const bool farForward =
        stream.haveDecoded && seconds > stream.lastDecodedSeconds + 1.0;
    if (!stream.haveDecoded || backwards || farForward)
    {
        if (seconds > 1e-3 || stream.haveDecoded)
        {
            const int64_t timestamp =
                int64_t(seconds / std::max(1e-9, av_q2d(stream.timeBase)));
            av_packet_unref(stream.packet);
            if (av_seek_frame(stream.format, stream.streamIndex, timestamp,
                              AVSEEK_FLAG_BACKWARD)
                < 0)
            {
                return stream.image;
            }
            avcodec_flush_buffers(stream.codec);
            stream.haveDecoded = false;
            stream.sentFlush = false;
        }
    }

    av_frame_unref(stream.candidate);
    bool haveCandidate = false;
    double candidateSeconds = -1.0;

    while (true)
    {
        const int received = avcodec_receive_frame(stream.codec, stream.frame);
        if (received == 0)
        {
            double pts = frameSeconds(stream, stream.frame);
            if (pts < 0.0)
                pts = haveCandidate ? candidateSeconds : seconds;

            if (pts + 1e-6 >= seconds)
            {
                const QImage image = convertFrame(stream, stream.frame);
                stream.lastDecodedSeconds = pts;
                stream.lastRequested = seconds;
                stream.haveDecoded = true;
                if (!image.isNull())
                {
                    stream.image = image;
                    *changed = true;
                }
                return stream.image;
            }

            av_frame_unref(stream.candidate);
            av_frame_ref(stream.candidate, stream.frame);
            candidateSeconds = pts;
            haveCandidate = true;
            continue;
        }

        if (received == AVERROR(EAGAIN))
        {
            if (stream.sentFlush)
                break;

            const int read = av_read_frame(stream.format, stream.packet);
            if (read < 0)
            {
                avcodec_send_packet(stream.codec, nullptr);
                stream.sentFlush = true;
                continue;
            }
            if (stream.packet->stream_index != stream.streamIndex)
            {
                av_packet_unref(stream.packet);
                continue;
            }
            const int sent = avcodec_send_packet(stream.codec, stream.packet);
            av_packet_unref(stream.packet);
            if (sent < 0 && sent != AVERROR(EAGAIN))
                continue;
            continue;
        }

        break;   // EOF or a decoder error
    }

    // The target is at or beyond the last frame: keep the last decoded frame.
    if (haveCandidate)
    {
        const QImage image = convertFrame(stream, stream.candidate);
        stream.lastDecodedSeconds = candidateSeconds;
        stream.lastRequested = seconds;
        stream.haveDecoded = true;
        if (!image.isNull())
        {
            stream.image = image;
            *changed = true;
        }
    }
    return stream.image;
}

void VideoPlayer::update(Project& project, int frame)
{
    const double fps = std::max(1, project.fps);
    const double seconds = std::max(0.0, frame / fps);
    QSet<int> alive;

    for (Entity& entity : project.mutableEntities())
    {
        if (entity.type != EntityType::Video || entity.sourcePath.isEmpty())
            continue;
        alive.insert(entity.id);

        Stream* stream = m_streams.value(entity.id, nullptr);
        if (!stream)
        {
            stream = new Stream;
            m_streams.insert(entity.id, stream);
        }
        if (stream->sourcePath != entity.sourcePath)
        {
            const QString decodePath = videoDecodePath(entity.sourcePath);
            if (!openStream(*stream, decodePath))
            {
                // Remember the failed path so every play tick does not retry
                // opening a missing or unsupported file.
                stream->sourcePath = entity.sourcePath;
                stream->path = decodePath;
                continue;
            }
            stream->sourcePath = entity.sourcePath;
        }
        if (entity.hidden || project.isEffectivelyHidden(entity.id))
            continue;

        bool changed = false;
        const QImage image = frameAt(*stream, seconds, &changed);
        if (!image.isNull() && (changed || entity.image.isNull()))
        {
            entity.image = image;
            entity.thumbnail = QImage();
            entity.pixmap = QPixmap();
        }
    }

    for (auto it = m_streams.begin(); it != m_streams.end();)
    {
        if (!alive.contains(it.key()))
        {
            closeStream(*it.value());
            delete it.value();
            it = m_streams.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace pvf
