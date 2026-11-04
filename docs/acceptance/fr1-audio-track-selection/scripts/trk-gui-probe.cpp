// trk-gui-probe.cpp — FR-1 多音轨选择 GUI 面验收探针（TRK-GUI 族）。
// 两模式（编排见同目录 run-trk.sh TRK-GUI 段）：
//   engine <multi.mkv> <wsRootA> <wsRootB> —— FFExtractor 槽直调（mdl-gui-probe engine 相同面）：
//        PROBEJSON    doProbe 双发射的 probeInfoDone JSON（与命令层 cmdProbe 同源 probeJson 单源）
//        TEXTZERO     probeDone 文本 == 旧静态入口 FFExtractor::probe 的文本（probeText 零漂移锚）
//        TASK_T1 / TASK_AUTO   doExtractAudio(track=1) / 缺省 track 两相位：产物名 _t1_audio.* /
//        不带 _t 段 + 版本记录 params {"track":1} / {}（worker 槽 track>=0 才组键）
//   gui <multi.mkv> <tiny.mp4> <wsRoot> <libRoot> —— MainWidget+MediaService 全链（offscreen
//        QApplication；QSettings 经编排侧 XDG_CONFIG_HOME 隔离，工作区/模型库根预写两键走 GUI
//        真实读路径 D7）验证轨道下拉接线（FR-1 D5）：
//        COMBO        multi.mkv 载入：4 项（自动+3 轨）缺省选中「自动」（itemData=-1）、音轨条目
//                     itemData=容器绝对索引、标签含 流#/语言/默认
//        COMBO_TINY   tiny.mp4（单音轨）载入：下拉隐藏（OQ-2 拍板「隐藏」）且仅剩自动项
//        RESET_SYNC   选轨 3 后换文件：openFile 同步重置（调用返回即 count=1/自动/隐藏，不等异步
//                     probeInfoDone——防跨文件 stale 索引的护栏正在此处）
//        RESET_AUTO   重新载入 multi.mkv：可见 4 项且选中回「自动」（换文件重置）
//        BUSYNOTLOCKED 任务运行中 cmbTrack 不锁（面板动态态族，aiVerb 同型）、任务按钮已锁
//        CAP / CAP2   选轨 3 提取 → audioRequested.track=3；「自动」→ track=-1（= 组包不带键）
//        CAPPIPE      选轨 3 管线预设 0 → pipelineRequested.track=3（顶层轨参随行）+
//                     jobDone「管线完成：」文案（TRK-GUI-002 文案零漂移）
//        TEXTZERO_G   txtInfo == FFExtractor::probe 静态文本（GUI 展示面零漂移，TRK-GUI-002）
//        文件对话框自动应答 20ms 轮询 activeModalWidget（fr4-gui-probe 配方），轮询相位变量
//        g_nextFile 决定每次 doOpen 填入的文件。
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPair>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

#include <atomic>
#include <cstdio>
#include <functional>

#include "model/data.h"// kSettings*/kWsIndexFile 键
#include "service/ffextractor.h"
#include "service/mediaservice.h"
#include "view/mainwidget.h"

namespace {

// 轮询等待谓词成真（30ms 节拍 + 超时兜底；fr4-gui-probe 同型）
bool waitUntil(const std::function<bool()>& pred, int timeoutMs) {
    QEventLoop loop;
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &loop, [&] { if (pred()) loop.quit(); });
    t.start(30);
    QTimer deadline;
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.setSingleShot(true);
    deadline.start(timeoutMs);
    loop.exec();
    return pred();
}

// 工作区首个版本的 params 与产物绝对路径（TRK-GUI worker 槽组键断言真值）
QPair<QJsonObject, QString> firstVersion(const QString& wsRoot) {
    const QStringList dirs = QDir(wsRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& d : dirs) {
        QFile f(wsRoot + "/" + d + "/" + QString::fromUtf8(kWsIndexFile));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonArray versions = QJsonDocument::fromJson(f.readAll()).object()
                                        .value(QStringLiteral("versions")).toArray();
        if (versions.isEmpty()) continue;
        const QJsonObject v = versions.at(0).toObject();
        const QString rel = v.value(QStringLiteral("output")).toObject()
                                .value(QStringLiteral("path")).toString();
        return {v.value(QStringLiteral("params")).toObject(), wsRoot + "/" + d + "/" + rel};
    }
    return {QJsonObject(), QStringLiteral("-")};
}

}// namespace

// ---------------- engine 相（FFExtractor 槽直调） ----------------
static int runEngine(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QString multi = QString::fromLocal8Bit(argv[2]);
    const QString wsA = QString::fromLocal8Bit(argv[3]);
    const QString wsB = QString::fromLocal8Bit(argv[4]);

    FFExtractor ex;
    // 双发射捕获：probeDone 文本 + probeInfoDone JSON（单遍探测两信号同帧）
    QString probeTextGui, probeJsonStr;
    QObject::connect(&ex, &FFExtractor::probeDone, [&](bool, const QString& info) { probeTextGui = info; });
    QObject::connect(&ex, &FFExtractor::probeInfoDone, [&](bool, const QString& json) { probeJsonStr = json; });
    bool t1Ok = false, autoOk = false;
    QString t1Msg, autoMsg;
    QObject::connect(&ex, &FFExtractor::jobDone, [&](bool o, const QString& m) {
        if (!t1Msg.isEmpty()) { if (autoMsg.isEmpty()) autoMsg = m, autoOk = o; }
        else t1Msg = m, t1Ok = o;
    });

    ex.doProbe(multi);
    std::printf("PROBEJSON %s\n", probeJsonStr.toUtf8().constData());
    QString legacyInfo;
    const bool legacyOk = FFExtractor::probe(multi, &legacyInfo);// 旧静态入口（probeText 零漂移锚）
    std::printf("TEXTZERO equal=%d legacyOk=%d\n",
                legacyOk && probeTextGui == legacyInfo ? 1 : 0, legacyOk ? 1 : 0);

    ex.setWorkspaceRoot(wsA);
    ex.doExtractAudio(multi, 1);// 显式轨：缺省名插 _t1 段 + 版本记录 params {"track":1}
    waitUntil([&] { return !t1Msg.isEmpty(); }, 30000);
    const auto r1 = firstVersion(wsA);
    std::printf("TASK_T1 ok=%d msg=%s\nparams=%s\nout=%s\n", t1Ok ? 1 : 0,
                t1Msg.toUtf8().constData(),
                QString::fromUtf8(QJsonDocument(r1.first).toJson(QJsonDocument::Compact)).toUtf8().constData(),
                r1.second.toUtf8().constData());

    ex.setWorkspaceRoot(wsB);
    ex.doExtractAudio(multi);// 缺省 track=-1：不带键（命名无 _t 段 + params {}）
    waitUntil([&] { return !autoMsg.isEmpty(); }, 30000);
    const auto r2 = firstVersion(wsB);
    std::printf("TASK_AUTO ok=%d msg=%s\nparams=%s\nout=%s\n", autoOk ? 1 : 0,
                autoMsg.toUtf8().constData(),
                QString::fromUtf8(QJsonDocument(r2.first).toJson(QJsonDocument::Compact)).toUtf8().constData(),
                r2.second.toUtf8().constData());
    return 0;
}

// ---------------- gui 相（MainWidget + MediaService 全链） ----------------
static int runGui(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);// 文件对话框强制 Qt 控件实现（可确定性应答）
    QApplication app(argc, argv);
    const QString multi = QString::fromLocal8Bit(argv[2]);
    const QString tiny = QString::fromLocal8Bit(argv[3]);
    const QString wsRoot = QString::fromLocal8Bit(argv[4]);
    const QString libRoot = QString::fromLocal8Bit(argv[5]);

    // 受控根注入：MainWidget 构造前预写两键（GUI 真实读路径 D7）
    QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), wsRoot);
    QSettings().setValue(QString::fromUtf8(kSettingsModelRoot), libRoot);

    MediaService svc;
    MainWidget w;
    w.setMediaService(&svc);
    w.show();// offscreen 下显式 show：显/隐契约（≥2 音轨显示、单轨隐藏）要求顶层已显示，isVisible 才有效

    // 请求捕获器（发射时同步到达：同线程直连）
    std::atomic<int> capAudioTrack{999};
    std::atomic<int> capPipeTrack{999};
    QString capPipeSteps, lastJobMsg, guiProbeInfo;
    bool g_jobDone = false;
    QObject::connect(&svc, &MediaService::audioRequested,
                     [&](const QString&, int track) { capAudioTrack = track; });
    QObject::connect(&svc, &MediaService::pipelineRequested,
                     [&](const QString&, const QString& steps, int track) {
                         capPipeSteps = steps;
                         capPipeTrack = track;
                     });
    QObject::connect(&svc, &MediaService::jobFinished, [&](bool, const QString& m) {
        lastJobMsg = m;
        g_jobDone = true;
    });
    QObject::connect(&svc, &MediaService::probeInfoFinished,
                     [&](bool, const QString& json) { guiProbeInfo = json; });

    // 文件对话框自动应答器：g_nextFile 为本次 doOpen 填入的文件（载入相位切换的锚）
    QString g_nextFile = multi;
    QTimer answerer;
    answerer.setInterval(20);
    QObject::connect(&answerer, &QTimer::timeout, &w, [&] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) return;
        if (auto* box = qobject_cast<QMessageBox*>(m)) {
            if (auto* b = box->button(QMessageBox::Ok)) b->click();
            else if (auto* d = box->defaultButton()) d->click();
        } else if (auto* fd = qobject_cast<QFileDialog*>(m)) {
            if (auto* le = fd->findChild<QLineEdit*>()) le->setText(g_nextFile);
            if (auto* bb = fd->findChild<QDialogButtonBox*>()) {
                for (auto* b : bb->buttons())
                    if (bb->buttonRole(b) == QDialogButtonBox::AcceptRole) { b->click(); break; }
            }
            static_cast<QDialog*>(fd)->accept();
        }
    });
    answerer.start();

    auto* cmbTrack = w.findChild<QComboBox*>(QStringLiteral("cmbTrack"));
    auto* btnAudio = w.findChild<QPushButton*>(QStringLiteral("btnAudio"));
    auto* btnPipe = w.findChild<QPushButton*>(QStringLiteral("btnPipeline"));
    auto* cmbPipe = w.findChild<QComboBox*>(QStringLiteral("cmbPipeline"));
    auto* txtInfo = w.findChild<QPlainTextEdit*>(QStringLiteral("txtInfo"));
    if (!cmbTrack || !btnAudio || !btnPipe || !cmbPipe) {
        std::printf("FAIL 控件缺失 cmbTrack/btnAudio/btnPipeline/cmbPipeline\n");
        return 1;
    }
    auto comboState = [&](QComboBox* c) {
        QString s = QStringLiteral("count=%1 vis=%2 idx=%3 data0=%4")
                       .arg(c->count()).arg(c->isVisible() ? 1 : 0)
                       .arg(c->currentIndex()).arg(c->itemData(0).toInt());
        for (int i = 1; i < c->count(); ++i)
            s += QStringLiteral(" |%1#%2").arg(c->itemData(i).toInt()).arg(c->itemText(i));
        return s;
    };

    // --- 载入 multi.mkv：下拉 4 项（自动+3 轨）缺省「自动」 ---
    w.doOpen();
    const bool comboUp = waitUntil([&] { return cmbTrack->isVisible() && cmbTrack->count() == 4; }, 15000);
    std::printf("COMBO up=%d %s\n", comboUp ? 1 : 0, comboState(cmbTrack).toUtf8().constData());
    std::printf("PROBEJSON_G %s\n", guiProbeInfo.toUtf8().constData());
    waitUntil([&] { return txtInfo->toPlainText() != QStringLiteral("正在读取流信息…")
                            && !txtInfo->toPlainText().isEmpty(); }, 5000);
    QString legacyInfo;
    FFExtractor::probe(multi, &legacyInfo);
    std::printf("TEXTZERO_G equal=%d\n", txtInfo->toPlainText() == legacyInfo ? 1 : 0);

    // --- 选轨 3 → 七槽提取：请求 track=3 + busy 期下拉不锁（任务按钮已锁对照组） ---
    cmbTrack->setCurrentIndex(3);
    capAudioTrack = 999;
    g_jobDone = false;
    w.doExtractAudio();
    const bool trackNotLocked = cmbTrack->isEnabled();
    const bool taskLocked = !btnAudio->isEnabled();
    std::printf("BUSYNOTLOCKED cmbEnabled=%d btnLocked=%d\n", trackNotLocked ? 1 : 0,
                taskLocked ? 1 : 0);
    waitUntil([&] { return g_jobDone; }, 30000);
    std::printf("CAP track=%d msg=%s\n", capAudioTrack.load(),
                lastJobMsg.toUtf8().constData());
    waitUntil([&] { return btnAudio->isEnabled(); }, 5000);

    // --- 「自动」再提取：请求 track=-1（= 组包不带键） ---
    cmbTrack->setCurrentIndex(0);
    capAudioTrack = 999;
    g_jobDone = false;
    w.doExtractAudio();
    waitUntil([&] { return g_jobDone; }, 30000);
    std::printf("CAP2 track=%d\n", capAudioTrack.load());
    waitUntil([&] { return btnAudio->isEnabled(); }, 5000);

    // --- 换文件同步重置：选轨后载入 tiny.mp4，doOpen 返回即重置（不等异步探测） ---
    cmbTrack->setCurrentIndex(2);
    g_nextFile = tiny;
    w.doOpen();
    const bool resetSync = cmbTrack->count() == 1 && cmbTrack->currentIndex() == 0
                           && cmbTrack->itemData(0).toInt() == -1 && !cmbTrack->isVisible();
    std::printf("RESET_SYNC ok=%d %s\n", resetSync ? 1 : 0, comboState(cmbTrack).toUtf8().constData());
    waitUntil([&] { return txtInfo->toPlainText() != QStringLiteral("正在读取流信息…"); }, 15000);
    std::printf("COMBO_TINY %s\n", comboState(cmbTrack).toUtf8().constData());

    // --- 重新载入 multi.mkv：可见 4 项且选中回「自动」+ 管线轨参随行 ---
    g_nextFile = multi;
    w.doOpen();
    const bool comboUp2 = waitUntil([&] { return cmbTrack->isVisible() && cmbTrack->count() == 4; }, 15000);
    const bool resetAuto = cmbTrack->currentIndex() == 0;
    std::printf("RESET_AUTO up=%d idx=%d\n", comboUp2 ? 1 : 0, cmbTrack->currentIndex());
    cmbTrack->setCurrentIndex(3);
    capPipeTrack = 999;
    g_jobDone = false;
    cmbPipe->setCurrentIndex(0);// 预设 0：aiDenoise → separate（模型两级解析取 libRoot 预装库）
    w.doPipeline();
    waitUntil([&] { return g_jobDone && capPipeTrack.load() != 999; }, 60000);
    std::printf("CAPPIPE track=%d steps=%s\nmsg=%s\n", capPipeTrack.load(),
                capPipeSteps.toUtf8().constData(), lastJobMsg.toUtf8().constData());

    std::printf("PROBE DONE\n");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 5 && QByteArray(argv[1]) == "engine") return runEngine(argc, argv);
    if (argc >= 6 && QByteArray(argv[1]) == "gui") return runGui(argc, argv);
    std::fprintf(stderr,
                 "用法: %s engine <multi.mkv> <wsRootA> <wsRootB> | gui <multi.mkv> <tiny.mp4> <wsRoot> <libRoot>\n",
                 argv[0]);
    return 2;
}
