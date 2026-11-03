// batch-gui-probe.cpp — FR-10 片 B（GUI worker 壳）验收探针（BATCH-GUIE 族）。
// 单模式 engine <wav> <wsRoot> <相位> —— FFExtractor::doBatch 槽直调（pipe-gui-probe engine 相
// 同型，QCoreApplication 无控件），验证壳行为（编排见同目录 run-batch.sh）：
//   ok-batch      免模型单步模板 × 2 件 → batchFinished ok=1 且载荷为批 result JSON（total/okCount/
//                 items 逐件可查）、进度序列终值 100、无 modelMissing、产物在盘；
//   missing-pre   appDir 无模型、模板首步 aiDenoise → batchFinished ok=0 文案「找不到AI 降噪模型：
//                 <bundled>」+ modelMissing(forVerb=aiDenoise) 且**先发 batchFinished 后发
//                 modelMissing**（附裁 1 顺序：引导对话框打开时 busy 已释放）+ 零工作区目录
//                 （整批速断零件启动）；
//   emid-item     appDir 降噪模型为垃圾字节（pre-check 放行）→ 件内引擎 setup E_MODEL →
//                 批 ok=1 okCount=0（编排层成功口径 OQ-1(a)）且**零 modelMissing**（附裁 1：
//                 件循环内禁发——busy 长持下引导下载被 if(busy) return 静默吞 = Y5 毒缝）；
//   cancel        件 1 收尾映射进度达 50 置取消 → batchFinished ok=0 message 逐字「已取消」
//                 （GUI 静默特判的文案契约，附裁 5）。
// 断言在编排侧（python），探针只转录事件序：TASK/BATCHDONE/RESULT/PROG/MISSING。
// 用法：probe engine <wav> <wsRoot> <ok-batch|missing-pre|emid-item|cancel>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QFile>
#include <QList>
#include <atomic>
#include <cstdio>

#include "service/ffextractor.h"

namespace {
QString g_msg;// 最近一次 batchFinished 载荷（ok=JSON / 失败=message）
bool g_ok = false;
int g_done = 0;// batchFinished 触发计数（整批恰好一次——busy 一次 acquire 一次释放的壳侧证据）
int g_missing = 0;// modelMissing 触发计数
QString g_missingFor;// 最近一次 modelMissing 的 forVerb
QString g_order;// 事件序（B=BatchFinished / M=ModelMissing，附裁 1 顺序断言锚）
QList<int> g_prog;// 最近一次任务的进度序列
}// namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::printf("usage: probe engine <wav> <wsRoot> <ok-batch|missing-pre|emid-item|cancel>\n");
        return 2;
    }
    const QString wav = QString::fromUtf8(argv[2]);
    const QString wsRoot = QString::fromUtf8(argv[3]);
    const QString phase = QString::fromUtf8(argv[4]);

    FFExtractor ex;
    QObject::connect(&ex, &FFExtractor::batchFinished, [](bool ok, const QString& jsonOrMessage) {
        g_ok = ok;
        g_msg = jsonOrMessage;
        ++g_done;
        g_order += QLatin1Char('B');
    });
    QObject::connect(&ex, &FFExtractor::progress, [](int pct) { g_prog.append(pct); });
    QObject::connect(&ex, &FFExtractor::modelMissing, [](const QString& forVerb) {
        ++g_missing;
        g_missingFor = forVerb;
        g_order += QLatin1Char('M');
    });
    ex.setWorkspaceRoot(wsRoot);

    // 件清单：两件 = 原文件 + 字节副本（QFile::copy）——路径互异过 inputs 重复拒收（D2），
    // 内容相同 → 内容哈希身份同一工作区（壳层面只关心逐件执行与信号面）
    const QString copy1 = wav + QStringLiteral(".probe1.wav");
    QFile::remove(copy1);
    if (!QFile::copy(wav, copy1)) {
        std::printf("FAIL 副本创建失败：%s\n", copy1.toUtf8().constData());
        return 1;
    }
    QJsonArray inputs;
    inputs.append(wav);
    inputs.append(copy1);

    QJsonArray steps;
    auto step = [](const char* cmd) {
        QJsonObject s;
        s.insert(QStringLiteral("cmd"), QString::fromLatin1(cmd));
        return s;
    };
    if (phase == QLatin1String("ok-batch") || phase == QLatin1String("cancel")) {
        QJsonObject pit = step("pitch");
        pit.insert(QStringLiteral("semi"), 2.0);
        steps.append(pit);// 免模型单步模板
    } else if (phase == QLatin1String("missing-pre") || phase == QLatin1String("emid-item")) {
        steps.append(step("aiDenoise"));// 模型步（无显式 model → 壳内两级缺省解析）
    } else {
        std::printf("unknown phase %s\n", phase.toUtf8().constData());
        return 2;
    }

    if (phase == QLatin1String("cancel")) {
        std::atomic<bool> flag{false};
        ex.setCancelFlag(&flag);
        QObject::connect(&ex, &FFExtractor::progress, [&flag](int pct) {
            if (pct >= 50) flag = true;// 件 1 收尾 100 的映射值 → 件 2 开工前整批取消
        });
    }

    const QString inputsJson = QString::fromUtf8(QJsonDocument(inputs).toJson(QJsonDocument::Compact));
    const QString stepsJson = QString::fromUtf8(QJsonDocument(steps).toJson(QJsonDocument::Compact));
    ex.doBatch(inputsJson, stepsJson);

    std::printf("TASK %s -> ok=%d msg=%s\n", phase.toUtf8().constData(), g_ok ? 1 : 0,
                g_msg.toUtf8().constData());
    std::printf("BATCHDONE %d order=%s\n", g_done, g_order.toUtf8().constData());
    std::printf("RESULT %s\n", g_ok ? g_msg.toUtf8().constData() : "");
    QStringList ps;
    for (int p : g_prog) ps << QString::number(p);
    std::printf("PROG %s : %s\n", phase.toUtf8().constData(), ps.join(QLatin1Char(',')).toUtf8().constData());
    std::printf("MISSING %d for=%s\n", g_missing, g_missingFor.toUtf8().constData());
    return 0;
}
