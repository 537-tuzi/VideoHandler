#include "aidenoiser.h"

#include "util/fft.h"

#include <cmath>
#include <onnxruntime_cxx_api.h>

// 帧网格与对齐约定（WP-1.1 parity 修复，对齐 sherpa-onnx/knf 参考实现）：
// 前端 = torch.stft(center=True, pad_mode='reflect') 网格：帧 i 覆盖样本 [i*hop-half, i*hop+half)，
//   头部按 numpy reflect（x[-k]=x[k]）由半窗前导缓冲取值；流式实现等价于帧网格整体偏移半窗。
// 合成 = WOLA + 滑动 Σw² 分母补偿（torch.istft 语义）；输出裁掉头部半窗延迟（首帧吞一个 hop）。
// 流末 = 补跑一帧反射尾帧（右端按 x[2N-2-a] 对最后样本镜像），输出总长 = (N/hop)*hop，与参考一致。
// 半窗 half = winLen/2 由 ONNX metadata 推得，勿硬编码。
struct AiDenoiser::Impl {
    Ort::Env env = Ort::Env(ORT_LOGGING_LEVEL_ERROR, "ai_denoiser");
    Ort::AllocatorWithDefaultOptions alloc;
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    std::unique_ptr<Ort::Session> sess;
    std::vector<std::string> inNameStore, outNameStore;// 保活名字符串
    std::vector<const char*> inNames, outNames;
    int mixInIdx = 0, enhOutIdx = 0;// 谱输入/增强输出槽位（按名字映射，防导出顺序变化）
    std::vector<int> cacheInIdx, cacheOutIdx;// 三个状态张量对应的输入/输出槽位
    std::vector<int64_t> convShape, traShape, interShape;// GRU/卷积状态形状
    int sampleRate = 0, nFft = 0, hop = 0, winLen = 0;
    QString windowType;
    std::vector<float> window, analysis, ola, wsum, pending, fftIn;
    std::vector<float> imBuf, specBuf, freBuf, fimBuf;// 复用的计算缓冲
    std::vector<Ort::Value> states;
    bool started = false;// 已跑过首帧（反射头帧吞一个 hop，等价 istft center 裁头）
    bool frame0Done = false;// 反射头帧已建（半窗前导缓冲凑齐）
    int framesDone = 0;// 已跑帧数（flush 反射尾帧的帧序）
    float belowA = 0.f;// 当前帧窗首样本前一格（反射取值最深源，滑动时留底）
    QString errMsg;// 运行期错误（契约：不得抛出，归口此处）
    int64_t totalIn = 0;

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

    // numpy 'reflect' 折叠索引：极短素材反射索引越界时逐级折返（周期 2n-2）
    static int foldIdx(int idx, int n) {
        if (n <= 1) return 0;
        while (idx < 0 || idx >= n)
            idx = idx < 0 ? -idx : 2 * n - 2 - idx;
        return idx;
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

    // 单帧推理：频谱进 → 增强谱出，状态自更新（输入/输出均按 setup 查好的名字槽位就位）
    Ort::Value runFrame(Ort::Value x) {
        std::vector<Ort::Value> inputs(inNames.size());
        inputs[static_cast<size_t>(mixInIdx)] = std::move(x);
        for (size_t k = 0; k < 3; ++k)
            inputs[static_cast<size_t>(cacheInIdx[k])] = std::move(states[k]);
        auto out = sess->Run(Ort::RunOptions{nullptr}, inNames.data(), inputs.data(),
                             inputs.size(), outNames.data(), outNames.size());
        states.clear();
        for (size_t k = 0; k < 3; ++k)
            states.push_back(std::move(out[static_cast<size_t>(cacheOutIdx[k])]));
        return std::move(out[static_cast<size_t>(enhOutIdx)]);
    }

    // 帧窗左移一个 hop：belowA 留底新窗首前一格样本；尾段填 m 个新到真实样本（m<=hop，余下由调用方反射补）
    void slideAnalysis(const float* src, int m) {
        belowA = analysis[static_cast<size_t>(hop) - 1];
        std::move(analysis.begin() + hop, analysis.end(), analysis.begin());
        std::copy(src, src + m, analysis.end() - hop);
    }

    // 绝对 idx 取反射源样本：先按 numpy reflect 折叠，再从 pending（流尾真实样本）/ 滑窗 / belowA 三处解析
    float reflectSrc(int idx, int base, int n) const {
        idx = foldIdx(idx, n);
        const int pendFloor = n - static_cast<int>(pending.size());
        if (idx >= pendFloor) return pending[static_cast<size_t>(idx - pendFloor)];
        if (idx >= base) return analysis[static_cast<size_t>(idx - base)];
        return belowA;// idx==base-1：反射取值最深恰好一格，滑动留底覆盖
    }

    // 跑一帧：analysis 就绪 → 加窗 STFT → 模型 → iSTFT → 窗 OLA 与 Σw² 累加 →（首帧外）除分母发射一个 hop
    void processFrame(std::vector<float>* out) {
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
        std::move(wsum.begin() + hop, wsum.end(), wsum.begin());
        std::fill(wsum.end() - hop, wsum.end(), 0.f);
        for (int i = 0; i < winLen; ++i) {
            const float w = window[static_cast<size_t>(i)];
            ola[static_cast<size_t>(i)] += freBuf[static_cast<size_t>(i)] * w;
            wsum[static_cast<size_t>(i)] += w * w;
        }
        ++framesDone;
        if (!started) {
            started = true;// 首帧（反射头帧）吞一个 hop：等价 istft center 裁头
            return;
        }
        for (int i = 0; i < hop; ++i) {
            const float d = wsum[static_cast<size_t>(i)];
            out->push_back(d > 1e-8f ? ola[static_cast<size_t>(i)] / d : 0.f);
        }
    }

    // 喂入驱动：凑齐半窗+1 建反射头帧（帧 0），此后帧窗凑满即滑动跑帧；输出可滞后，flush 收尾
    void feedPending(std::vector<float>* out) {
        const int half = winLen / 2;
        if (!frame0Done) {
            if (static_cast<int>(pending.size()) < half + 1) return;// 反射头需 x[half]，暂存前导半窗
            for (int j = 0; j < half; ++j)
                analysis[static_cast<size_t>(j)] = pending[static_cast<size_t>(half - j)];// P[0..half)=x[half..1] 反射
            std::copy(pending.begin(), pending.begin() + half, analysis.begin() + half);
            pending.erase(pending.begin(), pending.begin() + half);
            frame0Done = true;
            processFrame(out);
        }
        const int need = winLen - half;// 帧窗右端补满所需新样本数（GTCRN hop==half 时即 hop）
        while (static_cast<int>(pending.size()) >= need) {
            slideAnalysis(pending.data(), hop);
            pending.erase(pending.begin(), pending.begin() + hop);
            processFrame(out);
        }
    }

    // 流末冲刷：补跑反射尾帧（帧序=framesDone，右端真实样本尽后按 x[2N-2-a] 镜像）；输出总长 = (N/hop)*hop
    void flushTail(std::vector<float>* out) {
        const int n = static_cast<int>(totalIn);
        const int half = winLen / 2;
        if (n < hop) return;// 参考帧数约定 N/hop：不足一帧零输出
        if (!frame0Done) {// 仅 N∈[hop, half+1) 可达（更长流 feedPending 已建帧 0）
            for (int j = 0; j < winLen; ++j) {
                const int a = j < half ? half - j : j - half;
                analysis[static_cast<size_t>(j)] = pending[static_cast<size_t>(foldIdx(a, n))];
            }
            pending.erase(pending.begin(), pending.begin() + half);
            frame0Done = true;
            processFrame(out);
        }
        const int base = framesDone * hop - half;// 尾帧窗首样本绝对位置
        const int m = static_cast<int>(pending.size()) < hop ? static_cast<int>(pending.size()) : hop;
        slideAnalysis(pending.data(), m);
        for (int q = winLen - hop + m; q < winLen; ++q)// 真实样本已尽的右端按 reflect 镜像补齐
            analysis[static_cast<size_t>(q)] = reflectSrc(2 * n - 2 - (base + q), base, n);
        processFrame(out);// 发射末块，凑足 (N/hop)*hop
    }

    void resetStream() {
        analysis.assign(analysis.size(), 0.f);
        ola.assign(ola.size(), 0.f);
        wsum.assign(wsum.size(), 0.f);
        pending.clear();
        started = frame0Done = false;
        framesDone = 0;
        belowA = 0.f;
        totalIn = 0;
        states = initStates();
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
        if (impl_->nFft != impl_->winLen) {// fft_forward 按 winLen 缓冲跑 nFft 点，隐含假设二者相等
            if (err) *err = QStringLiteral("模型规格不支持：n_fft=%1 与 window_length=%2 不等")
                             .arg(impl_->nFft).arg(impl_->winLen);
            return false;
        }
        if (impl_->hop <= 0 || 2 * impl_->hop != impl_->winLen) {
            // 帧驱动按「每滑一步吃满 hop 新样本 + 反射最深源恰差一格（belowA 单留底）」实现，
            // 仅在 hop==winLen/2（50% 重叠）下成立且已对照参考验证；GTCRN 导出恒为此规格
            if (err) *err = QStringLiteral("模型规格不支持：hop_length=%1 须 == window_length/2（当前 %2）")
                             .arg(impl_->hop).arg(impl_->winLen);
            return false;
        }
        {// 按名字映射槽位：缓存输入 ↔ <名>_out 输出；谱输入/增强输出为各自剩余者
            auto findIdx = [](const std::vector<std::string>& v, const std::string& n) {
                for (size_t i = 0; i < v.size(); ++i)
                    if (v[i] == n) return static_cast<int>(i);
                return -1;
            };
            const char* caches[3] = {"conv_cache", "tra_cache", "inter_cache"};
            impl_->cacheInIdx.assign(3, -1);
            impl_->cacheOutIdx.assign(3, -1);
            int inUsed = 0, outUsed = 0;
            for (int k = 0; k < 3; ++k) {
                impl_->cacheInIdx[static_cast<size_t>(k)] = findIdx(impl_->inNameStore, caches[k]);
                impl_->cacheOutIdx[static_cast<size_t>(k)] =
                    findIdx(impl_->outNameStore, std::string(caches[k]) + "_out");
                if (impl_->cacheInIdx[static_cast<size_t>(k)] < 0
                    || impl_->cacheOutIdx[static_cast<size_t>(k)] < 0) {
                    if (err) *err = QStringLiteral("模型输入/输出名与 GTCRN 约定不符（缓存张量应名 %1/_out）")
                                     .arg(QLatin1String(caches[k]));
                    return false;
                }
                inUsed |= 1 << impl_->cacheInIdx[static_cast<size_t>(k)];
                outUsed |= 1 << impl_->cacheOutIdx[static_cast<size_t>(k)];
            }
            impl_->mixInIdx = 0;
            while (inUsed & (1 << impl_->mixInIdx)) impl_->mixInIdx++;
            impl_->enhOutIdx = 0;
            while (outUsed & (1 << impl_->enhOutIdx)) impl_->enhOutIdx++;
        }
        impl_->window = impl_->makeWindow();
        impl_->analysis.assign(static_cast<size_t>(impl_->winLen), 0.f);
        impl_->ola.assign(static_cast<size_t>(impl_->winLen), 0.f);
        impl_->wsum.assign(static_cast<size_t>(impl_->winLen), 0.f);
        impl_->fftIn.assign(static_cast<size_t>(impl_->winLen), 0.f);
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
    impl_->totalIn += frames;
    impl_->pending.insert(impl_->pending.end(), in, in + frames);
    try {// 契约：不得抛出，Run 异常归口 lastError
        impl_->feedPending(&out);
    } catch (const Ort::Exception& e) {
        impl_->errMsg = QStringLiteral("AI 降噪推理失败：%1").arg(QString::fromUtf8(e.what()));
    }
    return out;
}

std::vector<float> AiDenoiser::flush() {
    std::vector<float> out;
    if (!ready()) return out;
    try {// 契约：不得抛出，Run 异常归口 lastError
        impl_->flushTail(&out);
    } catch (const Ort::Exception& e) {
        impl_->errMsg = QStringLiteral("AI 降噪推理失败：%1").arg(QString::fromUtf8(e.what()));
    }
    impl_->resetStream();
    return out;
}
