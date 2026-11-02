#include "service/commandlayer.h"
#include "service/mediaservice.h"
#include "view/mainwidget.h"

#include "model/data.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cstdio>
#include <memory>

// ---- --json 模式（FR-24 D1）：stdin 字节 → QJsonDocument::fromJson（UTF-8 直解）→ execute → stdout ----
// 响应恰一个 JSON 文档 + 行尾 \n（UTF-8/Compact/键序确定）；任何失败（含 JSON 解析失败）也回合法 JSON；
// 进度不进 stdout/stderr（OQ-2：stdout 纯 JSON 是硬契约，CI 优先）
static int emitJsonResponse(const QJsonObject& resp) {
    const QByteArray out = QJsonDocument(resp).toJson(QJsonDocument::Compact);
    std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stdout);
    std::fputc('\n', stdout);
    return resp.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

static int runJson() {
    QByteArray raw;
    char buf[65536];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) raw.append(buf, n);
    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
    QJsonObject resp;
    if (parseErr.error != QJsonParseError::NoError)
        resp = CommandLayer::errorResponse({}, "E_PARAMS",
                                           QStringLiteral("JSON 解析失败：%1").arg(parseErr.errorString()));
    else if (!doc.isObject())
        resp = CommandLayer::errorResponse({}, "E_PARAMS", QStringLiteral("请求必须是 JSON 对象"));
    else
        resp = CommandLayer::execute(doc.object());
    return emitJsonResponse(resp);
}

// ---- 旧 CLI 动词薄壳（FR-24 D7）：argv → 组等价 JSON 请求 → execute → 按旧格式打印 ----
// OK/FAIL 文案与退出码逐字不变；模型缺省（third_party 相对 CWD）由薄壳显式注入 model 字段（缺省策略上移到壳）；
// 两处有意收紧（D7）：未知动词从「静默当 extractAudio」改为 FAIL；--pitch-wav 半音数 [-12,12] 且非 0
static int printShellResult(const QJsonObject& resp) {
    QTextStream ts(stdout);
    if (resp.value(QStringLiteral("ok")).toBool()) {
        ts << "OK: " << resp.value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("output")).toString() << "\n";
        return 0;
    }
    ts << "FAIL: " << resp.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString() << "\n";
    return 1;
}

static int runCli(int argc, char* argv[]) {
    const QString mode = QString::fromLocal8Bit(argv[1]);
    QJsonObject req;
    req.insert(QStringLiteral("input"), QString::fromLocal8Bit(argv[2]));

    if (mode == QStringLiteral("--extract-audio")) req.insert(QStringLiteral("cmd"), QStringLiteral("extractAudio"));
    else if (mode == QStringLiteral("--extract-video")) req.insert(QStringLiteral("cmd"), QStringLiteral("extractVideo"));
    else if (mode == QStringLiteral("--transcode-wav")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 --transcode-wav <输入> <输出.wav>\n";
            return 1;
        }
        req.insert(QStringLiteral("cmd"), QStringLiteral("transcode"));
        req.insert(QStringLiteral("output"), QString::fromLocal8Bit(argv[3]));
    }
    else if (mode == QStringLiteral("--denoise-wav")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 --denoise-wav <输入> <输出.wav> [50|60]\n";
            return 1;
        }
        QString hum = QStringLiteral("50");// 缺省 50Hz 向后兼容
        if (argc >= 5) {
            hum = QString::fromLocal8Bit(argv[4]);
            if (hum != QLatin1String("50") && hum != QLatin1String("60")) {
                QTextStream(stdout) << "FAIL: hum 基频参数只接受 50 或 60（用法 --denoise-wav <输入> <输出.wav> [50|60]）\n";
                return 1;
            }
        }
        req.insert(QStringLiteral("cmd"), QStringLiteral("denoise"));
        req.insert(QStringLiteral("output"), QString::fromLocal8Bit(argv[3]));
        req.insert(QStringLiteral("humBase"), hum);
    }
    else if (mode == QStringLiteral("--ai-denoise")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 --ai-denoise <输入> <输出.wav> [模型.onnx]\n";
            return 1;
        }
        req.insert(QStringLiteral("cmd"), QStringLiteral("aiDenoise"));
        req.insert(QStringLiteral("output"), QString::fromLocal8Bit(argv[3]));
        req.insert(QStringLiteral("model"), argc >= 5 ? QString::fromLocal8Bit(argv[4])
                                                      : QStringLiteral("third_party/models/denoiser/gtcrn_simple.onnx"));
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
        if (!(semi >= -12.0 && semi <= 12.0) || qFuzzyIsNull(semi)) {// D7 收紧：范围/非零对齐 GUI；否定式使 NaN 落拒收（审查 P2-1）
            QTextStream(stdout) << "FAIL: 半音数须在 -12 到 12 之间且不为 0\n";
            return 1;
        }
        req.insert(QStringLiteral("cmd"), QStringLiteral("pitch"));
        req.insert(QStringLiteral("output"), QString::fromLocal8Bit(argv[3]));
        req.insert(QStringLiteral("semi"), semi);
    }
    else if (mode == QStringLiteral("--sep-vocals") || mode == QStringLiteral("--sep-instrumental")) {
        if (argc < 4) {
            QTextStream(stdout) << "FAIL: 用法 " << mode << " <输入> <输出.wav> [模型.onnx]\n";
            return 1;
        }
        req.insert(QStringLiteral("cmd"), QStringLiteral("separate"));
        req.insert(QStringLiteral("stem"), mode == QStringLiteral("--sep-instrumental")
                                             ? QStringLiteral("instrumental") : QStringLiteral("vocals"));
        req.insert(QStringLiteral("output"), QString::fromLocal8Bit(argv[3]));
        req.insert(QStringLiteral("model"), argc >= 5 ? QString::fromLocal8Bit(argv[4])
                                                      : QStringLiteral("third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx"));
    }
    else {
        // 未知动词不再静默当 extractAudio（D7 收紧 #1：宽松分支是陷阱不是兼容）
        QTextStream(stdout) << "FAIL: 未知命令 " << mode << "（可用：--extract-audio|--extract-video|"
                               "--transcode-wav|--denoise-wav|--ai-denoise|--pitch-wav|--sep-vocals|--sep-instrumental|--json）\n";
        return 1;
    }
    return printShellResult(CommandLayer::execute(req));
}

int main(int argc, char* argv[]) {
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--json")) {
        if (argc != 2)// --json 不接多余参数（D1），仍回 E_PARAMS JSON
            return emitJsonResponse(CommandLayer::errorResponse(
                {}, "E_PARAMS", QStringLiteral("用法：--json 从 stdin 读请求，不接多余参数")));
        return runJson();
    }
    if (argc >= 3) {// CLI 模式无需 QApplication
        return runCli(argc, argv);
    }
    QApplication a(argc, argv);
    a.setOrganizationName(QStringLiteral("VideoHandler"));// QSettings 定位配置须先有组织/应用名
    a.setApplicationName(QStringLiteral("VideoHandler"));
    auto mediaService = std::make_unique<MediaService>();// 媒体服务
    MainWidget w;
    w.setWindowTitle(QString::fromUtf8(kAppTitle));
    w.setMediaService(mediaService.get());
    w.resize(560, 320);
    w.show();
    return a.exec();
}
