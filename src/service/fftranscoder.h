#pragma once

#include <QString>
#include <atomic>
#include <functional>

class IStreamEngine;

// 解码 → 按引擎期望规格重采样 → 流式引擎 → 输出规格 WAV(f32)：唯一转码通路。
// 输出规格默认跟随源（采样率夹取 8k–192k、声道取 min(源,2)）；kCanonicalRate 仅作兜底。
class FFTranscoder
{
public:
    using ProgressFn = std::function<void(int)>;
    // 实际生效规格与帧数（FR-24 D4 可观测缝）：仅在成功收尾单点回填，失败路径不填
    // （失败时半成品已删，回填即「报告规格与实际文件不符」的静默误报——spec 风险 2）
    struct OutInfo {
        int srcRate = 0, srcChannels = 0;// 解码上下文的源规格真值（探测近似不可替代）
        int outRate = 0, outChannels = 0;// 规整后实际写盘的输出规格
        int64_t frames = 0;// 输出帧数（每声道样本数）
    };
    // 轻量探测音轨规格：avformat 打开读流参数即关，供调用方在引擎 setup 前取源规格；失败回落兜底
    static void probeSourceSpec(const QString& inPath, int* rate, int* channels);
    // outRate/outChannels 传 0（缺省）即输出跟随源；模型锁定工作点的路径（AI/分离）应显式传兜底规格；
    // outInfo 可选出参（缺省 nullptr 不动现有调用方），成功时回填实际规格与帧数
    static QString transcodeStreamWav(const QString& inPath, const QString& outPath,
                                      IStreamEngine& engine, const std::atomic<bool>* cancel,
                                      QString* err, const ProgressFn& onProgress = {},
                                      int outRate = 0, int outChannels = 0, OutInfo* outInfo = nullptr);
};
