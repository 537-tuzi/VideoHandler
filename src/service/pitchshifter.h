#pragma once

#include <QString>
#include <memory>
#include "streamengine.h"

// 变调引擎：SoundTouch 封装，44.1kHz 立体声流式变调（时长不变）
class PitchShifter : public IStreamEngine
{
public:
    PitchShifter();
    ~PitchShifter();
    bool setup(int sampleRate, int channels, double semiTones, QString* err);// 初始化，半音数支持小数
    int inRate() const override;
    int inChannels() const override;
    std::vector<float> process(const float* in, int64_t frames) override;
    std::vector<float> flush() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
