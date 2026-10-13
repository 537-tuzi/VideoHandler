#include "ffextractor.h"

#include "aidenoiser.h"
#include "dspdenoiser.h"
#include "fftranscoder.h"
#include "pitchshifter.h"
#include "vocalseparator.h"
#include "model/data.h"
#include "util/ffutil.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {

// 打开输入并定位指定类型流，失败时已保证关闭输入
QString open_input(const QString& inPath, AVFormatContext** fmt, AVMediaType type, int* streamIdx) {
    int ret = avformat_open_input(fmt, inPath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) return QStringLiteral("打不开文件：%1").arg(ff_err(ret));
    ret = avformat_find_stream_info(*fmt, nullptr);
    if (ret < 0) {
        avformat_close_input(fmt);
        return QStringLiteral("读不到流信息：%1").arg(ff_err(ret));
    }
    ret = av_find_best_stream(*fmt, type, -1, -1, nullptr, 0);
    if (ret < 0) {
        avformat_close_input(fmt);
        return type == AVMEDIA_TYPE_AUDIO ? QStringLiteral("文件里没有音频流")
                                          : QStringLiteral("文件里没有视频流");
    }
    *streamIdx = ret;
    return {};
}

// 建输出容器并打开文件，失败时已保证释放上下文
QString open_output(AVFormatContext** out, const QString& outPath) {
    int ret = avformat_alloc_output_context2(out, nullptr, nullptr, outPath.toUtf8().constData());
    if (ret < 0) return QStringLiteral("建输出容器失败：%1").arg(ff_err(ret));
    ret = avio_open(&(*out)->pb, outPath.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        avformat_free_context(*out);
        *out = nullptr;
        return QStringLiteral("输出文件建不了：%1").arg(ff_err(ret));
    }
    return {};
}

// 建输出流、拷编码参数并写文件头
QString write_header(AVFormatContext* in, AVFormatContext* out, int idx) {
    AVStream* inSt = in->streams[idx];
    AVStream* outSt = avformat_new_stream(out, nullptr);
    if (!outSt) return QStringLiteral("建输出流失败");
    if (avcodec_parameters_copy(outSt->codecpar, inSt->codecpar) < 0)
        return QStringLiteral("拷贝编码参数失败");
    outSt->codecpar->codec_tag = 0;
    outSt->time_base = inSt->time_base;
    int ret = avformat_write_header(out, nullptr);
    if (ret < 0) return QStringLiteral("写文件头失败：%1").arg(ff_err(ret));
    return {};
}

// 只搬指定流的包，边搬边报进度
QString copy_stream(AVFormatContext* in, AVFormatContext* out, int idx, const std::function<void(int)>& onProgress) {
    AVStream* inSt = in->streams[idx];
    AVStream* outSt = out->streams[0];// 输出容器只有这一条流，包索引须重映射到它
    double durSec = in->duration > 0 ? in->duration * av_q2d(AV_TIME_BASE_Q) : 0.0;
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return QStringLiteral("内存不够");
    QString e;
    while (true) {
        int ret = av_read_frame(in, pkt);
        if (ret < 0) break;// 读到尾或出错都收尾
        if (pkt->stream_index == idx) {
            av_packet_rescale_ts(pkt, inSt->time_base, outSt->time_base);
            pkt->stream_index = outSt->index;
            ret = av_interleaved_write_frame(out, pkt);
            if (ret < 0) {
                e = QStringLiteral("写包失败：%1").arg(ff_err(ret));
                av_packet_unref(pkt);
                break;
            }
            if (onProgress && durSec > 0.0 && pkt->pts != AV_NOPTS_VALUE) {
                double sec = pkt->pts * av_q2d(inSt->time_base);
                onProgress(static_cast<int>(qBound(0.0, 100.0 * sec / durSec, 99.0)));
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return e;
}

// 统一收尾：关输出、关输入、失败删半成品
QString finish(const QString& e, AVFormatContext** out, AVFormatContext** in, QString* outPath, QString* err) {
    if (*out) {
        avio_closep(&(*out)->pb);
        avformat_free_context(*out);
    }
    if (*in) avformat_close_input(in);
    if (!e.isEmpty()) {
        if (!outPath->isEmpty()) QFile::remove(*outPath);
        outPath->clear();
        if (err) *err = e;
    }
    return *outPath;
}

}// namespace

FFExtractor::FFExtractor(QObject* parent) : QObject(parent) {}

void FFExtractor::setCancelFlag(std::atomic<bool>* flag) {
    cancelFlag = flag;
}

bool FFExtractor::probe(const QString& inPath, QString* info) {
    AVFormatContext* in = nullptr;
    int ret = avformat_open_input(&in, inPath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        if (info) *info = QStringLiteral("打不开文件：%1").arg(ff_err(ret));
        return false;
    }
    avformat_find_stream_info(in, nullptr);// 探测失败不拦，能报多少报多少
    QStringList lines;
    lines << QStringLiteral("容器：%1").arg(QString::fromUtf8(in->iformat->long_name));
    lines << QStringLiteral("大小：%1 MB").arg(QFileInfo(inPath).size() / 1048576.0, 0, 'f', 2);
    if (in->duration > 0) {
        int sec = static_cast<int>(in->duration / AV_TIME_BASE);
        lines << QStringLiteral("时长：%1:%2:%3").arg(sec / 3600, 2, 10, QChar('0'))
                                            .arg(sec / 60 % 60, 2, 10, QChar('0'))
                                            .arg(sec % 60, 2, 10, QChar('0'));
    }
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVCodecParameters* par = in->streams[i]->codecpar;
        QString kind = par->codec_type == AVMEDIA_TYPE_VIDEO ? QStringLiteral("视频")
                     : par->codec_type == AVMEDIA_TYPE_AUDIO ? QStringLiteral("音频")
                                                             : QStringLiteral("其他");
        QString line = QStringLiteral("流#%1：%2 %3").arg(i).arg(kind, QString::fromUtf8(avcodec_get_name(par->codec_id)));
        if (par->codec_type == AVMEDIA_TYPE_VIDEO)
            line += QStringLiteral(" %1x%2").arg(par->width).arg(par->height);
        if (par->codec_type == AVMEDIA_TYPE_AUDIO)
            line += QStringLiteral(" %1Hz %2声道").arg(par->sample_rate).arg(par->ch_layout.nb_channels);
        lines << line;
    }
    avformat_close_input(&in);
    if (info) *info = lines.join(QLatin1Char('\n'));
    return true;
}

QString FFExtractor::extractAudio(const QString& inPath, QString* err, const std::function<void(int)>& onProgress) {
    QString e;
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    QString outPath;
    int idx = -1;

    do {
        e = open_input(inPath, &in, AVMEDIA_TYPE_AUDIO, &idx);
        if (!e.isEmpty()) break;
        QFileInfo fi(inPath);
        outPath = fi.absolutePath() + "/" + fi.completeBaseName() + kSuffixAudio + audio_ext(in->streams[idx]->codecpar->codec_id);
        e = open_output(&out, outPath);
        if (!e.isEmpty()) break;
        e = write_header(in, out, idx);
        if (!e.isEmpty()) break;
        e = copy_stream(in, out, idx, onProgress);
        if (!e.isEmpty()) break;
        int ret = av_write_trailer(out);
        if (ret < 0) e = QStringLiteral("写文件尾失败：%1").arg(ff_err(ret));
    } while (false);

    return finish(e, &out, &in, &outPath, err);
}

QString FFExtractor::extractVideo(const QString& inPath, QString* err, const std::function<void(int)>& onProgress) {
    QString e;
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    QString outPath;
    int idx = -1;

    do {
        e = open_input(inPath, &in, AVMEDIA_TYPE_VIDEO, &idx);
        if (!e.isEmpty()) break;
        QFileInfo fi(inPath);
        outPath = fi.absolutePath() + "/" + fi.completeBaseName() + kSuffixMute + "." + fi.suffix();
        e = open_output(&out, outPath);
        if (!e.isEmpty()) break;
        e = write_header(in, out, idx);
        if (!e.isEmpty()) break;
        e = copy_stream(in, out, idx, onProgress);
        if (!e.isEmpty()) break;
        int ret = av_write_trailer(out);
        if (ret < 0) e = QStringLiteral("写文件尾失败：%1").arg(ff_err(ret));
    } while (false);

    return finish(e, &out, &in, &outPath, err);
}

void FFExtractor::doProbe(const QString& inPath) {
    QString info;
    bool ok = probe(inPath, &info);
    emit probeDone(ok, info);
}

void FFExtractor::runJob(const QString& inPath, IStreamEngine& engine, const char* suffix, const QString& okMsg) {
    QFileInfo fi(inPath);
    const QString outPath = fi.absolutePath() + "/" + fi.completeBaseName() + suffix + ".wav";
    QString err;
    QString out = FFTranscoder::transcodeStreamWav(inPath, outPath, engine, cancelFlag, &err,
                                                   [this](int pct) { emit progress(pct); });
    if (out.isEmpty()) emit jobDone(false, err);
    else emit jobDone(true, okMsg + QStringLiteral("：%1").arg(out));
}

void FFExtractor::doDenoiseWav(const QString& inPath, bool removeHum, bool removeWind) {
    DspDenoiser denoiser;
    denoiser.setup(kCanonicalRate, kCanonicalChannels, removeHum, removeWind);
    runJob(inPath, denoiser, kSuffixDenoised, QStringLiteral("降噪完成"));
}

void FFExtractor::doDenoiseAiWav(const QString& inPath) {
    const QString model = QCoreApplication::applicationDirPath() + "/" + kAiModelRelPath;
    if (!QFileInfo::exists(model)) {
        emit jobDone(false, QStringLiteral("找不到 AI 降噪模型：%1").arg(model));
        return;
    }
    AiDenoiser ai;
    QString err;
    if (!ai.setup(model, &err)) {
        emit jobDone(false, err);
        return;
    }
    runJob(inPath, ai, kSuffixAi, QStringLiteral("AI 降噪完成"));
}

void FFExtractor::doPitchWav(const QString& inPath, double semiTones) {
    PitchShifter shifter;
    QString err;
    if (!shifter.setup(kCanonicalRate, kCanonicalChannels, semiTones, &err)) {
        emit jobDone(false, err);
        return;
    }
    runJob(inPath, shifter, kSuffixPitch, QStringLiteral("变调完成（%1 半音）").arg(semiTones));
}

void FFExtractor::doSepVocals(const QString& inPath, bool instrumental) {
    const QString model = QCoreApplication::applicationDirPath() + "/" + kSepModelRelPath;
    if (!QFileInfo::exists(model)) {
        emit jobDone(false, QStringLiteral("找不到人声分离模型：%1").arg(model));
        return;
    }
    VocalSeparator sep;
    sep.setInstrumental(instrumental);
    QString err;
    if (!sep.setup(model, &err)) {
        emit jobDone(false, err);
        return;
    }
    runJob(inPath, sep, instrumental ? kSuffixInstrumental : kSuffixVocals,
           instrumental ? QStringLiteral("伴奏提取完成") : QStringLiteral("人声分离完成"));
}

void FFExtractor::doExtractAudio(const QString& inPath) {
    QString err;
    QString out = extractAudio(inPath, &err, [this](int pct) { emit progress(pct); });
    if (out.isEmpty()) emit jobDone(false, err);
    else emit jobDone(true, QStringLiteral("音轨提取完成：%1").arg(out));
}

void FFExtractor::doExtractVideo(const QString& inPath) {
    QString err;
    QString out = extractVideo(inPath, &err, [this](int pct) { emit progress(pct); });
    if (out.isEmpty()) emit jobDone(false, err);
    else emit jobDone(true, QStringLiteral("无声视频提取完成：%1").arg(out));
}
