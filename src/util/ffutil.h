#pragma once

#include <QString>

extern "C" {
#include <libavcodec/codec_id.h>
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
