// gui-batch-probe.cpp — FR-10 片 B（GUI widget 面）验收探针（BATCH-GUI 族，spec §6.9）。
// 三模式（编排见同目录 gui-batch.sh）：
//   gui <wav> <wsRoot> —— MainWidget+MediaService 全链主路径（零 QFileDialog 依赖：探针直填
//        QListWidget（UserRole=绝对路径）后 QMetaObject::invokeMethod 调对话框私有槽 doBatchStart，
//        显式清单直接进组包通路），三相位顺序跑：
//          packets —— 8 动作组合（5 引擎单步 + 3 预设链）× 动态参数面板注入面：逐 case 捕获
//                     MediaService::batchRequested(inputsJson, stepsJson) 打印 ACT 行（编排侧深度
//                     比对，gui-slot-eqv 方法论）；提交即锁面（七槽/管线/批处理入口/对话框内键
//                     全锁 + btnCancel 解禁）LOCKED 行、首个进度回调再采样 LOCKMID 行（批运行中
                     // 锁全批覆盖的时序证据）、完成即解 UNLOCKED 行；
//          mixed   —— 清单 [好件, 不存在件] × DSP 降噪：完成文案「批处理完成：1/2 件成功」+
//                     清单逐件渲染（行 0「—— 成功（版本 N）」/行 1「—— 失败：E_FILE：…」）；
//          cancel  —— 单步 pitch 批、进度达 50（件 1 收尾映射值）点 btnCancel → 状态行
//                     「已取消」+ 零弹窗（附裁 5 静默特判）+ 清单行不渲染（保持基名）。
//   miss <wav> <wsRoot> —— appDir 无模型 + 模型库空：动作「AI 降噪」开始 → 开工前解析失败 →
//        onBatchDone 弹警告（错误文案直通）+ onModelMissing 问询（应答器点「是」→ 模型管理
//        对话框打开）；断言两弹窗序 + busy 已释放。
//   add <wsRoot> <文件1> <文件2> —— 多选对话框应答段（尽力，fr9 加固配方扩展到 ExistingFiles
//        多选）：直调 doBatchAdd 出 QFileDialog::getOpenFileNames → 应答器行编辑引号多填
//        "f1" "f2" + AcceptRole 确认键；3s 兜底 reject 防挂死；打印 MULTIADD 行（n=0 即应答
//        不落定 → 编排侧记档降级为主路径，Windows round-4 手工，spec D8 口径）。
// 弹窗自动应答（mdl-gui-probe/pipe-gui-probe 配方）：20ms 轮询 activeModalWidget，信息/警告框
// 点 Ok、Yes/No 问询点 Yes（文本留档 MODALS 行）；offscreen + AA_DontUseNativeDialogs。
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <cstdio>

#include "model/data.h"// kSettings* 键（受控根注入）
#include "service/ffextractor.h"
#include "service/mediaservice.h"
#include "view/mainwidget.h"

namespace {
QStringList g_modals;// 应答过的模态弹窗文本留档（信息/警告/问询序）
QString g_addText;// add 模式：行编辑待填文本
QString g_inputs, g_steps;// 捕获 batchRequested 参数（组包断言锚）
QList<int> g_prog;// 最近一次任务的进度序列
// busy 锁面控件（提交即锁的采样集：管线行 + 批处理入口 + 对话框内键为具名锚；引擎七槽五键与
// 取消键无 objectName——按唯一文案锚定，标签见 mainwidget.cpp 构造段）
const char* kLockNames[] = {"btnPipeline", "cmbPipeline", "btnBatch", "btnBatchAdd",
                            "btnBatchRemove", "btnBatchClear", "cmbBatchAction", "btnBatchStart"};
const char* kLockTexts[] = {"DSP降噪导出WAV", "AI降噪导出WAV", "变调导出WAV",
                            "人声分离导出WAV", "伴奏提取导出WAV"};

// 跑一个含事件循环的等待（超时兜底防挂死；pipe-gui-probe 同型）
void runWithTimeout(int timeoutMs) {
    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}
}// namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);// 多选应答走 Qt 对话框（offscreen 确定性）
    QApplication app(argc, argv);
    if (argc < 4) {
        std::printf("usage: probe gui <wav> <wsRoot> | probe miss <wav> <wsRoot> | probe add <wsRoot> <f1> <f2>\n");
        return 2;
    }
    const QString mode = QString::fromUtf8(argv[1]);

    if (mode == QLatin1String("add")) {// 多选对话框应答段（尽力）
        const QString wsRoot = QString::fromUtf8(argv[2]);
        QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), wsRoot);
        QSettings().setValue(QString::fromUtf8(kSettingsModelRoot), wsRoot + QStringLiteral("/models-lib"));
        MainWidget w;
        MediaService svc;
        w.setMediaService(&svc);
        w.openBatchDialog();
        auto* lst = w.findChild<QListWidget*>(QStringLiteral("lstBatchFiles"));
        if (!lst) { std::printf("FAIL 控件缺失 lstBatchFiles\n"); return 1; }
        g_addText = QStringLiteral("\"%1\" \"%2\"").arg(QString::fromUtf8(argv[3]), QString::fromUtf8(argv[4]));
        QTimer answerer;
        QObject::connect(&answerer, &QTimer::timeout, &w, [&w] {
            if (QWidget* m = QApplication::activeModalWidget()) {
                if (auto* fd = qobject_cast<QFileDialog*>(m)) {
                    if (auto* le = fd->findChild<QLineEdit*>()) le->setText(g_addText);// 行编辑引号多填
                    if (auto* bb = fd->findChild<QDialogButtonBox*>()) {
                        for (auto* b : bb->buttons())
                            if (bb->buttonRole(b) == QDialogButtonBox::AcceptRole) { b->click(); break; }
                    }
                }
            }
        });
        answerer.start(20);
        QTimer::singleShot(3000, &w, [&w] {// 兜底：3s 仍未收尾即 reject（getOpenFileNames 返回空 → 降级）
            if (auto* fd = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
                fd->reject();
        });
        QMetaObject::invokeMethod(&w, "doBatchAdd");// 私有槽按名直调（getOpenFileNames 嵌套事件循环）
        QStringList paths;
        for (int i = 0; i < lst->count(); ++i)
            paths << lst->item(i)->data(Qt::UserRole).toString();
        std::printf("MULTIADD n=%d paths=%s\n", lst->count(),
                    paths.join(QLatin1Char(';')).toUtf8().constData());
        return 0;
    }

    const QString wav = QString::fromUtf8(argv[2]);
    const QString wsRoot = QString::fromUtf8(argv[3]);

    // 受控根注入（pipe-gui-probe gui 相同面）：XDG_CONFIG_HOME 只挡写泄漏——不注入则 MainWidget
    // 构造读空键归一缺省 ~/VideoHandler-Workspace 污染用户家目录；模型库根注空目录 → 两级缺省
    // 解析恒走 appDir 随包（gui/miss 两模式分别放 bin-full/bin-empty 由编排侧备妥）
    QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), wsRoot);
    QSettings().setValue(QString::fromUtf8(kSettingsModelRoot), wsRoot + QStringLiteral("/models-lib"));
    MainWidget w;
    MediaService svc;
    w.setMediaService(&svc);

    QObject::connect(&svc, &MediaService::batchRequested,
                     [](const QString& inputsJson, const QString& stepsJson) {
                         g_inputs = inputsJson;
                         g_steps = stepsJson;
                     });
    QObject::connect(&svc, &MediaService::progress, [](int pct) { g_prog.append(pct); });

    w.openBatchDialog();// 非模态，首次打开创建 batchDlg（buildBatchDialog）
    auto* lst = w.findChild<QListWidget*>(QStringLiteral("lstBatchFiles"));
    auto* cmbAct = w.findChild<QComboBox*>(QStringLiteral("cmbBatchAction"));
    auto* btnStart = w.findChild<QPushButton*>(QStringLiteral("btnBatchStart"));
    // btnCancel 无 objectName：按唯一文案「取消任务」锚定
    QPushButton* btnCancel = nullptr;
    for (auto* b : w.findChildren<QPushButton*>())
        if (b->text() == QStringLiteral("取消任务")) { btnCancel = b; break; }
    QLabel* lblStatus = nullptr;// lblStatus 无 objectName：按初始文案「就绪」锚定一次
    for (auto* l : w.findChildren<QLabel*>())
        if (l->text() == QStringLiteral("就绪")) { lblStatus = l; break; }
    if (!lst || !cmbAct || !btnStart || !btnCancel || !lblStatus) {
        std::printf("FAIL 控件缺失（lstBatchFiles/cmbBatchAction/btnBatchStart/btnCancel/lblStatus）\n");
        return 1;
    }
    // 锁面全集一次性采样（缺一即断言面不成立，防 findChild 落空静默弱化）
    QList<QWidget*> face;
    for (const char* n : kLockNames) face << w.findChild<QWidget*>(QString::fromLatin1(n));
    for (const char* t : kLockTexts) {
        QWidget* hit = nullptr;
        for (auto* b : w.findChildren<QPushButton*>())
            if (b->text() == QString::fromUtf8(t)) { hit = b; break; }
        face << hit;
    }
    if (face.contains(nullptr)) {
        std::printf("FAIL 锁面控件缺失（具名/文案锚定有落空）\n");
        return 1;
    }
    const int comboCount = cmbAct->count();
    std::printf("WIDGETS ok=1 combo=%d\n", comboCount);

    // 弹窗自动应答：信息/警告框点 Ok、问询点 Yes（文本留档）；应答器常开（各相位开始前清档）
    QTimer answerer;
    QObject::connect(&answerer, &QTimer::timeout, &w, [] {
        if (QWidget* m = QApplication::activeModalWidget()) {
            if (auto* box = qobject_cast<QMessageBox*>(m)) {
                g_modals << box->text();
                if (auto* b = box->button(QMessageBox::Ok)) b->click();
                else if (auto* b = box->button(QMessageBox::Yes)) b->click();
            }
        }
    });
    answerer.start(20);

    // 清单直填（零 QFileDialog 依赖）：UserRole = 绝对路径，显示文本 = 基名（doBatchAdd 同型）
    auto fillList = [&lst](const QStringList& absPaths) {
        lst->clear();
        for (const QString& p : absPaths) {
            auto* item = new QListWidgetItem(QFileInfo(p).fileName(), lst);
            item->setData(Qt::UserRole, p);
        }
    };
    // 锁面采样：face 全集（8 具名 + 5 引擎键）全部禁用且 btnCancel 解禁 → 1（busy 面）
    // unlockFace：同一全集全部解禁且 btnCancel 收回 → 1（busy 全批覆盖后的释放面）
    auto lockFace = [&face, btnCancel]() -> int {
        for (QWidget* x : face)
            if (x->isEnabled()) return 0;
        return btnCancel->isEnabled() ? 1 : 0;
    };
    auto unlockFace = [&face, btnCancel]() -> int {
        for (QWidget* x : face)
            if (!x->isEnabled()) return 0;
        return btnCancel->isEnabled() ? 0 : 1;
    };

    const QString copy1 = wav + QStringLiteral(".probe1.wav");// 第二件：字节副本（路径互异过查重）
    QFile::remove(copy1);
    if (!QFile::copy(wav, copy1)) { std::printf("FAIL 副本创建失败\n"); return 1; }
    const QStringList twoItems = {wav, copy1};

    // 等整批落定（batchFinished → onBatchDone 放锁；模型链较慢上限 180s——t 即毫秒，20ms 一旋）
    auto waitUnlock = [btnStart](int maxMs) {
        for (int t = 0; t < maxMs && !btnStart->isEnabled(); t += 20) runWithTimeout(20);
    };
    // 一个完整 case：面板态注入 → 清单直填 → doBatchStart → 锁面即时采样 → 等完成 → 状态/弹窗/行
    auto runCase = [&](const char* tag, int comboIdx, const char* hum, double semi, bool wind,
                       const QStringList& items) {
        auto* chkHum = w.findChild<QCheckBox*>(QStringLiteral("chkHum"));
        auto* cmbHum = w.findChild<QComboBox*>(QStringLiteral("cmbHumBase"));
        auto* chkWind = w.findChild<QCheckBox*>(QStringLiteral("chkWind"));
        auto* spin = w.findChild<QDoubleSpinBox*>(QStringLiteral("spinSemi"));
        const bool humOn = QLatin1String(hum) != QLatin1String("off");
        chkHum->setChecked(humOn);
        if (humOn) cmbHum->setCurrentIndex(QLatin1String(hum) == QLatin1String("60") ? 1 : 0);
        chkWind->setChecked(wind);
        spin->setValue(semi);
        cmbAct->setCurrentIndex(comboIdx);
        g_modals.clear();
        g_prog.clear();
        g_inputs.clear();
        g_steps.clear();
        fillList(items);
        int midSample = -1;// 首个进度回调时刻的锁面采样（批运行中：busy 全批覆盖的时序证据）
        QMetaObject::Connection progConn =
            QObject::connect(&svc, &MediaService::progress, [&midSample, &lockFace](int) {
                if (midSample < 0) midSample = lockFace();
            });
        QMetaObject::invokeMethod(&w, "doBatchStart");// 私有槽按名直调
        std::printf("ACT %s inputs=%s steps=%s\n", tag, g_inputs.toUtf8().constData(),
                    g_steps.toUtf8().constData());
        std::printf("LOCKED %s all=%d\n", tag, lockFace());
        waitUnlock(180000);
        runWithTimeout(400);// 静置等弹窗应答落档（onBatchDone 放锁后才弹信息框，防 MODALS 采样竞态）
        QObject::disconnect(progConn);
        std::printf("UNLOCKED %s all=%d mid=%d\n", tag, unlockFace(), midSample);
        std::printf("STATUS %s %s\n", tag, lblStatus->text().toUtf8().constData());
        std::printf("MODALS %s n=%d %s\n", tag, static_cast<int>(g_modals.size()),
                    g_modals.join(QLatin1Char(';')).toUtf8().constData());
        QStringList rows;
        for (int i = 0; i < lst->count(); ++i) rows << lst->item(i)->text();
        std::printf("ROWS %s %s\n", tag, rows.join(QLatin1Char('|')).toUtf8().constData());
        std::printf("PROG %s %s\n", tag,
                    [p = g_prog] { QStringList s; for (int x : p) s << QString::number(x); return s.join(QLatin1Char(',')); }().toUtf8().constData());
    };

    if (mode == QLatin1String("gui")) {
        // 组包断言面：8 动作组合（idx 0-4 单步 / 5-7 预设）× 动态参数注入（humBase/wind/semi）
        struct Case { const char* tag; int idx; const char* hum; double semi; bool wind; };
        const Case cases[] = {
            {"den60", 0, "60", 0.0, true},   // DSP 降噪单步：humBase=60 + wind
            {"denoff", 0, "off", 0.0, false},// DSP 降噪单步：hum off + wind 独立 false
            {"ai", 1, "-", 0.0, false},      // AI 降噪单步（模型步两级缺省解析在 worker）
            {"pit", 2, "-", 2.5, false},     // 变调单步：semi 取面板
            {"sepv", 3, "-", 0.0, false},    // 人声分离单步：stem=vocals
            {"sepi", 4, "-", 0.0, false},    // 伴奏提取单步：stem=instrumental
            {"pre0", 5, "50", 0.0, true},    // 预设 aiDenoise→separate（动态参数不入步）
            {"pre1", 6, "60", 0.0, true},    // 预设 denoise(60,wind)→separate
            {"pre2", 7, "-", -3.5, false},   // 预设 separate→pitch(-3.5)
        };
        for (const Case& c : cases) runCase(c.tag, c.idx, c.hum, c.semi, c.wind, twoItems);
        // 混合败件：好件 + 不存在件 → 1/2 件成功 + 逐件渲染双态（成功版本号 / 失败码+原文）
        runCase("mixed", 0, "50", 0.0, false,
                QStringList{wav, wsRoot + QStringLiteral("/no-such.wav")});
        // 整批取消：件 1 收尾映射 50 点取消 → 「已取消」静默（零弹窗）+ 清单行不渲染
        bool cancelled = false;
        QMetaObject::Connection canConn = QObject::connect(
            &svc, &MediaService::progress, [&cancelled, btnCancel](int pct) {
                if (!cancelled && pct >= 50) { cancelled = true; btnCancel->click(); }
            });
        runCase("cancel", 2, "-", 1.0, false, twoItems);
        QObject::disconnect(canConn);
        std::printf("CANCELCLICK %d\n", cancelled ? 1 : 0);
        return 0;
    }

    if (mode == QLatin1String("miss")) {// appDir 无模型 + 库空：开工前解析失败（附裁 1 顺序的 widget 面）
        fillList(twoItems);
        cmbAct->setCurrentIndex(1);// AI 降噪（模型步）
        g_modals.clear();
        QMetaObject::invokeMethod(&w, "doBatchStart");
        waitUnlock(30000);// 速断：busy 毫秒级释放（batchFinished 先发）
        for (int i = 0; i < 100 && g_modals.size() < 2; ++i) runWithTimeout(20);// 警告框 + 问询框序列
        runWithTimeout(200);// 点「是」后模型管理对话框打开 + 列表刷新
        auto* modelDlg = w.findChild<QDialog*>(QStringLiteral("modelDlg"));
        std::printf("STATUS miss %s\n", lblStatus->text().toUtf8().constData());
        std::printf("MODALS miss n=%d %s\n", static_cast<int>(g_modals.size()),
                    g_modals.join(QLatin1Char(';')).toUtf8().constData());
        std::printf("MODELDLG visible=%d unlocked=%d\n", modelDlg && modelDlg->isVisible() ? 1 : 0,
                    btnStart->isEnabled() ? 1 : 0);
        return 0;
    }

    std::printf("unknown mode %s\n", mode.toUtf8().constData());
    return 2;
}
