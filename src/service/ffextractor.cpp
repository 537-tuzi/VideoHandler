#include "ffextractor.h"

#include "commandlayer.h"
#include "dspdenoiser.h"// HumBase 制式枚举（槽参数 → 请求字段映射）
#include "model/data.h"
#include "util/ffutil.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QStringList>

#include <exception>// std::exception，runRequest 兜底 catch 用

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

// 只搬指定流的包，边搬边报进度；cancel 为包粒度取消检查点（CLI 传空）
QString copy_stream(AVFormatContext* in, AVFormatContext* out, int idx, const std::function<void(int)>& onProgress,
                    const std::atomic<bool>* cancel) {
    AVStream* inSt = in->streams[idx];
    AVStream* outSt = out->streams[0];// 输出容器只有这一条流，包索引须重映射到它
    double durSec = in->duration > 0 ? in->duration * av_q2d(AV_TIME_BASE_Q) : 0.0;
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return QStringLiteral("内存不够");
    QString e;
    while (true) {
        if (cancel && cancel->load()) {
            e = QStringLiteral("已取消");
            break;
        }
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

bool FFExtractor::probeInfo(const QString& inPath, ProbeInfo* info, QString* err) {
    AVFormatContext* in = nullptr;
    int ret = avformat_open_input(&in, inPath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        if (err) *err = QStringLiteral("打不开文件：%1").arg(ff_err(ret));
        return false;
    }
    avformat_find_stream_info(in, nullptr);// 探测失败不拦，能报多少报多少
    if (info) {
        info->container = QString::fromUtf8(in->iformat->long_name);
        info->sizeBytes = QFileInfo(inPath).size();
        info->durationUs = in->duration;
        info->streams.clear();
        for (unsigned i = 0; i < in->nb_streams; ++i) {
            const AVCodecParameters* par = in->streams[i]->codecpar;
            ProbeStream s;
            s.index = static_cast<int>(i);
            s.mediaType = par->codec_type == AVMEDIA_TYPE_VIDEO ? QStringLiteral("video")
                        : par->codec_type == AVMEDIA_TYPE_AUDIO ? QStringLiteral("audio")
                                                                : QStringLiteral("other");
            s.codec = QString::fromUtf8(avcodec_get_name(par->codec_id));
            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                s.width = par->width;
                s.height = par->height;
            }
            if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
                s.sampleRate = par->sample_rate;
                s.channels = par->ch_layout.nb_channels;
            }
            info->streams.append(s);
        }
    }
    avformat_close_input(&in);
    return true;
}

QString FFExtractor::probeText(const ProbeInfo& info) {
    QStringList lines;
    lines << QStringLiteral("容器：%1").arg(info.container);
    lines << QStringLiteral("大小：%1 MB").arg(info.sizeBytes / 1048576.0, 0, 'f', 2);
    if (info.durationUs > 0) {
        int sec = static_cast<int>(info.durationUs / AV_TIME_BASE);
        lines << QStringLiteral("时长：%1:%2:%3").arg(sec / 3600, 2, 10, QChar('0'))
                                            .arg(sec / 60 % 60, 2, 10, QChar('0'))
                                            .arg(sec % 60, 2, 10, QChar('0'));
    }
    for (const ProbeStream& s : info.streams) {
        QString kind = s.mediaType == QStringLiteral("video") ? QStringLiteral("视频")
                     : s.mediaType == QStringLiteral("audio") ? QStringLiteral("音频")
                                                              : QStringLiteral("其他");
        QString line = QStringLiteral("流#%1：%2 %3").arg(s.index).arg(kind, s.codec);
        if (s.mediaType == QStringLiteral("video"))
            line += QStringLiteral(" %1x%2").arg(s.width).arg(s.height);
        if (s.mediaType == QStringLiteral("audio"))
            line += QStringLiteral(" %1Hz %2声道").arg(s.sampleRate).arg(s.channels);
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

bool FFExtractor::probe(const QString& inPath, QString* info) {
    ProbeInfo pi;
    QString err;
    if (!probeInfo(inPath, &pi, &err)) {
        if (info) *info = err;
        return false;
    }
    if (info) *info = probeText(pi);
    return true;
}

QString FFExtractor::extractAudio(const QString& inPath, QString* err, const std::function<void(int)>& onProgress,
                                  const std::atomic<bool>* cancel, const QString& outPath) {
    QString e;
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    QString path;
    int idx = -1;

    do {
        e = open_input(inPath, &in, AVMEDIA_TYPE_AUDIO, &idx);
        if (!e.isEmpty()) break;
        QFileInfo fi(inPath);
        // 显式路径在输入成功打开后才落位：失败路径绝不误删既有文件
        if (!outPath.isEmpty()) path = outPath;
        else path = fi.absolutePath() + "/" + fi.completeBaseName() + kSuffixAudio + audio_ext(in->streams[idx]->codecpar->codec_id);
        e = open_output(&out, path);
        if (!e.isEmpty()) break;
        e = write_header(in, out, idx);
        if (!e.isEmpty()) break;
        e = copy_stream(in, out, idx, onProgress, cancel);
        if (!e.isEmpty()) break;
        int ret = av_write_trailer(out);
        if (ret < 0) e = QStringLiteral("写文件尾失败：%1").arg(ff_err(ret));
    } while (false);

    return finish(e, &out, &in, &path, err);
}

QString FFExtractor::extractVideo(const QString& inPath, QString* err, const std::function<void(int)>& onProgress,
                                  const std::atomic<bool>* cancel, const QString& outPath) {
    QString e;
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    QString path;
    int idx = -1;

    do {
        e = open_input(inPath, &in, AVMEDIA_TYPE_VIDEO, &idx);
        if (!e.isEmpty()) break;
        QFileInfo fi(inPath);
        // 显式路径在输入成功打开后才落位：失败路径绝不误删既有文件
        if (!outPath.isEmpty()) path = outPath;
        else path = fi.absolutePath() + "/" + fi.completeBaseName() + kSuffixMute + "." + fi.suffix();
        e = open_output(&out, path);
        if (!e.isEmpty()) break;
        e = write_header(in, out, idx);
        if (!e.isEmpty()) break;
        e = copy_stream(in, out, idx, onProgress, cancel);
        if (!e.isEmpty()) break;
        int ret = av_write_trailer(out);
        if (ret < 0) e = QStringLiteral("写文件尾失败：%1").arg(ff_err(ret));
    } while (false);

    return finish(e, &out, &in, &path, err);
}

void FFExtractor::doProbe(const QString& inPath) {
    QString info;
    bool ok = probe(inPath, &info);
    emit probeDone(ok, info);
}

// 统一命令层收尾（FR-24 片 D，spec D6）：组好的请求交 execute，取消标志/进度回调经 Ctx 注入；
// 兜底 catch 与命令层 E_INTERNAL 双保险——execute 契约不抛出，此处防其本身违约抛出穿透 worker 槽
void FFExtractor::runRequest(const QJsonObject& req, const QString& okMsg) {
    CommandLayer::Ctx ctx;
    ctx.cancel = cancelFlag;
    ctx.onProgress = [this](int pct) { emit progress(pct); };
    QJsonObject resp;
    try {
        resp = CommandLayer::execute(req, ctx);
    } catch (const std::exception& e) {
        emit jobDone(false, QStringLiteral("任务异常终止：%1").arg(QString::fromUtf8(e.what())));
        return;
    } catch (...) {
        emit jobDone(false, QStringLiteral("任务异常终止（未知异常）"));
        return;
    }
    if (resp.value(QStringLiteral("ok")).toBool()) {
        const QString out = resp.value(QStringLiteral("result")).toObject()
                                .value(QStringLiteral("output")).toString();
        emit jobDone(true, okMsg + QStringLiteral("：%1").arg(out));
    } else {
        // 失败文案直通 error.message：E_CANCELLED 的 message 即「已取消」，mainwidget 特判逐字命中
        emit jobDone(false, resp.value(QStringLiteral("error")).toObject()
                             .value(QStringLiteral("message")).toString());
    }
}

void FFExtractor::doDenoiseWav(const QString& inPath, HumBase humBase, bool removeWind) {
    QJsonObject req;
    req.insert(QStringLiteral("cmd"), QStringLiteral("denoise"));
    req.insert(QStringLiteral("input"), inPath);
    req.insert(QStringLiteral("humBase"),
               humBase == HumBase::Hz60 ? QStringLiteral("60")
                                        : humBase == HumBase::Off ? QStringLiteral("off")
                                                                  : QStringLiteral("50"));
    req.insert(QStringLiteral("wind"), removeWind);
    // 工作点跟随源（48k 全链免重采样）与 off+off 拒绝均由命令层承担（GUI 侧另有前置拒绝，纵深防御）
    runRequest(req, QStringLiteral("降噪完成"));
}

void FFExtractor::doDenoiseAiWav(const QString& inPath) {
    // GUI 壳保留 appDir 相对缺省解析与「找不到…」文案（缺省策略上移给壳，片 C D2）；
    // 命令层 model 必填，槽内先解析路径再组 model 字段
    const QString model = QCoreApplication::applicationDirPath() + "/" + kAiModelRelPath;
    if (!QFileInfo::exists(model)) {
        emit jobDone(false, QStringLiteral("找不到 AI 降噪模型：%1").arg(model));
        return;
    }
    QJsonObject req;
    req.insert(QStringLiteral("cmd"), QStringLiteral("aiDenoise"));
    req.insert(QStringLiteral("input"), inPath);
    req.insert(QStringLiteral("model"), model);
    // GTCRN 工作点锁 16k 单声道，输出锁兜底规格 44.1k 立体声——命令层 lockCanonical 承担（spec §2 有意保留）
    runRequest(req, QStringLiteral("AI 降噪完成"));
}

void FFExtractor::doPitchWav(const QString& inPath, double semiTones) {
    QJsonObject req;
    req.insert(QStringLiteral("cmd"), QStringLiteral("pitch"));
    req.insert(QStringLiteral("input"), inPath);
    req.insert(QStringLiteral("semi"), semiTones);// GUI 已拒 0/越界（mainwidget），命令层校验为纵深防御
    runRequest(req, QStringLiteral("变调完成（%1 半音）").arg(semiTones));
}

void FFExtractor::doSepVocals(const QString& inPath, bool instrumental) {
    // 同 doDenoiseAiWav：appDir 相对缺省解析 + GUI 文案留在壳，命令层拿显式 model
    const QString model = QCoreApplication::applicationDirPath() + "/" + kSepModelRelPath;
    if (!QFileInfo::exists(model)) {
        emit jobDone(false, QStringLiteral("找不到人声分离模型：%1").arg(model));
        return;
    }
    QJsonObject req;
    req.insert(QStringLiteral("cmd"), QStringLiteral("separate"));
    req.insert(QStringLiteral("input"), inPath);
    req.insert(QStringLiteral("model"), model);
    req.insert(QStringLiteral("stem"), instrumental ? QStringLiteral("instrumental")
                                                    : QStringLiteral("vocals"));
    // MDX 工作点锁 44.1k 立体声，输出锁兜底规格（spec §2 有意保留）；双 stem 后缀/文案由 instrumental 决定
    runRequest(req, instrumental ? QStringLiteral("伴奏提取完成") : QStringLiteral("人声分离完成"));
}

void FFExtractor::doExtractAudio(const QString& inPath) {
    QJsonObject req;
    req.insert(QStringLiteral("cmd"), QStringLiteral("extractAudio"));
    req.insert(QStringLiteral("input"), inPath);// 不传 output：缺省自动命名与现状同规则（底座源目录后缀规则）
    runRequest(req, QStringLiteral("音轨提取完成"));
}

void FFExtractor::doExtractVideo(const QString& inPath) {
    QJsonObject req;
    req.insert(QStringLiteral("cmd"), QStringLiteral("extractVideo"));
    req.insert(QStringLiteral("input"), inPath);// 不传 output：缺省自动命名与现状同规则
    runRequest(req, QStringLiteral("无声视频提取完成"));
}
