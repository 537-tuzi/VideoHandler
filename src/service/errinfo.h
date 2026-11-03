#pragma once

// 结构化错误通道（NFR-M1 片 B，spec D4）：错误码在产生点与中文文案同点诞生（ErrInfo），
// 消费方透传 code 而非对文案做前缀匹配（P3-1 收口，OQ-2 硬切一刀）。
// ErrCode 七类与命令层响应错误码 E_* 一一对应，None = 无错；串值的唯一来源是 errCodeText。
// Net 为 FR-8 新增（OQ-1 拍板）：网络传输域（DNS/连接拒绝/HTTP 4xx5xx/下载指纹不符）——
// 断网不是模型坏了，塞进 E_MODEL 会误导 NFR-R4 的「引导重新下载」识别。

#include <QString>
#include <QtGlobal>

enum class ErrCode {
    None = 0,// 无错（ErrInfo 空态）
    Params,// 请求参数/信封校验失败（E_PARAMS，命令层校验点产生）
    File,// 源/输出侧文件与媒体 I/O 失败（E_FILE；含源损坏的解码/写包/写尾，OQ-4 承载面修正）
    Model,// 模型缺失/不合规/输出契约违约（E_MODEL，NFR-R4 引导重下载）
    Engine,// 引擎运行期失败（E_ENGINE，推理异常/素材过短等）
    Net,// 网络传输失败（E_NET，FR-8：DNS/连接拒绝/HTTP 4xx5xx/下载指纹或 ETag 与目录不符）
    Cancelled,// 用户取消（E_CANCELLED）
    Internal,// 内部资源失败（内存/重采样器等，E_INTERNAL）
};

// 错误信息：message 为中文文案（GUI error.message 直通展示，逐字不变红线）
struct ErrInfo {
    ErrCode code = ErrCode::None;
    QString message;
    // 无错判定以 code 为准；断言拦「只赋文案漏赋码」的吞错形态（该形态会被 isEmpty 吞掉，
    // 审查 P3-2——产生点约定码与文案同赋，此处开发期兜底该约定）
    bool isEmpty() const {
        Q_ASSERT(code != ErrCode::None || message.isEmpty());
        return code == ErrCode::None;
    }
};

// 错误码 → 对外 JSON 串值（"E_FILE" 等，CMD 断言已锁定）。None 不该有序列化：
// 断言拦开发期，兜底 E_INTERNAL 对齐旧 classify 尾分支语义（片 A P2-1 同款防护）
inline const char* errCodeText(ErrCode code) {
    switch (code) {
    case ErrCode::Params:    return "E_PARAMS";
    case ErrCode::File:      return "E_FILE";
    case ErrCode::Model:     return "E_MODEL";
    case ErrCode::Engine:    return "E_ENGINE";
    case ErrCode::Net:       return "E_NET";
    case ErrCode::Cancelled: return "E_CANCELLED";
    case ErrCode::Internal:  return "E_INTERNAL";
    case ErrCode::None:      break;
    }
    Q_ASSERT(false);
    return "E_INTERNAL";
}
