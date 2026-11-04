// dfn3denoiser —— DeepFilterNet3 质量档引擎实现（FR-4）。
// 数学复刻参照（本地只读克隆 f1d19bff，只复刻数学不抄代码文本）：
//   libDF/src/lib.rs   —— vorbis 窗 STFT/WOLA、ERB 滤波组、特征归一、帧分析/合成
//   libDF/src/tract.rs —— 帧处理主流程、lsnr 三档门控、DF 滚动滤波、静音跳过、norm alpha
//   libDF/src/bin/enhance_wav.rs —— golden CLI 口径：缺省无后滤波/无衰减限幅/无延迟补偿，
//                                   不足 hop 的尾巴补零，输出长度 = 输入长度（附裁 A 钉死口径）。
//
// 块式 Run 接缝（附裁 C + OQ-2 round-1 实测修订）：导出 ONNX 把图内递归内化为 S 轴算子（无状态
// I/O），单帧 Run 恒从零状态起步、数值不成立——故按块 Run：非首块前缀重放 K=100 帧特征洗态
// （washup 段输出弃用）。块边界按「已处理帧」计数划定，与调用方分块方式无关 → 分块不变性成立
// （engine_test 三种块帧数逐字节一致）。发射对齐（round-1 钉死）：图已内化 conv_lookahead=2——
// enc 行 p 的门控/掩码即帧 p 的输出（行内容滞后特征 2 帧、对应谱帧 τ=p-2，τ<0 为官方零初值滚动谱），
// 行 p 随帧 p 就绪即发射，无需扣留后缀。
// 接缝本质（OQ-2 数据）：enc/erb_dec/df_dec 因果且对 Run 长度逐位稳定（同起点 Run 前缀逐位一致，
// 已实测），但注意力记忆有效无限——重放起点 ≠ 0 时后续行与整段连续态存在不随重放长度消失的偏差
// （K=100→K=5000 实测掩码偏差仅 0.31→0.10，不收敛零）。SEAM 判据据此量化登记（round-1）。
// 官方门控帧不推进解码器递归（erb_dec/df_dec 仅常规帧运行——tract.rs apply_stages 零掩码档
// (false,true,false) m=零张量直返不跑图、旁路档 (false,false,false) 全不跑）——块式下按 lsnr
// 压缩常规帧子序列喂解码器（含 washup 段常规帧洗态）；enc 恒喂全帧；发射游标仅在常规帧推进
//（round-8 修复：round-7 曾在零掩码帧误推进 → 错位 + 越界读，PAR-DF-004 护栏锁死）。
#include "dfn3denoiser.h"

#include "util/fft.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <new>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <string>

namespace {

constexpr int kWarmupFrames = 100;// 非首块前缀重放帧数（spec §7.1 附裁 C / OQ-2）
constexpr double kDefaultBlockSeconds = 30.0;// 内部块缺省 30s（spec §7.1 OQ-2）
constexpr int kSkipRmsGate = 5;// 静音跳过计数门槛（官方 skip_counter > 5）

// ERB 频率换算（lib.rs freq2erb/erb2freq，f32 同构）
inline float freq2erb(float f) { return 9.265f * std::log1p(f / (24.7f * 9.265f)); }
inline float erb2freq(float e) { return 24.7f * 9.265f * (std::exp(e / 9.265f) - 1.f); }

// ERB 滤波组：每 ERB 带的频点数（lib.rs erb_fb 逐句同构：最少 min_nb_freqs 点/带，末带找齐 n/2+1）
std::vector<int> makeErbFb(int sr, int fftSize, int nbBands, int minNbFreqs)
{
    const float nyq = static_cast<float>(sr / 2);
    const float freqWidth = static_cast<float>(sr) / static_cast<float>(fftSize);
    const float erbLow = freq2erb(0.f);
    const float erbHigh = freq2erb(nyq);
    const float step = (erbHigh - erbLow) / static_cast<float>(nbBands);
    std::vector<int> erb(static_cast<size_t>(nbBands), 0);
    int prevFreq = 0, freqOver = 0;
    for (int i = 1; i <= nbBands; ++i) {
        const float f = erb2freq(erbLow + static_cast<float>(i) * step);
        const int fb = static_cast<int>(std::round(f / freqWidth));
        int nbFreqs = fb - prevFreq - freqOver;
        if (nbFreqs < minNbFreqs) {
            freqOver = minNbFreqs - nbFreqs;
            nbFreqs = minNbFreqs;
        } else {
            freqOver = 0;
        }
        erb[static_cast<size_t>(i - 1)] = nbFreqs;
        prevFreq = fb;
    }
    erb[static_cast<size_t>(nbBands - 1)] += 1;// 频点共 fft/2+1 个
    const int tooLarge = std::accumulate(erb.begin(), erb.end(), 0) - (fftSize / 2 + 1);
    if (tooLarge > 0) erb[static_cast<size_t>(nbBands - 1)] -= tooLarge;
    return erb;
}

// 指数归一系数（tract.rs calc_norm_alpha 同构：exp(-dt/tau) 后按 10^p 精度取整至 <1）
float calcNormAlpha(int sr, int hop, float tau)
{
    const float dt = static_cast<float>(hop) / static_cast<float>(sr);
    const float alpha = std::exp(-dt / tau);
    float a = 1.0f, p10 = 1000.f;
    while (a >= 1.0f) {
        a = std::round(alpha * p10) / p10;
        p10 *= 10.f;
    }
    return a;
}

// lsnr 三档门控（tract.rs apply_stages 同构；阈值取 config.ini lsnr_min/lsnr_max，erb/df 同上限）
struct StageGating { bool applyGains; bool applyGainZeros; bool applyDf; };
StageGating applyStages(float lsnr, float minDb, float maxErbDb, float maxDfDb)
{
    if (lsnr < minDb) return {false, true, false};// 纯噪声：零掩码，不跑 DF
    if (lsnr > maxErbDb) return {false, false, false};// 干净语音：全旁路透传
    if (lsnr > maxDfDb) return {true, false, false};// 轻噪：仅一阶（erb=df 阈值时不可达，保官方分支面）
    return {true, false, true};// 常规：一阶 + 二阶
}

}// namespace

// 已处理帧记录：到达期完成静音判定与前端分析/特征（静音跳过帧不入队——官方跳帧不推进任何状态）
struct FrameRec {
    std::vector<float> specRe, specIm;// x(t) 频谱（nFreqs 频点）
    std::vector<float> featErb, featRe, featIm;// enc 特征（nbErb / nbDf×2）
};

// 输出块队列记录（绝对帧序）：静音跳过帧即产即绪；处理帧待其掩码（滞后 2 个已处理帧）随块就绪
struct ChunkRec {
    bool proc = true;// false = 静音跳过（零块）
    int64_t p = 0;// 已处理帧序号（proc=true 时有效）
};

struct Dfn3Denoiser::Impl {
    // ---- 模型参数（config.ini 伴读 + 图形状断言，勿硬编码）----
    int sr = 48000, hop = 480, fftSize = 960, nbErb = 32, nbDf = 96;
    int dfOrder = 5, convLookahead = 2, dfLookahead = 2, nFreqs = 481;
    float alpha = 0.990f, lsnrMin = -15.f, lsnrMax = 35.f;

    // ---- 前端 DSP 状态（跨块连续；lib.rs DFState 同构）----
    std::vector<float> window, analysisMem, synthesisMem;
    float wnorm = 1.f / 960.f;
    std::vector<int> erbFb;// 每 ERB 带频点数
    std::vector<float> meanNormState, unitNormState;
    FftPlan fftPlan;// 960 非 2 的幂：Bluestein 通路（FFT-PR 已验 −132dB）

    // ---- ORT 三会话（PIMPL 隔离；名字槽位绑定防导出顺序漂移）----
    Ort::Env env = Ort::Env(ORT_LOGGING_LEVEL_ERROR, "dfn3_denoiser");
    Ort::AllocatorWithDefaultOptions alloc;
    std::unique_ptr<Ort::Session> encSess, erbSess, dfSess;
    std::vector<const char*> encIn = {"feat_erb", "feat_spec"},
        encOut = {"e0", "e1", "e2", "e3", "emb", "c0", "lsnr"},
        erbIn = {"emb", "e3", "e2", "e1", "e0"}, erbOut = {"m"},
        dfIn = {"emb", "c0"}, dfOut = {"coefs"};// df_dec 第二输出（门控概率）官方运行面不消费，弃

    // ---- 流水线状态（已处理帧空间 + 绝对帧输出队列）----
    std::deque<FrameRec> frames;// 已处理帧 [frontProc, nextProc)：谱 + 特征（warmup 重放与 DF 回溯）
    int64_t frontProc = 0, nextProc = 0;// 谱队列首 / 下一到达的已处理帧
    std::deque<ChunkRec> pendChunks;// 未发射输出块（绝对帧序）
    int64_t blockStartProc = 0;// 下一待跑块的起点（已处理帧空间）；块末 conv_lookahead=2 帧扣留
    int64_t emittedSamples = 0, totalIn = 0;
    int skipCounter = 0;// 静音跳过计数（官方语义：均方<1e-7 或旁路帧 +1，其余清零，>5 跳过）
    double blockSeconds = kDefaultBlockSeconds;
    bool ready = false, flushed = false, failed = false;
    std::vector<float> pending;// 输入样本缓冲（不足 hop 的尾巴）
    size_t pendPos = 0;
    std::vector<float> outBuf;// 顺序就绪的输出样本
    ErrInfo errMsg;

    // ---- 复用计算缓冲 ----
    std::vector<float> fftRe, fftIm, enhRe, enhIm, zeroChunk, zeroSpec;

    int64_t blockFrames() const
    {
        return blockSeconds <= 0.0 ? std::numeric_limits<int64_t>::max()
                                   : llround(blockSeconds * sr / hop);
    }

    // ---- 名字槽位绑定（-1 = 图上无此名，setup 报 E_MODEL）----
    static int nameIndex(const Ort::Session& s, const char* name, bool input)
    {
        const size_t n = input ? s.GetInputCount() : s.GetOutputCount();
        for (size_t i = 0; i < n; ++i) {
            auto nm = input ? s.GetInputNameAllocated(i, Ort::AllocatorWithDefaultOptions{})
                            : s.GetOutputNameAllocated(i, Ort::AllocatorWithDefaultOptions{});
            if (nm.get() && name == std::string(nm.get())) return static_cast<int>(i);
        }
        return -1;
    }

    // 张量静态维断言（expect 负值 = 该维动态不比对；OQ-1(a)：config 伴读参数与图实际形状互证）
    static bool checkShape(const Ort::Session& s, int slot, bool input,
                           const std::vector<int64_t>& expect, const char* name, QString* err)
    {
        auto info = input ? s.GetInputTypeInfo(static_cast<size_t>(slot))
                          : s.GetOutputTypeInfo(static_cast<size_t>(slot));
        auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != expect.size()) {
            *err = QStringLiteral("%1 形状维度数 %2 与预期 %3 不符")
                       .arg(QString::fromLatin1(name)).arg(shape.size()).arg(expect.size());
            return false;
        }
        for (size_t i = 0; i < shape.size(); ++i) {
            if (expect[i] >= 0 && shape[i] >= 0 && shape[i] != expect[i]) {
                *err = QStringLiteral("%1 第 %2 维 %3 与 config.ini 的 %4 不符")
                           .arg(QString::fromLatin1(name)).arg(i).arg(qint64(shape[i])).arg(expect[i]);
                return false;
            }
        }
        return true;
    }

    // ---- 前端：帧分析（lib.rs frame_analysis 同构：vorbis 窗，前半旧样本后半新样本，输出 ×wnorm）----
    void frameAnalysis(const float* input, std::vector<float>& specRe, std::vector<float>& specIm)
    {
        const int half = fftSize - hop;
        for (int i = 0; i < half; ++i)
            fftRe[static_cast<size_t>(i)] = analysisMem[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        for (int i = 0; i < hop; ++i)
            fftRe[static_cast<size_t>(half + i)] = input[i] * window[static_cast<size_t>(half + i)];
        std::fill(fftIm.begin(), fftIm.end(), 0.f);
        fftPlan.run(fftRe.data(), fftIm.data(), -1);
        for (int k = 0; k < nFreqs; ++k) {
            specRe[static_cast<size_t>(k)] = fftRe[static_cast<size_t>(k)] * wnorm;
            specIm[static_cast<size_t>(k)] = fftIm[static_cast<size_t>(k)] * wnorm;
        }
        std::copy(input, input + hop, analysisMem.begin());
    }

    // ---- 前端：ERB 特征（lib.rs feat_erb：带内能量均值 → 10log10 → 指数均值归一 /40）----
    void featErb(const std::vector<float>& re, const std::vector<float>& im, std::vector<float>& out)
    {
        int base = 0;
        for (size_t b = 0; b < erbFb.size(); ++b) {
            const int band = erbFb[b];
            const float k = 1.f / static_cast<float>(band);
            float v = 0.f;
            for (int j = 0; j < band; ++j) {
                const size_t idx = static_cast<size_t>(base + j);
                v += (re[idx] * re[idx] + im[idx] * im[idx]) * k;
            }
            out[b] = std::log10(v + 1e-10f) * 10.f;
            base += band;
        }
        for (size_t b = 0; b < erbFb.size(); ++b) {// band_mean_norm_erb
            float& x = out[b];
            float& s = meanNormState[b];
            s = x * (1.f - alpha) + s * alpha;
            x -= s;
            x /= 40.f;
        }
    }

    // ---- 前端：复数特征（lib.rs feat_cplx：前 nbDf 频点单位归一）----
    void featCplx(const std::vector<float>& re, const std::vector<float>& im,
                  std::vector<float>& outRe, std::vector<float>& outIm)
    {
        for (int f = 0; f < nbDf; ++f) {
            const size_t i = static_cast<size_t>(f);
            const float norm = std::sqrt(re[i] * re[i] + im[i] * im[i]);
            float& s = unitNormState[i];
            s = norm * (1.f - alpha) + s * alpha;
            const float d = std::sqrt(s);
            outRe[i] = re[i] / d;
            outIm[i] = im[i] / d;
        }
    }

    // ---- 前端：帧合成（lib.rs frame_synthesis 同构：厄米特补全 → IFFT(含 1/n) × 窗 → 50% 交叠）----
    void frameSynthesis(const std::vector<float>& specRe, const std::vector<float>& specIm)
    {
        const int half = fftSize - hop;
        std::copy(specRe.begin(), specRe.end(), fftRe.begin());
        std::copy(specIm.begin(), specIm.end(), fftIm.begin());
        for (int k = 1; k < nFreqs - 1; ++k) {// X[n-k] = conj(X[k])（实信号逆变换）
            fftRe[static_cast<size_t>(fftSize - k)] = specRe[static_cast<size_t>(k)];
            fftIm[static_cast<size_t>(fftSize - k)] = -specIm[static_cast<size_t>(k)];
        }
        fftPlan.run(fftRe.data(), fftIm.data(), 1);
        // 官方 realfft 逆变换无归一化（×N），分析侧 wnorm=1/N²/2hop 与其配对；
        // 自研 FFT 逆变换含 1/N → 补 ×fftSize 对齐官方口径（分析×1/960 + 逆×960 = 恒等）
        for (int i = 0; i < fftSize; ++i)
            fftRe[static_cast<size_t>(i)] *= static_cast<float>(fftSize) * window[static_cast<size_t>(i)];
        for (int i = 0; i < hop; ++i)
            outBuf.push_back(fftRe[static_cast<size_t>(i)] + synthesisMem[static_cast<size_t>(i)]);
        for (int i = 0; i < half; ++i)// 50% 交叠：后半覆盖（hop = fft/2 时官方即此路径）
            synthesisMem[static_cast<size_t>(i)] = fftRe[static_cast<size_t>(hop + i)];
    }

    // ---- 到达期：逐帧静音判定 + 分析 + 特征 ----
    void analyzeArrival(const float* input)
    {
        float e = 0.f;
        for (int i = 0; i < hop; ++i) e += input[i] * input[i];
        const float rms = e / static_cast<float>(hop);
        if (rms < 1e-7f) ++skipCounter; else skipCounter = 0;
        if (skipCounter > kSkipRmsGate) {
            pendChunks.push_back(ChunkRec{false, 0});// 静音跳过：零块，不分析不推特征（官方即返回）
            return;
        }
        FrameRec rec;
        rec.specRe.assign(static_cast<size_t>(nFreqs), 0.f);
        rec.specIm.assign(static_cast<size_t>(nFreqs), 0.f);
        rec.featErb.assign(static_cast<size_t>(nbErb), 0.f);
        rec.featRe.assign(static_cast<size_t>(nbDf), 0.f);
        rec.featIm.assign(static_cast<size_t>(nbDf), 0.f);
        frameAnalysis(input, rec.specRe, rec.specIm);
        featErb(rec.specRe, rec.specIm, rec.featErb);
        featCplx(rec.specRe, rec.specIm, rec.featRe, rec.featIm);
        frames.push_back(std::move(rec));
        pendChunks.push_back(ChunkRec{true, nextProc});
        ++nextProc;
    }

    // 沿 S 轴（dims 中的 sAxis 维）按常规帧下标压缩（官方门控帧不喂解码器 → GRU 不推进；
    // 含 washup 段常规帧以洗态）。输出形状 = 源形状仅 S 维换成 R，行内布局与源一致。
    Ort::Value gatherS(const Ort::Value& src, const std::vector<int64_t>& idx, int sAxis)
    {
        std::vector<int64_t> dims = src.GetTensorTypeAndShapeInfo().GetShape();
        const int64_t srcS = dims[static_cast<size_t>(sAxis)];
        const int64_t R = static_cast<int64_t>(idx.size());
        dims[static_cast<size_t>(sAxis)] = R;
        int64_t preCount = 1, restSize = 1;
        for (int k = 0; k < sAxis; ++k) preCount *= dims[static_cast<size_t>(k)];
        for (size_t k = static_cast<size_t>(sAxis) + 1; k < dims.size(); ++k) restSize *= dims[k];
        Ort::Value out = Ort::Value::CreateTensor<float>(alloc, dims.data(), dims.size());
        const float* s = src.GetTensorData<float>();
        float* d = out.GetTensorMutableData<float>();
        for (int64_t o = 0; o < preCount; ++o)
            for (int64_t r = 0; r < R; ++r) {
                const float* from = s + (o * srcS + idx[static_cast<size_t>(r)]) * restSize;
                float* to = d + (o * R + r) * restSize;
                std::copy(from, from + restSize, to);
            }
        return out;
    }

    // ---- 执行一个 NN 块并发射其输出块：enc 输入已处理帧 [S, E)，块 p∈[P,E) 的掩码即 enc 行 p ----
    // S = 首块 0 / 余块 P-K（warmup 重放，washup 段输出弃用）。图内已内化 lookahead=2（行 p 的
    // 掩码内容滞后特征 2 帧、对应谱帧 τ=p-2），行 p 随帧 p 就绪即发射，无需扣留后缀。
    void runBlock(int64_t E)
    {
        const int64_t P = blockStartProc;
        const int64_t S = std::max<int64_t>(0, P - kWarmupFrames);
        const int64_t Ssz = E - S;

        // enc 输入：feat_erb [1,1,S,nbErb]、feat_spec [1,2,S,nbDf]（cplx [ch,1,nbDf,2] 的 [0,3,1,2] 重排）
        const std::vector<int64_t> erbDims{1, 1, Ssz, nbErb}, specDims{1, 2, Ssz, nbDf};
        Ort::Value erbT = Ort::Value::CreateTensor<float>(alloc, erbDims.data(), erbDims.size());
        Ort::Value specT = Ort::Value::CreateTensor<float>(alloc, specDims.data(), specDims.size());
        float* erbData = erbT.GetTensorMutableData<float>();
        float* specData = specT.GetTensorMutableData<float>();
        const int64_t dfPlane = Ssz * nbDf;
        for (int64_t p = 0; p < Ssz; ++p) {
            const FrameRec& rec = frames[static_cast<size_t>(S + p - frontProc)];
            std::copy(rec.featErb.begin(), rec.featErb.end(), erbData + p * nbErb);
            std::copy(rec.featRe.begin(), rec.featRe.end(), specData + p * nbDf);
            std::copy(rec.featIm.begin(), rec.featIm.end(), specData + dfPlane + p * nbDf);
        }
        std::vector<Ort::Value> encInputs;
        encInputs.push_back(std::move(erbT));
        encInputs.push_back(std::move(specT));
        auto encOutV = encSess->Run(Ort::RunOptions{nullptr}, encIn.data(), encInputs.data(),
                                    encInputs.size(), encOut.data(), encOut.size());
        // encOutV 顺序 = encOut 名单 {e0,e1,e2,e3,emb,c0,lsnr}
        const float* lsnrArr = encOutV[6].GetTensorData<float>();// [1,S,1]

        // 门控分类：常规帧才喂解码器（官方门控帧不跑 erb_dec/df_dec → 其递归不推进）
        std::vector<int64_t> regIdx;
        regIdx.reserve(static_cast<size_t>(Ssz));
        for (int64_t p = 0; p < Ssz; ++p) {
            const float l = lsnrArr[static_cast<size_t>(p)];
            if (!(l < lsnrMin) && !(l > lsnrMax)) regIdx.push_back(p);
        }
        const float* mArr = nullptr;
        const float* coefsArr = nullptr;
        std::vector<Ort::Value> keepAlive;// 解码器输出保活（mArr/coefsArr 裸指针指向其数据，发射期用）
        if (!regIdx.empty()) {
            const int64_t R = static_cast<int64_t>(regIdx.size());
            std::vector<Ort::Value> erbInputs;// {emb,e3,e2,e1,e0}，名字序绑定
            if (R == Ssz) {// 无门控帧：enc 输出直接透喂（免整块拷贝，常态路径）
                for (int i : {4, 3, 2, 1, 0}) erbInputs.push_back(std::move(encOutV[static_cast<size_t>(i)]));
            } else {
                erbInputs.push_back(gatherS(encOutV[4], regIdx, 1));// emb（S 轴=1）
                erbInputs.push_back(gatherS(encOutV[3], regIdx, 2));// e3
                erbInputs.push_back(gatherS(encOutV[2], regIdx, 2));// e2
                erbInputs.push_back(gatherS(encOutV[1], regIdx, 2));// e1
                erbInputs.push_back(gatherS(encOutV[0], regIdx, 2));// e0
            }
            auto mOut = erbSess->Run(Ort::RunOptions{nullptr}, erbIn.data(), erbInputs.data(),
                                     erbInputs.size(), erbOut.data(), erbOut.size());
            std::vector<Ort::Value> dfInputs;// {emb,c0}：emb 自 erbInputs 复用（其 Run 已完成）
            dfInputs.push_back(std::move(erbInputs[0]));
            dfInputs.push_back(R == Ssz ? std::move(encOutV[5]) : gatherS(encOutV[5], regIdx, 2));
            auto cOut = dfSess->Run(Ort::RunOptions{nullptr}, dfIn.data(), dfInputs.data(),
                                    dfInputs.size(), dfOut.data(), dfOut.size());
            mArr = mOut[0].GetTensorData<float>();// [1,1,R,nbErb]
            coefsArr = cOut[0].GetTensorData<float>();// [1,R,nbDf,dfOrder*2]
            keepAlive.push_back(std::move(mOut[0]));
            keepAlive.push_back(std::move(cOut[0]));
        }

        // 发射：绝对帧序走输出队列，本块块员 = 已处理帧 [P, E)（含其间静音跳过帧）
        size_t rPos = 0;
        while (rPos < regIdx.size() && S + regIdx[rPos] < P) ++rPos;// 跳过 washup 段常规帧（洗态用）
        while (!pendChunks.empty()) {
            const ChunkRec c = pendChunks.front();
            if (!c.proc) {// 静音跳过帧：零块，无状态推进（官方即返回 enh=0）
                outBuf.insert(outBuf.end(), zeroChunk.begin(), zeroChunk.end());
                emittedSamples += hop;
                pendChunks.pop_front();
                continue;
            }
            if (c.p >= E) break;// 下一块块员
            emitProcChunk(c.p, S, lsnrArr, mArr, coefsArr, &rPos);
            pendChunks.pop_front();
        }
        blockStartProc = E;
        // 回收谱队列前缀：保留 [P'-K, ...)（warmup 重放与 DF 回溯 ≤ 4 帧均覆盖）
        while (frontProc < blockStartProc - kWarmupFrames && frontProc < nextProc) {
            frames.pop_front();
            ++frontProc;
        }
    }

    // ---- 单个处理帧输出块：官方逐帧序 = 门控（enc 行 p）→ 掩码谱帧 τ=p-2 → DF → 合成 ----
    // 行 p 的掩码即 erb_dec/df_dec 以 enc 行 p 为输入的输出（图内已内化 2 帧前瞻对齐）。
    void emitProcChunk(int64_t p, int64_t S, const float* lsnrArr, const float* mArr,
                       const float* coefsArr, size_t* rPos)
    {
        const int64_t tau = p - 2;// 合成的谱帧（τ<0 为官方零初值滚动谱）
        const StageGating g = applyStages(lsnrArr[static_cast<size_t>(p - S)], lsnrMin, lsnrMax, lsnrMax);
        const bool hasTau = tau >= 0;
        if (!g.applyGains && !g.applyGainZeros) {// 旁路：原谱透传
            skipCounter += 1;
            frameSynthesis(hasTau ? frames[static_cast<size_t>(tau - frontProc)].specRe : zeroSpec,
                           hasTau ? frames[static_cast<size_t>(tau - frontProc)].specIm : zeroSpec);
        } else {
            skipCounter = 0;
            if (g.applyGainZeros) {// 零掩码（tract.rs apply_stages=(false,true,false)）：官方该档
                // 不跑 erb_dec/df_dec（m=零张量直返），不占解码器行——游标不推进，行↔常规帧保 1:1
                std::fill(enhRe.begin(), enhRe.end(), 0.f);
                std::fill(enhIm.begin(), enhIm.end(), 0.f);
            } else {
                const float* gains = mArr + static_cast<int64_t>(*rPos) * nbErb;
                const float* coefs = coefsArr + static_cast<int64_t>(*rPos) * nbDf * dfOrder * 2;
                ++*rPos;
                int base = 0;// 一阶：ERB 增益掩码（apply_interp_band_gain，带内平铺乘）
                for (size_t b = 0; b < erbFb.size(); ++b) {
                    for (int j = 0; j < erbFb[b]; ++j) {
                        const size_t idx = static_cast<size_t>(base + j);
                        enhRe[idx] = (hasTau ? frames[static_cast<size_t>(tau - frontProc)].specRe[idx] : 0.f) * gains[b];
                        enhIm[idx] = (hasTau ? frames[static_cast<size_t>(tau - frontProc)].specIm[idx] : 0.f) * gains[b];
                    }
                    base += erbFb[b];
                }
                if (g.applyDf) {// 二阶：DF 滚动复数滤波覆盖 [0, nbDf) 频点（j 升序 = 最旧帧配 j=0；
                    // 滚动谱 xi = τ-2+j = p-4+j，xi<0 为官方零初值滚动谱，零贡献跳过）
                    for (int f = 0; f < nbDf; ++f) {
                        enhRe[static_cast<size_t>(f)] = 0.f;
                        enhIm[static_cast<size_t>(f)] = 0.f;
                    }
                    for (int j = 0; j < dfOrder; ++j) {
                        const int64_t xi = p - 4 + j;
                        if (xi < 0) continue;
                        const FrameRec& xr = frames[static_cast<size_t>(xi - frontProc)];
                        const float* xRe = xr.specRe.data();
                        const float* xIm = xr.specIm.data();
                        for (int f = 0; f < nbDf; ++f) {
                            const size_t fi = static_cast<size_t>(f);
                            const float cr = coefs[static_cast<size_t>((f * dfOrder + j) * 2)];
                            const float ci = coefs[static_cast<size_t>((f * dfOrder + j) * 2 + 1)];
                            enhRe[fi] += xRe[fi] * cr - xIm[fi] * ci;// 复数乘 (xRe+ixIm)(cr+ici)
                            enhIm[fi] += xRe[fi] * ci + xIm[fi] * cr;
                        }
                    }
                }
                // 后滤波/衰减限幅：官方 CLI golden 缺省关（--pf/-a 才启用）；质量档动词不设参数，保持同口径
            }
            frameSynthesis(enhRe, enhIm);
        }
        emittedSamples += hop;
    }

    // ---- 块触发：整块就绪即跑（每到达一帧查一次 → 块边界与调用方分块无关，分块不变性成立）----
    void pumpBlocks(bool final)
    {
        for (;;) {
            const int64_t B = blockFrames();
            if (!final && (B == std::numeric_limits<int64_t>::max()
                           || nextProc - blockStartProc < B))
                break;// parity 模式（块长 0=∞）中途不跑块，整段留 flush 单 Run
            const int64_t E = final ? nextProc : blockStartProc + B;
            if (E <= blockStartProc) break;
            runBlock(E);
            if (final) break;
        }
        // 队首静音跳过帧无需掩码，无块推进也可发射
        while (!pendChunks.empty() && !pendChunks.front().proc) {
            outBuf.insert(outBuf.end(), zeroChunk.begin(), zeroChunk.end());
            emittedSamples += hop;
            pendChunks.pop_front();
        }
    }

    std::vector<float> drainOut()
    {
        if (outBuf.empty()) return {};
        std::vector<float> out;
        out.swap(outBuf);
        return out;
    }

    void fail(const QString& msg)
    {
        failed = true;
        errMsg = {ErrCode::Engine, msg};
        outBuf.clear();
    }
};

Dfn3Denoiser::Dfn3Denoiser() : impl_(std::make_unique<Impl>()) {}

Dfn3Denoiser::~Dfn3Denoiser() = default;

void Dfn3Denoiser::setBlockSeconds(double sec) { impl_->blockSeconds = sec; }

int Dfn3Denoiser::inRate() const { return impl_->sr; }

bool Dfn3Denoiser::setup(const QString& modelDir, QString* err)
{
    if (err) err->clear();
    try {
        auto bail = [&](const QString& m) {
            if (err) *err = m;
            return false;
        };
        const QDir dir(modelDir);
        if (!dir.exists())
            return bail(QStringLiteral("模型目录不完整（需 config.ini + enc/erb_dec/df_dec onnx）：%1").arg(modelDir));
        for (const char* f : {"config.ini", "enc.onnx", "erb_dec.onnx", "df_dec.onnx"})
            if (!QFileInfo::exists(dir.filePath(QString::fromLatin1(f))))
                return bail(QStringLiteral("模型目录不完整（缺 %1）：%2").arg(QString::fromLatin1(f), modelDir));

        QSettings cfg(dir.filePath(QStringLiteral("config.ini")), QSettings::IniFormat);// 只读伴读
        auto getInt = [&](const char* key, int* out) {
            bool ok = false;
            const int x = cfg.value(QLatin1String(key)).toInt(&ok);
            if (!ok) return bail(QStringLiteral("config.ini 缺少或非法键 %1").arg(QLatin1String(key))), false;
            *out = x;
            return true;
        };
        auto getFloat = [&](const char* key, float* out) {
            bool ok = false;
            const float x = cfg.value(QLatin1String(key)).toFloat(&ok);
            if (!ok) return bail(QStringLiteral("config.ini 缺少或非法键 %1").arg(QLatin1String(key))), false;
            *out = x;
            return true;
        };
        int minNbErbFreqs = 0;
        if (!getInt("df/sr", &impl_->sr) || !getInt("df/hop_size", &impl_->hop)
            || !getInt("df/fft_size", &impl_->fftSize) || !getInt("df/nb_erb", &impl_->nbErb)
            || !getInt("df/nb_df", &impl_->nbDf) || !getInt("df/min_nb_erb_freqs", &minNbErbFreqs)
            || !getInt("df/df_order", &impl_->dfOrder) || !getInt("df/df_lookahead", &impl_->dfLookahead)
            || !getInt("deepfilternet/conv_lookahead", &impl_->convLookahead)
            || !getFloat("df/lsnr_min", &impl_->lsnrMin) || !getFloat("df/lsnr_max", &impl_->lsnrMax))
            return false;
        if (cfg.value(QStringLiteral("train/model")).toString() != QLatin1String("deepfilternet3"))
            return bail(QStringLiteral("不支持的模型类型（仅 deepfilternet3）：%1").arg(modelDir));
        if (cfg.contains(QStringLiteral("df/norm_alpha"))) {
            if (!getFloat("df/norm_alpha", &impl_->alpha)) return false;
        } else {
            float tau = 0.f;
            if (!getFloat("df/norm_tau", &tau)) return false;
            impl_->alpha = calcNormAlpha(impl_->sr, impl_->hop, tau);
        }
        if (impl_->hop < 1 || impl_->fftSize != impl_->hop * 2 || impl_->nbErb < 2
            || impl_->nbDf < 1 || impl_->dfOrder != 5 || impl_->convLookahead != 2
            || impl_->dfLookahead < 0 || minNbErbFreqs < 1)
            return bail(QStringLiteral("config.ini 参数非法：支持域 = 官方 DFN3 工作点"
                                       "（conv_lookahead=2、fft=2hop、df_order=5，DF 回溯/帧记忆"
                                       "推进按此设计），其余键须满足 hop>0、nb_erb≥2、nb_df>0"));
        impl_->nFreqs = impl_->fftSize / 2 + 1;

        // 前端构造（lib.rs DFState::new 同构）
        impl_->fftPlan.init(impl_->fftSize);
        const double wh = impl_->fftSize / 2;
        impl_->window.resize(static_cast<size_t>(impl_->fftSize));
        for (int i = 0; i < impl_->fftSize; ++i) {// vorbis 窗：sin(π/2·sin²(π(i+0.5)/fft))，f64 算后落 f32
            const double s = std::sin(0.5 * kPi * (i + 0.5) / wh);
            impl_->window[static_cast<size_t>(i)] = static_cast<float>(std::sin(0.5 * kPi * s * s));
        }
        impl_->wnorm = 1.f / (static_cast<float>(impl_->fftSize * impl_->fftSize)
                              / static_cast<float>(2 * impl_->hop));
        impl_->erbFb = makeErbFb(impl_->sr, impl_->fftSize, impl_->nbErb, minNbErbFreqs);
        impl_->meanNormState.resize(static_cast<size_t>(impl_->nbErb));
        {
            const float step = (-90.f - -60.f) / static_cast<float>(impl_->nbErb - 1);
            for (int i = 0; i < impl_->nbErb; ++i)
                impl_->meanNormState[static_cast<size_t>(i)] = -60.f + static_cast<float>(i) * step;
        }
        impl_->unitNormState.resize(static_cast<size_t>(impl_->nbDf));
        {
            const float step = (0.0001f - 0.001f) / static_cast<float>(impl_->nbDf - 1);
            for (int i = 0; i < impl_->nbDf; ++i)
                impl_->unitNormState[static_cast<size_t>(i)] = 0.001f + static_cast<float>(i) * step;
        }
        impl_->analysisMem.assign(static_cast<size_t>(impl_->fftSize - impl_->hop), 0.f);
        impl_->synthesisMem.assign(static_cast<size_t>(impl_->fftSize - impl_->hop), 0.f);
        impl_->fftRe.assign(static_cast<size_t>(impl_->fftSize), 0.f);
        impl_->fftIm.assign(static_cast<size_t>(impl_->fftSize), 0.f);
        impl_->enhRe.assign(static_cast<size_t>(impl_->nFreqs), 0.f);
        impl_->enhIm.assign(static_cast<size_t>(impl_->nFreqs), 0.f);
        impl_->zeroChunk.assign(static_cast<size_t>(impl_->hop), 0.f);
        impl_->zeroSpec.assign(static_cast<size_t>(impl_->nFreqs), 0.f);// τ<0 的零初值滚动谱

        // 三会话：单线程 + 顺序执行（与产线 ORT 配置口径一致）
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetInterOpNumThreads(1);
        opts.SetExecutionMode(ORT_SEQUENTIAL);
        auto load = [&](const char* file) {
            const QString p = dir.filePath(QString::fromLatin1(file));
#ifdef _WIN32
            return std::make_unique<Ort::Session>(impl_->env, p.toStdWString().c_str(), opts);
#else
            return std::make_unique<Ort::Session>(impl_->env, p.toStdString().c_str(), opts);
#endif
        };
        impl_->encSess = load("enc.onnx");
        impl_->erbSess = load("erb_dec.onnx");
        impl_->dfSess = load("df_dec.onnx");

        // 名字槽位绑定 + 形状断言（config 伴读参数 ↔ 图实际形状互证）
        auto bindNames = [&](const Ort::Session& s, const std::vector<const char*>& names, bool input) {
            for (const char* n : names)
                if (Impl::nameIndex(s, n, input) < 0)
                    return bail(QStringLiteral("模型图缺少输入/输出 %1").arg(QString::fromLatin1(n))), false;
            return true;
        };
        if (!bindNames(*impl_->encSess, impl_->encIn, true) || !bindNames(*impl_->encSess, impl_->encOut, false)
            || !bindNames(*impl_->erbSess, impl_->erbIn, true) || !bindNames(*impl_->erbSess, impl_->erbOut, false)
            || !bindNames(*impl_->dfSess, impl_->dfIn, true) || !bindNames(*impl_->dfSess, impl_->dfOut, false))
            return false;
        if (!Impl::checkShape(*impl_->encSess, Impl::nameIndex(*impl_->encSess, "feat_erb", true), true,
                              {1, 1, -1, impl_->nbErb}, "enc.feat_erb", err)
            || !Impl::checkShape(*impl_->encSess, Impl::nameIndex(*impl_->encSess, "feat_spec", true), true,
                                 {1, 2, -1, impl_->nbDf}, "enc.feat_spec", err)
            || !Impl::checkShape(*impl_->encSess, Impl::nameIndex(*impl_->encSess, "e0", false), false,
                                 {1, -1, -1, impl_->nbErb}, "enc.e0", err)
            || !Impl::checkShape(*impl_->encSess, Impl::nameIndex(*impl_->encSess, "c0", false), false,
                                 {1, -1, -1, impl_->nbDf}, "enc.c0", err)
            || !Impl::checkShape(*impl_->encSess, Impl::nameIndex(*impl_->encSess, "lsnr", false), false,
                                 {-1, -1, 1}, "enc.lsnr", err)
            || !Impl::checkShape(*impl_->encSess, Impl::nameIndex(*impl_->encSess, "emb", false), false,
                                 {-1, -1, -1}, "enc.emb", err)
            || !Impl::checkShape(*impl_->erbSess, Impl::nameIndex(*impl_->erbSess, "m", false), false,
                                 {-1, 1, -1, impl_->nbErb}, "erb_dec.m", err)
            || !Impl::checkShape(*impl_->dfSess, Impl::nameIndex(*impl_->dfSess, "coefs", false), false,
                                 {-1, -1, impl_->nbDf, impl_->dfOrder * 2}, "df_dec.coefs", err))
            return false;

        // 流水线复位（setup 后对象即可用）
        impl_->frames.clear();
        impl_->pendChunks.clear();
        impl_->frontProc = impl_->nextProc = 0;
        impl_->blockStartProc = 0;
        impl_->emittedSamples = impl_->totalIn = 0;
        impl_->skipCounter = 0;
        impl_->pending.clear();
        impl_->pendPos = 0;
        impl_->outBuf.clear();
        impl_->flushed = false;
        impl_->failed = false;
        impl_->errMsg = {};
        impl_->ready = true;
        return true;
    } catch (const Ort::Exception& ex) {
        if (err) *err = QStringLiteral("模型加载失败（ORT）：%1").arg(QString::fromUtf8(ex.what()));
        return false;
    } catch (const std::bad_alloc&) {
        if (err) *err = QStringLiteral("内存不够");
        return false;
    }
}

std::vector<float> Dfn3Denoiser::process(const float* in, int64_t frames)
{
    if (impl_->failed || impl_->flushed || !impl_->ready) return {};
    try {
        impl_->totalIn += frames;
        impl_->pending.insert(impl_->pending.end(), in, in + frames);
        while (impl_->pending.size() - impl_->pendPos >= static_cast<size_t>(impl_->hop)) {
            impl_->analyzeArrival(impl_->pending.data() + impl_->pendPos);
            impl_->pendPos += static_cast<size_t>(impl_->hop);
            impl_->pumpBlocks(false);// 每帧一查：块边界钉在已处理帧计数上，与调用方分块无关
        }
        if (impl_->pendPos > 0) {// 回收已消费前缀
            impl_->pending.erase(impl_->pending.begin(),
                                 impl_->pending.begin() + static_cast<int64_t>(impl_->pendPos));
            impl_->pendPos = 0;
        }
    } catch (const Ort::Exception&) {
        impl_->fail(QStringLiteral("AI 降噪（质量档）推理失败"));
    } catch (const std::bad_alloc&) {
        impl_->fail(QStringLiteral("内存不够"));
    }
    return impl_->failed ? std::vector<float>{} : impl_->drainOut();
}

std::vector<float> Dfn3Denoiser::flush()
{
    if (impl_->failed) return {};
    if (!impl_->flushed) {
        impl_->flushed = true;
        if (impl_->ready) {
            try {
                impl_->pumpBlocks(true);// 余留帧一次跑完（parity 模式整段单 Run 亦在此触发）
                if (!impl_->failed) {// 尾零垫齐（CLI 口径：输出长度 = 输入长度，不足 hop 尾巴为零）
                    const int64_t tail = impl_->totalIn - impl_->emittedSamples;
                    if (tail > 0) {
                        impl_->outBuf.insert(impl_->outBuf.end(), static_cast<size_t>(tail), 0.f);
                        impl_->emittedSamples += tail;
                    }
                }
            } catch (const Ort::Exception&) {
                impl_->fail(QStringLiteral("AI 降噪（质量档）推理失败"));
            } catch (const std::bad_alloc&) {
                impl_->fail(QStringLiteral("内存不够"));
            }
        }
    }
    return impl_->failed ? std::vector<float>{} : impl_->drainOut();
}

ErrInfo Dfn3Denoiser::lastError() const { return impl_->errMsg; }
