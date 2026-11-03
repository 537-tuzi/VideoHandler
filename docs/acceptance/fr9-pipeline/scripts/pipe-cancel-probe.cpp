// pipe-cancel-probe.cpp — FR-9 管线取消/进度探针（PIPE-CANCEL/PIPE-PROG 代码级证据面）：
// CLI 单发同步不可达的取消语义经 CommandLayer::execute 的 Ctx 注入直证（gui-ws-probe 方法论，
// 但不绕命令层——直接驱动 execute 本体），四相运行（编排见同目录 run-pipe.sh）：
//   cancel-pre     —— 取消标志预置 → 第 1 步开工前检查点即取消：E_CANCELLED、message 逐字
//                     「已取消」（不缀「步骤 1（…）」前缀，GUI onJobDone 静默特判依赖）、step=1；
//   cancel-mid     —— 首个进度回调置标志 → 第 1 步分块循环检查点取消：step=1、零版本目录；
//   cancel-between —— 映射进度达 50（第 1 步收尾 onProgress(100)→(0+100)/2=50）置标志 →
//                     第 2 步开工前取消：step=2、v001 已提交保留（D5 部分失败保留已提交步）；
//   progress       —— 三步链完整跑完转录进度序列：含边界值（等分映射 (i*100+pct)/n）且终值 100。
// 断言在编排侧（json_check.py + workspace.json 实况），探针只转录响应与进度序列。
// 用法：probe <cancel-pre|cancel-mid|cancel-between|progress> <输入wav> <工作区根>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QList>
#include <atomic>
#include <cstdio>

#include "service/commandlayer.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        std::printf("usage: probe <cancel-pre|cancel-mid|cancel-between|progress> <输入wav> <工作区根>\n");
        return 2;
    }
    const QString phase = QString::fromUtf8(argv[1]);
    const QString input = QString::fromUtf8(argv[2]);
    const QString wsRoot = QString::fromUtf8(argv[3]);

    QJsonArray steps;// 固定两步模型无关链（denoise→pitch）；progress 相另组三步链
    auto step = [](const char* cmd) {
        QJsonObject s;
        s.insert(QStringLiteral("cmd"), QString::fromLatin1(cmd));
        return s;
    };
    if (phase == QLatin1String("progress")) {
        QJsonObject den = step("denoise");
        den.insert(QStringLiteral("humBase"), QStringLiteral("60"));
        steps.append(den);
        QJsonObject pit = step("pitch");
        pit.insert(QStringLiteral("semi"), 2.0);
        steps.append(pit);
        steps.append(step("transcode"));
    } else {
        QJsonObject den = step("denoise");
        den.insert(QStringLiteral("humBase"), QStringLiteral("60"));
        steps.append(den);
        QJsonObject pit = step("pitch");
        pit.insert(QStringLiteral("semi"), 2.0);
        steps.append(pit);
    }

    QJsonObject req;
    req.insert(QStringLiteral("v"), 1);
    req.insert(QStringLiteral("cmd"), QStringLiteral("pipeline"));
    req.insert(QStringLiteral("input"), input);
    req.insert(QStringLiteral("workspace"), wsRoot);
    req.insert(QStringLiteral("steps"), steps);

    std::atomic<bool> flag{false};
    QList<int> prog;
    CommandLayer::Ctx ctx;
    ctx.cancel = &flag;
    ctx.onProgress = [&](int pct) {
        prog.append(pct);
        if (phase == QLatin1String("cancel-mid") && !prog.isEmpty())
            flag = true;// 首个进度回调即置位 → 第 1 步块循环检查点取消
        if (phase == QLatin1String("cancel-between") && pct >= 50)
            flag = true;// 50 = 第 1 步收尾 100 的映射值 → 第 2 步开工前取消
    };
    if (phase == QLatin1String("cancel-pre")) flag = true;

    const QJsonObject resp = CommandLayer::execute(req, ctx);
    std::printf("RESP %s\n", QString::fromUtf8(QJsonDocument(resp).toJson(QJsonDocument::Compact))
                                 .toUtf8()
                                 .constData());
    QStringList ps;
    for (int p : prog) ps << QString::number(p);
    std::printf("PROG %s\n", ps.join(QLatin1Char(',')).toUtf8().constData());
    return 0;
}
