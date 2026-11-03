// pipe-gui-probe.cpp — FR-9 片 B（GUI 面）验收探针（PIPE-GUI 族）。
// 两模式（编排见同目录 gui-pipe.sh）：
//   engine <wav> <wsRoot> <相位> —— FFExtractor 槽直调（gui-ws-probe/mdl-gui-probe 同型，
//        QCoreApplication 无控件）验证 doPipeline 壳行为：
//          ok-chain        appDir 双模型在位 → jobDone ok 且文案「管线完成：<终产物路径>」（附裁 5）、
//                          产物在盘、进度序列终值 100、workspace.json 两版本带 pipeline 标记
//          missing-step1   appDir 无模型、链首 aiDenoise → 锁定文案「找不到AI 降噪模型：%1」+
//                          modelMissing(forVerb=aiDenoise)（壳内两级解析皆缺，七槽同型）
//          emid-chain      appDir 降噪模型为垃圾字节、链 [transcode, aiDenoise] → 步 2 引擎 setup
//                          E_MODEL → jobDone false + modelMissing(forVerb=aiDenoise)——附裁 1 实证：
//                          响应 cmd=pipeline 而 forVerb 取 error.stepCmd（照搬请求动词会得到
//                          "pipeline"，引导错用途模型）
//          emid-negative   同链但步 2 为 E_FILE（输入不存在）→ 失败但不触发 modelMissing
//                          （结构化信号仅随 E_MODEL 的负例守卫）
//          cancel-between  进度达 50（两步链第 1 步收尾映射值）置取消 → jobDone message 逐字
//                          「已取消」+ v001 保留（GUI 静默特判的文案契约）
//   gui <wav> <wsRoot> —— MainWidget+MediaService 全链（offscreen QApplication；QSettings 经
//        XDG_CONFIG_HOME 隔离）：
//        三条预设各自组包断言（OQ-3(a)）：下拉选中 → 面板态（hum 勾选/制式/wind/semi）注入 →
//        doPipeline 捕获 pipelineRequested(inPath, stepsJson) 与期望 JSON 深度比对（探针直调
//        比对请求 JSON，gui-slot-eqv 方法论）；busy 锁覆盖新按钮（提交即锁/完成即解）；
//        成功文案「管线完成：…」；弹窗自动应答 20ms 轮询 activeModalWidget（mdl-gui-probe 配方）。
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <cstdio>

#include "model/data.h"// kSettings* 键（gui 相受控根注入）
#include "service/dspdenoiser.h"// HumBase 制式枚举
#include "service/ffextractor.h"
#include "service/mediaservice.h"
#include "view/mainwidget.h"

namespace {
QString g_msg;// 最近一次 jobDone 文案
QList<int> g_prog;// 最近一次任务的进度序列
bool g_ok = false;
int g_missing = 0;// modelMissing 触发计数
QString g_missingFor;// 最近一次 modelMissing 的 forVerb（附裁 1 断言锚）
QString g_pipeIn, g_pipeSteps;// gui 相捕获的 pipelineRequested 参数

// 跑一个含事件循环的等待（超时兜底防挂死；mdl-gui-probe 同型）
void runWithTimeout(int timeoutMs) {
    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}
}// namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);// gui 相需控件；engine 相 QCoreApplication 亦可用 QApplication 顶着
    if (argc < 4) {
        std::printf("usage: probe engine <wav> <wsRoot> <相位> | probe gui <wav> <wsRoot>\n");
        return 2;
    }
    const QString mode = QString::fromUtf8(argv[1]);
    const QString wav = QString::fromUtf8(argv[2]);
    const QString wsRoot = QString::fromUtf8(argv[3]);

    if (mode == QLatin1String("engine")) {
        const QString phase = QString::fromUtf8(argv[4]);
        FFExtractor ex;
        QObject::connect(&ex, &FFExtractor::jobDone, [](bool ok, const QString& message) {
            g_ok = ok;
            g_msg = message;
        });
        QObject::connect(&ex, &FFExtractor::progress, [](int pct) { g_prog.append(pct); });
        QObject::connect(&ex, &FFExtractor::modelMissing, [](const QString& forVerb) {
            ++g_missing;
            g_missingFor = forVerb;
        });
        ex.setWorkspaceRoot(wsRoot);

        // 链形与预设同构（transcode→pitch 免模型正例；模型负例链按相位切换）；pitch 步带 semi=2
        auto stepsJson = [](const QString& a, const QString& b) {
            QJsonArray steps;
            for (const QString& cmd : {a, b}) {
                QJsonObject s;
                s.insert(QStringLiteral("cmd"), cmd);
                if (cmd == QLatin1String("pitch")) s.insert(QStringLiteral("semi"), 2.0);
                steps.append(s);
            }
            return QString::fromUtf8(QJsonDocument(steps).toJson(QJsonDocument::Compact));
        };
        g_msg.clear();
        g_prog.clear();
        if (phase == QLatin1String("ok-chain")) {
            ex.doPipeline(wav, stepsJson(QStringLiteral("transcode"), QStringLiteral("pitch")));
        } else if (phase == QLatin1String("missing-step1")) {
            ex.doPipeline(wav, stepsJson(QStringLiteral("aiDenoise"), QStringLiteral("separate")));
        } else if (phase == QLatin1String("emid-chain")) {
            // appDir 降噪模型为垃圾字节（编排侧落位）：壳 pre-check 因文件在盘放行，步 2 引擎
            // setup E_MODEL → 附裁 1：forVerb 取 error.stepCmd=aiDenoise（非请求 cmd=pipeline）
            ex.doPipeline(wav, stepsJson(QStringLiteral("transcode"), QStringLiteral("aiDenoise")));
        } else if (phase == QLatin1String("emid-negative")) {
            // 负例守卫：链级预校验只校参数，输入存在性在子请求执行期——步 1 transcode 对不存在
            // 输入 → E_FILE（带步骤归属前缀），不触发 modelMissing（结构化信号仅随 E_MODEL）
            ex.doPipeline(wav + QStringLiteral("/no-such.wav"),
                          stepsJson(QStringLiteral("transcode"), QStringLiteral("aiDenoise")));
        } else if (phase == QLatin1String("cancel-between")) {
            std::atomic<bool> flag{false};
            ex.setCancelFlag(&flag);
            QObject::connect(&ex, &FFExtractor::progress, [&flag](int pct) {
                if (pct >= 50) flag = true;// 第 1 步收尾 100 的映射值 → 步 2 开工前取消
            });
            ex.doPipeline(wav, stepsJson(QStringLiteral("transcode"), QStringLiteral("pitch")));
        } else {
            std::printf("unknown phase %s\n", phase.toUtf8().constData());
            return 2;
        }
        std::printf("TASK %s -> ok=%d msg=%s\n", phase.toUtf8().constData(), g_ok ? 1 : 0,
                    g_msg.toUtf8().constData());
        QStringList ps;
        for (int p : g_prog) ps << QString::number(p);
        std::printf("PROG %s : %s\n", phase.toUtf8().constData(), ps.join(QLatin1Char(',')).toUtf8().constData());
        std::printf("MISSING %d for=%s\n", g_missing, g_missingFor.toUtf8().constData());
        return 0;
    }

    if (mode == QLatin1String("gui")) {
        // 受控根注入（测试轮 P3-1）：XDG_CONFIG_HOME 隔离只挡写泄漏——不注入则 MainWidget 构造
        // 读到空键归一缺省 ~/VideoHandler-Workspace，任务产物落用户家目录（round-1 实况污染）；
        // 模型库根同理注入空库目录，两级缺省解析恒走 appDir 随包（gui-pipe.sh 已备 bin/models）
        QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), wsRoot);
        QSettings().setValue(QString::fromUtf8(kSettingsModelRoot),
                             wsRoot + QStringLiteral("/models-lib"));
        MainWidget w;
        MediaService svc;
        w.setMediaService(&svc);
        QObject::connect(&svc, &MediaService::pipelineRequested,
                         [](const QString& inPath, const QString& stepsJson) {
                             g_pipeIn = inPath;
                             g_pipeSteps = stepsJson;
                         });
        QObject::connect(&svc, &MediaService::jobFinished, [](bool ok, const QString& message) {
            g_ok = ok;
            g_msg = message;
        });
        // 弹窗自动应答（mdl-gui-probe 配方）：信息/警告框点 Ok、缺模型问询（Yes/No）点 Yes、
        // 文件选择框行编辑直填 + AcceptRole 确认键——offscreen 下 selectFile 的延迟选在挂载
        // iso9660 卷的环境不落（Qt 6.10.2 文件模型停滞 → accept 空选不收尾），测试轮 P3-2 实证
        QTimer answerer;
        QObject::connect(&answerer, &QTimer::timeout, &w, [&wav] {
            if (QWidget* m = QApplication::activeModalWidget()) {
                if (auto* box = qobject_cast<QMessageBox*>(m)) {
                    if (auto* b = box->button(QMessageBox::Ok)) b->click();
                    else if (auto* b = box->button(QMessageBox::Yes)) b->click();
                } else if (auto* fd = qobject_cast<QFileDialog*>(m)) {
                    if (auto* le = fd->findChild<QLineEdit*>()) le->setText(wav);// 行编辑直填
                    if (auto* bb = fd->findChild<QDialogButtonBox*>()) {// 走对话框自己的确认键（用户路径）
                        for (auto* b : bb->buttons())
                            if (bb->buttonRole(b) == QDialogButtonBox::AcceptRole) { b->click(); break; }
                    }
                    static_cast<QDialog*>(fd)->accept();// 兜底经基类公有槽入口（虚派发到覆写的校验收尾）
                }
            }
        });
        answerer.start(20);

        auto* cmb = w.findChild<QComboBox*>(QStringLiteral("cmbPipeline"));
        auto* btn = w.findChild<QPushButton*>(QStringLiteral("btnPipeline"));
        auto* cmbHum = w.findChild<QComboBox*>(QStringLiteral("cmbHumBase"));
        auto* chkHum = w.findChild<QCheckBox*>(QStringLiteral("chkHum"));
        auto* chkWind = w.findChild<QCheckBox*>(QStringLiteral("chkWind"));
        auto* spin = w.findChild<QDoubleSpinBox*>(QStringLiteral("spinSemi"));
        if (!cmb || !btn || !cmbHum || !chkHum || !chkWind || !spin) {
            std::printf("FAIL 控件缺失（cmbPipeline/btnPipeline/cmbHumBase/chkHum/chkWind/spinSemi）\n");
            return 1;
        }
        // 载入文件（doOpen 出选文件对话框，应答器选 wav 后 accept → openFile 异步探测；轮询等解禁）
        w.doOpen();
        for (int i = 0; i < 100 && !btn->isEnabled(); ++i) runWithTimeout(20);
        std::printf("LOADED btnPipeline=%d\n", btn->isEnabled() ? 1 : 0);

        // 三条预设组包断言（OQ-3(a)）：面板态注入 → doPipeline → 捕获 stepsJson 深度比对
        struct Case { int idx; const char* hum; double semi; bool wind; };
        const Case cases[] = {
            {0, "60", 0.0, true},   // ai→sep：预设不带动态参数，面板态不影响 steps
            {1, "60", 0.0, true},   // denoise(60)→sep：humBase 取制式下拉、wind 取勾选
            {1, "off", 0.0, true},  // denoise(off)→sep：hum 未勾选 → humBase=off（wind 独立仍 true）
            {2, "50", 2.5, true},   // sep→pitch(2.5)：semi 取变调框
        };
        for (const Case& c : cases) {
            cmb->setCurrentIndex(c.idx);
            const bool humOn = QString(QLatin1String(c.hum)) != QLatin1String("off");
            chkHum->setChecked(humOn);
            if (humOn)
                cmbHum->setCurrentIndex(QString(QLatin1String(c.hum)) == QLatin1String("60") ? 1 : 0);
            chkWind->setChecked(c.wind);
            spin->setValue(c.semi);
            g_pipeSteps.clear();
            w.doPipeline();
            std::printf("PRESET idx=%d hum=%s semi=%.1f wind=%d -> %s\n", c.idx, c.hum, c.semi,
                        c.wind ? 1 : 0, g_pipeSteps.toUtf8().constData());
            // busy 锁即时面：提交后按钮即锁（任务排队/运行中）；等完成解锁（模型链较慢，轮询上限 120s）
            std::printf("BUSY locked=%d\n", btn->isEnabled() ? 0 : 1);
            for (int i = 0; i < 6000 && !btn->isEnabled(); ++i) runWithTimeout(20);
            std::printf("BUSY unlocked=%d ok=%d msg=%s\n", btn->isEnabled() ? 1 : 0, g_ok ? 1 : 0,
                        g_msg.toUtf8().constData());
        }
        return 0;
    }

    std::printf("unknown mode %s\n", mode.toUtf8().constData());
    return 2;
}
