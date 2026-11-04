// fr4-gui-probe.cpp — FR-4 质量档 GUI 面验收探针（GFR4 族）。
// 两模式（编排见同目录 run-fr4.sh GFR4 段）：
//   engine <wav> <libRoot> <wsRoot> <相位> —— FFExtractor 槽直调（mdl-gui-probe engine 相同面）
//        验证质量档两级缺省解析（FR-8 D1 延伸到目录型模型）与结构化 modelMissing（D8）：
//          hq-missing     appDir 无随包 + 空库 → jobDone 文案含「找不到 AI 降噪（质量档）模型：」
//                        + modelMissing(aiDenoiseHq)（forVerb 逐字，防退化为通用/旧动词）
//          hq-lib         appDir 无随包 + 库已装 dfn3 → 任务 ok 且版本记录 model.path = 库路径
//                        （选用模型真值 = workspace.json，非任务文案）
//          fast-missing   零漂移锚：快速档同布局 → 旧文案「找不到 AI 降噪模型：」+
//                        modelMissing(aiDenoise) 逐字不变
//   gui <wav> <wav2> <wsRoot> <libRoot> —— MainWidget+MediaService 全链（offscreen QApplication；
//        QSettings 经编排侧 XDG_CONFIG_HOME 隔离，工作区/模型库根由本探针预写两键走 GUI 真实
//        读路径 D7）验证档位接线（FR-4 片 B）：
//          TIERDEFAULT    档位下拉缺省快速（idx=0），两项文案「快速（GTCRN）/质量（DeepFilterNet3）」
//          TIERPERSIST    切质量档即写 QSettings(aiDenoiseTier=quality)；新构造主窗读回 idx=1
//          PATH*          三路组包同源取 aiVerb()：七槽（aiDenoiseRequested.quality=true）、
//                        管线预设 0 步 0（steps[0].cmd=aiDenoiseHq）、批处理 AI 单步动作
//                        （steps[0].cmd=aiDenoiseHq，inputs=2）；切回快速档三面回落 aiDenoise
//                        （行尾 FAST 标记，档位是面板动态状态非一次性组包参数）
//        空库布局：三路请求均在 worker 预解析段毫秒级失败（E_MODEL→modelMissing→问询自动 Yes
//        →模型管理对话框），请求参数在发射时即被本探针捕获——失败路径不影响组包断言；
//        弹窗自动应答 20ms 轮询 activeModalWidget（mdl-gui-probe 配方）。
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
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

#include <atomic>
#include <cstdio>
#include <functional>

#include "model/data.h"// kSettings*/kWsIndexFile 键（受控根注入与版本记录读取）
#include "service/ffextractor.h"
#include "service/mediaservice.h"
#include "view/mainwidget.h"

namespace {

// 轮询等待谓词成真（30ms 节拍 + 超时兜底；事件循环驱动弹窗应答与跨线程信号，missing-text-probe 同型）
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

// 工作区首个版本记录的 model.path（选用模型真值；任务 ok 才有版本记录，否则回 "-"）
QString firstModelPath(const QString& wsRoot) {
    const QStringList dirs = QDir(wsRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& d : dirs) {
        QFile f(wsRoot + "/" + d + "/" + QString::fromUtf8(kWsIndexFile));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonArray versions = QJsonDocument::fromJson(f.readAll()).object()
                                        .value(QStringLiteral("versions")).toArray();
        if (!versions.isEmpty())
            return versions.at(0).toObject().value(QStringLiteral("model")).toObject()
                       .value(QStringLiteral("path")).toString();
    }
    return QStringLiteral("-");
}

// 工作区首个版本记录的产物绝对路径（output 是对象，path 为相对版本根的路径；round-2 GFR4-104 用）
QString firstOutputPath(const QString& wsRoot) {
    const QStringList dirs = QDir(wsRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& d : dirs) {
        QFile f(wsRoot + "/" + d + "/" + QString::fromUtf8(kWsIndexFile));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonArray versions = QJsonDocument::fromJson(f.readAll()).object()
                                        .value(QStringLiteral("versions")).toArray();
        if (versions.isEmpty()) continue;
        const QString rel = versions.at(0).toObject().value(QStringLiteral("output")).toObject()
                                .value(QStringLiteral("path")).toString();
        if (!rel.isEmpty()) return wsRoot + "/" + d + "/" + rel;
    }
    return QStringLiteral("-");
}

}// namespace

// ---------------- engine 相（FFExtractor 槽直调） ----------------
static int runEngine(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);// 无控件；无网络（库 fixture 由编排侧 CLI 预装）
    const QString wav = QString::fromLocal8Bit(argv[2]);
    const QString libRoot = QString::fromLocal8Bit(argv[3]);
    const QString wsRoot = QString::fromLocal8Bit(argv[4]);
    const QString phase = QString::fromLocal8Bit(argv[5]);

    FFExtractor ex;
    bool ok = false;
    QString msg, missingVerb;
    int missing = 0;
    QObject::connect(&ex, &FFExtractor::jobDone, [&](bool o, const QString& m) { ok = o; msg = m; });
    QObject::connect(&ex, &FFExtractor::modelMissing, [&](const QString& forVerb) {
        ++missing;
        missingVerb = forVerb;
    });

    ex.setWorkspaceRoot(wsRoot);// 版本记录（model.path 真值）落这里
    ex.setModelRoot(libRoot);
    // round-2：fast-lib 相（快速档 + 库内 gtcrn）——档位=快速产物面零漂移锚的载体（GFR4-104）
    const bool quality = phase == QStringLiteral("hq-missing") || phase == QStringLiteral("hq-lib");
    ex.doDenoiseAiWav(wav, quality);

    std::printf("TASK %s -> ok=%d msg=%s\n", phase.toUtf8().constData(), ok ? 1 : 0,
                msg.toUtf8().constData());
    std::printf("MODELMISSING %d %s\n", missing, missingVerb.toUtf8().constData());
    std::printf("MODELPATH %s\n", firstModelPath(wsRoot).toUtf8().constData());
    std::printf("MODELOUT %s\n", firstOutputPath(wsRoot).toUtf8().constData());
    return 0;
}

// ---------------- gui 相（MainWidget + MediaService 全链） ----------------
static int runGui(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);// 文件对话框强制 Qt 控件实现（可确定性应答）
    QApplication app(argc, argv);
    const QString wav = QString::fromLocal8Bit(argv[2]);
    const QString wav2 = QString::fromLocal8Bit(argv[3]);
    const QString wsRoot = QString::fromLocal8Bit(argv[4]);
    const QString libRoot = QString::fromLocal8Bit(argv[5]);

    // 受控根注入：MainWidget 构造前预写两键（GUI 真实读路径 D7）；libRoot = 空库（在位目录零条目）
    QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), wsRoot);
    QSettings().setValue(QString::fromUtf8(kSettingsModelRoot), libRoot);

    MediaService svc;
    MainWidget w;
    w.setMediaService(&svc);// 内部完成根下传（workspaceRoot/modelRoot → worker）

    // 请求捕获器（发射时同步到达：同线程直连）：三路组包同源取 aiVerb() 的断言锚
    std::atomic<int> capQuality{-1};
    QString capPipe, capBatchSteps;
    int capBatchInputs = 0;
    bool g_jobDone = false, g_batchDone = false;
    QObject::connect(&svc, &MediaService::aiDenoiseRequested, [&](const QString&, bool quality) {
        capQuality = static_cast<int>(quality);
    });
    QObject::connect(&svc, &MediaService::pipelineRequested,
                     [&](const QString&, const QString& steps) { capPipe = steps; });
    QObject::connect(&svc, &MediaService::batchRequested, [&](const QString& inputs, const QString& steps) {
        capBatchSteps = steps;
        capBatchInputs = QJsonDocument::fromJson(inputs.toUtf8()).array().size();
    });
    QObject::connect(&svc, &MediaService::jobFinished, [&](bool, const QString&) { g_jobDone = true; });
    QObject::connect(&svc, &MediaService::batchFinished, [&](bool, const QString&) { g_batchDone = true; });

    // 弹窗自动应答器（20ms 轮询 activeModalWidget）：Question 框 = onModelMissing 问询（唯一
    // Question 源）点 Yes → 打开模型管理对话框；其余消息框点 Ok/默认键；文件对话框直填+确认
    QString g_missingText;// round-2：问询文案捕获（GFR4-204/205；应答器同帧写入，相位打印）
    QTimer answerer;
    answerer.setInterval(20);
    QObject::connect(&answerer, &QTimer::timeout, &w, [&] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) return;
        if (auto* box = qobject_cast<QMessageBox*>(m)) {
            if (box->icon() == QMessageBox::Question) {
                g_missingText = box->text();// 先记后点：文案随相位捕获（hq/fast 两态断言）
                if (auto* b = box->button(QMessageBox::Yes)) b->click();
                return;
            }
            if (auto* b = box->button(QMessageBox::Ok)) b->click();
            else if (auto* d = box->defaultButton()) d->click();
        } else if (auto* fd = qobject_cast<QFileDialog*>(m)) {
            if (auto* le = fd->findChild<QLineEdit*>()) le->setText(wav);
            if (auto* bb = fd->findChild<QDialogButtonBox*>()) {
                for (auto* b : bb->buttons())
                    if (bb->buttonRole(b) == QDialogButtonBox::AcceptRole) { b->click(); break; }
            }
            static_cast<QDialog*>(fd)->accept();
        }
    });
    answerer.start();
    auto closeModelDlg = [&] {// 每相位复位：Yes→开框后关闭，保下一相位应答区分力
        if (auto* d = w.findChild<QDialog*>(QStringLiteral("modelDlg"))) d->close();
    };

    // --- 档位缺省 + 文案面 ---
    auto* cmbTier = w.findChild<QComboBox*>(QStringLiteral("cmbAiTier"));
    if (!cmbTier) { std::printf("FAIL 控件缺失 cmbAiTier\n"); return 1; }
    std::printf("TIERDEFAULT idx=%d texts=%s|%s\n", cmbTier->currentIndex(),
                cmbTier->itemText(0).toUtf8().constData(),
                cmbTier->itemText(1).toUtf8().constData());
    std::fflush(stdout);

    // --- 档位持久化：切质量即写键；新实例读回 ---
    cmbTier->setCurrentIndex(1);
    const QString saved = QSettings().value(QString::fromUtf8(kSettingsAiTier)).toString();
    MainWidget w2;
    w2.setMediaService(&svc);
    auto* cmbTier2 = w2.findChild<QComboBox*>(QStringLiteral("cmbAiTier"));
    std::printf("TIERPERSIST settings=%s reloadIdx=%d\n", saved.toUtf8().constData(),
                cmbTier2->currentIndex());
    std::fflush(stdout);
    MainWidget& mw = w2;// 后续相位全走读回质量档的实例

    // --- 载入素材（doOpen 弹文件对话框，应答器直填+确认 → openFile 异步探测；轮询等解禁） ---
    auto* btnPipe = mw.findChild<QPushButton*>(QStringLiteral("btnPipeline"));
    auto* cmbPipe = mw.findChild<QComboBox*>(QStringLiteral("cmbPipeline"));
    if (!btnPipe || !cmbPipe) { std::printf("FAIL 控件缺失 cmbPipeline/btnPipeline\n"); return 1; }
    mw.doOpen();
    waitUntil([&] { return btnPipe->isEnabled(); }, 15000);

    // --- 路 1：七槽（AI 降噪按钮 → aiDenoiseRequested(quality=true)） ---
    capQuality = -1;
    g_jobDone = false;
    mw.doDenoiseAiWav();
    waitUntil([&] { return g_jobDone && capQuality >= 0; }, 20000);
    std::printf("PATH7SLOT quality=%d\n", capQuality.load());
    std::printf("MISSINGTEXT hq=%s\n", g_missingText.toUtf8().constData());
    std::fflush(stdout);
    waitUntil([&] { return btnPipe->isEnabled(); }, 5000);// 空闲态确认（busy 放锁后；missing-text-probe 同型）
    closeModelDlg();

    // --- 路 2：管线预设 0（aiDenoise→separate，步 0 随档位走 aiDenoiseHq） ---
    capPipe.clear();
    g_jobDone = false;
    cmbPipe->setCurrentIndex(0);
    mw.doPipeline();
    waitUntil([&] { return g_jobDone && !capPipe.isEmpty(); }, 20000);
    std::printf("PATHPIPE steps=%s\n", capPipe.toUtf8().constData());
    std::fflush(stdout);
    waitUntil([&] { return btnPipe->isEnabled(); }, 5000);
    closeModelDlg();

    // --- 路 3：批处理 AI 单步动作（OQ-6 组合 index 1，随档位走 aiDenoiseHq） ---
    auto runBatch = [&](const char* tag) {
        mw.openBatchDialog();
        auto* lst = mw.findChild<QListWidget*>(QStringLiteral("lstBatchFiles"));
        auto* cmbAct = mw.findChild<QComboBox*>(QStringLiteral("cmbBatchAction"));
        auto* btnStart = mw.findChild<QPushButton*>(QStringLiteral("btnBatchStart"));
        if (!lst || !cmbAct || !btnStart) { std::printf("FAIL 控件缺失 lstBatchFiles/cmbBatchAction/btnBatchStart\n"); std::exit(1); }
        lst->clear();
        for (const QString& p : {wav, wav2}) {
            auto* item = new QListWidgetItem(QFileInfo(p).fileName(), lst);
            item->setData(Qt::UserRole, p);// 与「添加文件」同型落 UserRole 绝对路径
        }
        capBatchSteps.clear();
        capBatchInputs = 0;
        g_batchDone = false;
        cmbAct->setCurrentIndex(1);// AI 降噪单步动作
        btnStart->click();
        waitUntil([&] { return g_batchDone && capBatchInputs > 0; }, 20000);
        std::printf("PATHBATCH inputs=%d steps=%s%s\n", capBatchInputs,
                    capBatchSteps.toUtf8().constData(), tag);
        std::fflush(stdout);
        waitUntil([&] { return btnStart->isEnabled(); }, 5000);// busy 整批放锁
        closeModelDlg();
    };
    runBatch("");

    // --- 切回快速档三面回落（档位动态状态：换档即下次任务生效，非组包时固化） ---
    auto* cmbTierMw = mw.findChild<QComboBox*>(QStringLiteral("cmbAiTier"));
    cmbTierMw->setCurrentIndex(0);
    capQuality = -1;
    g_jobDone = false;
    mw.doDenoiseAiWav();
    waitUntil([&] { return g_jobDone && capQuality >= 0; }, 20000);
    std::printf("PATH7SLOT quality=%d FAST\n", capQuality.load());
    std::printf("MISSINGTEXT fast=%s\n", g_missingText.toUtf8().constData());
    std::fflush(stdout);
    closeModelDlg();
    capPipe.clear();
    g_jobDone = false;
    cmbPipe->setCurrentIndex(0);
    mw.doPipeline();
    waitUntil([&] { return g_jobDone && !capPipe.isEmpty(); }, 20000);
    std::printf("PATHPIPE steps=%s FAST\n", capPipe.toUtf8().constData());
    std::fflush(stdout);
    closeModelDlg();
    runBatch(" FAST");

    std::printf("PROBE DONE\n");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 6 && QByteArray(argv[1]) == "engine") return runEngine(argc, argv);
    if (argc >= 6 && QByteArray(argv[1]) == "gui") return runGui(argc, argv);
    std::fprintf(stderr, "用法: %s engine <wav> <libRoot> <wsRoot> <相位> | gui <wav> <wav2> <wsRoot> <libRoot>\n", argv[0]);
    return 2;
}
