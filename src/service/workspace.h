#pragma once

// 工作区版本化核心（FR-A4 片 A）：内容哈希身份定位工作区 + 版本目录规划 + 两段提交登记 + 只读查询。
// 布局（spec D1）：<root>/index.json（路径缓存，可重建）+ <root>/<stem>.<hash8>/workspace.json（权威索引：
// 源登记 + 版本清单 + head）+ 每版本一目录 vNNN_<cmd>（产物在内，append-only 不可变）。
// 原件只读红线：本模块对源文件只做只读哈希/探测（流式 64KiB，内存有界），落盘全部在 root 之下。
// 并发边界（D7 单写者假设）：GUI busy 锁 / Agent 串行 / CI 串行；同工作区并发提交未定义，v1 不加锁。

#include <QJsonObject>
#include <QString>

#include "errinfo.h"

namespace Workspace {

// 版本规划（planVersion 产出）：任务执行前定位工作区并占位版本目录
struct VersionPlan {
    QString root;// 工作区根（绝对路径）
    QString wsDir;// 工作区目录（<root>/<stem>.<hash8>，身份在内容哈希）
    QString versionDir;// 版本目录（<wsDir>/vNNN_<cmd>，plan 时已建）
    QString cmd;// 动词名（版本目录命名与版本记录用）
    int version = 0;// 版本号 NNN（单调递增，跳过既有孤儿目录不复用）
    QJsonObject input;// 本次任务输入登记 {path, sha256, sizeBytes}
    int parentVersion = 0;// 输入为本工作区某版本产物时的父版本号（0 = 无）
};

// 两段提交第一段：定位/建工作区（根级缓存 stat 命中免哈希；miss 全量 sha256 定身份后回填缓存）+ 规划并
// 占位版本目录。失败不产残迹（新工作区索引写失败会自清所建目录）；错误码在产生点赋值（File 族）
bool planVersion(const QString& root, const QString& input, const QString& cmd, VersionPlan* plan, ErrInfo* err);

// 两段提交第二段：任务成功后原子登记（QSaveFile 重写 workspace.json：追加版本记录 + head 推进 + 同内容
// 换路径时刷新源登记路径）。taskResult 为命令层 ok 响应的 result 对象（取 output 绝对路径与
// outputSpec/frames/durationSec/truncated 如实入记录）。索引写失败 = 任务失败：版本目录（含产物）已由
// 本函数删除、索引保持旧貌，返回 false + E_FILE
bool commitVersion(const VersionPlan& plan, const QJsonObject& params, const QJsonValue& model,
                   const QJsonObject& taskResult, ErrInfo* err);

// 失败/取消清理：删本次版本目录（产物半成品已由底座删，此处兜底整目录），索引不动（只在成功后写）
void discardVersion(const VersionPlan& plan);

// 查询（wsInfo）：只读定位工作区（缓存/哈希），回 {root, dir, exists, source, head, versions}。
// 查询不落盘、绝不顺手建目录；未登记 → exists:false + 空清单；err 仅在源不可读/权威索引损坏时置位
QJsonObject queryInfo(const QString& root, const QString& input, ErrInfo* err);

}// namespace Workspace
