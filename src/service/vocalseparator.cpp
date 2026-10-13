#include "vocalseparator.h"

#include "util/fft.h"

#include <array>
#include <cmath>
#include <deque>
#include <onnxruntime_cxx_api.h>

namespace {

constexpr int kHop = 1024;// MDX-Net STFT 跳距（UVR 全系 1024）

}// namespace

struct VocalSeparator::Impl {
    Ort::Env env = Ort::Env(ORT_LOGGING_LEVEL_ERROR, "vocal_sep");
    Ort::AllocatorWithDefaultOptions alloc;
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    std::unique_ptr<Ort::Session> sess;
    std::vector<std::string> inNameStore, outNameStore;
    std::vector<const char*> inNames, outNames;
    int dimF = 0, dimT = 0, nFft = 0;

    FftPlan plan;// nFft 点 DFT（Bluestein）
    std::vector<float> window;// 周期 Hann
    std::vector<float> prelude;// 头 win+1 帧暂存，够数后生成反射头（内存有界）
    std::deque<float> tailQ;// 尾 win+1 帧环形，flush 反射用（内存有界）
    std::deque<float> inQ;// P 域待分析样本（P = 反射头 + x + 反射尾，立体声交错）
    int64_t pBase = 0;// inQ 首样本对应的 P 位置
    int64_t nextFrame = 0;// 下一个待分析帧号
    int64_t inputLen = 0;// 已喂入的 x 帧数
    bool headDone = false;
    bool instrumental = false;// 输出伴奏（mix−人声）
    QString errMsg;
    std::deque<float> mixQ;// 伴奏模式下暂存的对齐 mix（滞后一个 WOLA 窗，有界）
    int specCount = 0;// 当前已累积进 tensor 的帧数
    std::vector<float> olaL, olaR, norm;// 重叠相加 + 窗²归一化
    std::deque<float> outQ;// P 域输出样本
    int64_t pOutBase = 0;// outQ 首样本对应的 P 位置
    std::vector<float> reBuf, imBuf, synthRe, synthIm, tensor;// 复用的计算缓冲

    int64_t win() const { return nFft; }

    void feed(const float* in, int64_t frames) {// 喂入 x：进 P 域 + 头暂存 + 尾环形
        for (int64_t i = 0; i < frames; ++i) {
            const float l = in[i * 2];
            const float r = in[i * 2 + 1];
            if (!headDone) {
                prelude.push_back(l);
                prelude.push_back(r);
                if (static_cast<int64_t>(prelude.size()) / 2 >= win() + 1) {
                    for (int64_t k = win(); k >= 1; --k) {// torch reflect 左填充：x[w]..x[1]
                        inQ.push_back(prelude[static_cast<size_t>(k) * 2]);
                        inQ.push_back(prelude[static_cast<size_t>(k) * 2 + 1]);
                    }
                    inQ.insert(inQ.end(), prelude.begin(), prelude.end());
                    prelude.clear();
                    headDone = true;
                }
            } else {
                inQ.push_back(l);
                inQ.push_back(r);
            }
            if (instrumental) {// 伴奏模式：mix 与人声输出同源对齐
                mixQ.push_back(l);
                mixQ.push_back(r);
            }
            tailQ.push_back(l);
            tailQ.push_back(r);
            if (static_cast<int64_t>(tailQ.size()) / 2 > win() + 1) {
                tailQ.pop_front();
                tailQ.pop_front();
            }
        }
        inputLen += frames;
    }

    void analyzeFrame(int64_t f, int slot) {// 频谱直接按模型布局写入 tensor：((plane*dimF)+bin)*dimT + slot
        const int64_t off = f * kHop - pBase;
        for (int i = 0; i < nFft; ++i)
            reBuf[static_cast<size_t>(i)] = inQ[static_cast<size_t>(off + i) * 2] * window[static_cast<size_t>(i)];
        std::fill(imBuf.begin(), imBuf.end(), 0.f);
        plan.run(reBuf.data(), imBuf.data(), -1);
        for (int k = 0; k < dimF; ++k) {
            const size_t base = static_cast<size_t>(k) * dimT + slot;
            tensor[base] = reBuf[static_cast<size_t>(k)];
            tensor[static_cast<size_t>(dimF) * dimT + base] = imBuf[static_cast<size_t>(k)];
        }
        for (int i = 0; i < nFft; ++i)
            reBuf[static_cast<size_t>(i)] = inQ[static_cast<size_t>(off + i) * 2 + 1] * window[static_cast<size_t>(i)];
        std::fill(imBuf.begin(), imBuf.end(), 0.f);
        plan.run(reBuf.data(), imBuf.data(), -1);
        for (int k = 0; k < dimF; ++k) {
            const size_t base = static_cast<size_t>(k) * dimT + slot;
            tensor[static_cast<size_t>(2 * dimF) * dimT + base] = reBuf[static_cast<size_t>(k)];
            tensor[static_cast<size_t>(3 * dimF) * dimT + base] = imBuf[static_cast<size_t>(k)];
        }
    }

    void ifftChannel(const float* spec, int planePair, std::vector<float>& re, std::vector<float>& im) {
        for (int k = 0; k < dimF; ++k) {
            re[static_cast<size_t>(k)] = spec[static_cast<size_t>(planePair * dimF + k)];
            im[static_cast<size_t>(k)] = spec[static_cast<size_t>((planePair + 1) * dimF + k)];
        }
        re[static_cast<size_t>(dimF)] = 0.f;// 奈奎斯特 bin 按模型约定置零
        im[static_cast<size_t>(dimF)] = 0.f;
        for (int k = 1; k < dimF; ++k) {// Hermitian 镜像
            re[static_cast<size_t>(nFft - k)] = re[static_cast<size_t>(k)];
            im[static_cast<size_t>(nFft - k)] = -im[static_cast<size_t>(k)];
        }
        plan.run(re.data(), im.data(), +1);
    }

    void synthesizeFrame(const float* spec) {
        std::move(olaL.begin() + kHop, olaL.end(), olaL.begin());
        std::fill(olaL.end() - kHop, olaL.end(), 0.f);
        std::move(olaR.begin() + kHop, olaR.end(), olaR.begin());
        std::fill(olaR.end() - kHop, olaR.end(), 0.f);
        std::move(norm.begin() + kHop, norm.end(), norm.begin());
        std::fill(norm.end() - kHop, norm.end(), 0.f);
        ifftChannel(spec, 0, synthRe, synthIm);
        for (int i = 0; i < nFft; ++i)
            olaL[static_cast<size_t>(i)] += synthRe[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        ifftChannel(spec, 2, synthRe, synthIm);
        for (int i = 0; i < nFft; ++i)
            olaR[static_cast<size_t>(i)] += synthRe[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        for (int i = 0; i < nFft; ++i)
            norm[static_cast<size_t>(i)] += window[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        for (int i = 0; i < kHop; ++i) {
            const float d = norm[static_cast<size_t>(i)];
            outQ.push_back(d > 1e-9f ? olaL[static_cast<size_t>(i)] / d : 0.f);
            outQ.push_back(d > 1e-9f ? olaR[static_cast<size_t>(i)] / d : 0.f);
        }
    }

    bool runModel() {
        const int have = specCount;
        if (have == 0) return true;
        for (int t = have; t < dimT; ++t)// 尾帧补零
            for (int i = 0; i < dimF * 4; ++i)
                tensor[static_cast<size_t>(i) * dimT + t] = 0.f;
        try {
            std::array<int64_t, 4> shape{1, 4, dimF, dimT};
            Ort::Value x = Ort::Value::CreateTensor<float>(mem, tensor.data(), tensor.size(), shape.data(), 4);
            auto out = sess->Run(Ort::RunOptions{nullptr}, inNames.data(), &x, 1,
                                 outNames.data(), outNames.size());
            const float* ys = out[0].GetTensorData<float>();
            for (int t = 0; t < have; ++t) synthesizeFrame(ys + static_cast<size_t>(t) * dimF * 4);
        } catch (const Ort::Exception& e) {
            errMsg = QStringLiteral("人声分离推理失败：%1").arg(QString::fromUtf8(e.what()));
            return false;
        }
        specCount = 0;
        return true;
    }

    std::vector<float> deliver() {// P 域 → x 域裁剪交付
        std::vector<float> out;
        const int64_t x0 = win();
        const int64_t x1 = win() + inputLen;
        while (!outQ.empty()) {
            const int64_t p = pOutBase;
            if (p < x0) {
                outQ.pop_front();
                outQ.pop_front();
                pOutBase++;
                continue;
            }
            if (p >= x1) break;
            float l = outQ.front();
            outQ.pop_front();
            float r = outQ.front();
            outQ.pop_front();
            if (instrumental) {// 伴奏 = mix − 人声
                l = mixQ.front() - l;
                mixQ.pop_front();
                r = mixQ.front() - r;
                mixQ.pop_front();
            }
            out.push_back(l);
            out.push_back(r);
            pOutBase++;
        }
        return out;
    }
};

VocalSeparator::VocalSeparator() : impl_(std::make_unique<Impl>()) {}

VocalSeparator::~VocalSeparator() = default;

bool VocalSeparator::setup(const QString& modelPath, QString* err) {
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(2);
        impl_->sess = std::make_unique<Ort::Session>(impl_->env, modelPath.toStdWString().c_str(), opts);
        const auto shape = impl_->sess->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || shape[1] != 4) {
            if (err) *err = QStringLiteral("模型输入规格不符，需要 [1,4,dimF,dimT] 的 MDX-Net 模型");
            return false;
        }
        impl_->dimF = static_cast<int>(shape[2]);
        impl_->dimT = static_cast<int>(shape[3]);
        impl_->nFft = impl_->dimF * 2;
        for (size_t i = 0; i < impl_->sess->GetInputCount(); ++i)
            impl_->inNameStore.emplace_back(impl_->sess->GetInputNameAllocated(i, impl_->alloc).get());
        for (size_t i = 0; i < impl_->sess->GetOutputCount(); ++i)
            impl_->outNameStore.emplace_back(impl_->sess->GetOutputNameAllocated(i, impl_->alloc).get());
        for (const auto& s : impl_->inNameStore) impl_->inNames.push_back(s.c_str());
        for (const auto& s : impl_->outNameStore) impl_->outNames.push_back(s.c_str());
        impl_->plan.init(impl_->nFft);
        impl_->window.resize(static_cast<size_t>(impl_->nFft));
        for (int i = 0; i < impl_->nFft; ++i)
            impl_->window[static_cast<size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * cos(2 * kPi * i / impl_->nFft));
        impl_->olaL.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->olaR.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->norm.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->reBuf.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->imBuf.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->synthRe.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->synthIm.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->tensor.assign(static_cast<size_t>(impl_->dimF) * 4 * impl_->dimT, 0.f);
        return true;
    } catch (const Ort::Exception& e) {
        if (err) *err = QStringLiteral("加载人声分离模型失败：%1").arg(QString::fromUtf8(e.what()));
        return false;
    }
}

bool VocalSeparator::ready() const {
    return impl_->sess != nullptr;
}

void VocalSeparator::setInstrumental(bool on) {
    impl_->instrumental = on;
}

QString VocalSeparator::lastError() const {
    return impl_->errMsg;
}

int VocalSeparator::inRate() const {
    return 44100;
}

std::vector<float> VocalSeparator::process(const float* in, int64_t frames) {
    std::vector<float> out;
    if (!ready() || !in || frames <= 0) return out;
    impl_->feed(in, frames);
    if (!impl_->headDone) return out;
    while (true) {
        const int64_t pEnd = impl_->pBase + static_cast<int64_t>(impl_->inQ.size()) / 2;
        const int64_t avail = (pEnd - impl_->win() - impl_->nextFrame * kHop) / kHop + 1;
        if (avail < 1) break;
        impl_->analyzeFrame(impl_->nextFrame, impl_->specCount);
        impl_->nextFrame++;
        impl_->specCount++;
        if (impl_->specCount >= impl_->dimT && !impl_->runModel()) return impl_->deliver();
        const int64_t need = impl_->nextFrame * kHop;// 丢弃已不用的 P 前缀（下一帧起点之前）
        while (impl_->pBase < need && !impl_->inQ.empty()) {
            impl_->inQ.pop_front();
            impl_->inQ.pop_front();
            impl_->pBase++;
        }
    }
    return impl_->deliver();
}

std::vector<float> VocalSeparator::flush() {
    std::vector<float> out;
    if (!impl_->headDone) {// 音频太短，无法构成一帧
        impl_->errMsg = QStringLiteral("音频太短，人声分离至少需要约 0.15 秒");
        return out;
    }
    const int64_t len = impl_->inputLen;
    const int64_t tailN = static_cast<int64_t>(impl_->tailQ.size()) / 2;// 恒为 win+1
    for (int64_t j = 0; j < impl_->win(); ++j) {// torch reflect 右填充：x[len-2], x[len-3], ...
        const int64_t t = (len - 2 - j) - (len - tailN);
        impl_->inQ.push_back(impl_->tailQ[static_cast<size_t>(t) * 2]);
        impl_->inQ.push_back(impl_->tailQ[static_cast<size_t>(t) * 2 + 1]);
    }
    const int64_t pCover = impl_->win() + len;// x 全部 final 所需的帧覆盖线
    while (impl_->nextFrame * kHop < pCover) {
        const int64_t pEnd = impl_->pBase + static_cast<int64_t>(impl_->inQ.size()) / 2;
        const int64_t avail = (pEnd - impl_->win() - impl_->nextFrame * kHop) / kHop + 1;
        if (avail >= 1) {
            impl_->analyzeFrame(impl_->nextFrame, impl_->specCount);
            impl_->nextFrame++;
        } else {// 样本不足补零谱帧推进
            for (int i = 0; i < impl_->dimF * 4; ++i)
                impl_->tensor[static_cast<size_t>(i) * impl_->dimT + impl_->specCount] = 0.f;
        }
        impl_->specCount++;
        if (impl_->specCount >= impl_->dimT) {
            if (!impl_->runModel()) return impl_->deliver();
        }
    }
    if (impl_->specCount > 0) impl_->runModel();
    return impl_->deliver();
}
