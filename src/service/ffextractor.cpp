#include "ffextractor.h"

#include "commandlayer.h"
#include "dspdenoiser.h"// HumBase 制式枚举（槽参数 → 请求字段映射）
#include "model/data.h"
#include "util/ffutil.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <exception>// std::exception，runRequest 兜底 catch 用

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {

// 打开输入并定位指定类型流，失败时已保证关闭输入
ErrInfo open_input(const QString& inPath, AVFormatContext** fmt, AVMediaType type, int* streamIdx) {
    int ret = avformat_open_input(fmt, inPath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) return {ErrCode::File, QStringLiteral("打不开文件：%1").arg(ff_err(ret))};
    ret = avformat_find_stream_info(*fmt, nullptr);
    if (ret < 0) {
        avformat_close_input(fmt);
        return {ErrCode::File, QStringLiteral("读不到流信息：%1").arg(ff_err(ret))};
    }
    ret = av_find_best_stream(*fmt, type, -1, -1, nullptr, 0);
    if (ret < 0) {
        avformat_close_input(fmt);
        return {ErrCode::File,
                type == AVMEDIA_TYPE_AUDIO ? QStringLiteral("文件里没有音频流")
                                           : QStringLiteral("文件里没有视频流")};
    }
    *streamIdx = ret;
    return {};
}

// 建输出容器并打开文件，失败时已保证释放上下文
ErrInfo open_output(AVFormatContext** out, const QString& outPath) {
    int ret = avformat_alloc_output_context2(out, nullptr, nullptr, outPath.toUtf8().constData());
    if (ret < 0) return {ErrCode::File, QStringLiteral("建输出容器失败：%1").arg(ff_err(ret))};
    ret = avio_open(&(*out)->pb, outPath.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        avformat_free_context(*out);
        *out = nullptr;
        return {ErrCode::File, QStringLiteral("输出文件建不了：%1").arg(ff_err(ret))};
    }
    return {};
}

// 建输出流、拷编码参数并写文件头
ErrInfo write_header(AVFormatContext* in, AVFormatContext* out, int idx) {
    AVStream* inSt = in->streams[idx];
    AVStream* outSt = avformat_new_stream(out, nullptr);
    if (!outSt) return {ErrCode::File, QStringLiteral("建输出流失败")};
    if (avcodec_parameters_copy(outSt->codecpar, inSt->codecpar) < 0)
        return {ErrCode::File, QStringLiteral("拷贝编码参数失败")};
    outSt->codecpar->codec_tag = 0;
    outSt->time_base = inSt->time_base;
    int ret = avformat_write_header(out, nullptr);
    if (ret < 0) return {ErrCode::File, QStringLiteral("写文件头失败：%1").arg(ff_err(ret))};
    return {};
}

// 只搬指定流的包，边搬边报进度；cancel 为包粒度取消检查点（CLI 传空）
ErrInfo copy_stream(AVFormatContext* in, AVFormatContext* out, int idx, const std::function<void(int)>& onProgress,
                    const std::atomic<bool>* cancel) {
    AVStream* inSt = in->streams[idx];
    AVStream* outSt = out->streams[0];// 输出容器只有这一条流，包索引须重映射到它
    double durSec = in->duration > 0 ? in->duration * av_q2d(AV_TIME_BASE_Q) : 0.0;
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return {ErrCode::Internal, QStringLiteral("内存不够")};
    ErrInfo e;
    while (true) {
        if (cancel && cancel->load()) {
            e = {ErrCode::Cancelled, QStringLiteral("已取消")};
            break;
        }
        int ret = av_read_frame(in, pkt);
        if (ret < 0) break;// 读到尾或出错都收尾
        if (pkt->stream_index == idx) {
            av_packet_rescale_ts(pkt, inSt->time_base, outSt->time_base);
            pkt->stream_index = outSt->index;
            ret = av_interleaved_write_frame(out, pkt);
            if (ret < 0) {
                e = {ErrCode::File, QStringLiteral("写包失败：%1").arg(ff_err(ret))};// OQ-4：输出侧 I/O 归 File
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
QString finish(const ErrInfo& e, AVFormatContext** out, AVFormatContext** in, QString* outPath, ErrInfo* err) {
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

void FFExtractor::setWorkspaceRoot(const QString& root) {
    workspaceRoot = root;
}

void FFExtractor::setModelRoot(const QString& root) {
    modelRoot = root;
}

// 引擎动词缺省模型两级解析（FR-8 片 B，D1——fr24 spec :85 接管点的 GUI 兑现）：
// 模型库该用途已装（modelList 视角）→ appDir bundled 兜底。查库走 execute 组 JSON（单一通路，
// 不直调 ModelManager）；根未注入（空串）不查库 = appDir 单级旧行为（探针/未配置环境零漂移，
// 亦免受机器缺省根 ~/VideoHandler-Models 存量态影响）。v1 每用途一模型无歧义（FR-4 多模型
// 档位是 §5 留缝）；查询失败不拦任务，兜底 bundled 路径交命令层给权威错误。
// 取「installed 且非 bundled」的首条：bundled 标志 = 该条目当前解析到
// 随包副本（目录条目未登记而 appDir 在位时为 true，modelmanager 目录视角），不滤则随包压过用户
// 导入的库内同用途模型——跨 id 遮蔽使 GUI 导入对该动词成为死胡同（审查轮 P1-1，GML-103b 回归）。
QString FFExtractor::resolveDefaultModel(const QString& forVerb, const QString& appDirModel) const {
    if (modelRoot.isEmpty()) return appDirModel;
    QJsonObject req;
    req.insert(QStringLiteral("v"), 1);
    req.insert(QStringLiteral("cmd"), QStringLiteral("modelList"));
    req.insert(QStringLiteral("root"), modelRoot);
    QJsonObject resp;
    try {
        resp = CommandLayer::execute(req);
    } catch (...) {
        return appDirModel;
    }
    if (!resp.value(QStringLiteral("ok")).toBool()) return appDirModel;
    const QJsonArray models = resp.value(QStringLiteral("result")).toObject()
                                  .value(QStringLiteral("models")).toArray();
    for (const QJsonValue& v : models) {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("for")).toString() == forVerb
            && m.value(QStringLiteral("status")).toString() == QStringLiteral("installed")
            && !m.value(QStringLiteral("bundled")).toBool())// 库内登记优先；bundled 在位留给第二级兜底
            return m.value(QStringLiteral("path")).toString();
    }
    return appDirModel;
}

// 处理历史查询（FR-A4 片 B）：wsInfo 只读动词走同一 execute，root 用注入成员（与任务请求同根，
// 保证历史查的正是任务落的）；result 序列化为 Compact JSON 经信号回传（QString，免自定义 QMetaType），
// 失败回传 error message。不占 busy——首查大文件哈希数秒，在 worker 线程跑正是为了不冻 UI
void FFExtractor::doWsInfo(const QString& input) {
    QJsonObject req;
    req.insert(QStringLiteral("v"), 1);
    req.insert(QStringLiteral("cmd"), QStringLiteral("wsInfo"));
    req.insert(QStringLiteral("root"), workspaceRoot);
    req.insert(QStringLiteral("input"), input);
    QJsonObject resp;
    try {
        resp = CommandLayer::execute(req);// 只读查询：无取消/进度语义，Ctx 缺省
    } catch (const std::exception& e) {
        emit wsInfoDone(false, QStringLiteral("查询异常终止：%1").arg(QString::fromUtf8(e.what())));
        return;
    } catch (...) {
        emit wsInfoDone(false, QStringLiteral("查询异常终止（未知异常）"));
        return;
    }
    if (resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonDocument doc(resp.value(QStringLiteral("result")).toObject());
        emit wsInfoDone(true, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    } else {
        emit wsInfoDone(false, resp.value(QStringLiteral("error")).toObject()
                             .value(QStringLiteral("message")).toString());
    }
}

// 模型列表查询（FR-8 片 B，doWsInfo 同型只读不占 busy）：modelList 走同一 execute，root 用注入
// 成员（与下载/导入同根，保证查的正是要落的库）；result 序列化为 Compact JSON 经信号回传，
// 失败回传 error message
void FFExtractor::doModelList() {
    QJsonObject req;
    req.insert(QStringLiteral("v"), 1);
    req.insert(QStringLiteral("cmd"), QStringLiteral("modelList"));
    if (!modelRoot.isEmpty()) req.insert(QStringLiteral("root"), modelRoot);
    QJsonObject resp;
    try {
        resp = CommandLayer::execute(req);// 只读查询：无取消/进度语义，Ctx 缺省
    } catch (const std::exception& e) {
        emit modelListDone(false, QStringLiteral("查询异常终止：%1").arg(QString::fromUtf8(e.what())));
        return;
    } catch (...) {
        emit modelListDone(false, QStringLiteral("查询异常终止（未知异常）"));
        return;
    }
    if (resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonDocument doc(resp.value(QStringLiteral("result")).toObject());
        emit modelListDone(true, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    } else {
        emit modelListDone(false, resp.value(QStringLiteral("error")).toObject()
                                 .value(QStringLiteral("message")).toString());
    }
}

// 模型族请求收尾（FR-8 片 B）：与 runRequest 同型的 Ctx 注入（下载占 busy、可取消、进度走既有
// progress 信号），差异三处——不插 workspace 键（模型族动词拒收 workspace，D5/OQ-8）；完成走
// modelOpDone 而非 jobDone（模型操作非七槽任务，MediaService 据此独立放锁）；不发 modelMissing
// （导入 E_MODEL 属「候选文件不是可用模型」校验语义，用户已在管理对话框内，无引导可给）。
void FFExtractor::runModelRequest(const QJsonObject& req, const QString& okPrefix) {
    CommandLayer::Ctx ctx;
    ctx.cancel = cancelFlag;
    ctx.onProgress = [this](int pct) { emit progress(pct); };
    QJsonObject resp;
    try {
        resp = CommandLayer::execute(req, ctx);
    } catch (const std::exception& e) {
        emit modelOpDone(false, QStringLiteral("模型操作异常终止：%1").arg(QString::fromUtf8(e.what())));
        return;
    } catch (...) {
        emit modelOpDone(false, QStringLiteral("模型操作异常终止（未知异常）"));
        return;
    }
    if (resp.value(QStringLiteral("ok")).toBool()) {
        const QString out = resp.value(QStringLiteral("result")).toObject()
                                .value(QStringLiteral("path")).toString();
        emit modelOpDone(true, okPrefix + QStringLiteral("：%1").arg(out));
    } else {
        emit modelOpDone(false, resp.value(QStringLiteral("error")).toObject()
                                 .value(QStringLiteral("message")).toString());
    }
}

void FFExtractor::doModelDownload(const QString& id, const QString& endpoint) {
    QJsonObject req;
    req.insert(QStringLiteral("v"), 1);
    req.insert(QStringLiteral("cmd"), QStringLiteral("modelDownload"));
    req.insert(QStringLiteral("id"), id);
    if (!modelRoot.isEmpty()) req.insert(QStringLiteral("root"), modelRoot);
    if (!endpoint.isEmpty()) req.insert(QStringLiteral("endpoint"), endpoint);
    runModelRequest(req, QStringLiteral("模型下载完成"));
}

void FFExtractor::doModelImport(const QString& path, const QString& forVerb) {
    QJsonObject req;
    req.insert(QStringLiteral("v"), 1);
    req.insert(QStringLiteral("cmd"), QStringLiteral("modelImport"));
    req.insert(QStringLiteral("path"), path);
    req.insert(QStringLiteral("for"), forVerb);// GUI 导入 = 自由导入形态（D6 形态 2，for 必带）
    if (!modelRoot.isEmpty()) req.insert(QStringLiteral("root"), modelRoot);
    runModelRequest(req, QStringLiteral("模型导入完成"));
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

QString FFExtractor::extractAudio(const QString& inPath, ErrInfo* err, const std::function<void(int)>& onProgress,
                                  const std::atomic<bool>* cancel, const QString& outPath, const QString& outDir) {
    ErrInfo e;
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    QString path;
    int idx = -1;

    do {
        e = open_input(inPath, &in, AVMEDIA_TYPE_AUDIO, &idx);
        if (!e.isEmpty()) break;
        QFileInfo fi(inPath);
        // 显式路径在输入成功打开后才落位：失败路径绝不误删既有文件；
        // 缺省命名规则仍是底座单一来源，outDir 非空时原样跑在该目录内（FR-A4 工作区只给目录不给文件名）
        if (!outPath.isEmpty()) path = outPath;
        else {
            const QString dir = outDir.isEmpty() ? fi.absolutePath() : outDir;
            path = dir + "/" + fi.completeBaseName() + kSuffixAudio + audio_ext(in->streams[idx]->codecpar->codec_id);
        }
        e = open_output(&out, path);
        if (!e.isEmpty()) break;
        e = write_header(in, out, idx);
        if (!e.isEmpty()) break;
        e = copy_stream(in, out, idx, onProgress, cancel);
        if (!e.isEmpty()) break;
        int ret = av_write_trailer(out);
        if (ret < 0) e = {ErrCode::File, QStringLiteral("写文件尾失败：%1").arg(ff_err(ret))};// OQ-4：输出侧 I/O 归 File
    } while (false);

    return finish(e, &out, &in, &path, err);
}

QString FFExtractor::extractVideo(const QString& inPath, ErrInfo* err, const std::function<void(int)>& onProgress,
                                  const std::atomic<bool>* cancel, const QString& outPath, const QString& outDir) {
    ErrInfo e;
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    QString path;
    int idx = -1;

    do {
        e = open_input(inPath, &in, AVMEDIA_TYPE_VIDEO, &idx);
        if (!e.isEmpty()) break;
        QFileInfo fi(inPath);
        // 显式路径在输入成功打开后才落位：失败路径绝不误删既有文件；outDir 语义同 extractAudio
        if (!outPath.isEmpty()) path = outPath;
        else {
            const QString dir = outDir.isEmpty() ? fi.absolutePath() : outDir;
            path = dir + "/" + fi.completeBaseName() + kSuffixMute + "." + fi.suffix();
        }
        e = open_output(&out, path);
        if (!e.isEmpty()) break;
        e = write_header(in, out, idx);
        if (!e.isEmpty()) break;
        e = copy_stream(in, out, idx, onProgress, cancel);
        if (!e.isEmpty()) break;
        int ret = av_write_trailer(out);
        if (ret < 0) e = {ErrCode::File, QStringLiteral("写文件尾失败：%1").arg(ff_err(ret))};// OQ-4：输出侧 I/O 归 File
    } while (false);

    return finish(e, &out, &in, &path, err);
}

void FFExtractor::doProbe(const QString& inPath) {
    QString info;
    bool ok = probe(inPath, &info);
    emit probeDone(ok, info);
}

// 统一命令层收尾（FR-24 片 D，spec D6）：组好的请求交 execute，取消标志/进度回调经 Ctx 注入；
// 兜底 catch 与命令层 E_INTERNAL 双保险——execute 契约不抛出，此处防其本身违约抛出穿透 worker 槽。
// FR-A4 片 B：根非空时在此单点组 workspace 键（七个任务槽共用，probe 走 doProbe 直调不经此、
// wsInfo 走 doWsInfo 自组请求——两者天然不插键）；空串不组键 = direct 模式行为零漂移
void FFExtractor::runRequest(const QJsonObject& req, const QString& okMsg) {
    QJsonObject request = req;
    if (!workspaceRoot.isEmpty()) request.insert(QStringLiteral("workspace"), workspaceRoot);
    CommandLayer::Ctx ctx;
    ctx.cancel = cancelFlag;
    ctx.onProgress = [this](int pct) { emit progress(pct); };
    QJsonObject resp;
    try {
        resp = CommandLayer::execute(request, ctx);
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
        const QJsonObject error = resp.value(QStringLiteral("error")).toObject();
        // 失败文案直通 error.message：E_CANCELLED 的 message 即「已取消」，mainwidget 特判逐字命中
        emit jobDone(false, error.value(QStringLiteral("message")).toString());
        // 结构化缺模型信号（FR-8 片 B，D8）：E_MODEL = 模型文件缺失/损坏（引擎 setup 失败同域），
        // 除文案外加发可编程识别的引导钩子——不走文案前缀匹配（NFR-M1 片 B P3-1 教训）
        if (error.value(QStringLiteral("code")).toString() == QLatin1String("E_MODEL"))
            emit modelMissing();
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
    // GUI 壳缺省解析两级化（FR-8 片 B，D1）：模型库该用途已装 → appDir bundled；命令层 model
    // 必填，槽内先解析路径再组 model 字段。两级皆缺时文案逐字不动（GUI-EQV 锁定，%1 仍显示
    // appDir bundled 路径——随包分发是缺省安装形态，库路径此刻不存在），并加发 modelMissing
    // 引导下载（D8 首次使用引导）
    const QString bundled = QCoreApplication::applicationDirPath() + "/" + kAiModelRelPath;
    const QString model = resolveDefaultModel(QStringLiteral("aiDenoise"), bundled);
    if (!QFileInfo::exists(model)) {
        emit jobDone(false, QStringLiteral("找不到 AI 降噪模型：%1").arg(bundled));
        emit modelMissing();
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
    // 同 doDenoiseAiWav：两级缺省解析（FR-8 片 B）+ GUI 文案留在壳（逐字不动），命令层拿显式 model
    const QString bundled = QCoreApplication::applicationDirPath() + "/" + kSepModelRelPath;
    const QString model = resolveDefaultModel(QStringLiteral("separate"), bundled);
    if (!QFileInfo::exists(model)) {
        emit jobDone(false, QStringLiteral("找不到人声分离模型：%1").arg(bundled));
        emit modelMissing();
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
