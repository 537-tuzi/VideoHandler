#include "vocalseparator.h"

#include "util/fft.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <array>
#include <cmath>
#include <deque>
#include <onnxruntime_cxx_api.h>

namespace {

constexpr int kHop = 1024;// MDX-Net STFT 跳距（UVR MDX 全系恒定，无元数据可读）

// UVR 官方 MDX 参数表：TRvlvr/application_data mdx_model_data/model_data_new.json
// 的 MDX 子集逐条原样嵌入（MIT 数据）。MDX ONNX 无 metadata，audio-separator/UVR5
// 与本引擎同用「模型文件末尾 10MiB 的 MD5」查此表取参——生态唯一取参通路。
// dim_t_set 为指数（8 → 2^8=256）；查不到 = 不认识的模型，拒绝运行（NFR-R4）。
const char kMdxModelData[] = R"mdx({"0ddfc0eb5792638ad5dc27850236c246":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Vocals"},"26d308f91f3423a67dc69a6d12a8793d":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":9,"mdx_n_fft_scale_set":8192,"primary_stem":"Other"},"2cdd429caac38f0194b133884160f2c6":{"compensate":1.045,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"2f5501189a2f6db6349916fabe8c90de":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Vocals","is_karaoke":true},"398580b6d5d973af3120df54cee6759d":{"compensate":1.75,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"488b3e6f8bd3717d9d7c428476be2d75":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"4910e7827f335048bdac11fa967772f9":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":7,"mdx_n_fft_scale_set":4096,"primary_stem":"Drums"},"53c4baf4d12c3e6c3831bb8f5b532b93":{"compensate":1.043,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"5d343409ef0df48c7d78cce9f0106781":{"compensate":1.075,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"5f6483271e1efb9bfb59e4a3e6d4d098":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":9,"mdx_n_fft_scale_set":6144,"primary_stem":"Vocals"},"65ab5919372a128e4167f5e01a8fda85":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":8192,"primary_stem":"Other"},"6703e39f36f18aa7855ee1047765621d":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":9,"mdx_n_fft_scale_set":16384,"primary_stem":"Bass"},"6b31de20e84392859a3d09d43f089515":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Vocals"},"867595e9de46f6ab699008295df62798":{"compensate":1.03,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"a3cd63058945e777505c01d2507daf37":{"compensate":1.03,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Vocals"},"b33d9b3950b6cbf5fe90a32608924700":{"compensate":1.03,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"c3b29bdce8c4fa17ec609e16220330ab":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":16384,"primary_stem":"Bass"},"ceed671467c1f64ebdfac8a2490d0d52":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"d2a1376f310e4f7fa37fb9b5774eb701":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"d7bff498db9324db933d913388cba6be":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Vocals"},"d94058f8c7f1fae4164868ae8ae66b20":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Vocals"},"dc41ede5961d50f277eb846db17f5319":{"compensate":1.035,"mdx_dim_f_set":2048,"mdx_dim_t_set":9,"mdx_n_fft_scale_set":4096,"primary_stem":"Drums"},"e5572e58abf111f80d8241d2e44e7fa4":{"compensate":1.028,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"e7324c873b1f615c35c1967f912db92a":{"compensate":1.03,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"1c56ec0224f1d559c42fd6fd2a67b154":{"compensate":1.025,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":5120,"primary_stem":"Instrumental"},"f2df6d6863d8f435436d8b561594ff49":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"b06327a00d5e5fbc7d96e1781bbdb596":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Instrumental"},"94ff780b977d3ca07c7a343dab2e25dd":{"compensate":1.039,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Instrumental"},"73492b58195c3b52d34590d5474452f6":{"compensate":1.043,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"970b3f9492014d18fefeedfe4773cb42":{"compensate":1.009,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"1d64a6d2c30f709b8c9b4ce1366d96ee":{"compensate":1.065,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":5120,"primary_stem":"Instrumental","is_karaoke":true},"203f2a3955221b64df85a41af87cf8f0":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Instrumental"},"291c2049608edb52648b96e27eb80e95":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Instrumental"},"ead8d05dab12ec571d67549b3aab03fc":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Instrumental"},"cc63408db3d80b4d85b0287d1d7c9632":{"compensate":1.033,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Instrumental"},"cd5b2989ad863f116c855db1dfe24e39":{"compensate":1.035,"mdx_dim_f_set":3072,"mdx_dim_t_set":9,"mdx_n_fft_scale_set":6144,"primary_stem":"Reverb"},"55657dd70583b0fedfba5f67df11d711":{"compensate":1.022,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":6144,"primary_stem":"Instrumental"},"b6bccda408a436db8500083ef3491e8b":{"compensate":1.02,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"8a88db95c7fb5dbe6a095ff2ffb428b1":{"compensate":1.026,"mdx_dim_f_set":2048,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":5120,"primary_stem":"Instrumental"},"b78da4afc6512f98e4756f5977f5c6b9":{"compensate":1.021,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Instrumental"},"77d07b2667ddf05b9e3175941b4454a0":{"compensate":1.021,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"},"0f2a6bc5b49d87d64728ee40e23bceb1":{"compensate":1.019,"mdx_dim_f_set":2560,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":5120,"primary_stem":"Instrumental"},"cb790d0c913647ced70fc6b38f5bea1a":{"compensate":1.01,"mdx_dim_f_set":2560,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":5120,"primary_stem":"Instrumental"},"b02be2d198d4968a121030cf8950b492":{"compensate":1.02,"mdx_dim_f_set":2560,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":5120,"primary_stem":"No Crowd"}})mdx";

// 模型文件末尾 10000 KiB 的 MD5（audio-separator 同算法、官方表即此范围生成）
QString tailMd5(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    qint64 start = f.size() - 10000LL * 1024;
    if (start < 0) start = 0;
    if (!f.seek(start)) return {};
    QCryptographicHash h(QCryptographicHash::Md5);
    char buf[65536];
    while (true) {
        const qint64 n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        h.addData(QByteArrayView(buf, static_cast<qsizetype>(n)));
    }
    return QString::fromLatin1(h.result().toHex());
}

struct MdxParams {
    int nFft = 0, dimF = 0, dimT = 0;
    double compensate = 1.0;
    bool primaryVocals = false;
};

bool lookupMdx(const QString& hash, MdxParams* p)
{
    const QJsonObject e = QJsonDocument::fromJson(QByteArray(kMdxModelData))
                               .object()
                               .value(hash)
                               .toObject();
    if (e.isEmpty()) return false;
    p->nFft = e.value(QStringLiteral("mdx_n_fft_scale_set")).toInt();
    p->dimF = e.value(QStringLiteral("mdx_dim_f_set")).toInt();
    p->dimT = 1 << e.value(QStringLiteral("mdx_dim_t_set")).toInt();
    p->compensate = e.value(QStringLiteral("compensate")).toDouble();
    p->primaryVocals = e.value(QStringLiteral("primary_stem")).toString()
                       == QStringLiteral("Vocals");
    return p->nFft > 0 && p->dimF > 0 && p->dimT > 0;
}

}// namespace

struct VocalSeparator::Impl {
    Ort::Env env = Ort::Env(ORT_LOGGING_LEVEL_ERROR, "vocal_sep");
    Ort::AllocatorWithDefaultOptions alloc;
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    std::unique_ptr<Ort::Session> sess;
    std::vector<std::string> inNameStore, outNameStore;
    std::vector<const char*> inNames, outNames;
    int dimF = 0, dimT = 0, nFft = 0;
    int outIdx = 0;// 目标输出在 Run 结果中的下标（setup 按名字选定）
    double compensate = 1.0;// 副 stem 波形相减系数（查表）
    bool primaryVocals = true;// 模型直出主 stem 是否为人声（查表）

    FftPlan plan;// nFft 点 DFT（2 的幂直通，否则 Bluestein）
    std::vector<float> window;// 周期 Hann
    std::vector<float> prelude;// 头 win+1 帧暂存，够数后生成零头（内存有界）
    std::deque<float> inQ;// P 域待分析样本（P = 2*trim 零头 + x + 零尾；P 坐标 = mixture 坐标 + trim）
    int64_t pBase = 0;// inQ 首样本对应的 P 位置
    int64_t inputLen = 0;// 已喂入的 x 帧数
    bool headDone = false;
    bool instrumental = false;// 请求副 stem（mix − compensate×主 stem）
    ErrInfo errMsg;// 运行期错误（码与文案同点赋值：输出契约违约 = Model / 推理与素材过短 = Engine）
    std::deque<float> mixQ;// 副 stem 模式下暂存的对齐 mix（滞后块级 OLA 排水线，有界）
    std::vector<float> wola, wnorm;// 块内 padded 域 WOLA 累计 + 窗²归一（每块重置；输出取 [trim, trim+C)）
    std::deque<float> outQ;// P 域输出样本（块级 OLA 排水产生）
    int64_t pOutBase = 0;// outQ 首样本对应的 P 位置
    std::vector<float> reBuf, imBuf, synthRe, synthIm, tensor, tarOut;// 复用的计算缓冲；tarOut=块内时域输出
    // —— 块级语义（WP-1.1 round-4：对齐 audio-separator demix 官方分块）——
    int64_t chunkLen = 0;// 块长 C = kHop*(dimT-1)（参考 chunk_size，261120@dimT=256）
    int64_t stepLen = 0;// 块步进 = C − C/4（参考 overlap 0.25 → step 75%，195840）
    int64_t trimLen = 0;// trim = nFft/2（参考 mixture 前置零段长，3840）
    int64_t genSize = 0;// C − 2*trim（参考尾部 pad 计算基，253440）
    std::vector<float> hannC;// 对称 hanning(C)（块级 OLA 窗，np.hanning 语义）
    int64_t nextChunk = 0;// 下一个待推理块号（块 b 起点 mixture 坐标 = b*stepLen）
    std::vector<float> resBuf, divBuf;// 块级 OLA 环形累计（res 交错立体声；div 单值，参考两声道同窗值）
    size_t rHead = 0;// resBuf/divBuf 中 rBase 对应下标
    int64_t rBase = 0, rLen = 0;// 环形队列覆盖 mixture [rBase, rBase+rLen)

    int64_t win() const { return nFft; }
    // 输出是否走 mix − compensate×直出：请求伴奏而主 stem 是人声（或反之）时才需相减
    bool wantSub() const { return instrumental == primaryVocals; }

    void feed(const float* in, int64_t frames) {// 喂入 x：进 P 域 + 头暂存
        for (int64_t i = 0; i < frames; ++i) {
            const float l = in[i * 2];
            const float r = in[i * 2 + 1];
            if (!headDone) {
                prelude.push_back(l);
                prelude.push_back(r);
                if (static_cast<int64_t>(prelude.size()) / 2 >= win() + 1) {
                    // 前置零段（非 reflect）：参考 demix 语义 = 前置 trim=nFft/2 零 + 块内
                    // torch.stft(center) 对零段反射仍是零，帧 0..7 窗见 [零|信号] 而非
                    // [反射信号|信号]（WP-1.1 round-3 判别实验：帧 8+ 谱机器精度一致、
                    // 头部帧因填充语义不同而谱异，MDX 感受野覆盖全 dimT 故全局传播）
                    for (int64_t k = 0; k < win(); ++k) {
                        inQ.push_back(0.f);
                        inQ.push_back(0.f);
                    }
                    inQ.insert(inQ.end(), prelude.begin(), prelude.end());
                    prelude.clear();
                    headDone = true;
                }
            } else {
                inQ.push_back(l);
                inQ.push_back(r);
            }
            if (wantSub()) {// 副 stem 模式：mix 与主 stem 输出同源对齐
                mixQ.push_back(l);
                mixQ.push_back(r);
            }
        }
        inputLen += frames;
    }

    void analyzeFrame(int64_t b, int f) {// 块 b 帧 f 谱写 tensor：((plane*dimF)+bin)*dimT + f
        const int64_t bP = b * stepLen + trimLen;// 块数据 P 坐标起点
        // 块内坐标 q 越界按 torch reflect（不重复边界样本：q<0 → −q；q≥C → 2C−2−q）。
        // 头块反射零段仍是零（等价零头）；中间块/尾块边界反射真实数据——官方分块语义
        auto samp = [&](int64_t q, int ch) -> float {
            if (q < 0) q = -q;
            else if (q >= chunkLen) q = 2 * chunkLen - 2 - q;
            return inQ[static_cast<size_t>((bP + q - pBase) * 2 + ch)];
        };
        const int64_t t0 = f * kHop - trimLen;// 帧窗块内起点（center 语义）
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < nFft; ++i)
                reBuf[static_cast<size_t>(i)] = samp(t0 + i, ch) * window[static_cast<size_t>(i)];
            std::fill(imBuf.begin(), imBuf.end(), 0.f);
            plan.run(reBuf.data(), imBuf.data(), -1);
            for (int k = 0; k < dimF; ++k) {// 前 3 频点置零（对齐 mdx_separator.py:438）
                const size_t base = (static_cast<size_t>(2 * ch) * dimF + k) * dimT + f;
                tensor[base] = k >= 3 ? reBuf[static_cast<size_t>(k)] : 0.f;
                tensor[base + static_cast<size_t>(dimF) * dimT] = k >= 3 ? imBuf[static_cast<size_t>(k)] : 0.f;
            }
        }
    }

    void ifftChannel(const float* spec, int planePair, int t, std::vector<float>& re, std::vector<float>& im) {
        // 中间段 [dimF, nFft-dimF] 清零：参考 inverse 对 dimF..nFft/2 零填充后由
        // torch.istft 共轭展开；此前未清会残留上一帧脏数据（复用缓冲），污染 17.6kHz
        // 以上频段（WP-1.1 round-3 实锤）
        for (int k = dimF; k <= nFft - dimF; ++k) {
            re[static_cast<size_t>(k)] = 0.f;
            im[static_cast<size_t>(k)] = 0.f;
        }
        for (int k = 0; k < dimF; ++k) {// plane-major：元素 (p,f,t) 在 (p·dimF+f)·dimT+t
            const size_t off = (static_cast<size_t>(planePair * dimF + k)) * dimT + t;
            re[static_cast<size_t>(k)] = spec[off];
            im[static_cast<size_t>(k)] = spec[off + static_cast<size_t>(dimF) * dimT];
        }
        for (int k = 1; k < dimF; ++k) {// Hermitian 镜像
            re[static_cast<size_t>(nFft - k)] = re[static_cast<size_t>(k)];
            im[static_cast<size_t>(nFft - k)] = -im[static_cast<size_t>(k)];
        }
        plan.run(re.data(), im.data(), +1);
    }

    void synthesizeChunk(const float* ys) {// 块内 dimT 帧 → padded 域 WOLA → tarOut[chunkLen]
        // torch.istft(center) 语义：帧 f 窗落在 padded [f*kHop, f*kHop+nFft)，padded 长
        // C+nFft；输出裁 [trim, trim+C)。注意 dimT*kHop = C+kHop > C——尾帧后半必超界，靠
        // padded 缓冲+统一裁剪实现（不能按「每帧排 kHop」的流式网格，其帧贡献差半窗）
        std::fill(wola.begin(), wola.end(), 0.f);
        std::fill(wnorm.begin(), wnorm.end(), 0.f);
        for (int f = 0; f < dimT; ++f) {
            const size_t base = static_cast<size_t>(f) * kHop;
            ifftChannel(ys, 0, f, synthRe, synthIm);
            for (int i = 0; i < nFft; ++i) {
                const float w = window[static_cast<size_t>(i)];
                wola[(base + i) * 2] += synthRe[static_cast<size_t>(i)] * w;
                wnorm[base + i] += w * w;
            }
            ifftChannel(ys, 2, f, synthRe, synthIm);
            for (int i = 0; i < nFft; ++i)
                wola[(base + i) * 2 + 1] += synthRe[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        }
        for (int64_t q = 0; q < chunkLen; ++q) {
            const float d = wnorm[static_cast<size_t>(q + trimLen)];
            tarOut[static_cast<size_t>(q) * 2] = d > 1e-9f ? wola[static_cast<size_t>(q + trimLen) * 2] / d : 0.f;
            tarOut[static_cast<size_t>(q) * 2 + 1] = d > 1e-9f ? wola[static_cast<size_t>(q + trimLen) * 2 + 1] / d : 0.f;
        }
    }

    bool runModel() {// 推理 + 输出契约校验（dtype/shape/plane-major），违例归口 errMsg（Model）
        const float* ys = nullptr;
        try {
            std::array<int64_t, 4> shape{1, 4, dimF, dimT};
            Ort::Value x = Ort::Value::CreateTensor<float>(mem, tensor.data(), tensor.size(), shape.data(), 4);
            auto out = sess->Run(Ort::RunOptions{nullptr}, inNames.data(), &x, 1,
                                 outNames.data(), outNames.size());
            if (out.size() <= static_cast<size_t>(outIdx)) {
                errMsg = {ErrCode::Model,
                          QStringLiteral("模型推理输出缺失（%1 个输出）").arg(out.size())};
                return false;
            }
            const auto info = out[static_cast<size_t>(outIdx)].GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                errMsg = {ErrCode::Model,
                          QStringLiteral("模型输出 dtype 非 float32（type=%1）")
                              .arg(static_cast<int>(info.GetElementType()))};
                return false;
            }
            const auto dims = info.GetShape();
            QStringList ds;
            for (int64_t d : dims) ds << QString::number(d);
            if (dims.size() != 4 || dims[0] != 1 || dims[1] != 4
                || dims[2] != dimF || dims[3] != dimT) {
                errMsg = {ErrCode::Model,
                          QStringLiteral("模型输出布局不符：需要 [1,4,%1,%2] plane-major，实际 [%3]")
                              .arg(dimF).arg(dimT).arg(ds.join(QLatin1Char(',')))};
                return false;
            }
            ys = out[static_cast<size_t>(outIdx)].GetTensorData<float>();
            synthesizeChunk(ys);
        } catch (const Ort::Exception& e) {
            errMsg = {ErrCode::Engine,
                      QStringLiteral("人声分离推理失败：%1").arg(QString::fromUtf8(e.what()))};
            return false;
        }
        return true;
    }

    // 块 b（窗长 la，win 指向块级 OLA 窗）推理 + 块内合成 + 块级 OLA 累计
    bool runChunk(int64_t b, int64_t la, const float* win) {
        const int64_t cap = static_cast<int64_t>(divBuf.size());
        const int64_t want = b * stepLen + la;// 队列尾扩到块尾（前块未排水段已在队列内，只补差量）
        while (rBase + rLen < want) {
            const size_t o = static_cast<size_t>((rHead + rLen) % cap) * 2;
            resBuf[o] = 0.f;
            resBuf[o + 1] = 0.f;
            divBuf[o / 2] = 0.f;
            rLen++;
        }
        for (int f = 0; f < dimT; ++f)
            analyzeFrame(b, f);
        if (!runModel()) return false;
        for (int64_t j = 0; j < la; ++j) {
            const size_t o = static_cast<size_t>((rHead + (b * stepLen + j - rBase)) % cap) * 2;
            const float w = win[static_cast<size_t>(j)];
            resBuf[o] += tarOut[static_cast<size_t>(j) * 2] * w;
            resBuf[o + 1] += tarOut[static_cast<size_t>(j) * 2 + 1] * w;
            divBuf[o / 2] += w;
        }
        return true;
    }

    void drain(int64_t upto) {// mixture 坐标 < upto 的块级 OLA 定稿 → outQ（P = mixture + trim）
        const int64_t cap = static_cast<int64_t>(divBuf.size());
        if (outQ.empty()) pOutBase = rBase + trimLen;// 队空时对齐下一个入队样本的 P 坐标
        while (rBase < upto && rLen > 0) {
            const float d = divBuf[rHead];
            const size_t o = static_cast<size_t>(rHead) * 2;
            outQ.push_back(d > 1e-9f ? resBuf[o] / d : 0.f);
            outQ.push_back(d > 1e-9f ? resBuf[o + 1] / d : 0.f);
            rHead = static_cast<size_t>((rHead + 1) % cap);
            rBase++;
            rLen--;
        }
    }

    std::vector<float> deliver() {// P 域 → x 域裁剪交付
        std::vector<float> out;
        const int64_t x0 = win();
        const int64_t x1 = win() + inputLen;
        const bool sub = wantSub();
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
            if (sub) {// 副 stem = mix − compensate×主 stem（波形域，官方语义）
                l = static_cast<float>(mixQ.front() - compensate * l);
                mixQ.pop_front();
                r = static_cast<float>(mixQ.front() - compensate * r);
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
        opts.DisableCpuMemArena();// arena 稳态驻留是分离路径 RSS ~2.6GB 主因（wp-1.0 round-3），关池交还系统分配器
#ifdef _WIN32
        impl_->sess = std::make_unique<Ort::Session>(impl_->env, modelPath.toStdWString().c_str(), opts);
#else
        impl_->sess = std::make_unique<Ort::Session>(impl_->env, modelPath.toStdString().c_str(), opts);// 非 Windows 走窄路径（UTF-8）
#endif
        const auto shape = impl_->sess->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || shape[1] != 4) {
            if (err) *err = QStringLiteral("模型输入规格不符，需要 [1,4,dimF,dimT] 的 MDX-Net 模型");
            return false;
        }
        // 取参：末 10000 KiB MD5 查 UVR 官方表（MDX 无 metadata；nFft=dimF*2 推导已废除）
        const QString hash = tailMd5(modelPath);
        MdxParams mp;
        if (!lookupMdx(hash, &mp)) {
            if (err) *err = QStringLiteral("不认识的模型：末 10000KiB MD5=%1 不在内置 UVR 参数表，请使用 UVR 官方 MDX-Net 模型").arg(hash);
            return false;
        }
        if ((shape[2] > 0 && shape[2] != mp.dimF) || (shape[3] > 0 && shape[3] != mp.dimT)) {
            if (err) *err = QStringLiteral("模型形状与参数表不符：输入 [%1,%2] vs 表 dimF=%3 dimT=%4")
                             .arg(shape[2]).arg(shape[3]).arg(mp.dimF).arg(mp.dimT);
            return false;
        }
        if (mp.nFft / 2 + 1 < mp.dimF) {
            if (err) *err = QStringLiteral("模型参数自相矛盾（n_fft=%1 不足 dimF=%2）").arg(mp.nFft).arg(mp.dimF);
            return false;
        }
        impl_->dimF = mp.dimF;
        impl_->dimT = mp.dimT;
        impl_->nFft = mp.nFft;
        impl_->compensate = mp.compensate;
        impl_->primaryVocals = mp.primaryVocals;
        for (size_t i = 0; i < impl_->sess->GetInputCount(); ++i)
            impl_->inNameStore.emplace_back(impl_->sess->GetInputNameAllocated(i, impl_->alloc).get());
        for (size_t i = 0; i < impl_->sess->GetOutputCount(); ++i)
            impl_->outNameStore.emplace_back(impl_->sess->GetOutputNameAllocated(i, impl_->alloc).get());
        // 目标输出按名字选定（不靠位置；单输出直取）
        impl_->outIdx = -1;
        if (impl_->outNameStore.size() == 1) {
            impl_->outIdx = 0;
        } else {
            for (size_t i = 0; i < impl_->outNameStore.size(); ++i)
                if (impl_->outNameStore[i] == "output") impl_->outIdx = static_cast<int>(i);
        }
        if (impl_->outIdx < 0) {
            QString names;
            for (const auto& s : impl_->outNameStore)
                names += QString::fromStdString(s) + QLatin1Char(',');
            if (err) *err = QStringLiteral("模型输出无法按名字选定（输出：%1）").arg(names);
            return false;
        }
        for (const auto& s : impl_->inNameStore) impl_->inNames.push_back(s.c_str());
        for (const auto& s : impl_->outNameStore) impl_->outNames.push_back(s.c_str());
        impl_->plan.init(impl_->nFft);
        impl_->window.resize(static_cast<size_t>(impl_->nFft));
        for (int i = 0; i < impl_->nFft; ++i)
            impl_->window[static_cast<size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * cos(2 * kPi * i / impl_->nFft));
        // 块级参数（参考 demix：chunk=hop*(segment-1)、step=75%、trim=n_fft/2、pad 基 gen_size）
        impl_->chunkLen = kHop * (impl_->dimT - 1);
        impl_->stepLen = impl_->chunkLen - impl_->chunkLen / 4;
        impl_->trimLen = impl_->nFft / 2;
        impl_->genSize = impl_->chunkLen - 2 * impl_->trimLen;
        impl_->hannC.resize(static_cast<size_t>(impl_->chunkLen));
        for (int64_t k = 0; k < impl_->chunkLen; ++k)// 对称 hanning（np.hanning 语义，分母 C-1）
            impl_->hannC[static_cast<size_t>(k)] =
                static_cast<float>(0.5 - 0.5 * cos(2 * kPi * k / (impl_->chunkLen - 1)));
        impl_->resBuf.assign(static_cast<size_t>(impl_->chunkLen) * 2, 0.f);
        impl_->divBuf.assign(static_cast<size_t>(impl_->chunkLen), 0.f);
        impl_->tarOut.assign(static_cast<size_t>(impl_->chunkLen) * 2, 0.f);
        impl_->wola.assign(static_cast<size_t>(impl_->chunkLen + impl_->nFft) * 2, 0.f);
        impl_->wnorm.assign(static_cast<size_t>(impl_->chunkLen + impl_->nFft), 0.f);
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

ErrInfo VocalSeparator::lastError() const {
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
        const int64_t bP = impl_->nextChunk * impl_->stepLen + impl_->trimLen;
        if (bP + impl_->chunkLen > pEnd) break;// 块数据未齐（齐则其后必有数据 → 非尾块，块长恒 C）
        if (!impl_->runChunk(impl_->nextChunk, impl_->chunkLen, impl_->hannC.data()))
            return impl_->deliver();
        impl_->nextChunk++;
        impl_->drain(impl_->nextChunk * impl_->stepLen);
        const int64_t need = impl_->nextChunk * impl_->stepLen + impl_->trimLen;// 丢弃已不用的块前数据
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
        impl_->errMsg = {ErrCode::Engine,
                         QStringLiteral("音频太短，人声分离至少需要约 %1 秒")
                             .arg((impl_->nFft + 1) / 44100.0, 0, 'f', 2)};
        return out;
    }
    const int64_t n = impl_->inputLen;
    const int64_t mixLen = impl_->trimLen + n + (impl_->genSize + impl_->trimLen - n % impl_->genSize);
    // P 域补零到最后块数据全长（参考尾块 pad 零 + 块内反射可伸入 [la, C) 补零区）；
    // 补零量 ≤ 2C 有界（pad = genSize+trim−n%genSize ≤ genSize+trim，块尾差 < C）
    const int64_t lastStart = ((mixLen + impl_->stepLen - 1) / impl_->stepLen - 1) * impl_->stepLen;
    const int64_t pEnd = impl_->pBase + static_cast<int64_t>(impl_->inQ.size()) / 2;
    for (int64_t j = pEnd; j < lastStart + impl_->chunkLen + impl_->trimLen; ++j) {
        impl_->inQ.push_back(0.f);
        impl_->inQ.push_back(0.f);
    }
    std::vector<float> tailWin;// 尾块短窗（la<C 按参考 np.hanning(la) 独立生成，非长窗截断）
    while (impl_->nextChunk * impl_->stepLen < mixLen) {
        const int64_t la = std::min(impl_->chunkLen, mixLen - impl_->nextChunk * impl_->stepLen);
        const float* win = impl_->hannC.data();
        if (la < impl_->chunkLen) {
            tailWin.resize(static_cast<size_t>(la));
            for (int64_t k = 0; k < la; ++k)
                tailWin[static_cast<size_t>(k)] = static_cast<float>(0.5 - 0.5 * cos(2 * kPi * k / (la - 1)));
            win = tailWin.data();
        }
        if (!impl_->runChunk(impl_->nextChunk, la, win))
            return impl_->deliver();
        impl_->nextChunk++;
        impl_->drain(std::min(impl_->nextChunk * impl_->stepLen, mixLen));
        const int64_t need = impl_->nextChunk * impl_->stepLen + impl_->trimLen;
        while (impl_->pBase < need && !impl_->inQ.empty()) {// 同 process：块前数据即时丢弃保内存有界
            impl_->inQ.pop_front();
            impl_->inQ.pop_front();
            impl_->pBase++;
        }
    }
    return impl_->deliver();
}
