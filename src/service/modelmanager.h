#pragma once

// 模型管理核心（FR-8 片 A，spec §2 D1–D6 + FR-4 目录型模型）：内置模型目录静态表（可下载什么，
// 编译期）+ models.json 本地清单（装了什么：QSaveFile 原子写、损坏按空处理、磁盘实况为准）+
// modelList 三视角合并 + modelDownload（QNAM resolve 契约：HEAD 跟重定向 → ETag 指纹对照（仅 HF
// 源）→ Range 续 .part 跨运行续传 → 流式 sha256 → 同卷 rename 原子落位 → 登记）+ modelImport
// （setup 试开 = 引擎既有校验器，复制进库绝不移动）。FR-4 目录型（dfn3）：下载物/导入源 = tar.gz，
// 系统 tar 解包 → 整目录落位登记（keyFiles 完整性 = 在位判据）。仅 Qt Core + Network 依赖；
// QNAM 异步模型由局部 QEventLoop 同步化（命令层单发同步契约不变，spec D3），壳须保证
// QCoreApplication 在位（事件循环前提）。

#include "errinfo.h"

#include <QJsonObject>
#include <atomic>
#include <functional>

namespace ModelManager {

// 调用上下文：取消标志与进度回调由壳注入（CLI 传空，GUI 片 B 注入；与 CommandLayer::Ctx 同型透传）
struct Ctx {
    const std::atomic<bool>* cancel = nullptr;// 块间检查 → abort + E_CANCELLED（.part 保留作续传缓存）
    std::function<void(int)> onProgress;// 进度百分比 0–100（下载块间 0–99、收尾 100；总量未知则不回调）
};

// 只读列表（root 空 = 缺省根 ~/VideoHandler-Models）：内置目录 ∪ models.json ∪ 磁盘实况三视角合并；
// 查询不建目录不落盘（wsInfo 同款纪律）。恒返回 result = {root, models:[{id,name,for,status,
// installed/available/stale,bundled,path?,sizeBytes,sha256,license}]}（path 仅 installed 时带）。
QJsonObject listModels(const QString& root);

// 下载 → 校验 → 落位 → 登记（幂等：库或 appDir 已在位且指纹/大小符合 → ok 返回现路径零网络请求）。
// 失败语义（OQ-6 拍板：.part 是下载缓存非任务产物，NFR-R1 豁免）：取消/连接断 → .part 保留（续传）；
// sha256 不符 / 服务器 200 不支持 Range → 弃 .part 从头；登记失败 → 删最终产物不留孤儿。
// 成功返回 result = {id,path,sha256,sizeBytes}，失败返回空对象并置 *err。
QJsonObject downloadModel(const QString& id, const QString& root, const QString& endpoint,
                          const Ctx& ctx, ErrInfo* err);

// 导入（复制进库绝不移动，原件只读）：id 给定 → 指纹对目录条目后按条目登记（license 取条目值）；
// id 空且 for 给定 → 该动词注册条目的 create 闭包对源文件 setup 试开（D6：setup 即权威校验器）
// → 复制进库登记（id = stem 净化、license = unknown 用户自行核验）；两者皆缺 → E_PARAMS 不猜用途。
// 成功返回 result = {id,path,sha256,sizeBytes,license}，失败返回空对象并置 *err。
QJsonObject importModel(const QString& id, const QString& forVerb, const QString& path,
                        const QString& root, const Ctx& ctx, ErrInfo* err);

}// namespace ModelManager
