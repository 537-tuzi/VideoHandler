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
    // 轻量探测音轨规格：avformat 打开读流参数即关，供调用方在引擎 setup 前取源规格；失败回落兜底
    static void probeSourceSpec(const QString& inPath, int* rate, int* channels);
    // outRate/outChannels 传 0（缺省）即输出跟随源；模型锁定工作点的路径（AI/分离）应显式传兜底规格
    static QString transcodeStreamWav(const QString& inPath, const QString& outPath,
                                      IStreamEngine& engine, const std::atomic<bool>* cancel,
                                      QString* err, const ProgressFn& onProgress = {},
                                      int outRate = 0, int outChannels = 0);
};
