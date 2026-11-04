#pragma once

#include <QString>
#include <atomic>
#include <functional>

#include "errinfo.h"

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
        bool truncated = false;// 源中途读错（av_read_frame ≠EOF）标志：输出为可解前缀，如实上报（FR-A4 OQ-5 收口）
    };
    // 轻量探测音轨规格：avformat 打开读流参数即关，供调用方在引擎 setup 前取源规格；失败回落兜底。
    // FR-1 track：容器内音频流的绝对流索引（<0 = av_find_best_stream 缺省）。返回 false 仅当
    // track≥0 且不可解析（容器打不开/读不到流信息/track 越界或非音频流）——显式选轨静默回落
    // 兜底规格 = 按错误采样率 setup 引擎的静默错轨，必须硬失败（err 在产生点赋值，E_FILE 族）；
    // track<0 = 现状：探测失败不拦任务（后续 open_decoder 给正式报错），回落兜底规格恒返回 true
    static bool probeSourceSpec(const QString& inPath, int* rate, int* channels,
                                int track = -1, ErrInfo* err = nullptr);
    // outRate/outChannels 传 0（缺省）即输出跟随源；模型锁定工作点的路径（AI/分离）应显式传兜底规格；
    // outInfo 可选出参（缺省 nullptr 不动现有调用方），成功时回填实际规格与帧数；
    // err 为结构化错误（码在产生点赋值：文件/媒体 I/O 族 File，取消 Cancelled，内存/重采样器 Internal）；
    // track（FR-1）：解码选轨，与 probeSourceSpec 的选轨必须同值——两点各开一次容器，错配即
    // 「按缺省轨规格 setup、按指定轨解码」的静默规格错配（spec §7.1 拍板 C 首要风险）
    static QString transcodeStreamWav(const QString& inPath, const QString& outPath,
                                      IStreamEngine& engine, const std::atomic<bool>* cancel,
                                      ErrInfo* err, const ProgressFn& onProgress = {},
                                      int outRate = 0, int outChannels = 0, OutInfo* outInfo = nullptr,
                                      int track = -1);
};
