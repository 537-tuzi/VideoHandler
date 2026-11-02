#include "commandlayer.h"

#include "engineregistry.h"
#include "errinfo.h"
#include "ffextractor.h"
#include "fftranscoder.h"
#include "streamengine.h"
#include "model/data.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QList>
#include <QStringList>

#include <exception>
#include <memory>

namespace CommandLayer {
namespace {

using EngineRegistry::EngineEntry;// 注册表条目（本文件多处引用，收窄前缀）

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

// 动词允许的专有字段白名单由条目供给（EngineRegistry::isDeclaredParam，spec D2 硬约束 1）；
// probe 无 output（探测无落盘语义，见内置动词表）

// ---- 输出路径决策（spec §5.3 工作区缝：显式 output 优先，缺省走源目录后缀规则；
// 工作区包替换此函数即可接管全部转码族动词的落盘位置。extract 族缺省名依赖源流编解码器，
// 由底座 extractAudio/extractVideo 内部同一规则兜底——传空 outPath 即自动命名）----
QString outputOr(const QJsonObject& req, const QString& suffix) {
    const QJsonValue o = req.value(QStringLiteral("output"));
    if (o.isString() && !o.toString().isEmpty()) return o.toString();
    const QFileInfo fi(req.value(QStringLiteral("input")).toString());
    return fi.absolutePath() + "/" + fi.completeBaseName() + suffix + QStringLiteral(".wav");
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

// 转码族共用收尾：跑 transcodeStreamWav（取消/进度经 Ctx 透传，OutInfo 回传规格与帧数）。
// 失败码直接取自底座/引擎的 ErrInfo（码在产生点赋值，文案透传——前缀匹配已退役，spec D4/OQ-2）
QJsonObject runTranscode(const QJsonObject& req, const Ctx& ctx, IStreamEngine& engine,
                         const QString& outPath, bool lockCanonical) {
    const QString cmd = reqCmd(req);
    FFTranscoder::OutInfo info;
    ErrInfo err;
    // lockCanonical：AI/分离路径输出锁兜底 44.1k 立体声（模型工作点，P0 #4 有意保留）
    const QString out = FFTranscoder::transcodeStreamWav(
        req.value(QStringLiteral("input")).toString(), outPath, engine, ctx.cancel, &err, ctx.onProgress,
        lockCanonical ? kCanonicalRate : 0, lockCanonical ? kCanonicalChannels : 0, &info);
    if (out.isEmpty()) return errorResponse(cmd, errCodeText(err.code), err.message);
    return okResponse(cmd, transcodeResult(out, info, engine));
}

// ---- 动词处理器（匿名 ns 独立函数、签名一致；注册表缝——NFR-M1 换自描述条目，分发层重写为零）----

QJsonObject cmdProbe(const QJsonObject& req, const Ctx&) {
    ProbeInfo info;
    QString err;
    if (!FFExtractor::probeInfo(req.value(QStringLiteral("input")).toString(), &info, &err))
        return errorResponse(reqCmd(req), errCodeText(ErrCode::File), err);// 探测失败恒文件域（现状语义）
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

// ---- 引擎路径通用段（条目驱动：校验 → 语义 → 模型存在性 → 工厂 → 转码收尾）----
// 信封校验已在 execute 完成；lockCanonical 与缺省后缀由条目供给，五转码族动词共用（NFR-M1）
QJsonObject runEngine(const EngineEntry& entry, const QJsonObject& req, const Ctx& ctx) {
    const QString& cmd = entry.id;
    QJsonObject args;// 补缺省后的参数副本（defStr 单源）
    const QString verr = EngineRegistry::validateParams(entry, req, &args);
    if (!verr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), verr);
    if (entry.semanticCheck) {
        const QString serr = entry.semanticCheck(args);
        if (!serr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), serr);
    }
    if (entry.requiresModel) {
        const QString model = args.value(QStringLiteral("model")).toString();
        if (!QFileInfo::exists(model))
            return errorResponse(cmd, errCodeText(ErrCode::Model),
                                 QStringLiteral("找不到模型 %1").arg(model));
    }
    int rate = 0, channels = 0;
    FFTranscoder::probeSourceSpec(req.value(QStringLiteral("input")).toString(), &rate, &channels);// 工作点跟随源
    QString createErr;
    std::unique_ptr<IStreamEngine> engine = entry.create(args, rate, channels, &createErr);
    if (!engine) {// setup 失败按条目 ErrCode 归类；None（create 永不失败的条目）落此为编程错误——断言拦开发期，Engine 兜底（片 A P2-1 同款防护）
        Q_ASSERT(entry.setupErrorClass != ErrCode::None);
        return errorResponse(cmd, errCodeText(entry.setupErrorClass != ErrCode::None
                                                  ? entry.setupErrorClass : ErrCode::Engine),
                             createErr);
    }
    return runTranscode(req, ctx, *engine, outputOr(req, entry.outputSuffix(args)), entry.lockCanonicalOutput);
}

// 显式 output（extract 族缺省命名依赖源流编解码器，由底座内部规则决定——此处只取显式值）
QString explicitOutput(const QJsonObject& req) {
    const QJsonValue o = req.value(QStringLiteral("output"));
    return (o.isString() && !o.toString().isEmpty()) ? o.toString() : QString();
}

QJsonObject cmdExtractAudio(const QJsonObject& req, const Ctx& ctx) {
    ErrInfo err;
    const QString out = FFExtractor::extractAudio(req.value(QStringLiteral("input")).toString(), &err,
                                                  ctx.onProgress, ctx.cancel, explicitOutput(req));
    if (out.isEmpty()) return errorResponse(reqCmd(req), errCodeText(err.code), err.message);
    QJsonObject result;
    result.insert(QStringLiteral("output"), out);
    return okResponse(reqCmd(req), result);
}

QJsonObject cmdExtractVideo(const QJsonObject& req, const Ctx& ctx) {
    ErrInfo err;
    const QString out = FFExtractor::extractVideo(req.value(QStringLiteral("input")).toString(), &err,
                                                  ctx.onProgress, ctx.cancel, explicitOutput(req));
    if (out.isEmpty()) return errorResponse(reqCmd(req), errCodeText(err.code), err.message);
    QJsonObject result;
    result.insert(QStringLiteral("output"), out);
    return okResponse(reqCmd(req), result);
}

// ---- 内置动词表（probe/extract 族：非引擎——探测与流复制，无 IStreamEngine 参与、无引擎参数、
// 错误分类独立；保留专有处理器，spec D3。与引擎注册表并列成 execute 的双查表）----
struct BuiltinVerb {
    const char* id;
    QJsonObject (*handler)(const QJsonObject&, const Ctx&);
    bool allowsOutput;// probe 探测无落盘语义，拒收 output
};

const QList<BuiltinVerb>& builtinVerbs() {
    static const QList<BuiltinVerb> verbs = {
        {"probe", cmdProbe, false},
        {"extractAudio", cmdExtractAudio, true},
        {"extractVideo", cmdExtractVideo, true},
    };
    return verbs;
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
        if (!cmdV.isString()) return errorResponse({}, errCodeText(ErrCode::Params), QStringLiteral("cmd 缺失或不是字符串"));
        cmd = cmdV.toString();
        // —— 双查表：引擎注册表（转码族 5 动词）→ 内置表（probe/extract 族 3 动词）→ 未命中 E_PARAMS ——
        const EngineEntry* entry = nullptr;
        for (const EngineEntry& e : EngineRegistry::engineTable())
            if (e.id == cmd) { entry = &e; break; }
        const BuiltinVerb* builtin = nullptr;
        if (!entry)
            for (const BuiltinVerb& b : builtinVerbs())
                if (cmd == QLatin1String(b.id)) { builtin = &b; break; }
        if (!entry && !builtin)
            return errorResponse({}, errCodeText(ErrCode::Params), QStringLiteral("未知命令：%1").arg(cmd));
        if (request.contains(QStringLiteral("v"))) {
            const QJsonValue v = request.value(QStringLiteral("v"));
            if (!v.isDouble() || v.toInt() != 1)
                return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("v 仅支持 1"));
        }
        // —— 未知字段拒收（动词级白名单由条目/内置表供给；QJsonObject 键序确定 → 报错确定性）——
        const bool allowsOutput = entry ? true : builtin->allowsOutput;
        for (const QString& key : request.keys()) {
            if (key == QLatin1String("v") || key == QLatin1String("cmd") || key == QLatin1String("input")) continue;
            if (key == QLatin1String("output") && allowsOutput) continue;
            if (entry && EngineRegistry::isDeclaredParam(*entry, key)) continue;
            return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("未知字段：%1").arg(key));
        }
        // —— 公共字段：input 必填；output（若允许）非空 + 原件只读防线（FR-A4 根基）——
        QString input;
        const QString ierr = requireString(request, "input", &input);
        if (!ierr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), ierr);
        if (allowsOutput && request.contains(QStringLiteral("output"))) {
            const QJsonValue o = request.value(QStringLiteral("output"));
            if (!o.isString()) return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("output 必须是字符串"));
            if (o.toString().isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("output 不能为空"));
            if (QFileInfo(o.toString()).absoluteFilePath() == QFileInfo(input).absoluteFilePath())
                return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("output 不能与 input 相同（原件只读）"));
        }
        // —— 分发：引擎条目走通用路径，内置动词走专有处理器（if/else 链与 kVerbs/verbFields 已退役）——
        if (entry) return runEngine(*entry, request, ctx);
        return builtin->handler(request, ctx);
    } catch (const std::exception& e) {// 异常屏障：引擎契约不得抛出，命令层自兜底（D3 E_INTERNAL）
        return errorResponse(cmd, errCodeText(ErrCode::Internal), QStringLiteral("内部异常：%1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        return errorResponse(cmd, errCodeText(ErrCode::Internal), QStringLiteral("内部异常（未知异常）"));
    }
}

}// namespace CommandLayer
