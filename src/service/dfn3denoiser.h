#pragma once

#include <QString>
#include <memory>
#include "streamengine.h"

// AI 降噪（质量档）引擎：DeepFilterNet3 流式语音增强（48kHz 单声道，FR-4 质量档）。
// 前端 DSP（vorbis 窗 STFT/WOLA、32 带 ERB、指数归一特征、lsnr 三档门控、DF 滚动复数滤波、
// 静音跳过）按官方 libDF 数学 C++ 复刻；三张无状态 ONNX（enc/erb_dec/df_dec，GRU 内化于 S 轴）
// 块式 Run：内部块 30s + 非首块 K=100 帧 warmup 前缀重放（洗图内 GRU 状态）；图已内化 2 帧
// 前瞻（行 p 内容滞后特征 2 帧、对应谱帧 τ=p-2），行 p 随帧 p 就绪即发射，无需扣留后缀
//（实现期对 spec 附裁 C 的重解释，正确性见 fr4 round-1 §2）。模型参数一律从模型目录
// config.ini 伴读 + 图形状断言（支持域 = 官方工作点 conv_lookahead=2/fft=2hop/df_order=5）。
class Dfn3Denoiser : public IStreamEngine
{
public:
    Dfn3Denoiser();
    ~Dfn3Denoiser();

    bool setup(const QString& modelDir, QString* err);// 载模型目录（config.ini + enc/erb_dec/df_dec.onnx）并断言图形状
    void setBlockSeconds(double sec);// 内部块秒数（缺省 30；0=整段单 Run——仅 engine_test parity 模式，长素材违内存界）
    int inRate() const override;// 模型工作采样率（config.ini）
    int inChannels() const override { return 1; }// 单声道
    std::vector<float> process(const float* in, int64_t frames) override;
    std::vector<float> flush() override;
    ErrInfo lastError() const override;// 运行期错误（推理失败等），无错为空

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
