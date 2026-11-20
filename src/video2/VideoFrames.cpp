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

// How far forward the decoder may walk from the last displayed frame before a
// fresh seek is cheaper than decoding the gap.
constexpr double kMaxForwardGapSeconds = 2.0;

struct Input
{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;    // scratch frame used while draining
    AVFrame* displayed = nullptr; // last frame with timestamp <= target
    AVFrame* pending = nullptr;   // first frame with timestamp > target
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
        if (pending)
            av_frame_free(&pending);
        if (displayed)
            av_frame_free(&displayed);
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
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const int openStatus =
        avformat_open_input(&input.format, utf8.constData(), nullptr, &options);
    av_dict_free(&options);
    if (openStatus < 0)
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
    input.displayed = av_frame_alloc();
    input.pending = av_frame_alloc();
    if (!input.packet || !input.frame || !input.displayed || !input.pending)
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

// YUV -> RGB matrix for the source.  Unspecified streams follow the SD/HD
// convention FFmpeg itself uses, so phone footage tagged BT.709 does not get
// the BT.601 matrix (issue #131).
int sourceColorspace(const AVFrame* frame)
{
    switch (frame->colorspace)
    {
    case AVCOL_SPC_BT709:
        return SWS_CS_ITU709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return SWS_CS_ITU601;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return SWS_CS_BT2020;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    default:
        break;
    }
    return frame->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
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

    // Colour conversion follows the stream's own matrix and range: full-range
    // phone footage stays full-range instead of washing out, and BT.709 HD
    // stays BT.709.  RGBA output is full range.
    const int colorspace = sourceColorspace(frame);
    const int* sourceTable = sws_getCoefficients(colorspace);
    const int* targetTable = sws_getCoefficients(SWS_CS_ITU709);
    const int sourceRange =
        frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    sws_setColorspaceDetails(sws, sourceTable, sourceRange, targetTable, 1, 0,
                             1 << 16, 1 << 16);

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

} // namespace

// One open container/decoder that survives between requests.  `displayed`
// holds the frame at or before the last requested time and `pending` the first
// frame after it, so consecutive requests decode strictly forward.
struct VideoFrames::Decoder
{
    Input input;
    QString path;
    int fps = 0;
    bool eof = false;
    bool haveDisplayed = false;
    bool havePending = false;
    int64_t displayedTimestamp = 0;
    int64_t pendingTimestamp = 0;
    int64_t lastTimestamp = 0;
    bool started = false;

    bool isOpenFor(const QString& otherPath, int otherFps) const
    {
        return input.format != nullptr && path == otherPath && fps == otherFps;
    }

    void reset()
    {
        input.close();
        path.clear();
        fps = 0;
        eof = false;
        haveDisplayed = false;
        havePending = false;
        displayedTimestamp = 0;
        pendingTimestamp = 0;
        lastTimestamp = 0;
    }

    double secondsOf(int64_t timestamp) const
    {
        return (timestamp - input.startTime) * av_q2d(input.timeBase);
    }
};

VideoFrames::VideoFrames(int proxyMaxWidth, int memoryEntries)
    : m_proxyMaxWidth(proxyMaxWidth > 0 ? proxyMaxWidth : 960)
    , m_decoder(std::make_unique<Decoder>())
{
    m_cache.setMaxCost(std::max(0, memoryEntries));
}

VideoFrames::~VideoFrames() = default;

QString VideoFrames::cacheKey(const Request& request) const
{
    return QStringList{
        request.path,
        QString::number(request.sourceFrame, 'g', 17),
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

    Decoder& decoder = *m_decoder;
    if (!decoder.isOpenFor(request.path, request.fps))
    {
        decoder.reset();
        if (!openInput(decoder.input, request.path, error))
            return QImage();
        decoder.path = request.path;
        decoder.fps = request.fps;
        ++m_stats.opens;
    }
    Input& input = decoder.input;

    // Target timestamp in the stream time base.  The fractional part is kept:
    // 2x/4x export sampling asks for times between project frames (issue #131).
    const double sourceFrame = std::max(0.0, request.sourceFrame);
    const int64_t targetTimestamp =
        av_rescale_q_rnd(int64_t(std::llround(sourceFrame * 1000000.0)),
                         AVRational{1, request.fps * 1000000},
                         input.timeBase, AV_ROUND_DOWN)
        + input.startTime;

    // Pending is the first frame after the previous target; it may already be
    // the answer for this one.
    if (decoder.havePending && decoder.pendingTimestamp <= targetTimestamp)
    {
        av_frame_unref(input.displayed);
        if (av_frame_ref(input.displayed, input.pending) == 0)
        {
            decoder.haveDisplayed = true;
            decoder.displayedTimestamp = decoder.pendingTimestamp;
        }
        decoder.havePending = false;
    }

    const double targetSeconds =
        sourceFrame / double(request.fps);
    const bool needSeek =
        !decoder.haveDisplayed
        || targetSeconds < decoder.secondsOf(decoder.displayedTimestamp)
        || targetSeconds - decoder.secondsOf(decoder.displayedTimestamp)
               > kMaxForwardGapSeconds;

    if (needSeek)
    {
        decoder.haveDisplayed = false;
        decoder.havePending = false;
        decoder.eof = false;
        const bool atStart = targetTimestamp <= input.startTime;
        if (!atStart)
        {
            if (av_seek_frame(input.format, input.streamIndex, targetTimestamp,
                              AVSEEK_FLAG_BACKWARD) < 0)
            {
                fail(input, error, QStringLiteral("无法定位视频帧"));
                decoder.reset();
                return QImage();
            }
            ++m_stats.seeks;
            avcodec_flush_buffers(input.codec);
        }
        else if (decoder.started)
        {
            // Rewind to the first frame after a forward walk.  Some containers
            // reject a seek to time 0, so a failed rewind re-opens the file.
            if (av_seek_frame(input.format, input.streamIndex, input.startTime,
                              AVSEEK_FLAG_BACKWARD)
                < 0)
            {
                const QString path = decoder.path;
                const int fps = decoder.fps;
                decoder.reset();
                QString reopenError;
                if (!openInput(decoder.input, path, &reopenError))
                {
                    if (error)
                        *error = reopenError;
                    return QImage();
                }
                decoder.path = path;
                decoder.fps = fps;
                ++m_stats.opens;
            }
            else
            {
                ++m_stats.seeks;
                avcodec_flush_buffers(input.codec);
            }
        }
        decoder.started = false;
    }

    const int64_t fallbackStep = std::max<int64_t>(
        1, av_rescale_q(int64_t(input.frameDuration * 1000000.0),
                        AVRational{1, 1000000}, input.timeBase));

    int64_t timestamp = AV_NOPTS_VALUE;
    auto assign = [&](AVFrame* target, AVFrame* source, int64_t ts) {
        av_frame_unref(target);
        if (av_frame_ref(target, source) < 0)
            return false;
        if (target == input.displayed)
        {
            decoder.haveDisplayed = true;
            decoder.displayedTimestamp = ts;
        }
        else
        {
            decoder.havePending = true;
            decoder.pendingTimestamp = ts;
        }
        return true;
    };

    auto accept = [&](AVFrame* frame) {
        int64_t frameTimestamp = timestampOf(frame);
        if (frameTimestamp == AV_NOPTS_VALUE)
        {
            frameTimestamp = timestamp == AV_NOPTS_VALUE
                                 ? targetTimestamp
                                 : timestamp + fallbackStep;
        }
        timestamp = frameTimestamp;
        decoder.lastTimestamp = frameTimestamp;
        decoder.started = true;

        if (frameTimestamp > targetTimestamp)
        {
            // Keep the first frame past the target for the next request.
            if (!assign(input.pending, frame, frameTimestamp))
                decoder.havePending = false;
            return true; // stop decoding
        }
        if (!assign(input.displayed, frame, frameTimestamp))
            decoder.haveDisplayed = false;
        return false;
    };

    while (!decoder.havePending && !decoder.eof)
    {
        const int received = avcodec_receive_frame(input.codec, input.frame);
        if (received == 0)
        {
            const bool done = accept(input.frame);
            av_frame_unref(input.frame);
            if (done)
                break;
            continue;
        }

        if (received != AVERROR(EAGAIN))
        {
            decoder.eof = true;
            break;
        }

        while (true)
        {
            const int read = av_read_frame(input.format, input.packet);
            if (read < 0)
            {
                avcodec_send_packet(input.codec, nullptr);
                decoder.eof = true;
                break;
            }
            if (input.packet->stream_index != input.streamIndex)
            {
                av_packet_unref(input.packet);
                continue;
            }

            const int sent = avcodec_send_packet(input.codec, input.packet);
            av_packet_unref(input.packet);
            if (sent == AVERROR(EAGAIN))
                break; // drain the decoder before feeding again
            break;     // packet accepted or corrupt: move on
        }
    }

    if (!decoder.haveDisplayed)
    {
        if (error)
            *error = QStringLiteral("未能解码出视频帧");
        return QImage();
    }

    ++m_stats.decodes;
    const QImage image = convert(input.displayed, request, m_proxyMaxWidth, error);
    if (image.isNull())
        return QImage();

    if (m_cache.maxCost() > 0)
        m_cache.insert(key, new QImage(image));
    return image;
}

void VideoFrames::clear()
{
    m_cache.clear();
    if (m_decoder)
        m_decoder->reset();
    m_stats = Stats();
}

VideoFrames::Stats VideoFrames::stats() const
{
    return m_stats;
}

} // namespace pvf::video2
