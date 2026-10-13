#include "fftranscoder.h"

#include "streamengine.h"
#include "model/data.h"
#include "util/ffutil.h"

#include <QFile>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace {

constexpr int kFlushFrames = 8192;// 冲刷残样时的缓冲帧数

// 打开输入、定位音轨并起解码器，失败时已保证释放
QString open_decoder(const QString& inPath, AVFormatContext** fmt, AVCodecContext** dec, int* streamIdx) {
    int ret = avformat_open_input(fmt, inPath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) return QStringLiteral("打不开文件：%1").arg(ff_err(ret));
    ret = avformat_find_stream_info(*fmt, nullptr);
    if (ret < 0) {
        avformat_close_input(fmt);
        return QStringLiteral("读不到流信息：%1").arg(ff_err(ret));
    }
    ret = av_find_best_stream(*fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (ret < 0) {
        avformat_close_input(fmt);
        return QStringLiteral("文件里没有音频流");
    }
    *streamIdx = ret;
    const AVCodec* codec = avcodec_find_decoder((*fmt)->streams[*streamIdx]->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(fmt);
        return QStringLiteral("不认识的音频编码");
    }
    *dec = avcodec_alloc_context3(codec);
    if (!*dec) {
        avformat_close_input(fmt);
        return QStringLiteral("内存不够");
    }
    if (avcodec_parameters_to_context(*dec, (*fmt)->streams[*streamIdx]->codecpar) < 0 ||
        avcodec_open2(*dec, codec, nullptr) < 0) {
        avcodec_free_context(dec);
        avformat_close_input(fmt);
        return QStringLiteral("解码器起不来");
    }
    if ((*dec)->ch_layout.nb_channels <= 0)// 怪文件缺声道描述时按统一声道数猜
        av_channel_layout_default(&(*dec)->ch_layout, kCanonicalChannels);
    return {};
}

// 建 WAV 输出容器并写文件头，失败时已保证释放
QString open_wav(const QString& outPath, AVFormatContext** out) {
    int ret = avformat_alloc_output_context2(out, nullptr, nullptr, outPath.toUtf8().constData());
    if (ret < 0) return QStringLiteral("建输出容器失败：%1").arg(ff_err(ret));
    AVStream* st = avformat_new_stream(*out, nullptr);
    if (!st) {
        avformat_free_context(*out);
        *out = nullptr;
        return QStringLiteral("建输出流失败");
    }
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_PCM_F32LE;
    st->codecpar->format = AV_SAMPLE_FMT_FLT;
    st->codecpar->sample_rate = kCanonicalRate;
    av_channel_layout_default(&st->codecpar->ch_layout, kCanonicalChannels);
    st->codecpar->codec_tag = 0;
    st->time_base = av_make_q(1, kCanonicalRate);
    ret = avio_open(&(*out)->pb, outPath.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        avformat_free_context(*out);
        *out = nullptr;
        return QStringLiteral("输出文件建不了：%1").arg(ff_err(ret));
    }
    ret = avformat_write_header(*out, nullptr);
    if (ret < 0) {
        avio_closep(&(*out)->pb);
        avformat_free_context(*out);
        *out = nullptr;
        return QStringLiteral("写文件头失败：%1").arg(ff_err(ret));
    }
    return {};
}

// 写一段交错 f32 样本进输出容器，pkt 由调用方持有复用
QString write_samples(const uint8_t* data, int frames, AVFormatContext* out, int64_t* samplePos, AVPacket* pkt) {
    if (frames <= 0) return {};
    if (av_new_packet(pkt, frames * kCanonicalChannels * static_cast<int>(sizeof(float))) < 0)
        return QStringLiteral("内存不够");
    QString e;
    memcpy(pkt->data, data, static_cast<size_t>(pkt->size));
    pkt->pts = pkt->dts = *samplePos;
    pkt->duration = frames;
    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    if (av_interleaved_write_frame(out, pkt) < 0) e = QStringLiteral("写包失败");
    *samplePos += frames;
    av_packet_unref(pkt);
    return e;
}

}// namespace

QString FFTranscoder::transcodeStreamWav(const QString& inPath, const QString& outPath,
                                         IStreamEngine& engine, const std::atomic<bool>* cancel,
                                         QString* err, const ProgressFn& onProgress) {
    QString e;
    AVFormatContext* in = nullptr;
    AVCodecContext* dec = nullptr;
    AVFormatContext* out = nullptr;
    SwrContext* toAi = nullptr;// 输入 → 引擎期望规格
    SwrContext* toOut = nullptr;// 引擎输出 → 44.1kHz 立体声（规格已一致时省去）
    int idx = -1;
    int64_t samplePos = 0;
    const int ch = engine.inChannels();
    const bool bypassOut = engine.inRate() == kCanonicalRate && ch == kCanonicalChannels;

    do {
        e = open_decoder(inPath, &in, &dec, &idx);
        if (!e.isEmpty()) break;
        e = open_wav(outPath, &out);
        if (!e.isEmpty()) break;
        AVChannelLayout aiLayout;
        av_channel_layout_default(&aiLayout, ch);
        int ret = swr_alloc_set_opts2(&toAi, &aiLayout, AV_SAMPLE_FMT_FLT, engine.inRate(),
                                      &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0, nullptr);
        if (ret < 0 || swr_init(toAi) < 0) {
            e = QStringLiteral("重采样器起不来");
            break;
        }
        if (!bypassOut) {
            AVChannelLayout stLayout = AV_CHANNEL_LAYOUT_STEREO;
            ret = swr_alloc_set_opts2(&toOut, &stLayout, AV_SAMPLE_FMT_FLT, kCanonicalRate,
                                      &aiLayout, AV_SAMPLE_FMT_FLT, engine.inRate(), 0, nullptr);
            if (ret < 0 || swr_init(toOut) < 0) {
                e = QStringLiteral("重采样器起不来");
                break;
            }
        }

        AVPacket* pkt = av_packet_alloc();
        AVPacket* wpkt = av_packet_alloc();// 写盘复用包
        AVFrame* frame = av_frame_alloc();
        std::vector<float> aiIn, convIn, convOut, enhanced;
        size_t hPos = 0;
        if (!pkt || !wpkt || !frame) e = QStringLiteral("内存不够");
        else {
            const double estFrames = in->duration > 0 ? in->duration / 1000000.0 * engine.inRate() : 0.0;
            const int chunk = engine.inRate();// 按秒分块送引擎
            int64_t fed = 0;
            auto drainEnhanced = [&]() -> QString {// 引擎输出 → 直写/回转 → 写盘
                while (enhanced.size() - hPos >= static_cast<size_t>(ch)) {
                    const int n = static_cast<int>(std::min<size_t>((enhanced.size() - hPos) / ch, kFlushFrames));
                    const float* src = enhanced.data() + hPos;
                    if (bypassOut) {
                        e = write_samples(reinterpret_cast<const uint8_t*>(src), n, out, &samplePos, wpkt);
                        if (!e.isEmpty()) break;
                    } else {
                        const int maxOut = swr_get_out_samples(toOut, n);
                        if (maxOut > 0) {
                            convOut.resize(static_cast<size_t>(maxOut) * kCanonicalChannels);
                            uint8_t* outPtr = reinterpret_cast<uint8_t*>(convOut.data());
                            uint8_t* inPtr = reinterpret_cast<uint8_t*>(const_cast<float*>(src));
                            const int got = swr_convert(toOut, &outPtr, maxOut,
                                                        const_cast<const uint8_t**>(&inPtr), n);
                            if (got > 0) {
                                e = write_samples(reinterpret_cast<const uint8_t*>(convOut.data()),
                                                  got, out, &samplePos, wpkt);
                                if (!e.isEmpty()) break;
                            }
                        }
                    }
                    hPos += static_cast<size_t>(n) * ch;
                }
                enhanced.erase(enhanced.begin(), enhanced.begin() + static_cast<long>(hPos));// 压实消费前缀
                hPos = 0;
                return e;
            };
            auto feedAi = [&](AVFrame* f) -> QString {// 解码帧 → 引擎期望规格 → 送引擎
                const int maxOut = swr_get_out_samples(toAi, f->nb_samples);
                if (maxOut <= 0) return {};
                convIn.resize(static_cast<size_t>(maxOut) * ch);
                uint8_t* outPtr = reinterpret_cast<uint8_t*>(convIn.data());
                const int got = swr_convert(toAi, &outPtr, maxOut,
                                            const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
                aiIn.insert(aiIn.end(), convIn.begin(), convIn.begin() + std::max(got, 0) * ch);
                fed += std::max(got, 0);
                if (static_cast<int>(aiIn.size()) >= chunk * ch) {
                    auto aiOut = engine.process(aiIn.data(), static_cast<int64_t>(aiIn.size()) / ch);
                    enhanced.insert(enhanced.end(), aiOut.begin(), aiOut.end());
                    aiIn.clear();
                    if (!engine.lastError().isEmpty()) return engine.lastError();
                    if (onProgress && estFrames > 0.0)
                        onProgress(static_cast<int>(qBound(0.0, 100.0 * fed / estFrames, 99.0)));
                }
                return {};
            };

            while (e.isEmpty()) {
                if (cancel && cancel->load()) {// 分块粒度的取消检查点
                    e = QStringLiteral("已取消");
                    break;
                }
                ret = av_read_frame(in, pkt);
                if (ret < 0) break;// 读到尾或出错都收尾
                if (pkt->stream_index == idx) {
                    ret = avcodec_send_packet(dec, pkt);
                    if (ret < 0 && ret != AVERROR(EAGAIN)) {
                        av_packet_unref(pkt);
                        e = QStringLiteral("送包解码失败：%1").arg(ff_err(ret));
                        break;
                    }
                    while (e.isEmpty()) {
                        ret = avcodec_receive_frame(dec, frame);
                        if (ret < 0) break;// EAGAIN 或解码尾
                        e = feedAi(frame);
                        av_frame_unref(frame);
                    }
                }
                av_packet_unref(pkt);
            }
            if (e.isEmpty()) {// 冲刷解码器尾帧
                avcodec_send_packet(dec, nullptr);
                while (e.isEmpty()) {
                    ret = avcodec_receive_frame(dec, frame);
                    if (ret < 0) break;
                    e = feedAi(frame);
                    av_frame_unref(frame);
                }
            }
            if (e.isEmpty()) {// 冲刷重采样残样
                const int maxOut = swr_get_out_samples(toAi, 1);
                convIn.resize(static_cast<size_t>(std::max(maxOut, 1)) * ch);
                while (e.isEmpty()) {
                    uint8_t* outPtr = reinterpret_cast<uint8_t*>(convIn.data());
                    const int got = swr_convert(toAi, &outPtr, maxOut, nullptr, 0);
                    if (got <= 0) break;
                    aiIn.insert(aiIn.end(), convIn.begin(), convIn.begin() + got * ch);
                    fed += got;
                }
            }
            if (e.isEmpty()) {// 尾块喂引擎后冲刷流式引擎
                if (!aiIn.empty()) {
                    auto aiOut = engine.process(aiIn.data(), static_cast<int64_t>(aiIn.size()) / ch);
                    enhanced.insert(enhanced.end(), aiOut.begin(), aiOut.end());
                    aiIn.clear();
                }
                auto tail = engine.flush();
                enhanced.insert(enhanced.end(), tail.begin(), tail.end());
                if (!engine.lastError().isEmpty()) e = engine.lastError();
                else e = drainEnhanced();
            }
            if (e.isEmpty() && toOut) {// 冲刷回转重采样残样
                const int maxOut = swr_get_out_samples(toOut, 1);
                convOut.resize(static_cast<size_t>(std::max(maxOut, 1)) * kCanonicalChannels);
                while (e.isEmpty()) {
                    uint8_t* outPtr = reinterpret_cast<uint8_t*>(convOut.data());
                    const int got = swr_convert(toOut, &outPtr, maxOut, nullptr, 0);
                    if (got <= 0) break;
                    e = write_samples(reinterpret_cast<const uint8_t*>(convOut.data()), got, out, &samplePos, wpkt);
                }
            }
            if (e.isEmpty() && onProgress) onProgress(100);
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
        av_packet_free(&wpkt);
        if (!e.isEmpty()) break;
        ret = av_write_trailer(out);
        if (ret < 0) e = QStringLiteral("写文件尾失败：%1").arg(ff_err(ret));
    } while (false);

    if (toOut) swr_free(&toOut);
    if (toAi) swr_free(&toAi);
    if (dec) avcodec_free_context(&dec);
    if (out) {
        avio_closep(&out->pb);
        avformat_free_context(out);
    }
    if (in) avformat_close_input(&in);
    if (!e.isEmpty()) {
        QFile::remove(outPath);
        if (err) *err = e;
        return {};
    }
    return outPath;
}
