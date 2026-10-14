#include "aidenoiser.h"

#include "util/fft.h"

#include <cmath>
#include <deque>
#include <onnxruntime_cxx_api.h>

struct AiDenoiser::Impl {
    Ort::Env env = Ort::Env(ORT_LOGGING_LEVEL_ERROR, "ai_denoiser");
    Ort::AllocatorWithDefaultOptions alloc;
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    std::unique_ptr<Ort::Session> sess;
    std::vector<std::string> inNameStore, outNameStore;// 保活名字符串
    std::vector<const char*> inNames, outNames;
    std::vector<int64_t> convShape, traShape, interShape;// GRU/卷积状态形状
    int sampleRate = 0, nFft = 0, hop = 0, winLen = 0;
    QString windowType;
    std::vector<float> window, analysis, ola, pending, fftIn, zeroHop;
    std::vector<float> imBuf, specBuf, freBuf, fimBuf;// 复用的计算缓冲
    std::vector<Ort::Value> states;
    bool started = false;
    QString errMsg;// 运行期错误（契约：不得抛出，归口此处）
    int64_t totalIn = 0, totalOut = 0;

    bool readMetaInt(const Ort::ModelMetadata& meta, OrtAllocator* al, const char* key, int* out) {
        auto v = meta.LookupCustomMetadataMapAllocated(key, al);
        if (!v) return false;
        *out = atoi(v.get());
        return true;
    }

    bool readMetaShape(const Ort::ModelMetadata& meta, OrtAllocator* al, const char* key, std::vector<int64_t>* out) {
        auto v = meta.LookupCustomMetadataMapAllocated(key, al);
        if (!v) return false;
        const char* p = v.get();
        while (*p) {// 逗号分隔整数
            char* end = nullptr;
            long val = strtol(p, &end, 10);
            if (end == p) return false;
            out->push_back(val);
            p = *end ? end + 1 : end;
        }
        return !out->empty();
    }

    std::vector<float> makeWindow() {
        std::vector<float> w(static_cast<size_t>(winLen));
        for (int i = 0; i < winLen; ++i) {
            double v = 0;
            if (windowType == QStringLiteral("hann"))
                v = 0.5 - 0.5 * cos(2 * kPi * i / winLen);
            else if (windowType == QStringLiteral("hann_sqrt"))
                v = sqrt(0.5 - 0.5 * cos(2 * kPi * i / winLen));
            else if (windowType == QStringLiteral("vorbis"))
                v = sin(kPi / 2 * pow(sin(kPi * (i + 0.5) / winLen), 2));
            w[static_cast<size_t>(i)] = static_cast<float>(v);
        }
        return w;
    }

    std::vector<Ort::Value> initStates() {
        std::vector<std::vector<int64_t>*> shapes = {&convShape, &traShape, &interShape};
        std::vector<Ort::Value> states;
        states.reserve(shapes.size());
        for (auto* shape : shapes) {
            Ort::Value t = Ort::Value::CreateTensor<float>(
                alloc, shape->data(), shape->size());
            size_t n = 1;
            for (int64_t d : *shape) n *= static_cast<size_t>(d);
            float* p = t.GetTensorMutableData<float>();
            std::fill(p, p + n, 0.f);
            states.push_back(std::move(t));
        }
        return states;
    }

    // 单帧推理：频谱进 → 增强谱出，状态自更新
    Ort::Value runFrame(Ort::Value x) {
        std::vector<Ort::Value> inputs;
        inputs.push_back(std::move(x));
        for (auto& s : states) inputs.push_back(std::move(s));
        auto out = sess->Run(Ort::RunOptions{nullptr}, inNames.data(), inputs.data(),
                             inputs.size(), outNames.data(), outNames.size());
        states.clear();
        for (size_t k = 1; k < out.size(); ++k) states.push_back(std::move(out[k]));
        return std::move(out[0]);
    }

    // 滑一个 hop：分析加窗 → STFT → 模型 → iSTFT → 重叠相加
    void processHop(const float* hopData, std::vector<float>* out) {
        std::move(analysis.begin() + hop, analysis.end(), analysis.begin());
        std::copy(hopData, hopData + hop, analysis.end() - hop);
        for (int i = 0; i < winLen; ++i) fftIn[static_cast<size_t>(i)] = analysis[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        std::fill(imBuf.begin(), imBuf.end(), 0.f);
        fft_forward(fftIn.data(), imBuf.data(), nFft);
        const int bins = nFft / 2 + 1;
        for (int k = 0; k < bins; ++k) {
            specBuf[static_cast<size_t>(2 * k)] = fftIn[static_cast<size_t>(k)];
            specBuf[static_cast<size_t>(2 * k + 1)] = imBuf[static_cast<size_t>(k)];
        }
        Ort::Value x = Ort::Value::CreateTensor<float>(
            mem, specBuf.data(), specBuf.size(),
            std::array<int64_t, 4>{1, bins, 1, 2}.data(), 4);
        Ort::Value enh = runFrame(std::move(x));
        const float* es = enh.GetTensorData<float>();
        std::fill(fimBuf.begin(), fimBuf.end(), 0.f);
        freBuf[0] = es[0];// 直流按实数处理
        freBuf[bins - 1] = es[2 * (bins - 1)];// 奈奎斯特按实数处理
        for (int k = 1; k < bins - 1; ++k) {
            freBuf[static_cast<size_t>(k)] = es[static_cast<size_t>(2 * k)];
            fimBuf[static_cast<size_t>(k)] = es[static_cast<size_t>(2 * k + 1)];
            freBuf[static_cast<size_t>(nFft - k)] = es[static_cast<size_t>(2 * k)];
            fimBuf[static_cast<size_t>(nFft - k)] = -es[static_cast<size_t>(2 * k + 1)];
        }
        fft_inverse(freBuf.data(), fimBuf.data(), nFft);
        std::move(ola.begin() + hop, ola.end(), ola.begin());
        std::fill(ola.end() - hop, ola.end(), 0.f);
        for (int i = 0; i < winLen; ++i)
            ola[static_cast<size_t>(i)] += freBuf[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        if (!started) {
            started = true;// 首帧吞掉一个 hop，补偿 STFT 算法延迟
            return;
        }
        out->insert(out->end(), ola.begin(), ola.begin() + hop);
    }
};

AiDenoiser::AiDenoiser() : impl_(std::make_unique<Impl>()) {}

AiDenoiser::~AiDenoiser() = default;

bool AiDenoiser::setup(const QString& modelPath, QString* err) {
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);// 小模型单线程足够，免线程池开销
#ifdef _WIN32
        impl_->sess = std::make_unique<Ort::Session>(impl_->env, modelPath.toStdWString().c_str(), opts);
#else
        impl_->sess = std::make_unique<Ort::Session>(impl_->env, modelPath.toStdString().c_str(), opts);// 非 Windows 走窄路径（UTF-8）
#endif
        Ort::AllocatorWithDefaultOptions al;
        Ort::ModelMetadata meta = impl_->sess->GetModelMetadata();
        for (size_t i = 0; i < impl_->sess->GetInputCount(); ++i) {
            auto p = impl_->sess->GetInputNameAllocated(i, al);
            impl_->inNameStore.emplace_back(p.get());
        }
        for (size_t i = 0; i < impl_->sess->GetOutputCount(); ++i) {
            auto p = impl_->sess->GetOutputNameAllocated(i, al);
            impl_->outNameStore.emplace_back(p.get());
        }
        for (const auto& s : impl_->inNameStore) impl_->inNames.push_back(s.c_str());
        for (const auto& s : impl_->outNameStore) impl_->outNames.push_back(s.c_str());
        bool ok = impl_->readMetaInt(meta, al, "sample_rate", &impl_->sampleRate) &&
                  impl_->readMetaInt(meta, al, "n_fft", &impl_->nFft) &&
                  impl_->readMetaInt(meta, al, "hop_length", &impl_->hop) &&
                  impl_->readMetaInt(meta, al, "window_length", &impl_->winLen);
        {
            auto v = meta.LookupCustomMetadataMapAllocated("window_type", al);
            if (v) impl_->windowType = QString::fromUtf8(v.get());
        }
        ok = ok && impl_->readMetaShape(meta, al, "conv_cache_shape", &impl_->convShape) &&
             impl_->readMetaShape(meta, al, "tra_cache_shape", &impl_->traShape) &&
             impl_->readMetaShape(meta, al, "inter_cache_shape", &impl_->interShape);
        if (!ok || impl_->windowType.isEmpty() || impl_->inNames.size() != 4 || impl_->outNames.size() != 4) {
            if (err) *err = QStringLiteral("模型元数据不完整，确认是 gtcrn_simple.onnx");
            return false;
        }
        impl_->window = impl_->makeWindow();
        impl_->analysis.assign(static_cast<size_t>(impl_->winLen), 0.f);
        impl_->ola.assign(static_cast<size_t>(impl_->winLen), 0.f);
        impl_->fftIn.assign(static_cast<size_t>(impl_->winLen), 0.f);
        impl_->zeroHop.assign(static_cast<size_t>(impl_->hop), 0.f);
        const int bins = impl_->nFft / 2 + 1;
        impl_->imBuf.assign(static_cast<size_t>(impl_->winLen), 0.f);
        impl_->specBuf.assign(static_cast<size_t>(bins) * 2, 0.f);
        impl_->freBuf.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->fimBuf.assign(static_cast<size_t>(impl_->nFft), 0.f);
        impl_->states = impl_->initStates();
        return true;
    } catch (const Ort::Exception& e) {
        if (err) *err = QStringLiteral("加载 AI 模型失败：%1").arg(QString::fromUtf8(e.what()));
        return false;
    }
}

bool AiDenoiser::ready() const {
    return impl_->sess != nullptr;
}

QString AiDenoiser::lastError() const {
    return impl_->errMsg;
}

int AiDenoiser::inRate() const {
    return impl_->sampleRate;
}

std::vector<float> AiDenoiser::process(const float* in, int64_t frames) {
    std::vector<float> out;
    if (!ready() || !in || frames <= 0) return out;
    const int n = static_cast<int>(frames);
    impl_->totalIn += n;
    impl_->pending.insert(impl_->pending.end(), in, in + n);
    try {// 契约：不得抛出，Run 异常归口 lastError
        while (static_cast<int>(impl_->pending.size()) >= impl_->hop) {
            impl_->processHop(impl_->pending.data(), &out);
            impl_->pending.erase(impl_->pending.begin(), impl_->pending.begin() + impl_->hop);
        }
    } catch (const Ort::Exception& e) {
        impl_->errMsg = QStringLiteral("AI 降噪推理失败：%1").arg(QString::fromUtf8(e.what()));
    }
    impl_->totalOut += static_cast<int64_t>(out.size());
    return out;
}

std::vector<float> AiDenoiser::flush() {
    std::vector<float> out;
    if (!ready()) return out;
    try {// 契约：不得抛出，Run 异常归口 lastError
        if (!impl_->pending.empty()) {// 残余补零成整 hop
            std::vector<float> padded(static_cast<size_t>(impl_->hop), 0.f);
            std::copy(impl_->pending.begin(), impl_->pending.end(), padded.begin());
            impl_->processHop(padded.data(), &out);
            impl_->pending.clear();
        }
        if (impl_->started) impl_->processHop(impl_->zeroHop.data(), &out);
    } catch (const Ort::Exception& e) {
        impl_->errMsg = QStringLiteral("AI 降噪推理失败：%1").arg(QString::fromUtf8(e.what()));
    }
    const int64_t remaining = impl_->totalIn - impl_->totalOut - static_cast<int64_t>(out.size());
    if (remaining < 0 && out.size() > static_cast<size_t>(-remaining))
        out.resize(out.size() - static_cast<size_t>(-remaining));// 总输出不得超总输入
    impl_->totalOut += static_cast<int64_t>(out.size());
    impl_->analysis.assign(impl_->analysis.size(), 0.f);
    impl_->ola.assign(impl_->ola.size(), 0.f);
    impl_->started = false;
    impl_->states = impl_->initStates();
    return out;
}
