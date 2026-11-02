#include "service/aidenoiser.h"
#include "service/dspdenoiser.h"
#include "service/ffextractor.h"
#include "service/fftranscoder.h"
#include "service/mediaservice.h"
#include "service/pitchshifter.h"
#include "service/vocalseparator.h"
#include "view/mainwidget.h"

#include "model/data.h"

#include <QApplication>
#include <QFileInfo>
#include <QTextStream>
#include <memory>

// CLI 冒烟分支：--extract-audio|--extract-video <文件> / --transcode-wav|--denoise-wav <输入> <输出>，免 GUI 自动化验证
static int run_cli(int argc, char* argv[]) {
    QString mode = QString::fromLocal8Bit(argv[1]);
    QString in = QString::fromLocal8Bit(argv[2]);
    QString err;
    QString out;
    if (mode == QStringLiteral("--extract-video")) out = FFExtractor::extractVideo(in, &err);
    else if (mode == QStringLiteral("--transcode-wav")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 --transcode-wav <输入> <输出.wav>\n";
            return 1;
        }
        int rate = 0, channels = 0;
        FFTranscoder::probeSourceSpec(in, &rate, &channels);// 直通引擎与输出规格均跟随源（P0 债 #4）
        DspDenoiser passthrough;
        passthrough.setup(rate, channels, false, false);// 无滤波即直通
        out = FFTranscoder::transcodeStreamWav(in, QString::fromLocal8Bit(argv[3]), passthrough, nullptr, &err);
    }
    else if (mode == QStringLiteral("--denoise-wav")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 --denoise-wav <输入> <输出.wav>\n";
            return 1;
        }
        int rate = 0, channels = 0;
        FFTranscoder::probeSourceSpec(in, &rate, &channels);// 陷波/高通系数在源率上计算，48k 全链免重采样
        DspDenoiser denoiser;
        denoiser.setup(rate, channels, true, true);
        out = FFTranscoder::transcodeStreamWav(in, QString::fromLocal8Bit(argv[3]), denoiser, nullptr, &err);
    }
    else if (mode == QStringLiteral("--ai-denoise")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 --ai-denoise <输入> <输出.wav> [模型.onnx]\n";
            return 1;
        }
        QString model = argc >= 5 ? QString::fromLocal8Bit(argv[4])
                                  : QStringLiteral("third_party/models/denoiser/gtcrn_simple.onnx");
        if (!QFileInfo::exists(model)) {
            QTextStream(stdout) << "FAIL: 找不到模型 " << model << "\n";
            return 1;
        }
        AiDenoiser ai;
        QString setupErr;
        if (!ai.setup(model, &setupErr)) {
            QTextStream(stdout) << "FAIL: " << setupErr << "\n";
            return 1;
        }
        // GTCRN 工作点锁 16k 单声道，输出保持兜底规格 44.1k 立体声——本工作包有意不改此路径（spec §2）
        out = FFTranscoder::transcodeStreamWav(in, QString::fromLocal8Bit(argv[3]), ai, nullptr, &err,
                                               {}, kCanonicalRate, kCanonicalChannels);
    }
    else if (mode == QStringLiteral("--pitch-wav")) {
        if (argc < 5) {
            QTextStream(stdout) << "FAIL: 用法 --pitch-wav <输入> <输出.wav> <半音数>\n";
            return 1;
        }
        bool ok = false;
        const double semi = QString::fromLocal8Bit(argv[4]).toDouble(&ok);
        if (!ok) {
            QTextStream(stdout) << "FAIL: 半音数不是数字\n";
            return 1;
        }
        int rate = 0, channels = 0;
        FFTranscoder::probeSourceSpec(in, &rate, &channels);// SoundTouch 在源率上变调，48k 全链免重采样
        PitchShifter shifter;
        QString setupErr;
        if (!shifter.setup(rate, channels, semi, &setupErr)) {
            QTextStream(stdout) << "FAIL: " << setupErr << "\n";
            return 1;
        }
        out = FFTranscoder::transcodeStreamWav(in, QString::fromLocal8Bit(argv[3]), shifter, nullptr, &err);
    }
    else if (mode == QStringLiteral("--sep-vocals") || mode == QStringLiteral("--sep-instrumental")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 " << mode << " <输入> <输出.wav> [模型.onnx]\n";
            return 1;
        }
        QString model = argc >= 5 ? QString::fromLocal8Bit(argv[4])
                                  : QStringLiteral("third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx");
        if (!QFileInfo::exists(model)) {
            QTextStream(stdout) << "FAIL: 找不到模型 " << model << "\n";
            return 1;
        }
        VocalSeparator sep;
        sep.setInstrumental(mode == QStringLiteral("--sep-instrumental"));
        QString setupErr;
        if (!sep.setup(model, &setupErr)) {
            QTextStream(stdout) << "FAIL: " << setupErr << "\n";
            return 1;
        }
        // MDX 工作点锁 44.1k 立体声，输出保持兜底规格——本工作包有意不改此路径（spec §2）
        out = FFTranscoder::transcodeStreamWav(in, QString::fromLocal8Bit(argv[3]), sep, nullptr, &err,
                                               {}, kCanonicalRate, kCanonicalChannels);
    }
    else out = FFExtractor::extractAudio(in, &err);
    QTextStream ts(stdout);
    if (out.isEmpty()) {
        ts << "FAIL: " << err << "\n";
        return 1;
    }
    ts << "OK: " << out << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 3) {// CLI 模式无需 QApplication
        return run_cli(argc, argv);
    }
    QApplication a(argc, argv);
    auto mediaService = std::make_unique<MediaService>();// 媒体服务
    MainWidget w;
    w.setWindowTitle(QString::fromUtf8(kAppTitle));
    w.setMediaService(mediaService.get());
    w.resize(560, 320);
    w.show();
    return a.exec();
}
