// mdl-gui-probe.cpp — FR-8 片 B（GUI 面）验收探针（GML 族）。
// 两模式（编排见同目录 run-models.sh GML 段）：
//   engine <wav> <libRoot> <wsRoot> <相位> —— FFExtractor 槽直调（gui-ws-probe 同型）验证两级
//        缺省解析（D1）与 modelMissing 触发面（D8）：
//          ai-missing     appDir 无模型 + 库无已装 → 锁定文案「找不到 AI 降噪模型：%1」+ modelMissing
//          ai-libonly     appDir 无模型 + 库已装 gtcrn → 任务 ok 且版本记录 model.path = 库路径
//          ai-libbundled  appDir 有 gtcrn + 库已装 gtcrn → 取库（库优先于随包，优先级实证）
//          ai-importshadow appDir 有 gtcrn + 库已自由导入 my-gtcrn → 取库内导入条目（跨 id：
//                         bundled 视角在位不遮蔽用户导入，审查轮 P1-1 回归锚）
//          ai-bundledonly appDir 有 gtcrn + 库空 → 取随包
//          ai-noroot      appDir 有 gtcrn、根未注入（成员空串）→ 取随包（不查库 = 旧行为零漂移，
//                         此时库根参数在盘上确有 gtcrn，若误查库则会取到库路径）
//          sep-lib        appDir 有 uvr + 库已装 uvr → 取库（separator 槽同型覆盖）
//          sep-missing    appDir 无模型 + 库空 → 「找不到人声分离模型：%1」+ modelMissing
//          ai-badinput    appDir 有 gtcrn + 输入文件不存在 → E_FILE 任务失败且不触发
//                         modelMissing（负例守卫：结构化信号仅随 E_MODEL）
//        选用模型真值的 ground truth = 工作区版本记录的 model.path（workspace.cpp 登记），
//        非任务文案（文案只含产物路径）。
//   gui <wav> <endpoint> <modelRoot> <wsRoot> <importSrc> —— MainWidget+MediaService 全链
//        （offscreen QApplication；QSettings 经编排侧 XDG_CONFIG_HOME 隔离，模型库根/工作区根
//        由本探针先写 QSettings 再构造主窗 = 走 GUI 真实读路径 D1/D7）：
//        对话框开/关与列表渲染（状态中文映射）、缺省 endpoint（D4）、下载占 busy 可取消、
//        列表查询不占 busy、E_MODEL→modelMissing→问询→打开对话框引导链（appDir 降噪模型为损坏
//        垃圾字节：pre-check 因文件在盘而放行，E_MODEL 必出自 runRequest 引擎 setup 失败）、
//        导入全链（QFileDialog 自动选文件 + 用途对话框捕获 §161 许可提示原文 + 提交）、
//        endpoint/库根 QSettings 记忆写路径。
//        弹窗自动应答：20ms 轮询 activeModalWidget——QMessageBox 按文案分流（「模型不可用」→Yes，
//        其余→Ok），QFileDialog（文件模式行编辑直填+AcceptRole / 目录模式 setDirectory+accept），
//        其余 QDialog 视为用途对话框：记录许可提示后点「导入」。
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>

#include <atomic>
#include <cstdio>
#include <functional>

#include "model/data.h"
#include "service/dspdenoiser.h"// HumBase 制式枚举
#include "service/ffextractor.h"
#include "service/mediaservice.h"
#include "view/mainwidget.h"

namespace {
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

bool hasPart(const QString& root) {// 库树内是否存在 .part（下载中/取消保留）
    QDirIterator it(root, {"*.part"}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

// 轮询等待谓词成真（30ms 节拍 + 超时；事件循环驱动：跨线程队列信号/弹窗自动应答都靠它转）
bool waitUntil(const std::function<bool()>& pred, int timeoutMs) {
    QEventLoop loop;
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &loop, [&] { if (pred()) loop.quit(); });
    t.start(30);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return pred();
}
}// namespace

// ---------------- engine 相（FFExtractor 槽直调） ----------------

static int runEngine(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);// 无控件；QNAM 不用（库 fixture 由编排侧 CLI 预装）
    const QString wav = QString::fromLocal8Bit(argv[2]);
    const QString libRoot = QString::fromLocal8Bit(argv[3]);
    const QString wsRoot = QString::fromLocal8Bit(argv[4]);
    const QString phase = QString::fromLocal8Bit(argv[5]);

    FFExtractor ex;
    int missing = 0;
    QString msg;
    bool ok = false;
    QObject::connect(&ex, &FFExtractor::jobDone, [&](bool o, const QString& m) { ok = o; msg = m; });
    QObject::connect(&ex, &FFExtractor::modelMissing, [&] { ++missing; });

    ex.setWorkspaceRoot(wsRoot);// 版本记录（model.path 真值）落这里
    if (phase != QStringLiteral("ai-noroot")) ex.setModelRoot(libRoot);// noroot：根在盘有 gtcrn 但不注入
    if (phase.startsWith(QStringLiteral("sep-"))) ex.doSepVocals(wav, false);
    else ex.doDenoiseAiWav(wav);

    const QByteArray name = phase.toUtf8();
    std::printf("APPDIR %s\n", QCoreApplication::applicationDirPath().toUtf8().constData());
    std::printf("TASK %s -> ok=%d msg=%s\n", name.constData(), ok ? 1 : 0, msg.toUtf8().constData());
    std::printf("MODELMISSING %d\n", missing);
    std::printf("MODELPATH %s\n", firstModelPath(wsRoot).toUtf8().constData());
    return 0;
}

// ---------------- gui 相（MainWidget + MediaService 全链） ----------------

static int runGui(int argc, char* argv[]) {
    QApplication app(argc, argv);// offscreen（编排侧 QT_QPA_PLATFORM=offscreen；AA_DontUseNativeDialogs
    // 已在 main 构造前置位：文件/目录对话框强制 Qt 控件实现，行编辑直填+确认键才可确定性自动应答）
    const QString wav = QString::fromLocal8Bit(argv[2]);
    const QString endpoint = QString::fromLocal8Bit(argv[3]);
    const QString modelRoot = QString::fromLocal8Bit(argv[4]);
    const QString wsRoot = QString::fromLocal8Bit(argv[5]);
    const QString importSrc = QString::fromLocal8Bit(argv[6]);

    // QSettings 预置（GUI 真实读路径：MainWidget 构造期读这两键）；XDG_CONFIG_HOME 隔离由编排侧
    QSettings().setValue(QString::fromUtf8(kSettingsModelRoot), modelRoot);
    QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), wsRoot);

    // E_MODEL 注入源：appDir 降噪模型 = 损坏垃圾字节（文件在盘 → 两级解析 pre-check 放行 →
    // 引擎 setup 失败 E_MODEL 出自 runRequest，正中 D8「任务响应 E_MODEL」触发面；AiDenoiser
    // setup 异常屏障 → E_MODEL 为 fr24 片已实证路径，不赌 vocalseparator 的抛出面）
    QDir(QCoreApplication::applicationDirPath()).mkpath(QStringLiteral("models/denoiser"));
    QFile bad(QCoreApplication::applicationDirPath() + "/" + QString::fromUtf8(kAiModelRelPath));
    if (bad.open(QIODevice::WriteOnly)) bad.write("garbage-not-an-onnx-model");

    // 弹窗自动应答机（20ms 轮询；三类：消息框/文件对话框/用途对话框）
    QString g_notice, g_importPath = importSrc, g_pickDir = modelRoot + QStringLiteral("-changed");
    bool g_sawMissingQ = false, g_missingYes = false;
    QStringList g_infos;
    QTimer answerer;
    answerer.setInterval(20);
    QObject::connect(&answerer, &QTimer::timeout, [&] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) return;
        if (auto* box = qobject_cast<QMessageBox*>(m)) {
            if (box->text().contains(QStringLiteral("模型不可用"))) {// D8 问询：答 Yes 走引导
                g_sawMissingQ = true;
                if (auto* b = box->button(QMessageBox::Yes)) { g_missingYes = true; b->click(); }
                return;
            }
            g_infos << box->text();
            if (auto* b = box->button(QMessageBox::Ok)) b->click();
            else if (auto* b2 = box->defaultButton()) b2->click();
        } else if (auto* fd = qobject_cast<QFileDialog*>(m)) {// 静态文件/目录对话框自动完成
            if (fd->fileMode() == QFileDialog::Directory) fd->setDirectory(g_pickDir);
            else {
                // 行编辑直填 + AcceptRole 确认（FR-9 测试轮 P3-2 同型加固：selectFile 的延迟选
                // 在挂载 iso9660 卷的 offscreen 环境不落，Qt 6.10.2 文件模型停滞 → 空选不收尾）
                if (auto* le = fd->findChild<QLineEdit*>()) le->setText(g_importPath);// 直写文件名框
            }
            if (auto* box = fd->findChild<QDialogButtonBox*>()) {// 走对话框自己的确认键（用户路径）
                for (auto* b : box->buttons())
                    if (box->buttonRole(b) == QDialogButtonBox::AcceptRole) { b->click(); break; }
            }
            static_cast<QDialog*>(fd)->accept();// 经基类公有槽入口（虚派发到覆写的校验收尾）
        } else if (auto* okBtn = m->findChild<QPushButton*>(QStringLiteral("modelImportOk"))) {
            if (auto* n = m->findChild<QLabel*>(QStringLiteral("modelImportNotice"))) g_notice = n->text();
            okBtn->click();
        }
    });
    answerer.start();

    MediaService svc;
    MainWidget w;
    w.setMediaService(&svc);// 内部完成根下传（modelRoot/workspaceRoot → worker）

    // 记录器：jobFinished / modelOpFinished / modelListFinished（探针侧旁听，驱动断言旗标）
    bool g_jobOk = true; QString g_jobMsg;
    bool g_opDone = false, g_opOk = false; QString g_opMsg;
    bool g_downloadActive = false, g_listDuringBusy = false;
    QObject::connect(&svc, &MediaService::jobFinished, [&](bool o, const QString& m) { g_jobOk = o; g_jobMsg = m; });
    QObject::connect(&svc, &MediaService::modelOpFinished, [&](bool o, const QString& m) {
        g_opDone = true; g_opOk = o; g_opMsg = m; g_downloadActive = false;
    });
    QObject::connect(&svc, &MediaService::modelListFinished, [&](bool, const QString&) {
        if (g_downloadActive && w.findChild<QPushButton*>(QStringLiteral("btnModelDownload"))
                && !w.findChild<QPushButton*>(QStringLiteral("btnModelDownload"))->isEnabled())
            g_listDuringBusy = true;// 下载进行中（busy 锁面）收到列表回传 = 查询未被 busy 拦
    });
    int g_progMax = -1;// 导入进度真值记录器（P5 前复位；收尾 100 = D5 进度面兑现，审查轮 P2-3）
    QObject::connect(&svc, &MediaService::progress, [&](int p) { if (p > g_progMax) g_progMax = p; });

    auto dlg = [&] { return w.findChild<QDialog*>(QStringLiteral("modelDlg")); };
    auto tree = [&] { return w.findChild<QTreeWidget*>(QStringLiteral("modelTree")); };
    auto btnDl = [&] { return w.findChild<QPushButton*>(QStringLiteral("btnModelDownload")); };
    auto btnCa = [&] { return w.findChild<QPushButton*>(QStringLiteral("btnModelCancel")); };
    auto btnIm = [&] { return w.findChild<QPushButton*>(QStringLiteral("btnModelImport")); };
    auto btnRc = [&] { return w.findChild<QPushButton*>(QStringLiteral("btnModelRootChange")); };
    auto ep = [&] { return w.findChild<QComboBox*>(QStringLiteral("cmbModelEndpoint")); };
    auto rows = [&] { return tree() ? tree()->topLevelItemCount() : 0; };
    auto selectRow = [&](const QString& id) {
        for (int i = 0; i < rows(); ++i)
            if (tree()->topLevelItem(i)->text(0) == id) { tree()->setCurrentItem(tree()->topLevelItem(i)); return true; }
        return false;
    };
    auto statusText = [&](const QString& id) {
        for (int i = 0; i < rows(); ++i)
            if (tree()->topLevelItem(i)->text(0) == id) return tree()->topLevelItem(i)->text(2);
        return QString();
    };
    auto flush = [] { std::fflush(stdout); };

    // P1 打开：列表渲染（modelList→onModelListDone，状态中文映射）+ 缺省 endpoint + 库根标签（读路径）
    w.openModelDialog();
    waitUntil([&] { return rows() > 0; }, 5000);
    std::printf("EPDEFAULT %s\n", ep()->currentText().toUtf8().constData());
    auto* mn = w.findChild<QLabel*>(QStringLiteral("lblMirrorNote"));// D4 镜像适用性注记（审查 P1-2）
    std::printf("MIRRORNOTE %s\n", mn ? mn->text().toUtf8().constData() : "-");
    std::printf("ROOTLBL %s\n", w.findChild<QLabel*>(QStringLiteral("lblModelRoot"))->text().toUtf8().constData());
    std::printf("ROW0 %s %s\n", rows() > 0 ? tree()->topLevelItem(0)->text(0).toUtf8().constData() : "-",
                statusText(QStringLiteral("gtcrn")).toUtf8().constData());
    dlg()->close();
    std::printf("DLGCLOSE %d\n", dlg() && !dlg()->isVisible() ? 1 : 0);
    flush();

    // P2 下载占 busy（慢速服务节流拉长传输窗）+ 列表查询不占 busy + 取消
    w.openModelDialog();
    waitUntil([&] { return rows() > 0; }, 5000);
    selectRow(QStringLiteral("uvr-mdx-voc-ft"));
    ep()->lineEdit()->setText(endpoint);// 手填自定义源（editable 组合框真实入口）
    emit ep()->lineEdit()->editingFinished();
    g_opDone = false; g_downloadActive = true;
    btnDl()->click();
    const bool partSeen = waitUntil([&] { return hasPart(modelRoot); }, 8000);
    const bool busyLocked = partSeen && !btnDl()->isEnabled() && btnCa()->isEnabled()
                            && w.findChild<QProgressBar*>(QStringLiteral("modelBar"))->isVisible();
    std::printf("DLBUSY %d\n", busyLocked ? 1 : 0);
    svc.modelList();// busy 中发起只读查询（D8：modelList 不占 busy）
    waitUntil([&] { return g_listDuringBusy; }, 8000);
    std::printf("LISTDURINGBUSY %d\n", g_listDuringBusy ? 1 : 0);
    btnCa()->click();// 对话框内取消（复用服务取消通道）
    waitUntil([&] { return g_opDone; }, 10000);
    std::printf("CANCELMSG %s\n", g_opMsg.toUtf8().constData());
    std::printf("CANCELPART %d\n", hasPart(modelRoot) ? 1 : 0);// OQ-6：取消保留 .part 续传缓存
    std::printf("UNBUSY %d\n", btnDl()->isEnabled() && !btnCa()->isEnabled() ? 1 : 0);
    flush();

    // P3 E_MODEL→引导链：损坏随包降噪模型 + 库无已装 gtcrn → runRequest E_MODEL → jobDone 文案
    //    + modelMissing → onJobDone 警告框（自动 Ok）→ onModelMissing 问询（自动 Yes）→ 打开对话框
    g_jobOk = true; g_jobMsg.clear();
    svc.denoiseAiWav(wav);
    waitUntil([&] { return !g_jobOk && g_sawMissingQ; }, 20000);
    std::printf("EQMSG %s\n", g_jobMsg.toUtf8().constData());
    std::printf("MISSINGQ %d\n", g_missingYes ? 1 : 0);
    waitUntil([&] { return rows() > 0; }, 5000);// 问询 Yes 后对话框已开且列表已刷
    std::printf("DLGOPEN %d\n", dlg()->isVisible() ? 1 : 0);
    flush();

    // P4 下载完成闭环：续传 .part → 落位 → ok 文案 + 列表翻转「已安装」
    g_opDone = false; g_downloadActive = true;
    selectRow(QStringLiteral("uvr-mdx-voc-ft"));
    btnDl()->click();
    waitUntil([&] { return g_opDone && g_opOk; }, 30000);
    std::printf("DLOKMSG %s\n", g_opMsg.toUtf8().constData());
    std::printf("DLOK %d\n", g_opOk && statusText(QStringLiteral("uvr-mdx-voc-ft")) == QStringLiteral("已安装")
                              && !hasPart(modelRoot) ? 1 : 0);
    // 测试轮 D1 回归：完成刷新（onModelOpDone→refreshModels→onModelListDone）后选中须按 id 恢复，
    // 不得恒回首行（修复前选中跳 gtcrn——再点下载即静默改目标，且错击直连条目会真外联）
    waitUntil([&] { return tree()->currentItem()
                        && tree()->currentItem()->text(0) == QStringLiteral("uvr-mdx-voc-ft"); }, 5000);
    std::printf("SELRESTORED %s\n",
                tree()->currentItem() ? tree()->currentItem()->text(0).toUtf8().constData() : "-");
    flush();

    // P5 导入全链：按钮 → QFileDialog（自动选文件）→ 用途对话框（捕获 §161 许可提示原文 + 选用途）
    //    → 提交 → modelOpDone ok → 列表新增条目「已安装」
    waitUntil([&] { return rows() > 0; }, 5000);
    g_opDone = false;
    g_progMax = -1;// 复位：只统计导入段（前序下载段进度不算数）
    btnIm()->click();
    waitUntil([&] { return g_opDone; }, 15000);
    std::printf("NOTICE %s\n", g_notice.toUtf8().constData());
    std::printf("IMPORTOK %d\n", g_opOk ? 1 : 0);
    waitUntil([&] { return statusText(QStringLiteral("my-gtcrn")) == QStringLiteral("已安装"); }, 8000);
    std::printf("IMPORTROW %d\n",
                statusText(QStringLiteral("my-gtcrn")) == QStringLiteral("已安装") ? 1 : 0);
    std::printf("IMPORTPROG %d\n", g_progMax);// 期望 100（复制段 0–99 + 登记 succeeded 收尾 100）
    flush();

    // P6 endpoint QSettings 记忆（写路径：下拉换选预设镜像 → activated 持久化；setCurrentText 只改
    // 编辑框文本不发选中信号，可编程模拟用户下拉选择的是 setCurrentIndex）
    ep()->setCurrentIndex(1);// 预设第 2 项 = https://hf-mirror.com
    std::printf("EPSAVED %s\n", QSettings().value(QString::fromUtf8(kSettingsModelEndpoint)).toString().toUtf8().constData());
    flush();

    // P7 库根换根全链（doChangeModelRoot：目录对话框自动选 → QSettings 写 + 标签/下传 + 列表刷新）
    QDir().mkpath(g_pickDir);
    btnRc()->click();
    waitUntil([&] { return w.findChild<QLabel*>(QStringLiteral("lblModelRoot"))->text().endsWith(g_pickDir); }, 5000);
    std::printf("ROOTCHANGED %d\n",
                w.findChild<QLabel*>(QStringLiteral("lblModelRoot"))->text().endsWith(g_pickDir) ? 1 : 0);
    std::printf("ROOTSAVED %s\n", QSettings().value(QString::fromUtf8(kSettingsModelRoot)).toString().toUtf8().constData());
    flush();
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 6 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("engine")) return runEngine(argc, argv);
    if (argc >= 7 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("gui")) {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);// 须在 QApplication 构造前置位
        return runGui(argc, argv);
    }
    std::fprintf(stderr, "用法: %s engine <wav> <libRoot> <wsRoot> <相位> | "
                         "gui <wav> <endpoint> <modelRoot> <wsRoot> <importSrc>\n", argv[0]);
    return 2;
}
