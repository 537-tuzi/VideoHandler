// engine_test — 无头引擎 DUT（WP-1.2）：裸 f32 交错流直调引擎，parity/属性测试宿主
// 绕过 GUI 与 FFTranscoder 重采样，暴露引擎原生行为；分块不变性验收即用本工具：
// 同输入不同 [块帧数] 的输出必须逐字节一致（IStreamEngine 流式契约）。
// 用法：engine_test <dsp|ai|dfn3|pitch|sep-voc|sep-inst> <in.f32> <out.f32> [块帧数=4096] [参数]
//   输入须为引擎原生规格的交错 float32 裸流：dsp/pitch/sep 为 44100Hz 2ch，ai 为 16000Hz 1ch，
//   dfn3 为 48000Hz 1ch（FR-4 质量档）
//   （ffmpeg 生成：ffmpeg -i x.wav -f f32le -c:a pcm_f32le -ar 44100 -ac 2 out.f32）
//   ai/sep 的 [参数] = 模型路径；pitch 的 [参数] = 半音数（默认 2.0）；dsp 的 [参数] = hum 基频 50|60（默认 50）
//   dfn3 的 [参数] = 模型目录 [内部块秒数=30]（0=整段单 Run，仅 parity 用，长素材违内存界）
#include "service/aidenoiser.h"
#include "service/dfn3denoiser.h"
#include "service/dspdenoiser.h"
#include "service/pitchshifter.h"
#include "service/streamengine.h"
#include "service/vocalseparator.h"

#include "model/data.h"

#include <QString>
#include <memory>
#include <cstdio>
#include <vector>

namespace {

bool readF32(const char* path, std::vector<float>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    float buf[4096];
    size_t n;
    while ((n = fread(buf, sizeof(float), 4096, f)) > 0)
        out.insert(out.end(), buf, buf + n);
    const bool ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

bool writeF32(const char* path, const std::vector<float>& v)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = !v.empty() ? fwrite(v.data(), sizeof(float), v.size(), f) == v.size() : true;
    fclose(f);
    return ok;
}

void usage()
{
    fprintf(stderr,
            "用法：engine_test <dsp|ai|dfn3|pitch|sep-voc|sep-inst> <in.f32> <out.f32> [块帧数=4096] [参数]\n"
            "  dsp/pitch/sep 输入 44100Hz 2ch 交错 f32；ai 输入 16000Hz 1ch；dfn3 输入 48000Hz 1ch\n"
            "  ai/sep 参数=模型路径；pitch 参数=半音数（默认 2.0）；dsp 参数=hum 基频 50|60（默认 50）\n"
            "  dfn3 参数=模型目录 [块秒数=30]（0=整段单 Run，仅 parity 用）\n");
}

}// namespace

int main(int argc, char* argv[])
{
    if (argc < 4 || argc > 7) {
        usage();
        return 2;
    }
    const QString kind = QString::fromLocal8Bit(argv[1]);
    const char* inPath = argv[2];
    const char* outPath = argv[3];
    const long long chunk = argc >= 5 ? atoll(argv[4]) : 4096;
    if (chunk < 1) {
        fprintf(stderr, "FAIL: 块帧数须 >= 1\n");
        return 2;
    }

    std::unique_ptr<IStreamEngine> eng;
    QString err;
    if (kind == QStringLiteral("dsp")) {
        HumBase humBase = HumBase::Hz50;// 缺省 50，与 --denoise-wav 向后兼容口径一致
        if (argc >= 6) {
            const QString b = QString::fromLocal8Bit(argv[5]);
            if (b == QLatin1String("50")) humBase = HumBase::Hz50;
            else if (b == QLatin1String("60")) humBase = HumBase::Hz60;
            else {
                fprintf(stderr, "FAIL: dsp 参数 hum 基频只接受 50 或 60\n");
                return 2;
            }
        }
        auto* e = new DspDenoiser;
        e->setup(kCanonicalRate, kCanonicalChannels, humBase, true);// 与 --denoise-wav 同配置
        eng.reset(e);
    } else if (kind == QStringLiteral("ai")) {
        if (argc < 6) {
            fprintf(stderr, "FAIL: ai 需要模型路径参数\n");
            return 2;
        }
        auto* e = new AiDenoiser;
        if (!e->setup(QString::fromLocal8Bit(argv[5]), &err)) {
            fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        eng.reset(e);
    } else if (kind == QStringLiteral("dfn3")) {
        if (argc < 6) {
            fprintf(stderr, "FAIL: dfn3 需要模型目录参数\n");
            return 2;
        }
        auto* e = new Dfn3Denoiser;
        if (argc >= 7) e->setBlockSeconds(QString::fromLocal8Bit(argv[6]).toDouble());
        if (!e->setup(QString::fromLocal8Bit(argv[5]), &err)) {
            fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        eng.reset(e);
    } else if (kind == QStringLiteral("pitch")) {
        const double semi = argc >= 6 ? QString::fromLocal8Bit(argv[5]).toDouble() : 2.0;
        auto* e = new PitchShifter;
        if (!e->setup(kCanonicalRate, kCanonicalChannels, semi, &err)) {
            fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        eng.reset(e);
    } else if (kind == QStringLiteral("sep-voc") || kind == QStringLiteral("sep-inst")) {
        if (argc < 6) {
            fprintf(stderr, "FAIL: sep 需要模型路径参数\n");
            return 2;
        }
        auto* e = new VocalSeparator;
        e->setInstrumental(kind == QStringLiteral("sep-inst"));
        if (!e->setup(QString::fromLocal8Bit(argv[5]), &err)) {
            fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        eng.reset(e);
    } else {
        usage();
        return 2;
    }

    std::vector<float> in;
    if (!readF32(inPath, in) || in.empty()) {
        fprintf(stderr, "FAIL: 读入 %s 失败或为空\n", inPath);
        return 1;
    }
    const int ch = eng->inChannels();
    if (in.size() % ch != 0) {
        fprintf(stderr, "FAIL: 输入样本数 %zu 不是 %d 声道的整数倍\n", in.size(), ch);
        return 1;
    }
    const long long frames = static_cast<long long>(in.size() / ch);

    std::vector<float> out;
    for (long long off = 0; off < frames; off += chunk) {
        const long long n = std::min(chunk, frames - off);
        std::vector<float> blk = eng->process(in.data() + off * ch, n);
        out.insert(out.end(), blk.begin(), blk.end());
        if (!eng->lastError().isEmpty()) break;
    }
    if (eng->lastError().isEmpty()) {
        std::vector<float> tail = eng->flush();
        out.insert(out.end(), tail.begin(), tail.end());
    }
    if (!eng->lastError().isEmpty()) {
        fprintf(stderr, "FAIL: %s\n", qPrintable(eng->lastError().message));
        return 1;
    }
    if (!writeF32(outPath, out)) {
        fprintf(stderr, "FAIL: 写出 %s 失败\n", outPath);
        return 1;
    }
    printf("OK: %s in=%lld帧 out=%zu样本 块=%lld\n", qPrintable(kind), frames, out.size(), chunk);
    return 0;
}
