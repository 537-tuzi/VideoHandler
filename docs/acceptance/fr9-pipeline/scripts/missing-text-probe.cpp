// missing-text-probe.cpp — FR-9 round-2 Fix 2 锁定探针（MIST 族）：onModelMissing 的
// forVerb 用途文案映射独立验证。背景：GML-114 只锁问询出现次数（MISSINGQ 1），mdl-gui-probe
// 应答器 contains("模型不可用") 分流——新旧文案都命中，映射退化（如丢 forVerb 恒弹通用文案）
// 现有门禁全绿静默漏检；本探针逐字锁定三态文案 + Yes→模型管理引导链。
// 单模式：gui <wav> <wsRoot>（offscreen QApplication，XDG_CONFIG_HOME 由编排侧隔离）。
//   受控根注入（pipe-gui-probe gui 相相同面）：QSettings 预写 kSettingsWorkspaceRoot=<wsRoot>、
//   kSettingsModelRoot=<wsRoot>/models-lib（空库目录）→ MainWidget 构造期真实读入（D7）。
//   appDir 布局 = 探针 bin 旁不放 models/（两级缺省解析皆缺 → pre-check E_MODEL，
//   mdl-gui-probe ai-missing/sep-missing 实证路径）。
//   T1 管线预设 0（aiDenoise→separate）→ 壳内步 1 预解析缺 → jobDone「找不到AI 降噪模型：…」+
//      modelMissing(aiDenoise)（ffextractor doPipeline 附裁 1 同型路径）→ 问询应逐字
//      「AI 降噪模型不可用，是否打开模型管理下载？」
//   T2 管线预设 2（separate→pitch，semi 预设 2.5——无害腰带：触发序上壳内步 1 separate
//      预解析缺先失败（ffextractor doPipeline 组请求前），semi 值实际不可达）→
//      modelMissing(separate) → 「人声分离模型不可用，是否打开模型管理下载？」
//   T3 信号直发 emit svc.modelMissing("futureVerb")（未登记动词防御退路——当前无引擎动词
//      发未登记 forVerb，无真实触发面）→ 通用文案「模型不可用，是否打开模型管理下载？」
//   问询分流按 icon==Question（唯一 Question 源是 onModelMissing，不依赖被测文案自身匹配）；
//   每相位输出 PHASE/JOB/MISSINGTEXT/DLGOPEN 四行，末相位后关闭对话框复位（保 Yes→开框
//   断言区分力：每相位 Yes 前对话框不可见）。
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>

#include <cstdio>
#include <functional>

#include "model/data.h"// kSettings* 键（受控根注入）
#include "service/mediaservice.h"
#include "view/mainwidget.h"

namespace {
// 轮询等待谓词成真（30ms 节拍 + 超时兜底；事件循环驱动弹窗应答与跨线程信号，mdl-gui-probe 同型）。
// 超时兜底用栈上 deadline QTimer（析构自动从定时器队列摘除，零悬挂）而非静态 singleShot：首轮
// 调试曾见谓词早退后悬挂单发致 qFatal，归因「receiver 形 singleShot 不受析构保护」经主线程
// 复现证伪（receiver 析构断连生效，早退后到期无副作用）——首轮实现细节未入仓不可考；此处保留
// 更稳形态属防御性选择，mdl/pipe 两探针现形态经复现判定安全不改（round-2.md 记档）。
bool waitUntil(const std::function<bool()>& pred, int timeoutMs) {
    QEventLoop loop;
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &loop, [&] { if (pred()) loop.quit(); });
    t.start(30);
    QTimer deadline;// 超时兜底（栈对象，析构自动摘除定时器——不悬挂）
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.setSingleShot(true);
    deadline.start(timeoutMs);
    loop.exec();
    return pred();
}
}// namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);// 文件对话框强制 Qt 控件实现（可确定性应答）
    QApplication app(argc, argv);
    if (argc < 4) {
        std::fprintf(stderr, "用法: %s gui <wav> <wsRoot>\n", argv[0]);
        return 2;
    }
    const QString wav = QString::fromLocal8Bit(argv[2]);
    const QString wsRoot = QString::fromLocal8Bit(argv[3]);

    // 受控根注入：MainWidget 构造前预写两键（GUI 真实读路径），模型库根指向空目录
    QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), wsRoot);
    QSettings().setValue(QString::fromUtf8(kSettingsModelRoot),
                         wsRoot + QStringLiteral("/models-lib"));

    MediaService svc;
    MainWidget w;
    w.setMediaService(&svc);// 内部完成根下传（workspaceRoot/modelRoot → worker）

    // 记录器：jobFinished（真实链路锚——文案证明问询出自管线链中 E_MODEL）
    bool g_jobDone = false, g_jobOk = true;
    QString g_jobMsg;
    QString g_phase;// 当前触发相位标记（应答器按相位记问询文案）
    QMap<QString, QString> g_texts;// 相位 → 问询完整文案（逐字）
    QObject::connect(&svc, &MediaService::jobFinished, [&](bool ok, const QString& m) {
        g_jobDone = true;
        g_jobOk = ok;
        g_jobMsg = m;
    });

    // 弹窗自动应答器（20ms 轮询 activeModalWidget，mdl-gui-probe 配方）：Question 框 =
    // onModelMissing 问询（唯一 Question 源）记录文案后点 Yes；其余消息框点 Ok/默认键；
    // 文件对话框行编辑直填 + AcceptRole 确认（FR-9 测试轮 P3-2 加固配方）
    QTimer answerer;
    answerer.setInterval(20);
    QObject::connect(&answerer, &QTimer::timeout, &w, [&] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) return;
        if (auto* box = qobject_cast<QMessageBox*>(m)) {
            if (box->icon() == QMessageBox::Question) {// onModelMissing 问询（不依赖被测文案分流）
                g_texts[g_phase] = box->text();
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

    auto* cmb = w.findChild<QComboBox*>(QStringLiteral("cmbPipeline"));
    auto* btn = w.findChild<QPushButton*>(QStringLiteral("btnPipeline"));
    auto* spin = w.findChild<QDoubleSpinBox*>(QStringLiteral("spinSemi"));
    if (!cmb || !btn || !spin) {
        std::printf("FAIL 控件缺失（cmbPipeline/btnPipeline/spinSemi）\n");
        return 1;
    }
    auto dlg = [&] { return w.findChild<QDialog*>(QStringLiteral("modelDlg")); };
    auto rows = [&] {
        auto* t = w.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
        return t ? t->topLevelItemCount() : 0;
    };

    // 载入 5s 素材（doOpen 弹文件对话框，应答器直填+确认 → openFile 异步探测；轮询等解禁）
    w.doOpen();
    waitUntil([&] { return btn->isEnabled(); }, 15000);
    std::printf("LOADED btnPipeline=%d\n", btn->isEnabled() ? 1 : 0);
    std::fflush(stdout);

    // T1/T2 收尾（真实链路相位）。竞态教训（首轮调试实锤）：管线预解析缺是毫秒级失败，
    // jobFinished/modelMissing/问询应答全落在等待循环的事件派发里——必须**先设相位标记与
    // 复位旗标、再触发、最后等**；若在触发后才设标记，事件到达时 g_phase 仍是旧值，问询
    // 记到错键、jobMsg 被事后 clear 抹掉，谓词恒 false 概率性超时。
    auto runTrig = [&](const char* phase, const std::function<void()>& trigger) {
        waitUntil([&] { return btn->isEnabled(); }, 5000);// 空闲态确认（上一相位放锁后）
        g_phase = QString::fromLatin1(phase);// 先设标记（应答器记录键）
        g_jobDone = false;
        g_jobMsg.clear();
        trigger();// 再触发（busy 置位；后续事件在下方循环里派发）
        waitUntil([&] { return g_jobDone && g_texts.contains(g_phase); }, 30000);
        waitUntil([&] { return rows() > 0; }, 8000);// Yes→openModelDialog→modelList 回传渲染
        std::printf("PHASE %s\n", phase);
        std::printf("JOB %s ok=%d msg=%s\n", phase, g_jobOk ? 1 : 0, g_jobMsg.toUtf8().constData());
        std::printf("MISSINGTEXT %s=%s\n", phase,
                    g_texts.value(QString::fromLatin1(phase), QStringLiteral("<未捕获>"))
                        .toUtf8().constData());
        std::printf("DLGOPEN %s=%d\n", phase, dlg() && dlg()->isVisible() ? 1 : 0);
        std::fflush(stdout);
        if (dlg()) dlg()->close();// 复位：下一相位 Yes 前不可见，保 DLGOPEN 区分力
    };

    // T1 管线预设 0（aiDenoise→separate）：appDir 无模型 + 空库 → 壳内步 1 预解析缺（附裁 1 路径）
    runTrig("ai", [&] {
        cmb->setCurrentIndex(0);
        w.doPipeline();
    });

    // T2 管线预设 2（separate→pitch）：semi 先设 2.5（无害腰带——触发序上步 1 预解析缺先失败）
    runTrig("sep", [&] {
        spin->setValue(2.5);
        cmb->setCurrentIndex(2);
        w.doPipeline();
    });

    // T3 未登记动词直发（防御退路；emit 同线程直连 → onModelMissing 同步弹问询 → 应答器记录+Yes）
    g_phase = QStringLiteral("future");
    emit svc.modelMissing(QStringLiteral("futureVerb"));
    waitUntil([&] { return g_texts.contains(QStringLiteral("future")); }, 8000);
    std::printf("PHASE future\n");
    std::printf("MISSINGTEXT future=%s\n",
                g_texts.value(QStringLiteral("future"), QStringLiteral("<未捕获>"))
                    .toUtf8().constData());
    std::printf("DLGOPEN future=%d\n", dlg() && dlg()->isVisible() ? 1 : 0);
    std::fflush(stdout);

    std::printf("PROBE DONE\n");
    return 0;
}
