#include "engineregistry.h"

#include "aidenoiser.h"
#include "dfn3denoiser.h"
#include "dspdenoiser.h"
#include "pitchshifter.h"
#include "vocalseparator.h"
#include "model/data.h"

#include <QtGlobal>

// 引擎注册表实现（NFR-M1 片 A）：五条目 + 通用校验器 + 工厂闭包同居本文件（spec 风险 3：
// 条目 schema 与闭包类型化抽取一屏内互相印证）。新增引擎在此追加条目即可（D1 改动面收敛点）。

namespace EngineRegistry {
namespace {

// ---- 缺省输出后缀（data.h 常量的 QString 包装；separate 按 stem 双后缀）----

QString suffixTranscoded(const QJsonObject&) { return QString::fromLatin1(kSuffixTranscoded); }
QString suffixDenoised(const QJsonObject&) { return QString::fromLatin1(kSuffixDenoised); }
QString suffixAi(const QJsonObject&) { return QString::fromLatin1(kSuffixAi); }
QString suffixAiHq(const QJsonObject&) { return QString::fromLatin1(kSuffixAiHq); }
QString suffixPitch(const QJsonObject&) { return QString::fromLatin1(kSuffixPitch); }
QString suffixSeparate(const QJsonObject& args) {
    return args.value(QLatin1String("stem")).toString() == QLatin1String("instrumental")
               ? QString::fromLatin1(kSuffixInstrumental)
               : QString::fromLatin1(kSuffixVocals);
}

// ---- 跨字段语义检查（返回错误文案，空 = 通过；args 已补缺省）----

QString semanticDenoise(const QJsonObject& args) {// 与 GUI 拒绝逻辑一致（mainwidget.cpp doDenoiseWav）
    if (args.value(QLatin1String("humBase")).toString() == QLatin1String("off")
        && !args.value(QLatin1String("wind")).toBool())
        return QStringLiteral("humBase 为 off 且 wind 为 false：没有可执行的降噪项");
    return {};
}

// ---- 工厂闭包（吞五引擎 setup 签名异构，引擎头文件零改动；args 已过 schema、缺省已补）----

std::unique_ptr<IStreamEngine> createTranscode(const QJsonObject&, int srcRate, int srcCh, QString*) {
    auto engine = std::make_unique<DspDenoiser>();
    engine->setup(srcRate, srcCh, HumBase::Off, false);// 无滤波即直通（直通也是一种引擎配置，OQ-3）
    return engine;
}

std::unique_ptr<IStreamEngine> createDenoise(const QJsonObject& args, int srcRate, int srcCh, QString*) {
    const QString hum = args.value(QLatin1String("humBase")).toString();// 必为 50/60/off（schema 保证 + 缺省已补）
    const HumBase humBase = hum == QLatin1String("60") ? HumBase::Hz60
                          : hum == QLatin1String("off") ? HumBase::Off : HumBase::Hz50;
    auto engine = std::make_unique<DspDenoiser>();
    engine->setup(srcRate, srcCh, humBase, args.value(QLatin1String("wind")).toBool());
    return engine;
}

std::unique_ptr<IStreamEngine> createAiDenoise(const QJsonObject& args, int, int, QString* err) {
    auto engine = std::make_unique<AiDenoiser>();// 工作点由模型 metadata 锁定，源规格不参与 setup
    if (!engine->setup(args.value(QLatin1String("model")).toString(), err)) return nullptr;
    return engine;
}

std::unique_ptr<IStreamEngine> createAiDenoiseHq(const QJsonObject& args, int, int, QString* err) {
    auto engine = std::make_unique<Dfn3Denoiser>();// 48k 单声道工作点由 config.ini 锁定，源规格不参与 setup
    if (!engine->setup(args.value(QLatin1String("model")).toString(), err)) return nullptr;
    return engine;
}

std::unique_ptr<IStreamEngine> createPitch(const QJsonObject& args, int srcRate, int srcCh, QString* err) {
    auto engine = std::make_unique<PitchShifter>();
    if (!engine->setup(srcRate, srcCh, args.value(QLatin1String("semi")).toDouble(), err)) return nullptr;
    return engine;
}

std::unique_ptr<IStreamEngine> createSeparate(const QJsonObject& args, int, int, QString* err) {
    auto engine = std::make_unique<VocalSeparator>();
    engine->setInstrumental(args.value(QLatin1String("stem")).toString()
                             == QLatin1String("instrumental"));// 伴奏请求须先于 setup 声明
    if (!engine->setup(args.value(QLatin1String("model")).toString(), err)) return nullptr;
    return engine;
}

// ---- 参数 schema（现状五处理器语义的逐字段转录，D2 硬约束 4：缺省值不变；
// 全字段显式初始化：-Wextra 下跳过字段即警告，也防字段序漂移）----

const ParamSpec kHumBase = {// humBase 枚举，缺省 50Hz（对齐旧 CLI/GUI 缺省）
    .name = "humBase",
    .kind = ParamSpec::Kind::Enum,
    .required = false,
    .min = 0,
    .max = 0,
    .hasRange = false,
    .rejectZero = false,
    .values = {"50", "60", "off"},
    .defStr = "50",
};
const ParamSpec kWind = {// wind 布尔，缺省开
    .name = "wind",
    .kind = ParamSpec::Kind::Bool,
    .required = false,
    .min = 0,
    .max = 0,
    .hasRange = false,
    .rejectZero = false,
    .values = {},
    .defStr = "true",
};
const ParamSpec kSemi = {// semi 数字必填，[-12,12] 且非 0（范围对齐 GUI spinSemi）
    .name = "semi",
    .kind = ParamSpec::Kind::Number,
    .required = true,
    .min = -12.0,
    .max = 12.0,
    .hasRange = true,
    .rejectZero = true,
    .values = {},
    .defStr = nullptr,
};
const ParamSpec kStem = {// stem 枚举，缺省 vocals
    .name = "stem",
    .kind = ParamSpec::Kind::Enum,
    .required = false,
    .min = 0,
    .max = 0,
    .hasRange = false,
    .rejectZero = false,
    .values = {"vocals", "instrumental"},
    .defStr = "vocals",
};

// 枚举合法值文案渲染：顿号连接、末值前加「或」（「50、60 或 off」/「vocals 或 instrumental」，D2 硬约束 2）
QString enumValuesText(const QList<const char*>& values) {
    QString text;
    for (int i = 0; i < values.size(); ++i) {
        if (i > 0) text += (i == values.size() - 1) ? QStringLiteral(" 或 ") : QStringLiteral("、");
        text += QLatin1String(values[i]);
    }
    return text;
}

}// namespace

const QList<EngineEntry>& engineTable() {
    // 函数局部静态惰性构造（D5）：首次调用构造（C++11 magic statics 线程安全），无跨翻译单元
    // 初始化顺序问题、无静态析构顺序问题；engine 库不依赖 QCoreApplication 生命周期（engine_test 无 QApplication）
    static const QList<EngineEntry> table = {
        EngineEntry{
            .id = QStringLiteral("transcode"),
            .params = {},
            .requiresModel = false,
            .setupErrorClass = ErrCode::None,
            .lockCanonicalOutput = false,
            .outputSuffix = suffixTranscoded,
            .semanticCheck = nullptr,
            .create = createTranscode,
        },
        EngineEntry{// 校验序：humBase → wind → semantic（现状 cmdDenoise 同序）
            .id = QStringLiteral("denoise"),
            .params = {kHumBase, kWind},
            .requiresModel = false,
            .setupErrorClass = ErrCode::None,
            .lockCanonicalOutput = false,
            .outputSuffix = suffixDenoised,
            .semanticCheck = semanticDenoise,
            .create = createDenoise,
        },
        EngineEntry{// 校验序：model → 存在性（现状 cmdAiDenoise 同序）
            .id = QStringLiteral("aiDenoise"),
            .params = {},
            .requiresModel = true,
            .setupErrorClass = ErrCode::Model,
            .lockCanonicalOutput = true,
            .outputSuffix = suffixAi,
            .semanticCheck = nullptr,
            .create = createAiDenoise,
        },
        EngineEntry{// FR-4 质量档：DeepFilterNet3，48k 工作点但不锁兜底——输出跟随源（NFR-Q3 首个 AI 引擎）
            .id = QStringLiteral("aiDenoiseHq"),
            .params = {},
            .requiresModel = true,
            .setupErrorClass = ErrCode::Model,
            .lockCanonicalOutput = false,
            .outputSuffix = suffixAiHq,
            .semanticCheck = nullptr,
            .create = createAiDenoiseHq,
        },
        EngineEntry{// setup 失败归 Engine（现状 cmdPitch 同类）
            .id = QStringLiteral("pitch"),
            .params = {kSemi},
            .requiresModel = false,
            .setupErrorClass = ErrCode::Engine,
            .lockCanonicalOutput = false,
            .outputSuffix = suffixPitch,
            .semanticCheck = nullptr,
            .create = createPitch,
        },
        EngineEntry{// 校验序：model → stem → 存在性（现状 cmdSeparate 同序）
            .id = QStringLiteral("separate"),
            .params = {kStem},
            .requiresModel = true,
            .setupErrorClass = ErrCode::Model,
            .lockCanonicalOutput = true,
            .outputSuffix = suffixSeparate,
            .semanticCheck = nullptr,
            .create = createSeparate,
        },
    };
    Q_ASSERT([] {// id 唯一性防御（D5：开发期断言，静态表由维护者保证，运行期不做降级逻辑）
        for (int i = 0; i < table.size(); ++i)
            for (int j = i + 1; j < table.size(); ++j)
                if (table[i].id == table[j].id) return false;
        return true;
    }());
    return table;
}

QString validateParams(const EngineEntry& entry, const QJsonObject& req, QJsonObject* filled) {
    QJsonObject args = req;// 副本补缺省后交下游（缺省单源：defStr）
    if (entry.requiresModel) {// model 先于 params 声明序（现状 aiDenoise/separate 即此序，D2 硬约束 2）
        const QJsonValue v = req.value(QLatin1String("model"));
        if (v.isUndefined()) return QStringLiteral("model 缺失");
        if (!v.isString()) return QStringLiteral("model 必须是字符串");
        if (v.toString().isEmpty()) return QStringLiteral("model 不能为空");
    }
    for (const ParamSpec& spec : entry.params) {
        const QLatin1String key(spec.name);
        const QJsonValue v = req.value(key);
        switch (spec.kind) {
        case ParamSpec::Kind::String:
            if (v.isUndefined()) {
                if (spec.required) return QStringLiteral("%1 缺失").arg(key);
                if (spec.defStr) args.insert(key, QLatin1String(spec.defStr));
            } else if (!v.isString()) {
                return QStringLiteral("%1 必须是字符串").arg(key);
            } else if (v.toString().isEmpty()) {
                return QStringLiteral("%1 不能为空").arg(key);
            }
            break;
        case ParamSpec::Kind::Number: {
            if (!v.isDouble()) return QStringLiteral("%1 必须是数字").arg(key);// 涵盖缺失与类型错（现状 semi 语义）
            const double x = v.toDouble();
            if (spec.hasRange && !(x >= spec.min && x <= spec.max))// 否定式：NaN 恒 false → 落拒收（审查 P2-1，D2 硬约束 3）
                return QStringLiteral("%1 须在 [%2, %3] 内").arg(key).arg(spec.min).arg(spec.max);
            if (spec.rejectZero && qFuzzyIsNull(x)) return QStringLiteral("%1 不能为 0").arg(key);
            break;
        }
        case ParamSpec::Kind::Bool:
            if (v.isUndefined()) {
                if (spec.defStr) args.insert(key, QLatin1String(spec.defStr) == QLatin1String("true"));
            } else if (!v.isBool()) {
                return QStringLiteral("%1 必须是布尔值").arg(key);
            }
            break;
        case ParamSpec::Kind::Enum: {
            if (v.isUndefined()) {
                if (spec.defStr) args.insert(key, QLatin1String(spec.defStr));
                break;
            }
            if (!v.isString()) return QStringLiteral("%1 必须是字符串").arg(key);
            const QString s = v.toString();
            bool ok = false;
            for (const char* cand : spec.values)
                if (s == QLatin1String(cand)) { ok = true; break; }
            if (!ok) return QStringLiteral("%1 只接受 %2").arg(key).arg(enumValuesText(spec.values));
            break;
        }
        }
    }
    if (filled) *filled = args;
    return {};
}

bool isDeclaredParam(const EngineEntry& entry, const QString& key) {
    if (entry.requiresModel && key == QLatin1String("model")) return true;
    for (const ParamSpec& spec : entry.params)
        if (key == QLatin1String(spec.name)) return true;
    return false;
}

}// namespace EngineRegistry
