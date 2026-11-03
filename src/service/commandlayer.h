#pragma once

// 无头命令层核心（FR-24）：JSON 请求 → JSON 响应的纯函数分发，与传输/线程模型解耦。
// CLI 壳（main.cpp --json）与 GUI worker 壳（ffextractor 槽，片 D）是同一核心的两个客户端；
// 响应形：{"v":1,"ok":true,"cmd":...,"result":{...}} / {"v":1,"ok":false,"cmd":...,"error":{"code":...,"message":...}}
// （cmd 能识别才回显；键序由 QJsonObject 字典序保证，输出确定性可断言）

#include <QJsonObject>
#include <atomic>
#include <functional>

namespace CommandLayer {

// 调用上下文：取消标志与进度回调由壳注入（CLI 传空，GUI 片 D 注入）。
// FR-9 D5/附裁 4：pipelineIndex/pipelineTotal 为管线子调用的内部标记通路——cmdPipeline 逐步组合
// execute 时注入，runInWorkspace 据此给版本记录插 pipeline:{index,total} 键；缺省 0 = 非管线调用
// 不插键（对外请求面无此字段，信封白名单零触碰）
struct Ctx {
    const std::atomic<bool>* cancel = nullptr;// GUI 注入取消标志（CLI 传空）
    std::function<void(int)> onProgress;// GUI 注入进度回调（CLI 不接）
    int pipelineIndex = 0;// 当前管线步骤序号（1 基；0 = 非管线调用）
    int pipelineTotal = 0;// 管线总步数（0 = 非管线调用）
};

// 执行一条请求：纯函数、无全局态、每次调用自建引擎实例（可串行重入，FR-10 前提）；
// 任何失败（校验/文件/模型/引擎错误与内部异常）都以 ok:false 响应返回，不抛出
QJsonObject execute(const QJsonObject& request, const Ctx& ctx = {});

// 响应构造（公开给壳复用，如 CLI 侧 stdin JSON 解析失败时组 E_PARAMS）
QJsonObject okResponse(const QString& cmd, const QJsonObject& result);
QJsonObject errorResponse(const QString& cmd, const char* code, const QString& message);

}// namespace CommandLayer
