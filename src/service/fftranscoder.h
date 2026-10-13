#pragma once

#include <QString>
#include <atomic>
#include <functional>

class IStreamEngine;

// 解码 → 按引擎期望规格重采样 → 流式引擎 → 44.1kHz 立体声 → WAV：唯一转码通路
class FFTranscoder
{
public:
    using ProgressFn = std::function<void(int)>;
    static QString transcodeStreamWav(const QString& inPath, const QString& outPath,
                                      IStreamEngine& engine, const std::atomic<bool>* cancel,
                                      QString* err, const ProgressFn& onProgress = {});
};
