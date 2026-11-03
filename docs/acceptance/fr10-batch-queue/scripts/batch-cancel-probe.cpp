// batch-cancel-probe.cpp — FR-10 批处理取消/进度探针（BATCH-CANCEL/BATCH-PROG 代码级证据面）：
// CLI 单发同步不可达的取消语义经 CommandLayer::execute 的 Ctx 注入直证（pipe-cancel-probe 方法论，
// 但不绕命令层——直接驱动 execute 本体），四相运行（编排见同目录 run-batch.sh）：
//   cancel-pre     —— 取消标志预置 → 第 1 件开工前检查点即取消：E_CANCELLED、message 逐字
//                     「已取消」（GUI 静默特判依赖，D7/OQ-2(b)）、error.completed=0/total=2、无 items；
//   cancel-mid     —— 首个进度回调置标志 → 第 1 件引擎分块循环检查点取消：completed=0、零版本目录；
//   cancel-between —— 件 1 收尾映射进度达 50（(0+100)/2）置标志 → 第 2 件开工前取消：completed=1、
//                     件 1 版本已提交保留（OQ-2(b)：被取消的当前件不计完成度）、件 2 无残迹；
//   progress       —— 3 件 × 单步模板完整跑完转录进度序列：件层等分 (i*100+pct)/n 边界 33/66、
//                     单调、终值 100（D6(a)）；
//   progress2      —— 2 件 × 两步模板（denoise→pitch）：件层与 pipeline 步层等分复合映射
//                     边界 25/50/75/100（步 j 收尾 (j*100+100)/2 再映件层 (i*100+that)/n）。
// 断言在编排侧（python + workspace.json 实况），探针只转录响应与进度序列。
// 用法：probe <cancel-pre|cancel-mid|cancel-between|progress|progress2> <输入wav> <工作区根>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QFile>
#include <QList>
#include <atomic>
#include <cstdio>

#include "service/commandlayer.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        std::printf("usage: probe <cancel-pre|cancel-mid|cancel-between|progress|progress2> <输入wav> <工作区根>\n");
        return 2;
    }
    const QString phase = QString::fromUtf8(argv[1]);
    const QString input = QString::fromUtf8(argv[2]);
    const QString wsRoot = QString::fromUtf8(argv[3]);

    // 件清单：追加件 = 输入的字节副本（QFile::copy 至同目录 *.probeN.wav）——路径互异过
    // inputs 重复拒收（D2 字符串级查重），内容相同 → 内容哈希身份落同一工作区（便于断言
    // 件 1 已提交 v001 / 件 2 无残迹：cancel 相位在第 2 件开工前即停，续号不干扰）
    const QString copy1 = input + QStringLiteral(".probe1.wav");
    const QString copy2 = input + QStringLiteral(".probe2.wav");
    QFile::remove(copy1);
    QFile::remove(copy2);
    if (!QFile::copy(input, copy1)) {
        std::printf("FAIL 副本创建失败：%s\n", copy1.toUtf8().constData());
        return 1;
    }
    QJsonArray inputs;
    inputs.append(input);
    inputs.append(copy1);
    if (phase == QLatin1String("progress")) {// 3 件 × 单步：第三件再备一副本
        if (!QFile::copy(input, copy2)) {
            std::printf("FAIL 副本创建失败：%s\n", copy2.toUtf8().constData());
            return 1;
        }
        inputs.append(copy2);
    }

    QJsonArray steps;// 模板：cancel/progress = 单步 pitch（1 步模板最常见批形态，D3）；
    auto step = [](const char* cmd) {// progress2 = 两步 denoise→pitch（步层等分复合）
        QJsonObject s;
        s.insert(QStringLiteral("cmd"), QString::fromLatin1(cmd));
        return s;
    };
    if (phase == QLatin1String("progress2")) {
        QJsonObject den = step("denoise");
        den.insert(QStringLiteral("humBase"), QStringLiteral("60"));
        steps.append(den);
        QJsonObject pit = step("pitch");
        pit.insert(QStringLiteral("semi"), 2.0);
        steps.append(pit);
    } else {
        QJsonObject pit = step("pitch");
        pit.insert(QStringLiteral("semi"), 2.0);
        steps.append(pit);
    }

    QJsonObject req;
    req.insert(QStringLiteral("v"), 1);
    req.insert(QStringLiteral("cmd"), QStringLiteral("batch"));
    req.insert(QStringLiteral("inputs"), inputs);
    req.insert(QStringLiteral("workspace"), wsRoot);
    req.insert(QStringLiteral("steps"), steps);

    std::atomic<bool> flag{false};
    QList<int> prog;
    CommandLayer::Ctx ctx;
    ctx.cancel = &flag;
    ctx.onProgress = [&](int pct) {
        prog.append(pct);
        if (phase == QLatin1String("cancel-mid") && !prog.isEmpty())
            flag = true;// 首个进度回调即置位 → 第 1 件引擎块循环检查点取消
        if (phase == QLatin1String("cancel-between") && pct >= 50)
            flag = true;// 50 = 件 1 收尾 100 的映射值 (0+100)/2 → 第 2 件开工前取消
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
