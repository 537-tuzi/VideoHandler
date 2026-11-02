#include "commandlayer.h"

#include "aidenoiser.h"
#include "dspdenoiser.h"
#include "ffextractor.h"
#include "fftranscoder.h"
#include "pitchshifter.h"
#include "vocalseparator.h"
#include "model/data.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QStringList>

#include <algorithm>
#include <exception>

namespace CommandLayer {
namespace {

// ---- 错误码常量（D3 六类；字面量即对外合同）----
constexpr auto kErrParams    = "E_PARAMS";
constexpr auto kErrFile      = "E_FILE";
constexpr auto kErrModel     = "E_MODEL";
constexpr auto kErrEngine    = "E_ENGINE";
constexpr auto kErrCancelled = "E_CANCELLED";
constexpr auto kErrInternal  = "E_INTERNAL";

// E_FILE 文案前缀表：底座 open_*/write_header 家族的失败文本（ffextractor.cpp / fftranscoder.cpp）。
// 注意中文前缀必须 QStringLiteral（UTF-16）—— QLatin1String 装不了多字节字符，永远匹配不上
bool isFileError(const QString& msg) {
    static const QStringList prefixes = {
        QStringLiteral("打不开文件："), QStringLiteral("读不到流信息："),
        QStringLiteral("文件里没有音频流"), QStringLiteral("文件里没有视频流"),
        QStringLiteral("不认识的音频编码"), QStringLiteral("解码器起不来"),
        QStringLiteral("建输出容器失败："), QStringLiteral("输出文件建不了："),
        QStringLiteral("写文件头失败："), QStringLiteral("建输出流失败"),
        QStringLiteral("拷贝编码参数失败"),
    };
    return std::any_of(prefixes.begin(), prefixes.end(),
                       [&msg](const QString& p) { return msg.startsWith(p); });
}

// 底座错误文本 → 错误码：取消优先；引擎 lastError 非空即运行期错（streamengine.h 归口约定）；
// 文件族按前缀；其余（写包失败/内存不够等）兜底 E_INTERNAL
const char* classifyTranscodeError(const QString& msg, IStreamEngine* engine) {
    if (msg == QStringLiteral("已取消")) return kErrCancelled;
    if (engine && !engine->lastError().isEmpty()) {
        // WP-1.4 输出契约校验族（vocalseparator runModel 三条「模型…」文案，负例测试已固化）= 模型不合规
        // → E_MODEL（NFR-R4：Agent 据此引导重下载）；其余运行期错（「推理失败：…」/「音频太短…」）= 引擎域 → E_ENGINE。
        // 审查 P1-1 拍板：改 classify 而非改 spec D3 文档；触发路径受 MD5 查表护甲不可构造 e2e，以代码级证据收口
        if (engine->lastError().startsWith(QStringLiteral("模型"))) return kErrModel;
        return kErrEngine;
    }
    if (isFileError(msg)) return kErrFile;
    return kErrInternal;
}

const char* classifyExtractError(const QString& msg) {// extract 族无引擎参与
    if (msg == QStringLiteral("已取消")) return kErrCancelled;
    if (isFileError(msg)) return kErrFile;
    return kErrInternal;
}

// ---- 校验辅助（严格 schema：未知字段/类型/枚举/范围一律 E_PARAMS，D2）----

// 必填字符串：缺失/类型错/空串 → 具体错误说明（空串 = 通过）
QString requireString(const QJsonObject& req, const char* key, QString* out) {
    const QJsonValue v = req.value(QLatin1String(key));
    if (v.isUndefined()) return QStringLiteral("%1 缺失").arg(QLatin1String(key));
    if (!v.isString()) return QStringLiteral("%1 必须是字符串").arg(QLatin1String(key));
    *out = v.toString();
    if (out->isEmpty()) return QStringLiteral("%1 不能为空").arg(QLatin1String(key));
    return {};
}

QString reqCmd(const QJsonObject& req) { return req.value(QStringLiteral("cmd")).toString(); }

// 动词允许的专有字段（信封 v/cmd/input/output 之外）；probe 无 output（探测无落盘语义）
QStringList verbFields(const QString& cmd) {
    if (cmd == QStringLiteral("denoise")) return {QStringLiteral("humBase"), QStringLiteral("wind")};
    if (cmd == QStringLiteral("aiDenoise")) return {QStringLiteral("model")};
    if (cmd == QStringLiteral("pitch")) return {QStringLiteral("semi")};
    if (cmd == QStringLiteral("separate")) return {QStringLiteral("model"), QStringLiteral("stem")};
    return {};
}

bool verbAllowsOutput(const QString& cmd) { return cmd != QStringLiteral("probe"); }

// ---- 输出路径决策（spec §5.3 工作区缝：显式 output 优先，缺省走源目录后缀规则；
// 工作区包替换此函数即可接管全部转码族动词的落盘位置。extract 族缺省名依赖源流编解码器，
// 由底座 extractAudio/extractVideo 内部同一规则兜底——传空 outPath 即自动命名）----
QString outputOr(const QJsonObject& req, const char* suffix) {
    const QJsonValue o = req.value(QStringLiteral("output"));
    if (o.isString() && !o.toString().isEmpty()) return o.toString();
    const QFileInfo fi(req.value(QStringLiteral("input")).toString());
    return fi.absolutePath() + "/" + fi.completeBaseName() + QLatin1String(suffix) + ".wav";
}

// ---- result 组装（D4：输出规格可观测——P0 #4 行为进合同）----

QJsonObject specObject(int rate, int channels) {
    QJsonObject spec;
    spec.insert(QStringLiteral("sampleRate"), rate);
    spec.insert(QStringLiteral("channels"), channels);
    return spec;
}

// 转码族 result：OutInfo（转码器成功收尾回填的解码真值）+ engineResampled（必要重采样标志）
QJsonObject transcodeResult(const QString& out, const FFTranscoder::OutInfo& info, IStreamEngine& engine) {
    QJsonObject outputSpec = specObject(info.outRate, info.outChannels);
    outputSpec.insert(QStringLiteral("format"), QStringLiteral("pcm_f32le"));
    QJsonObject result;
    result.insert(QStringLiteral("output"), out);
    result.insert(QStringLiteral("sourceSpec"), specObject(info.srcRate, info.srcChannels));
    result.insert(QStringLiteral("outputSpec"), outputSpec);
    result.insert(QStringLiteral("engineResampled"),
                  engine.inRate() != info.srcRate || engine.inChannels() != info.srcChannels);
    result.insert(QStringLiteral("frames"), static_cast<double>(info.frames));
    result.insert(QStringLiteral("durationSec"),
                  info.outRate > 0 ? static_cast<double>(info.frames) / info.outRate : 0.0);
    return result;
}

// 转码族共用收尾：跑 transcodeStreamWav（取消/进度经 Ctx 透传，OutInfo 回传规格与帧数）
QJsonObject runTranscode(const QJsonObject& req, const Ctx& ctx, IStreamEngine& engine,
                         const QString& outPath, bool lockCanonical) {
    const QString cmd = reqCmd(req);
    FFTranscoder::OutInfo info;
    QString err;
    // lockCanonical：AI/分离路径输出锁兜底 44.1k 立体声（模型工作点，P0 #4 有意保留）
    const QString out = FFTranscoder::transcodeStreamWav(
        req.value(QStringLiteral("input")).toString(), outPath, engine, ctx.cancel, &err, ctx.onProgress,
        lockCanonical ? kCanonicalRate : 0, lockCanonical ? kCanonicalChannels : 0, &info);
    if (out.isEmpty()) return errorResponse(cmd, classifyTranscodeError(err, &engine), err);
    return okResponse(cmd, transcodeResult(out, info, engine));
}

// ---- 动词处理器（匿名 ns 独立函数、签名一致；注册表缝——NFR-M1 换自描述条目，分发层重写为零）----

QJsonObject cmdProbe(const QJsonObject& req, const Ctx&) {
    ProbeInfo info;
    QString err;
    if (!FFExtractor::probeInfo(req.value(QStringLiteral("input")).toString(), &info, &err))
        return errorResponse(reqCmd(req), kErrFile, err);
    QJsonObject result;
    result.insert(QStringLiteral("container"), info.container);
    result.insert(QStringLiteral("sizeBytes"), info.sizeBytes);
    result.insert(QStringLiteral("durationSec"),
                  info.durationUs > 0 ? QJsonValue(info.durationUs / 1000000.0) : QJsonValue());
    QJsonArray streams;
    for (const ProbeStream& s : info.streams) {
        QJsonObject o;
        o.insert(QStringLiteral("index"), s.index);
        o.insert(QStringLiteral("type"), s.mediaType);
        o.insert(QStringLiteral("codec"), s.codec);
        if (s.mediaType == QStringLiteral("video")) {
            o.insert(QStringLiteral("width"), s.width);
            o.insert(QStringLiteral("height"), s.height);
        } else if (s.mediaType == QStringLiteral("audio")) {
            o.insert(QStringLiteral("sampleRate"), s.sampleRate);
            o.insert(QStringLiteral("channels"), s.channels);
        }
        streams.append(o);
    }
    result.insert(QStringLiteral("streams"), streams);
    return okResponse(reqCmd(req), result);
}

QJsonObject cmdTranscode(const QJsonObject& req, const Ctx& ctx) {
    int rate = 0, channels = 0;
    FFTranscoder::probeSourceSpec(req.value(QStringLiteral("input")).toString(), &rate, &channels);// 工作点跟随源
    DspDenoiser passthrough;
    passthrough.setup(rate, channels, HumBase::Off, false);// 无滤波即直通
    return runTranscode(req, ctx, passthrough, outputOr(req, kSuffixTranscoded), false);
}

QJsonObject cmdDenoise(const QJsonObject& req, const Ctx& ctx) {
    const QString cmd = reqCmd(req);
    HumBase humBase = HumBase::Hz50;// 缺省 50Hz（对齐旧 CLI/GUI 缺省）
    const QJsonValue hb = req.value(QStringLiteral("humBase"));
    if (!hb.isUndefined()) {
        if (!hb.isString()) return errorResponse(cmd, kErrParams, QStringLiteral("humBase 必须是字符串"));
        const QString s = hb.toString();
        if (s == QLatin1String("50")) humBase = HumBase::Hz50;
        else if (s == QLatin1String("60")) humBase = HumBase::Hz60;
        else if (s == QLatin1String("off")) humBase = HumBase::Off;
        else return errorResponse(cmd, kErrParams, QStringLiteral("humBase 只接受 50、60 或 off"));
    }
    bool wind = true;
    const QJsonValue w = req.value(QStringLiteral("wind"));
    if (!w.isUndefined()) {
        if (!w.isBool()) return errorResponse(cmd, kErrParams, QStringLiteral("wind 必须是布尔值"));
        wind = w.toBool();
    }
    if (humBase == HumBase::Off && !wind)// 与 GUI 拒绝逻辑一致（mainwidget.cpp doDenoiseWav）
        return errorResponse(cmd, kErrParams, QStringLiteral("humBase 为 off 且 wind 为 false：没有可执行的降噪项"));
    int rate = 0, channels = 0;
    FFTranscoder::probeSourceSpec(req.value(QStringLiteral("input")).toString(), &rate, &channels);// 工作点跟随源
    DspDenoiser denoiser;
    denoiser.setup(rate, channels, humBase, wind);
    return runTranscode(req, ctx, denoiser, outputOr(req, kSuffixDenoised), false);
}

QJsonObject cmdAiDenoise(const QJsonObject& req, const Ctx& ctx) {
    const QString cmd = reqCmd(req);
    QString model;
    const QString perr = requireString(req, "model", &model);
    if (!perr.isEmpty()) return errorResponse(cmd, kErrParams, perr);
    if (!QFileInfo::exists(model))
        return errorResponse(cmd, kErrModel, QStringLiteral("找不到模型 %1").arg(model));
    AiDenoiser ai;
    QString setupErr;
    if (!ai.setup(model, &setupErr)) return errorResponse(cmd, kErrModel, setupErr);
    return runTranscode(req, ctx, ai, outputOr(req, kSuffixAi), true);
}

QJsonObject cmdPitch(const QJsonObject& req, const Ctx& ctx) {
    const QString cmd = reqCmd(req);
    const QJsonValue sv = req.value(QStringLiteral("semi"));
    if (!sv.isDouble()) return errorResponse(cmd, kErrParams, QStringLiteral("semi 必须是数字"));
    const double semi = sv.toDouble();
    if (!(semi >= -12.0 && semi <= 12.0))// 范围对齐 GUI（mainwidget spinSemi [-12,12]）；否定式使 NaN 落拒收（审查 P2-1）
        return errorResponse(cmd, kErrParams, QStringLiteral("semi 须在 [-12, 12] 内"));
    if (qFuzzyIsNull(semi)) return errorResponse(cmd, kErrParams, QStringLiteral("semi 不能为 0"));// 对齐 GUI 拒绝逻辑
    int rate = 0, channels = 0;
    FFTranscoder::probeSourceSpec(req.value(QStringLiteral("input")).toString(), &rate, &channels);// 工作点跟随源
    PitchShifter shifter;
    QString setupErr;
    if (!shifter.setup(rate, channels, semi, &setupErr)) return errorResponse(cmd, kErrEngine, setupErr);
    return runTranscode(req, ctx, shifter, outputOr(req, kSuffixPitch), false);
}

QJsonObject cmdSeparate(const QJsonObject& req, const Ctx& ctx) {
    const QString cmd = reqCmd(req);
    QString model;
    const QString perr = requireString(req, "model", &model);
    if (!perr.isEmpty()) return errorResponse(cmd, kErrParams, perr);
    bool instrumental = false;// stem 缺省 vocals
    const QJsonValue st = req.value(QStringLiteral("stem"));
    if (!st.isUndefined()) {
        if (!st.isString()) return errorResponse(cmd, kErrParams, QStringLiteral("stem 必须是字符串"));
        const QString s = st.toString();
        if (s == QLatin1String("vocals")) instrumental = false;
        else if (s == QLatin1String("instrumental")) instrumental = true;
        else return errorResponse(cmd, kErrParams, QStringLiteral("stem 只接受 vocals 或 instrumental"));
    }
    if (!QFileInfo::exists(model))
        return errorResponse(cmd, kErrModel, QStringLiteral("找不到模型 %1").arg(model));
    VocalSeparator sep;
    sep.setInstrumental(instrumental);
    QString setupErr;
    if (!sep.setup(model, &setupErr)) return errorResponse(cmd, kErrModel, setupErr);
    return runTranscode(req, ctx, sep, outputOr(req, instrumental ? kSuffixInstrumental : kSuffixVocals), true);
}

// 显式 output（extract 族缺省命名依赖源流编解码器，由底座内部规则决定——此处只取显式值）
QString explicitOutput(const QJsonObject& req) {
    const QJsonValue o = req.value(QStringLiteral("output"));
    return (o.isString() && !o.toString().isEmpty()) ? o.toString() : QString();
}

QJsonObject cmdExtractAudio(const QJsonObject& req, const Ctx& ctx) {
    QString err;
    const QString out = FFExtractor::extractAudio(req.value(QStringLiteral("input")).toString(), &err,
                                                  ctx.onProgress, ctx.cancel, explicitOutput(req));
    if (out.isEmpty()) return errorResponse(reqCmd(req), classifyExtractError(err), err);
    QJsonObject result;
    result.insert(QStringLiteral("output"), out);
    return okResponse(reqCmd(req), result);
}

QJsonObject cmdExtractVideo(const QJsonObject& req, const Ctx& ctx) {
    QString err;
    const QString out = FFExtractor::extractVideo(req.value(QStringLiteral("input")).toString(), &err,
                                                  ctx.onProgress, ctx.cancel, explicitOutput(req));
    if (out.isEmpty()) return errorResponse(reqCmd(req), classifyExtractError(err), err);
    QJsonObject result;
    result.insert(QStringLiteral("output"), out);
    return okResponse(reqCmd(req), result);
}

}// namespace

QJsonObject okResponse(const QString& cmd, const QJsonObject& result) {
    QJsonObject resp;
    resp.insert(QStringLiteral("v"), 1);
    resp.insert(QStringLiteral("ok"), true);
    if (!cmd.isEmpty()) resp.insert(QStringLiteral("cmd"), cmd);
    resp.insert(QStringLiteral("result"), result);
    return resp;
}

QJsonObject errorResponse(const QString& cmd, const char* code, const QString& message) {
    QJsonObject error;
    error.insert(QStringLiteral("code"), QLatin1String(code));
    error.insert(QStringLiteral("message"), message);
    QJsonObject resp;
    resp.insert(QStringLiteral("v"), 1);
    resp.insert(QStringLiteral("ok"), false);
    if (!cmd.isEmpty()) resp.insert(QStringLiteral("cmd"), cmd);
    resp.insert(QStringLiteral("error"), error);
    return resp;
}

QJsonObject execute(const QJsonObject& request, const Ctx& ctx) {
    QString cmd;// 兜底 catch 回显用（能识别才回显）
    try {
        // —— 信封校验：cmd 先于 v（合法 cmd 在 v/字段错误下仍回显）——
        const QJsonValue cmdV = request.value(QStringLiteral("cmd"));
        if (!cmdV.isString()) return errorResponse({}, kErrParams, QStringLiteral("cmd 缺失或不是字符串"));
        cmd = cmdV.toString();
        static const QStringList kVerbs = {
            QStringLiteral("probe"),    QStringLiteral("transcode"), QStringLiteral("denoise"),
            QStringLiteral("aiDenoise"), QStringLiteral("pitch"),     QStringLiteral("separate"),
            QStringLiteral("extractAudio"), QStringLiteral("extractVideo"),
        };
        if (!kVerbs.contains(cmd))
            return errorResponse({}, kErrParams, QStringLiteral("未知命令：%1").arg(cmd));
        if (request.contains(QStringLiteral("v"))) {
            const QJsonValue v = request.value(QStringLiteral("v"));
            if (!v.isDouble() || v.toInt() != 1)
                return errorResponse(cmd, kErrParams, QStringLiteral("v 仅支持 1"));
        }
        // —— 未知字段拒收（动词级白名单；QJsonObject 键序确定 → 报错确定性）——
        const QStringList extra = verbFields(cmd);
        const bool allowsOutput = verbAllowsOutput(cmd);
        for (const QString& key : request.keys()) {
            if (key == QLatin1String("v") || key == QLatin1String("cmd") || key == QLatin1String("input")) continue;
            if (key == QLatin1String("output") && allowsOutput) continue;
            if (extra.contains(key)) continue;
            return errorResponse(cmd, kErrParams, QStringLiteral("未知字段：%1").arg(key));
        }
        // —— 公共字段：input 必填；output（若允许）非空 + 原件只读防线（FR-A4 根基）——
        QString input;
        const QString ierr = requireString(request, "input", &input);
        if (!ierr.isEmpty()) return errorResponse(cmd, kErrParams, ierr);
        if (allowsOutput && request.contains(QStringLiteral("output"))) {
            const QJsonValue o = request.value(QStringLiteral("output"));
            if (!o.isString()) return errorResponse(cmd, kErrParams, QStringLiteral("output 必须是字符串"));
            if (o.toString().isEmpty()) return errorResponse(cmd, kErrParams, QStringLiteral("output 不能为空"));
            if (QFileInfo(o.toString()).absoluteFilePath() == QFileInfo(input).absoluteFilePath())
                return errorResponse(cmd, kErrParams, QStringLiteral("output 不能与 input 相同（原件只读）"));
        }
        // —— 分发（if/else 链；NFR-M1 注册表缝）——
        if (cmd == QStringLiteral("probe")) return cmdProbe(request, ctx);
        if (cmd == QStringLiteral("transcode")) return cmdTranscode(request, ctx);
        if (cmd == QStringLiteral("denoise")) return cmdDenoise(request, ctx);
        if (cmd == QStringLiteral("aiDenoise")) return cmdAiDenoise(request, ctx);
        if (cmd == QStringLiteral("pitch")) return cmdPitch(request, ctx);
        if (cmd == QStringLiteral("separate")) return cmdSeparate(request, ctx);
        if (cmd == QStringLiteral("extractAudio")) return cmdExtractAudio(request, ctx);
        return cmdExtractVideo(request, ctx);
    } catch (const std::exception& e) {// 异常屏障：引擎契约不得抛出，命令层自兜底（D3 E_INTERNAL）
        return errorResponse(cmd, kErrInternal, QStringLiteral("内部异常：%1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        return errorResponse(cmd, kErrInternal, QStringLiteral("内部异常（未知异常）"));
    }
}

}// namespace CommandLayer
