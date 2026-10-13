#pragma once

#include <QString>
#include <memory>
#include "streamengine.h"

// 人声分离引擎：UVR MDX-Net 流式推理（44.1kHz 立体声），内部带 STFT/重叠相加状态
class VocalSeparator : public IStreamEngine
{
public:
    VocalSeparator();
    ~VocalSeparator();
    bool setup(const QString& modelPath, QString* err);// 加载模型并校验输入规格
    void setInstrumental(bool on);// 输出伴奏（mix−人声）而非人声，默认人声
    bool ready() const;
    QString lastError() const;// 运行期错误（音频太短等），空为无错
    int inRate() const override;
    int inChannels() const override { return 2; }
    std::vector<float> process(const float* in, int64_t frames) override;
    std::vector<float> flush() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
