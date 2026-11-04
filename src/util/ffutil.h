#pragma once

#include <QString>

#include "service/errinfo.h"

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

// 编解码器 → 音频容器扩展名映射
inline const char* audio_ext(AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_AAC:    return ".m4a";
        case AV_CODEC_ID_MP3:    return ".mp3";
        case AV_CODEC_ID_AC3:    return ".ac3";
        case AV_CODEC_ID_EAC3:   return ".eac3";
        case AV_CODEC_ID_FLAC:   return ".flac";
        case AV_CODEC_ID_OPUS:   return ".opus";
        case AV_CODEC_ID_VORBIS: return ".ogg";
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S24LE:
        case AV_CODEC_ID_PCM_S32LE:
        case AV_CODEC_ID_PCM_F32LE:
        case AV_CODEC_ID_PCM_U8: return ".wav";
        default:                 return ".mka";
    }
}

// FFmpeg 错误码 → 可读字符串
inline QString ff_err(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// 统一音频选轨（FR-1 D7 单源，fftranscoder/ffextractor 三选轨点共用）：track<0 → av_find_best_stream
// 原式（缺省路径逐字节现状，调用方以返回值 <0 判「文件里没有音频流」）；track≥0 → 校验后直取容器内
// 音频流绝对流索引，越界/非音频流在产生点赋 E_FILE（与「文件里没有音频流」同域的文件结构事实，§7.1 拍板 B OQ-1）
inline int resolveAudioStream(AVFormatContext* fmt, int track, ErrInfo* err) {
    if (track < 0) return av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (track >= static_cast<int>(fmt->nb_streams)) {
        if (err) *err = {ErrCode::File, QStringLiteral("track %1 超出流范围（共 %2 条流）")
                                      .arg(track).arg(static_cast<int>(fmt->nb_streams))};
        return -1;
    }
    if (fmt->streams[track]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        if (err) *err = {ErrCode::File, QStringLiteral("track %1 不是音频流").arg(track)};
        return -1;
    }
    return track;
}
