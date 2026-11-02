// gui-slot-probe.cpp — GUI 槽级等价探针（FR-24 片 D，round-2）：直接调用 FFExtractor 七个 do* 槽，
// 逐槽转录 jobDone/probeDone 文案与进度序列。同一源码分别链接基线/当前两树的 libengine.a，
// 同媒体对照 → 转录逐字一致 + 产物逐字节一致，即 spec D6「GUI 文案逐字不变」的运行时实证。
// 编译与编排见同目录 gui-slot-eqv.sh；用法：probe <媒体目录> <phase:nomodel|withmodel>
#include <QCoreApplication>
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
}// namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 3) { std::printf("usage: probe <mediaDir> <nomodel|withmodel>\n"); return 2; }
    const QString media = QString::fromUtf8(argv[1]);
    const bool withModel = QString::fromUtf8(argv[2]) == QStringLiteral("withmodel");

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

    // 调一个槽并转录结果（槽在同线程直调，信号于调用内同步送达；与后台线程仅调度面不同）
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

    const QString st = media + "/st.wav";
    const QString st48 = media + "/st48.wav";
    const QString mp4 = media + "/tiny.mp4";
    const QString none = media + "/不存在.wav";

    run("probe", [&] { ex.doProbe(mp4); });
    run("extractAudio", [&] { ex.doExtractAudio(mp4); });
    run("extractVideo", [&] { ex.doExtractVideo(mp4); });
    run("denoise-50-wind", [&] { ex.doDenoiseWav(st, HumBase::Hz50, true); });
    run("denoise-60-nowind", [&] { ex.doDenoiseWav(st, HumBase::Hz60, false); });
    run("denoise-off-wind", [&] { ex.doDenoiseWav(st, HumBase::Off, true); });
    run("denoise-48k", [&] { ex.doDenoiseWav(st48, HumBase::Hz50, true); });
    run("pitch+2.0", [&] { ex.doPitchWav(st, 2.0); });
    run("pitch-1.5", [&] { ex.doPitchWav(st, -1.5); });
    run("fail-noinput", [&] { ex.doDenoiseWav(none, HumBase::Hz50, true); });
    run("fail-extractVideo-on-wav", [&] { ex.doExtractVideo(st); });
    if (!withModel) {
        // 模型缺失分支：appDir 相对解析 +「找不到…」GUI 文案（缺省策略上移给壳，spec D2/D6）
        run("ai-missing", [&] { ex.doDenoiseAiWav(st); });
        run("sep-missing", [&] { ex.doSepVocals(st, false); });
    } else {
        run("ai", [&] { ex.doDenoiseAiWav(st); });
        run("sep-vocals", [&] { ex.doSepVocals(st, false); });
        run("sep-inst", [&] { ex.doSepVocals(st, true); });
    }
    // 取消路径：预置标志 → 底座首检点返回「已取消」→（新）E_CANCELLED message 直通 jobDone
    // ——mainwidget 对该字符串特判（不弹警告框），文案漂移即 GUI 行为回归。
    // 用独立副本输入，避免取消清半成品时删掉前面正常用例的产物
    std::atomic<bool> flag{true};
    ex.setCancelFlag(&flag);
    run("cancel-denoise", [&] { ex.doDenoiseWav(media + "/cancel-st.wav", HumBase::Hz50, true); });
    run("cancel-extractAudio", [&] { ex.doExtractAudio(media + "/cancel-tiny.mp4"); });
    ex.setCancelFlag(nullptr);
    return 0;
}
