#include "DecoderInfo.h"

#include <QtGlobal>

extern "C" {
#include <libavutil/avutil.h>
}

namespace pvf {

DecoderInfo decoderInfo()
{
    DecoderInfo info;
    info.ffmpegVersion = QString::fromLatin1(av_version_info());
    info.video = QStringLiteral("FFmpeg %1 · 软件解码（视频首帧）")
                     .arg(info.ffmpegVersion);
    info.audioDecode = QStringLiteral("FFmpeg %1 + libswresample · 软件解码")
                           .arg(info.ffmpegVersion);
    info.audioOutput = QStringLiteral("Qt Multimedia %1 · FFmpeg 音频后端")
                           .arg(QString::fromLatin1(qVersion()));
    info.hardware =
        QStringLiteral("硬件视频解码未启用（当前只解码视频首帧，软件路径足够）");
    return info;
}

QString DecoderInfo::toText() const
{
    return QStringLiteral("视频解码：%1\n音频解码：%2\n音频播放：%3\n硬件加速：%4")
        .arg(video, audioDecode, audioOutput, hardware);
}

} // namespace pvf
