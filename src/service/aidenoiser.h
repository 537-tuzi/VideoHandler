#pragma once

#include <QString>
#include <memory>
#include "streamengine.h"

// AI 降噪引擎：GTCRN 流式语音增强（16kHz 单声道），内部带 STFT/重叠相加状态
class AiDenoiser : public IStreamEngine
{
public:
    AiDenoiser();
    ~AiDenoiser();
    bool setup(const QString& modelPath, QString* err);// 加载模型并读元数据
    bool ready() const;
    int inRate() const override;// 模型工作采样率
    int inChannels() const override { return 1; }// 单声道
    std::vector<float> process(const float* in, int64_t frames) override;
    std::vector<float> flush() override;
    QString lastError() const override;// 运行期错误（推理失败等），空为无错
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
