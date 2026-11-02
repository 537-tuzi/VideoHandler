// gui-ws-probe.cpp — GUI 工作区接线探针（FR-A4 片 B，round-2）：直接调用 FFExtractor 槽验证 D5 接线，
// 四相运行（编排见同目录 gui-ws.sh）：
//   ws     —— setWorkspaceRoot 后七槽任务 + doWsInfo 历史查询 + 「用此版本继续」链（以 v001 产物为输入
//             开新任务，OQ-3）+ probe 槽（root 注入后仍正常，证 probe 请求未被插 workspace 键）；
//   cancel —— 预置 cancelFlag → 「已取消」直通 + 版本目录补删（索引不动由编排侧逐字节比对）；
//   direct —— 不 setWorkspaceRoot（worker 成员空串 = 任务不带 workspace 键）→ 产物落源目录；
//   adv    —— 对抗增补相（round-3）：换根只影响后续任务 / 注入后置空串回 direct（对称面）/
//             doWsInfo 对不在盘文件回失败载荷（onWsInfoDone 警告分支的数据面）/ 中文根+中文素材直通。
// 槽在同线程直调，信号于调用内同步送达（与后台线程仅调度面不同）；断言为绝对断言（基线无此功能）。
// 用法：probe <媒体目录> <工作区根|-> <ws|cancel|direct|adv>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdio>

#include "service/dspdenoiser.h"// HumBase 制式枚举
#include "service/ffextractor.h"

namespace {
QString g_msg;// 最近一次 jobDone/probeDone 文案
QList<int> g_prog;// 最近一次任务的进度序列
bool g_ok = false;
QString g_wsInfo;// 最近一次 wsInfoDone 回传（ok=true → result 的 Compact JSON；失败 → 错误说明）
}// namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        std::printf("usage: probe <媒体目录> <工作区根|-> <ws|cancel|direct>\n");
        return 2;
    }
    const QString media = QString::fromUtf8(argv[1]);
    const QString wsRoot = QString::fromUtf8(argv[2]);
    const QString phase = QString::fromUtf8(argv[3]);

    FFExtractor ex;
    QObject::connect(&ex, &FFExtractor::jobDone, [](bool ok, const QString& message) {
        g_ok = ok;
        g_msg = message;
    });
    QObject::connect(&ex, &FFExtractor::progress, [](int pct) { g_prog.append(pct); });
    QObject::connect(&ex, &FFExtractor::probeDone, [](bool ok, const QString& info) {
        g_ok = ok;
        g_msg = info;
    });
    QObject::connect(&ex, &FFExtractor::wsInfoDone, [](bool ok, const QString& json) {
        g_ok = ok;
        g_wsInfo = json;
    });

    // 调一个任务槽并转录结果（文案 + 进度序列），形态与 fr24 的 gui-slot-probe 一致
    auto run = [&ex](const char* name, auto&& slotCall) {
        g_msg.clear();
        g_prog.clear();
        g_ok = false;
        slotCall();
        std::printf("TASK %s -> ok=%d msg=%s\n", name, g_ok ? 1 : 0, g_msg.toUtf8().constData());
        QStringList ps;
        for (int p : g_prog) ps << QString::number(p);
        std::printf("PROG %s : %s\n", name, ps.join(QLatin1Char(',')).toUtf8().constData());
        std::fflush(stdout);
    };
    // 调 doWsInfo 并转录回传（ok=1 时 payload 为 result 的 Compact JSON，编排侧 python 深度比对）
    auto runWs = [&ex](const char* name, auto&& slotCall) {
        g_wsInfo.clear();
        g_ok = false;
        slotCall();
        std::printf("WSINFO %s -> ok=%d %s\n", name, g_ok ? 1 : 0, g_wsInfo.toUtf8().constData());
        std::fflush(stdout);
    };

    const QString st = media + "/st.wav";
    const QString mp4 = media + "/tiny.mp4";

    if (phase == QStringLiteral("ws")) {
        ex.setWorkspaceRoot(wsRoot);// GUI 经 MediaService 队列注入的同一槽
        run("extractAudio", [&] { ex.doExtractAudio(mp4); });
        run("extractVideo", [&] { ex.doExtractVideo(mp4); });
        run("denoise", [&] { ex.doDenoiseWav(st, HumBase::Hz50, true); });
        run("aiDenoise", [&] { ex.doDenoiseAiWav(st); });// 模型经 appDir 相对解析（编排侧落位）
        run("pitch", [&] { ex.doPitchWav(st, 2.0); });
        run("sep-vocals", [&] { ex.doSepVocals(st, false); });
        run("sep-inst", [&] { ex.doSepVocals(st, true); });
        // 历史查询（「处理历史」按钮背后的槽）：st 工作区此刻 5 个版本
        runWs("wsInfo", [&] { ex.doWsInfo(st); });
        // 「用此版本继续」（OQ-3）：对话框回拼产物绝对路径（result.dir + 记录内相对路径）后 openFile
        // 等价链路——此处直取 v001 产物为输入开新任务（同工作区 → parentVersion=1）
        const QJsonObject result = QJsonDocument::fromJson(g_wsInfo.toUtf8()).object();
        const QString product = result.value(QStringLiteral("dir")).toString() + "/" +
                                result.value(QStringLiteral("versions")).toArray().at(0).toObject()
                                    .value(QStringLiteral("output")).toObject()
                                    .value(QStringLiteral("path")).toString();
        std::printf("CHAIN product=%s\n", product.toUtf8().constData());
        std::fflush(stdout);
        run("chain-denoise", [&] { ex.doDenoiseWav(product, HumBase::Hz50, true); });
        runWs("wsInfo2", [&] { ex.doWsInfo(st); });// 终态清单（含 v006 与 parent 链），供深度比对
        // GWS-007：root 已注入 worker，probe 槽仍走 direct（doProbe 不经 runRequest 插键路径）
        run("probe", [&] { ex.doProbe(mp4); });
    } else if (phase == QStringLiteral("cancel")) {
        ex.setWorkspaceRoot(wsRoot);
        // 预置取消标志 → plan 后任务首检点返回「已取消」→ 工作区层补删版本目录、索引不动
        std::atomic<bool> flag{true};
        ex.setCancelFlag(&flag);
        run("cancel-denoise", [&] { ex.doDenoiseWav(media + "/cancel-st.wav", HumBase::Hz50, true); });
        run("cancel-extractAudio", [&] { ex.doExtractAudio(media + "/cancel-tiny.mp4"); });
        ex.setCancelFlag(nullptr);
    } else if (phase == QStringLiteral("direct")) {
        // 不 setWorkspaceRoot：worker 成员为空串 → runRequest 不插 workspace 键（direct 模式零漂移）
        run("extractAudio", [&] { ex.doExtractAudio(mp4); });
        run("extractVideo", [&] { ex.doExtractVideo(mp4); });
        run("denoise", [&] { ex.doDenoiseWav(st, HumBase::Hz50, true); });
        run("probe", [&] { ex.doProbe(mp4); });
    } else if (phase == QStringLiteral("adv")) {
        // 对抗增补相（round-3，测试轮残余由主线程接手跑）：同输入跨根/置空/失败载荷/中文路径
        ex.setWorkspaceRoot(wsRoot);
        run("adv-denoise-r1", [&] { ex.doDenoiseWav(st, HumBase::Hz50, true); });// root1 落 v001
        const QString root2 = wsRoot + "_2";
        ex.setWorkspaceRoot(root2);
        run("adv-denoise-r2", [&] { ex.doDenoiseWav(st, HumBase::Hz50, true); });// 换根只影响后续任务：root2 独立工作区、版本号从 1 起
        const QString rootCn = wsRoot + "_中文根";
        ex.setWorkspaceRoot(rootCn);
        run("adv-denoise-cn", [&] { ex.doDenoiseWav(media + "/中文素材.wav", HumBase::Hz50, true); });
        // doWsInfo 失败载荷：输入不在盘（身份解析无从谈起）→ ok=0 + 错误说明（onWsInfoDone 警告分支）
        runWs("adv-wsinfo-miss", [&] { ex.doWsInfo(media + "/不存在.wav"); });
        // 注入过 root 后置空串 → 回 direct（空串不插键的对称面，换根 UI 只换不清空的兜底语义）
        ex.setWorkspaceRoot(QString());
        run("adv-denoise-emptyroot", [&] { ex.doDenoiseWav(st, HumBase::Hz50, true); });
    } else {
        std::printf("usage: probe <媒体目录> <工作区根|-> <ws|cancel|direct|adv>\n");
        return 2;
    }
    return 0;
}
