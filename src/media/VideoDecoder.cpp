#include "VideoDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace pvf {

QImage decodeVideoFirstFrame(const QString& path, QString* error, double* duration)
{
    const QByteArray utf8 = path.toUtf8();
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* sws = nullptr;
    QImage result;

    auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        if (sws)
            sws_freeContext(sws);
        if (frame)
            av_frame_free(&frame);
        if (packet)
            av_packet_free(&packet);
        if (codec)
            avcodec_free_context(&codec);
        if (format)
            avformat_close_input(&format);
        return QImage();
    };

    if (avformat_open_input(&format, utf8.constData(), nullptr, nullptr) < 0)
        return fail(QStringLiteral("无法打开视频文件"));

    if (avformat_find_stream_info(format, nullptr) < 0)
        return fail(QStringLiteral("无法读取视频信息"));

    if (duration)
    {
        if (format->duration > 0)
            *duration = format->duration / double(AV_TIME_BASE);
        else
            *duration = 0.0;
    }

    const int streamIndex =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0)
        return fail(QStringLiteral("文件中没有视频流"));

    const AVCodecParameters* params = format->streams[streamIndex]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(params->codec_id);
    if (!decoder)
        return fail(QStringLiteral("找不到视频解码器"));

    codec = avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec, params) < 0
        || avcodec_open2(codec, decoder, nullptr) < 0)
        return fail(QStringLiteral("无法初始化解码器"));

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame)
        return fail(QStringLiteral("内存不足"));

    while (av_read_frame(format, packet) >= 0)
    {
        if (packet->stream_index != streamIndex)
        {
            av_packet_unref(packet);
            continue;
        }

        const int sent = avcodec_send_packet(codec, packet);
        av_packet_unref(packet);
        if (sent < 0)
            continue;

        if (avcodec_receive_frame(codec, frame) == 0)
        {
            sws = sws_getContext(frame->width, frame->height,
                                 static_cast<AVPixelFormat>(frame->format), frame->width,
                                 frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                 nullptr, nullptr);
            if (!sws)
                return fail(QStringLiteral("无法转换视频帧"));

            result = QImage(frame->width, frame->height, QImage::Format_RGBA8888);
            uint8_t* dstData[4] = {result.bits(), nullptr, nullptr, nullptr};
            int dstLinesize[4] = {int(result.bytesPerLine()), 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dstData,
                      dstLinesize);
            break;
        }
    }

    if (result.isNull())
        return fail(QStringLiteral("未能解码出视频帧"));

    if (sws)
        sws_freeContext(sws);
    if (frame)
        av_frame_free(&frame);
    if (packet)
        av_packet_free(&packet);
    if (codec)
        avcodec_free_context(&codec);
    if (format)
        avformat_close_input(&format);
    return result;
}

} // namespace pvf
