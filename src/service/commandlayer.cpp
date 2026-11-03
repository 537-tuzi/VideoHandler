#include "commandlayer.h"

#include "engineregistry.h"
#include "errinfo.h"
#include "ffextractor.h"
#include "fftranscoder.h"
#include "modelmanager.h"
#include "streamengine.h"
#include "model/data.h"
#include "workspace.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QList>
#include <QStringList>

#include <exception>
#include <functional>
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

// 可选字符串（model 族动词的 root/endpoint/for 等）：缺省（无键）→ 空串通过；
// 给了但类型错/空串 → 与 requireString 同款模板报错（宽松缺省 + 严格给定，D5）
QString optionalString(const QJsonObject& req, const char* key, QString* out) {
    const QJsonValue v = req.value(QLatin1String(key));
    if (v.isUndefined()) {
        out->clear();
        return {};
    }
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
    result.insert(QStringLiteral("truncated"), info.truncated);// 截断源如实上报（OQ-5 收口：可解前缀 + 标志）
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

// 版本记录 params：filled 副本剥去信封键（v/cmd/input/output/workspace/model）——引擎参数 + 补缺省值
// （缺省单源 defStr；cmd/input/model 在记录顶层各有归属槽位，不重复入 params）
QJsonObject recordParams(QJsonObject filled) {
    for (const QLatin1String k : {QLatin1String("v"), QLatin1String("cmd"), QLatin1String("input"),
                                  QLatin1String("output"), QLatin1String("workspace"), QLatin1String("model")})
        filled.remove(k);
    return filled;
}

// ---- 工作区两段提交外壳（FR-A4 片 A，spec D2/D4）----
// plan（定位/建工作区 + 占位版本目录）→ runTask 跑任务（落盘进版本目录）→ ok 则 commitVersion 原子登记
// （失败自删版本目录 = 任务失败）；任一环失败/任务失败 → discardVersion 兜底、索引不动。
// 成功响应的 result 注入 workspace:{root,dir,version}（纯增量键，direct 模式不带）
QJsonObject runInWorkspace(const QJsonObject& req, const QJsonObject& params, const QJsonValue& model,
                           const std::function<QJsonObject(const Workspace::VersionPlan&)>& runTask,
                           const QJsonObject& pipelineMarker = {}) {
    const QString cmd = reqCmd(req);
    Workspace::VersionPlan plan;
    ErrInfo werr;
    if (!Workspace::planVersion(req.value(QStringLiteral("workspace")).toString(),
                                req.value(QStringLiteral("input")).toString(), cmd, &plan, &werr))
        return errorResponse(cmd, errCodeText(werr.code), werr.message);
    plan.pipeline = pipelineMarker;// 管线标记（FR-9 D5）：空对象 = 非管线任务，commit 不插键
    const QJsonObject resp = runTask(plan);
    if (!resp.value(QStringLiteral("ok")).toBool()) {// 任务失败：底座已删产物半成品，补删版本目录
        Workspace::discardVersion(plan);
        return resp;
    }
    ErrInfo cerr;
    if (!Workspace::commitVersion(plan, params, model, resp.value(QStringLiteral("result")).toObject(), &cerr))
        return errorResponse(cmd, errCodeText(cerr.code), cerr.message);// 登记失败=任务失败（断链宁可重算）
    QJsonObject result = resp.value(QStringLiteral("result")).toObject();
    QJsonObject ws;
    ws.insert(QStringLiteral("root"), plan.root);
    ws.insert(QStringLiteral("dir"), plan.wsDir);
    ws.insert(QStringLiteral("version"), plan.version);
    result.insert(QStringLiteral("workspace"), ws);
    QJsonObject out = resp;
    out.insert(QStringLiteral("result"), result);
    return out;
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

// ---- 引擎路径静态校验段（FR-9 D3 从 runEngine 原样抽取：validateParams → semanticCheck → 模型存在性；
// 校验顺序与消息文案单一来源，runEngine 与管线整链预校验共用。不调 create——setup 载模型是开工期事，
// 模型装载错误留给该步开工时的既有 E_MODEL/E_ENGINE 路径。返回空对象 = 通过（args 回填补缺省副本），
// 非空 = 完整错误响应（E_PARAMS/E_MODEL 在此赋码）----
QJsonObject staticCheck(const EngineEntry& entry, const QJsonObject& req, QJsonObject* args) {
    const QString verr = EngineRegistry::validateParams(entry, req, args);
    if (!verr.isEmpty()) return errorResponse(entry.id, errCodeText(ErrCode::Params), verr);
    if (entry.semanticCheck) {
        const QString serr = entry.semanticCheck(*args);
        if (!serr.isEmpty()) return errorResponse(entry.id, errCodeText(ErrCode::Params), serr);
    }
    if (entry.requiresModel) {
        const QString model = args->value(QStringLiteral("model")).toString();
        if (!QFileInfo::exists(model))
            return errorResponse(entry.id, errCodeText(ErrCode::Model),
                                 QStringLiteral("找不到模型 %1").arg(model));
    }
    return {};
}

// ---- 引擎路径通用段（条目驱动：校验 → 语义 → 模型存在性 → 工厂 → 转码收尾）----
// 信封校验已在 execute 完成；lockCanonical 与缺省后缀由条目供给，五转码族动词共用（NFR-M1）
QJsonObject runEngine(const EngineEntry& entry, const QJsonObject& req, const Ctx& ctx) {
    const QString& cmd = entry.id;
    QJsonObject args;// 补缺省后的参数副本（defStr 单源）
    const QJsonObject chk = staticCheck(entry, req, &args);
    if (!chk.isEmpty()) return chk;
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
    // 落盘决策（D4）：workspace 非空 → 工作区模式（版本目录规划接管，产物名 = 输入 stem + 条目后缀）；
    // 空 = direct 模式，缺省命名逐字节照旧（零漂移硬门禁）
    if (req.value(QStringLiteral("workspace")).toString().isEmpty())
        return runTranscode(req, ctx, *engine, outputOr(req, entry.outputSuffix(args)), entry.lockCanonicalOutput);
    QJsonValue model;// 模型摘要（D3：路径+size+mtime，无模型动词为 null）
    if (entry.requiresModel) {
        const QFileInfo mf(args.value(QStringLiteral("model")).toString());
        QJsonObject m;
        m.insert(QStringLiteral("path"), mf.absoluteFilePath());
        m.insert(QStringLiteral("sizeBytes"), static_cast<double>(mf.size()));
        m.insert(QStringLiteral("mtimeMs"), static_cast<double>(mf.lastModified().toMSecsSinceEpoch()));
        model = m;
    }
    const QString suffix = entry.outputSuffix(args);// 缺省后缀单一来源：版本产物沿用注册条目后缀自描述
    const QString input = req.value(QStringLiteral("input")).toString();
    QJsonObject pipelineMarker;// 管线子调用标记（FR-9 D5）：cmdPipeline 组子请求时经 Ctx 注入
    if (ctx.pipelineTotal > 0) {
        pipelineMarker.insert(QStringLiteral("index"), ctx.pipelineIndex);
        pipelineMarker.insert(QStringLiteral("total"), ctx.pipelineTotal);
    }
    return runInWorkspace(req, recordParams(args), model, [&](const Workspace::VersionPlan& plan) {
        const QString outPath = plan.versionDir + QStringLiteral("/") + QFileInfo(input).completeBaseName()
                              + suffix + QStringLiteral(".wav");
        return runTranscode(req, ctx, *engine, outPath, entry.lockCanonicalOutput);
    }, pipelineMarker);
}

// ---- 管线动词（FR-9，spec D1–D6/D9）：多遍执行——每步一次完整单动词 execute（组合非复制）----

// 失败步响应改造（D4）：E_* 与原 message 在产生点透传，缀「步骤 N（cmd）：」前缀并加 step/stepCmd
// 归属键（既有消费者只读 code/message 不受影响）；E_CANCELLED 例外——message 逐字不缀前缀
// （GUI onJobDone 对「已取消」全文特判，mainwidget.cpp，缀前缀即行为回归）
QJsonObject stepError(const QString& code, const QString& message, int stepNo, const QString& stepCmd) {
    const QString msg = code == QLatin1String(errCodeText(ErrCode::Cancelled))
                            ? message
                            : QStringLiteral("步骤 %1（%2）：").arg(stepNo).arg(stepCmd) + message;
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), msg);
    error.insert(QStringLiteral("step"), stepNo);
    error.insert(QStringLiteral("stepCmd"), stepCmd);
    QJsonObject resp;
    resp.insert(QStringLiteral("v"), 1);
    resp.insert(QStringLiteral("ok"), false);
    resp.insert(QStringLiteral("cmd"), QStringLiteral("pipeline"));
    resp.insert(QStringLiteral("error"), error);
    return resp;
}

QJsonObject cmdPipeline(const QJsonObject& req, const Ctx& ctx) {
    const QString cmd = QStringLiteral("pipeline");
    // —— ① steps schema：必填/数组/≥2（链式动词语义下限，附裁 2）/元素对象/cmd 命中引擎表/未知字段 ——
    const QJsonValue stepsV = req.value(QStringLiteral("steps"));
    if (stepsV.isUndefined())
        return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("steps 缺失"));
    if (!stepsV.isArray())
        return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("steps 必须是数组"));
    const QJsonArray steps = stepsV.toArray();
    if (steps.size() < 2)
        return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("steps 至少需要 2 步"));
    QList<const EngineEntry*> stepEntries;// 每步命中的引擎条目（schema 段定序，预校验/执行段复用）
    for (int i = 0; i < steps.size(); ++i) {
        const QString where = QStringLiteral("steps[%1]").arg(i);
        const QJsonValue stepV = steps.at(i);
        if (!stepV.isObject())
            return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("%1 必须是对象").arg(where));
        const QJsonObject step = stepV.toObject();
        const QJsonValue stepCmdV = step.value(QStringLiteral("cmd"));
        if (stepCmdV.isUndefined())
            return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("%1 缺少 cmd").arg(where));
        if (!stepCmdV.isString())
            return errorResponse(cmd, errCodeText(ErrCode::Params),
                                 QStringLiteral("%1 的 cmd 必须是字符串").arg(where));
        const EngineEntry* entry = nullptr;
        for (const EngineEntry& e : EngineRegistry::engineTable())
            if (e.id == stepCmdV.toString()) { entry = &e; break; }
        if (!entry)// 内置动词与 pipeline 自身都不在引擎表 → 天然拒收（递归深度有界为 1，D2/D9）
            return errorResponse(cmd, errCodeText(ErrCode::Params),
                                 QStringLiteral("%1 的 cmd 不可链式：%2（仅引擎动词可入链）")
                                     .arg(where, stepCmdV.toString()));
        for (const QString& key : step.keys()) {
            // 步骤允许字段 = {cmd} ∪ isDeclaredParam；链级键 input/output/workspace/v 随之拒收
            //（链位次决定输入、信封决定 workspace——子请求组装单点见下，不会被步骤内键污染）
            if (key == QLatin1String("cmd")) continue;
            if (EngineRegistry::isDeclaredParam(*entry, key)) continue;
            return errorResponse(cmd, errCodeText(ErrCode::Params),
                                 QStringLiteral("%1 未知字段：%2").arg(where, key));
        }
        stepEntries.append(entry);
    }
    // —— ② 整链静态预校验（D3：全部步骤开工前过一遍——静态校验零副作用、毫秒级，「跑完两步才发现
    //    第 3 步 semi:15 越界」不可接受；失败缀步骤前缀同 D4（E_PARAMS/E_MODEL 透传）——
    for (int i = 0; i < steps.size(); ++i) {
        QJsonObject args;
        const QJsonObject chk = staticCheck(*stepEntries.at(i), steps.at(i).toObject(), &args);
        if (!chk.isEmpty()) {
            const QJsonObject err = chk.value(QStringLiteral("error")).toObject();
            return stepError(err.value(QStringLiteral("code")).toString(),
                             err.value(QStringLiteral("message")).toString(), i + 1, stepEntries.at(i)->id);
        }
    }
    // —— ③ workspace 必填（D6/OQ-1(a)：管线只提供工作区模式——direct 中间产物不可追溯不可续跑）——
    const QString wsRoot = req.value(QStringLiteral("workspace")).toString();
    if (wsRoot.isEmpty())
        return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("pipeline 必须带 workspace"));
    // —— ④ 循环组合：每步 execute 子请求（planVersion→转码→commitVersion 全走既有路径，单动词代码零复制；
    //    引擎实例随子请求建毁 → 峰值 RSS = 逐步最大值而非总和，NFR-P4/D1）——
    const int total = steps.size();
    QString input = req.value(QStringLiteral("input")).toString();
    QJsonArray summaries;
    QJsonObject lastWs;// 末步工作区视角（result.workspace = 末步版本）
    bool truncatedAny = false;
    for (int i = 0; i < total; ++i) {
        const QString stepCmd = stepEntries.at(i)->id;
        if (ctx.cancel && ctx.cancel->load())// 每步开工前加查（步间空隙不漏检，NFR-R1 ≤1s 语义）
            return stepError(errCodeText(ErrCode::Cancelled), QStringLiteral("已取消"), i + 1, stepCmd);
        QJsonObject sub = steps.at(i).toObject();// 子请求单点组装（风险 2：链级键只在此并入）
        sub.insert(QStringLiteral("v"), 1);
        sub.insert(QStringLiteral("input"), input);
        sub.insert(QStringLiteral("workspace"), wsRoot);
        CommandLayer::Ctx subCtx;// 进度等分（D4/OQ-2(a)）：第 i 步（0 基）[0,100] 线性映到
        subCtx.cancel = ctx.cancel;// [i*100/total,(i+1)*100/total]，末步 100 即整链 100；cancel 原样传递
        subCtx.pipelineIndex = i + 1;
        subCtx.pipelineTotal = total;
        const int base = i * 100;
        if (ctx.onProgress)
            subCtx.onProgress = [ctx, base, total](int pct) { ctx.onProgress((base + pct) / total); };
        const QJsonObject subResp = execute(sub, subCtx);
        if (!subResp.value(QStringLiteral("ok")).toBool()) {
            // 失败/取消：该步版本目录已在其内部 runInWorkspace discard，前序已提交步保留（D5）
            const QJsonObject err = subResp.value(QStringLiteral("error")).toObject();
            return stepError(err.value(QStringLiteral("code")).toString(),
                             err.value(QStringLiteral("message")).toString(), i + 1, stepCmd);
        }
        const QJsonObject subResult = subResp.value(QStringLiteral("result")).toObject();
        lastWs = subResult.value(QStringLiteral("workspace")).toObject();
        QJsonObject summary = subResult;// 步骤摘要：单动词 result 全量 − workspace + cmd/version 归属
        summary.remove(QStringLiteral("workspace"));
        summary.insert(QStringLiteral("cmd"), stepCmd);
        summary.insert(QStringLiteral("version"), lastWs.value(QStringLiteral("version")));
        summaries.append(summary);
        truncatedAny = truncatedAny || subResult.value(QStringLiteral("truncated")).toBool();
        input = subResult.value(QStringLiteral("output")).toString();// 本步产物即下步输入
    }
    QJsonObject result;
    result.insert(QStringLiteral("output"), input);// 终产物 = 末步产物
    result.insert(QStringLiteral("workspace"), lastWs);
    result.insert(QStringLiteral("steps"), summaries);
    result.insert(QStringLiteral("truncated"), truncatedAny);// 各步真值在 steps[] 内逐项可查（D5）
    return okResponse(cmd, result);
}

// 显式 output（extract 族缺省命名依赖源流编解码器，由底座内部规则决定——此处只取显式值）
QString explicitOutput(const QJsonObject& req) {
    const QJsonValue o = req.value(QStringLiteral("output"));
    return (o.isString() && !o.toString().isEmpty()) ? o.toString() : QString();
}

// extract 族通用处理器：direct 模式与现状逐字节同路；workspace 模式传 outDir=版本目录（缺省命名规则
// 仍是底座单一来源，工作区只给目录不给文件名，D4）；extract 无引擎参数，版本记录 params 为空对象
using ExtractFn = QString (*)(const QString&, ErrInfo*, const std::function<void(int)>&,
                              const std::atomic<bool>*, const QString&, const QString&);

QJsonObject extractVerb(const QJsonObject& req, const Ctx& ctx, ExtractFn fn) {
    const QString cmd = reqCmd(req);
    const QString input = req.value(QStringLiteral("input")).toString();
    auto run = [&](const QString& outPath, const QString& outDir) {
        ErrInfo err;
        const QString out = fn(input, &err, ctx.onProgress, ctx.cancel, outPath, outDir);
        if (out.isEmpty()) return errorResponse(cmd, errCodeText(err.code), err.message);
        QJsonObject result;
        result.insert(QStringLiteral("output"), out);
        return okResponse(cmd, result);
    };
    if (req.value(QStringLiteral("workspace")).toString().isEmpty())
        return run(explicitOutput(req), {});
    return runInWorkspace(req, QJsonObject(), QJsonValue(QJsonValue::Null),
                          [&](const Workspace::VersionPlan& plan) { return run({}, plan.versionDir); });
}

QJsonObject cmdExtractAudio(const QJsonObject& req, const Ctx& ctx) {
    return extractVerb(req, ctx, &FFExtractor::extractAudio);
}

QJsonObject cmdExtractVideo(const QJsonObject& req, const Ctx& ctx) {
    return extractVerb(req, ctx, &FFExtractor::extractVideo);
}

// wsInfo：工作区只读查询（D4）——root 必填（requireString 复用动词级消息模板），定位走缓存/哈希，
// 未登记回空清单（查询不落盘）；源不可读/索引损坏才报错
QJsonObject cmdWsInfo(const QJsonObject& req, const Ctx&) {
    QString root;
    const QString rerr = requireString(req, "root", &root);
    if (!rerr.isEmpty()) return errorResponse(reqCmd(req), errCodeText(ErrCode::Params), rerr);
    ErrInfo err;
    const QJsonObject result = Workspace::queryInfo(root, req.value(QStringLiteral("input")).toString(), &err);
    if (result.isEmpty()) return errorResponse(reqCmd(req), errCodeText(err.code), err.message);
    return okResponse(reqCmd(req), result);
}

// ---- model 族动词（FR-8 片 A，spec D5）：无素材语义（不需要 input），核心在 ModelManager；
// 壳只做字段校验 + Ctx 透传，错误码在产生点赋值后 errCodeText 透传（Net 域为新增七类码）----

// modelList：只读三视角合并（内置目录 ∪ models.json ∪ 磁盘实况）；查询不建目录不落盘
QJsonObject cmdModelList(const QJsonObject& req, const Ctx&) {
    QString root;
    const QString rerr = optionalString(req, "root", &root);
    if (!rerr.isEmpty()) return errorResponse(reqCmd(req), errCodeText(ErrCode::Params), rerr);
    return okResponse(reqCmd(req), ModelManager::listModels(root));
}

// modelDownload：幂等下载（库/appDir 已在位且指纹或大小符合 → 返回现路径零网络请求，D5）
QJsonObject cmdModelDownload(const QJsonObject& req, const Ctx& ctx) {
    const QString cmd = reqCmd(req);
    QString id;
    const QString ierr = requireString(req, "id", &id);
    if (!ierr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), ierr);
    QString root, endpoint;
    QString oerr = optionalString(req, "root", &root);
    if (oerr.isEmpty()) oerr = optionalString(req, "endpoint", &endpoint);
    if (!oerr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), oerr);
    ErrInfo err;
    const ModelManager::Ctx mctx{ctx.cancel, ctx.onProgress};// 取消/进度透传（同型字段直转）
    const QJsonObject result = ModelManager::downloadModel(id, root, endpoint, mctx, &err);
    if (result.isEmpty()) return errorResponse(cmd, errCodeText(err.code), err.message);
    return okResponse(cmd, result);
}

// modelImport：复制进库绝不移动（原件只读）；id/for 二选一语义在 ModelManager 内归口（D6）
QJsonObject cmdModelImport(const QJsonObject& req, const Ctx& ctx) {
    const QString cmd = reqCmd(req);
    QString path;
    const QString perr = requireString(req, "path", &path);
    if (!perr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), perr);
    QString id, forVerb, root;
    QString oerr = optionalString(req, "id", &id);
    if (oerr.isEmpty()) oerr = optionalString(req, "for", &forVerb);
    if (oerr.isEmpty()) oerr = optionalString(req, "root", &root);
    if (!oerr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), oerr);
    ErrInfo err;
    const ModelManager::Ctx mctx{ctx.cancel, ctx.onProgress};
    const QJsonObject result = ModelManager::importModel(id, forVerb, path, root, mctx, &err);
    if (result.isEmpty()) return errorResponse(cmd, errCodeText(err.code), err.message);
    return okResponse(cmd, result);
}

// ---- 内置动词表（probe/extract 族 + wsInfo + model 族 + pipeline：非引擎——探测、流复制、
// 只读查询、模型库管理与多遍管线编排，无 IStreamEngine 参与、无引擎参数、错误分类独立；
// 保留专有处理器，spec D3。与引擎注册表并列成 execute 的双查表）----
struct BuiltinVerb {
    const char* id;
    QJsonObject (*handler)(const QJsonObject&, const Ctx&);
    bool allowsOutput;// probe 探测无落盘语义拒收 output；wsInfo/model 族无落盘语义同拒
    bool allowsWorkspace;// 工作区接管的是落盘动词；无落盘语义的动词同拒 workspace（model 族 D5）
    QList<const char*> extraKeys;// 信封白名单附加键（wsInfo/model 族的专有键；引擎动词的参数由条目供给）
    bool requiresInput = true;// input 必填豁免：model 族管理模型库不碰媒体文件（FR-8 D5）；末位+缺省=旧条目零改
};

const QList<BuiltinVerb>& builtinVerbs() {
    static const QList<BuiltinVerb> verbs = {
        {"probe", cmdProbe, false, false, {}},
        {"extractAudio", cmdExtractAudio, true, true, {}},
        {"extractVideo", cmdExtractVideo, true, true, {}},
        {"wsInfo", cmdWsInfo, false, false, {"root"}},
        {"modelList", cmdModelList, false, false, {"root"}, false},
        {"modelDownload", cmdModelDownload, false, false, {"id", "root", "endpoint"}, false},
        {"modelImport", cmdModelImport, false, false, {"path", "id", "for", "root"}, false},
        // 管线（FR-9 D9）：无引擎参与的编排处理器；拒 output（终产物路径由工作区版本化决定，D6）；
        // workspace 必填（cmdPipeline 内检，缺即 E_PARAMS）；steps 专有键
        {"pipeline", cmdPipeline, false, true, {"steps"}},
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
        const bool requiresInput = entry ? true : builtin->requiresInput;// model 族豁免（D5）：input 随之不进白名单，给了即未知字段
        for (const QString& key : request.keys()) {
            if (key == QLatin1String("v") || key == QLatin1String("cmd")) continue;
            if (key == QLatin1String("input") && requiresInput) continue;
            if (key == QLatin1String("output") && allowsOutput) continue;
            if (key == QLatin1String("workspace")) continue;// workspace 在信封段专检（能力/类型/互斥，见下）
            if (entry && EngineRegistry::isDeclaredParam(*entry, key)) continue;
            if (builtin) {
                bool extra = false;
                for (const char* k : builtin->extraKeys)
                    if (key == QLatin1String(k)) { extra = true; break; }
                if (extra) continue;
            }
            return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("未知字段：%1").arg(key));
        }
        // —— 公共字段：input 必填（model 族豁免——无素材语义，D5）；output（若允许）非空 + 原件只读防线（FR-A4 根基）——
        QString input;
        if (requiresInput) {
            const QString ierr = requireString(request, "input", &input);
            if (!ierr.isEmpty()) return errorResponse(cmd, errCodeText(ErrCode::Params), ierr);
        }
        // —— workspace 字段（FR-A4 D4）：单一路径决策——带 = 命令层定路径，output 同给即互斥违例；
        // 无落盘语义的动词（probe/wsInfo）拒收（与 allowsOutput=false 拒 output 同型）——
        if (request.contains(QStringLiteral("workspace"))) {
            const QJsonValue ws = request.value(QStringLiteral("workspace"));
            const bool allowsWorkspace = entry ? true : builtin->allowsWorkspace;
            if (!allowsWorkspace)
                return errorResponse(cmd, errCodeText(ErrCode::Params),
                                     QStringLiteral("%1 不接受 workspace").arg(cmd));
            if (!ws.isString())
                return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("workspace 必须是字符串"));
            if (ws.toString().isEmpty())
                return errorResponse(cmd, errCodeText(ErrCode::Params), QStringLiteral("workspace 不能为空"));
            if (request.contains(QStringLiteral("output")))
                return errorResponse(cmd, errCodeText(ErrCode::Params),
                                     QStringLiteral("output 与 workspace 不能同给"));
        }
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
