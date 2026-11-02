#pragma once

// 引擎注册表（NFR-M1 片 A）：转码族动词的自描述条目——动词名 / 参数 schema / 工厂闭包 / 错误归类 /
// 输出规格策略 / 缺省后缀。新增引擎 = 新 .h/.cpp + engineTable() 追加条目 + CMake 两行，
// 命令层 / GUI / 服务层零改（NFR-M1「新增引擎注册即枚举」的实现载体，FR-24 留缝的兑现）。
// 严格校验语义与消息模板的唯一权威在 validateParams（与条目同居 engineregistry.cpp）。

#include <QJsonObject>
#include <QList>
#include <QString>

#include <memory>

#include "errinfo.h"

class IStreamEngine;

namespace EngineRegistry {

// 参数 schema 单项：Kind 决定校验模板（消息文案见 validateParams，与旧处理器逐字兼容）
struct ParamSpec {
    const char* name = "";// 字段名（如 "humBase"）
    enum class Kind { String, Number, Bool, Enum } kind = Kind::String;
    bool required = false;// String：缺失即「%1 缺失」；Number：必填（缺失落「%1 必须是数字」，现状 semi 语义）
    double min = 0, max = 0;// Number：范围端点
    bool hasRange = false;// Number：启用范围校验
    bool rejectZero = false;// Number：拒绝 0（qFuzzyIsNull 语义，对齐 GUI 拒绝逻辑）
    QList<const char*> values;// Enum：合法值（顿号连接、末值前「或」渲染进错误文案）
    const char* defStr = nullptr;// 缺省值（字符串形态；nullptr = 无缺省。Bool 以 "true"/"false" 表达）
};

// 转码族动词条目（表驱动分派的数据源；语义按现状五处理器逐字段转录，spec D2 落表）
struct EngineEntry {
    QString id;// 动词名 = cmd（ASCII 小驼峰，与现动词同风格）
    QList<ParamSpec> params;// 参数 schema（按声明序校验、首个错误即返）
    bool requiresModel = false;// model 必填字符串 + 存在性检查 → E_MODEL「找不到模型 %1」
    ErrCode setupErrorClass = ErrCode::None;// create/setup 失败归类（Model 模型引擎 / Engine pitch 等）；None = setup 不会失败
    bool lockCanonicalOutput = false;// 输出锁兜底 44.1k/2ch（模型工作点，P0 #4 有意保留）
    QString (*outputSuffix)(const QJsonObject& args);// 缺省输出后缀（可依赖参数，如 separate 按 stem）
    QString (*semanticCheck)(const QJsonObject& args);// 跨字段校验：返回错误文案，空 = 通过（args 已补缺省）
    std::unique_ptr<IStreamEngine> (*create)(const QJsonObject& args, int srcRate, int srcCh,
                                             QString* err);// 工厂闭包：吞引擎 setup 异构；失败置 *err 返回 nullptr
};

// 中央静态表（编译期注册，D1；函数局部静态惰性构造，D5——首次调用构造、线程安全、无跨翻译单元顺序问题）
const QList<EngineEntry>& engineTable();

// 通用校验器（D2：schema 是唯一校验权威）：requiresModel 的 model 先于 params 声明序，
// 逐项检查、首个错误即返（多错并存时报错与旧处理器逐字一致）；
// filled 非空时回填「补缺省后的参数副本」，供 semanticCheck / outputSuffix / create 消费（缺省单源：defStr）
QString validateParams(const EngineEntry& entry, const QJsonObject& req, QJsonObject* filled = nullptr);

// 动词允许的专有字段判定（信封白名单的条目供给，D2 硬约束 1）：params 名集 ∪（requiresModel ? {model} : {}）
bool isDeclaredParam(const EngineEntry& entry, const QString& key);

}// namespace EngineRegistry
