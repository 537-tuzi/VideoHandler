#pragma once

#include <QString>
#include <vector>

// 流式引擎统一接口：喂入交错样本，内部缓冲可致输出滞后；转码轨道按期望规格自动重采样
class IStreamEngine
{
public:
    virtual ~IStreamEngine() = default;
    virtual int inRate() const = 0;// 期望输入采样率
    virtual int inChannels() const = 0;// 期望输入声道数
    virtual std::vector<float> process(const float* in, int64_t frames) = 0;// 喂入并取回输出
    virtual std::vector<float> flush() = 0;// 流末冲刷，终态调用，之后不得再 process
    virtual QString lastError() const { return {}; }// 运行期错误，空为无错
};
