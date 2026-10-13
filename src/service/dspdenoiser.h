#pragma once

#include <vector>
#include "streamengine.h"

// DSP 降噪引擎：陷波滤除电流 hum + 高通滤除风声，无滤波器时为直通
class DspDenoiser : public IStreamEngine
{
public:
    void setup(int sampleRate, int channels, bool removeHum, bool removeWind);// 初始化滤波器组
    int inRate() const override;
    int inChannels() const override;
    std::vector<float> process(const float* in, int64_t frames) override;// 副本上原地滤波后返回
    std::vector<float> flush() override { return {}; }// 无内部缓冲，无尾段
private:
    struct Biquad {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;// 已归一化系数
        std::vector<float> z1, z2;// 每声道一组状态
        float run(float x, int ch);// 直接 II 转置型单步
    };
    void run(float* samples, int64_t frames);// 原地滤波核心
    std::vector<Biquad> filters;// 串联滤波器链
    int rate = 0, channels = 0;
};
