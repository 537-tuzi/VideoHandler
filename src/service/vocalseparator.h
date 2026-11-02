#pragma once

#include <QString>
#include <memory>
#include "streamengine.h"

// 人声分离引擎：UVR MDX-Net 流式推理（44.1kHz 立体声），内部带 STFT/重叠相加状态
// 模型参数按「文件末尾 10000 KiB MD5 查内置 UVR 官方表」取得（MDX ONNX 无 metadata），
// 查不到即拒绝运行；输出按 plane-major [1,4,dimF,dimT] 契约校验后合成。
class VocalSeparator : public IStreamEngine
{
public:
    VocalSeparator();
    ~VocalSeparator();
    bool setup(const QString& modelPath, QString* err);// 加载模型 + 查表取参 + 规格校验
    void setInstrumental(bool on);// 请求伴奏输出：主 stem 为人声的模型给 mix − compensate×人声，反之直出
    bool ready() const;
    ErrInfo lastError() const override;// 运行期错误（音频太短等），无错为空
    int inRate() const override;
    int inChannels() const override { return 2; }
    std::vector<float> process(const float* in, int64_t frames) override;
    std::vector<float> flush() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
