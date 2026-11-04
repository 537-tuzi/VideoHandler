#include "mainwidget.h"
#include "service/mediaservice.h"

#include "model/data.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "service/dspdenoiser.h"// HumBase 制式枚举

namespace {
QString modelSizeText(double bytes) {// 目录表 sizeBytes → MiB 一位小数（模型管理列表展示）
    return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

// 文件对话框过滤器（doOpen 与 doBatchAdd 同源单点——审查 P3-3：两份字面量拷贝会漂移）
const QString kMediaFilter = QStringLiteral(
    "媒体文件 (*.mp4 *.mkv *.mov *.avi *.flv *.webm *.ts *.m4a *.mp3 *.wav *.aac *.flac *.ogg *.opus);;所有文件 (*)");

// 管线预设三条（FR-9 OQ-3(a) 拍板）：只定链形（动词序），denoise/pitch 步的动态参数
// （humBase/wind/semi）组包时取当前面板值注入（doPipeline）；预设均为两步链
const struct { const char* name; const char* verbs[2]; } kPipePresets[] = {
    {"AI 降噪 → 人声分离", {"aiDenoise", "separate"}},
    {"DSP 降噪 → 人声分离", {"denoise", "separate"}},
    {"人声分离 → 变调", {"separate", "pitch"}},
};
constexpr int kPipePresetCount = sizeof(kPipePresets) / sizeof(kPipePresets[0]);

// 批处理单动词动作五条（FR-10 OQ-6(a)）：动作组合前 5 项 = 引擎动词单步（separate 双 stem 拆两条），
// 后 3 项 = 管线预设（kPipePresets 顺序）；无 transcode（格式转换无「批处理清理」场景语义）。
// stem 空 = 该动词无 stem 参数（denoise/aiDenoise/pitch）；动态参数（humBase/wind/semi）组包时
// 取当前面板（doPipeline 同一面）
const struct { const char* name; const char* verb; const char* stem; } kBatchSingleActions[] = {
    {"DSP 降噪", "denoise", nullptr},
    {"AI 降噪", "aiDenoise", nullptr},
    {"变调", "pitch", nullptr},
    {"人声分离", "separate", "vocals"},
    {"伴奏提取", "separate", "instrumental"},
};
constexpr int kBatchSingleCount = sizeof(kBatchSingleActions) / sizeof(kBatchSingleActions[0]);
}// namespace

MainWidget::MainWidget(QWidget* parent)
    : QWidget(parent), media(nullptr), curFile(), workspaceRoot(), modelRoot(),
      btnOpen(nullptr), btnAudio(nullptr), btnVideo(nullptr),
      btnDenoise(nullptr), btnAi(nullptr), btnPitch(nullptr), btnSep(nullptr), btnInstrumental(nullptr), btnCancel(nullptr),
      btnWsChange(nullptr), btnHistory(nullptr), btnModels(nullptr),
      chkHum(nullptr), cmbHumBase(nullptr), chkWind(nullptr), cmbAiTier(nullptr), spinSemi(nullptr), cmbTrack(nullptr),
      cmbPipeline(nullptr), btnPipeline(nullptr), btnBatch(nullptr),
      lblFile(nullptr), lblWs(nullptr), lblStatus(nullptr), txtInfo(nullptr), bar(nullptr),
      modelDlg(nullptr), modelTree(nullptr), modelBar(nullptr),
      btnModelDownload(nullptr), btnModelImport(nullptr), btnModelCancel(nullptr),
      btnModelOpenDir(nullptr), btnModelRootChange(nullptr), cmbModelEndpoint(nullptr), lblModelRoot(nullptr),
      batchDlg(nullptr), lstBatchFiles(nullptr), cmbBatchAction(nullptr),
      btnBatchAdd(nullptr), btnBatchRemove(nullptr), btnBatchClear(nullptr), btnBatchStart(nullptr)
{
    build_ui();
    init_connect();
}

void MainWidget::build_ui() {
    btnOpen = new QPushButton(QStringLiteral("选择文件"), this);
    btnAudio = new QPushButton(QStringLiteral("提取音频"), this);
    btnAudio->setObjectName(QStringLiteral("btnAudio"));// 探针锚（TRK-GUI busy 期任务按钮锁定对照组）
    btnVideo = new QPushButton(QStringLiteral("提取无声视频"), this);
    btnDenoise = new QPushButton(QStringLiteral("DSP降噪导出WAV"), this);
    btnAi = new QPushButton(QStringLiteral("AI降噪导出WAV"), this);
    btnPitch = new QPushButton(QStringLiteral("变调导出WAV"), this);
    btnSep = new QPushButton(QStringLiteral("人声分离导出WAV"), this);
    btnInstrumental = new QPushButton(QStringLiteral("伴奏提取导出WAV"), this);
    btnCancel = new QPushButton(QStringLiteral("取消任务"), this);
    chkHum = new QCheckBox(QStringLiteral("除电流声"), this);
    chkHum->setObjectName(QStringLiteral("chkHum"));
    cmbHumBase = new QComboBox(this);
    cmbHumBase->setObjectName(QStringLiteral("cmbHumBase"));// 探针锚（FR-9 管线预设动态参数注入面）
    cmbHumBase->addItem(QStringLiteral("50Hz"));
    cmbHumBase->addItem(QStringLiteral("60Hz"));// 电网制式：50/60Hz 陷波基频
    cmbHumBase->setCurrentIndex(QSettings().value(QStringLiteral("humBaseHz"), 50).toInt() == 60 ? 1 : 0);// 记住上次选择，缺省 50Hz
    cmbHumBase->setEnabled(true);// chkHum 缺省勾选；随勾选状态联动见 init_connect
    chkWind = new QCheckBox(QStringLiteral("除风声(低频)"), this);
    chkWind->setObjectName(QStringLiteral("chkWind"));
    chkHum->setChecked(true);
    chkWind->setChecked(true);
    cmbAiTier = new QComboBox(this);// AI 降噪档位（FR-4）：快速 = GTCRN（现状引擎）/质量 = DeepFilterNet3
    cmbAiTier->setObjectName(QStringLiteral("cmbAiTier"));
    cmbAiTier->addItem(QStringLiteral("快速（GTCRN）"));
    cmbAiTier->addItem(QStringLiteral("质量（DeepFilterNet3）"));
    cmbAiTier->setCurrentIndex(QSettings().value(QString::fromUtf8(kSettingsAiTier),
                                                 QStringLiteral("fast")).toString()
                                       == QLatin1String("quality") ? 1 : 0);// 记住上次选择，缺省快速档
    spinSemi = new QDoubleSpinBox(this);
    spinSemi->setObjectName(QStringLiteral("spinSemi"));
    spinSemi->setRange(-12.0, 12.0);
    spinSemi->setSingleStep(1.0);
    spinSemi->setDecimals(1);
    spinSemi->setValue(0.0);
    spinSemi->setSuffix(QStringLiteral(" 半音"));
    cmbTrack = new QComboBox(this);// 轨道下拉（FR-1）：文件属性位挂 topRow；探测 ≥2 音轨才显示（resetTrackCombo 建单条缺省项并隐藏）
    cmbTrack->setObjectName(QStringLiteral("cmbTrack"));// 探针锚（TRK-GUI 断言：条目数/缺省选中/换文件重置）
    resetTrackCombo();
    cmbPipeline = new QComboBox(this);// 管线预设下拉（FR-9）：条目 = 静态预设表（链形见 kPipePresets）
    cmbPipeline->setObjectName(QStringLiteral("cmbPipeline"));
    for (const auto& p : kPipePresets) cmbPipeline->addItem(QString::fromUtf8(p.name));
    btnPipeline = new QPushButton(QStringLiteral("一键执行"), this);
    btnPipeline->setObjectName(QStringLiteral("btnPipeline"));
    btnBatch = new QPushButton(QStringLiteral("批处理…"), this);// FR-10：清单在对话框内自选，入口不依赖当前文件
    btnBatch->setObjectName(QStringLiteral("btnBatch"));
    lblFile = new QLabel(QStringLiteral("未选择文件"), this);
    // 工作区根（FR-A4 片 B，D5/OQ-1）：QSettings 只在此读（GUI 线程，D7），缺省 ~/VideoHandler-Workspace
    workspaceRoot = QSettings().value(QString::fromUtf8(kSettingsWorkspaceRoot),
                                      QDir::home().filePath(QString::fromUtf8(kWsDefaultRootName))).toString();
    if (workspaceRoot.isEmpty())// 键存过空串（手改 QSettings 等）→ 归一缺省，消除「任务 direct 而历史查询报 root 不能为空」两副面孔
        workspaceRoot = QDir::home().filePath(QString::fromUtf8(kWsDefaultRootName));
    lblWs = new QLabel(QStringLiteral("工作区：%1").arg(workspaceRoot), this);
    btnWsChange = new QPushButton(QStringLiteral("修改"), this);
    btnHistory = new QPushButton(QStringLiteral("处理历史"), this);
    // 模型库根（FR-8 片 B，D1/OQ-3）：QSettings 只在此读（GUI 线程，D7），缺省 ~/VideoHandler-Models；
    // 空串归一缺省（workspaceRoot 同型）
    modelRoot = QSettings().value(QString::fromUtf8(kSettingsModelRoot),
                                  QDir::home().filePath(QString::fromUtf8(kModelsDefaultRootName))).toString();
    if (modelRoot.isEmpty())
        modelRoot = QDir::home().filePath(QString::fromUtf8(kModelsDefaultRootName));
    btnModels = new QPushButton(QStringLiteral("模型管理…"), this);
    lblStatus = new QLabel(QStringLiteral("就绪"), this);
    txtInfo = new QPlainTextEdit(this);
    txtInfo->setObjectName(QStringLiteral("txtInfo"));// 探针锚（TRK-GUI 文本展示零漂移断言）
    txtInfo->setReadOnly(true);
    txtInfo->setMaximumHeight(120);
    txtInfo->setPlaceholderText(QStringLiteral("文件流信息"));
    bar = new QProgressBar(this);
    bar->setRange(0, 100);
    bar->setValue(0);
    btnAudio->setEnabled(false);
    btnVideo->setEnabled(false);
    btnDenoise->setEnabled(false);
    btnAi->setEnabled(false);
    btnPitch->setEnabled(false);
    btnSep->setEnabled(false);
    btnInstrumental->setEnabled(false);
    btnPipeline->setEnabled(false);
    btnCancel->setEnabled(false);
    btnHistory->setEnabled(false);// wsInfo 契约 = root+input，未载入文件时无查询对象
    bar->hide();// 没任务时不占位

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(btnOpen);
    topRow->addWidget(lblFile, 1);
    topRow->addWidget(cmbTrack);// 轨道下拉（FR-1 D5）：文件属性语义随文件行（非任务参数行），单轨文件隐藏 = 单轨 UI 零漂移
    auto* wsRow = new QHBoxLayout;// 工作区根展示/可改 + 处理历史 + 模型管理入口（D5 最小 UI 同排）
    wsRow->addWidget(lblWs, 1);
    wsRow->addWidget(btnWsChange);
    wsRow->addWidget(btnHistory);
    wsRow->addWidget(btnModels);
    auto* actRow = new QHBoxLayout;
    actRow->addWidget(btnAudio);
    actRow->addWidget(btnVideo);
    auto* denRow = new QHBoxLayout;
    denRow->addWidget(chkHum);
    denRow->addWidget(cmbHumBase);
    denRow->addWidget(chkWind);
    denRow->addWidget(btnDenoise);
    denRow->addWidget(cmbAiTier);
    denRow->addWidget(btnAi);
    auto* pitchRow = new QHBoxLayout;
    pitchRow->addWidget(spinSemi);
    pitchRow->addWidget(btnPitch);
    pitchRow->addWidget(btnSep);
    pitchRow->addWidget(btnInstrumental);
    pitchRow->addWidget(btnCancel);
    auto* pipeRow = new QHBoxLayout;// 管线一键（FR-9）：预设下拉 + 一键执行 + 批处理入口（FR-10）
    pipeRow->addWidget(cmbPipeline);
    pipeRow->addWidget(btnPipeline);
    pipeRow->addStretch(1);
    pipeRow->addWidget(btnBatch);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(topRow);
    lay->addLayout(wsRow);
    lay->addWidget(txtInfo);
    lay->addLayout(actRow);
    lay->addLayout(denRow);
    lay->addLayout(pitchRow);
    lay->addLayout(pipeRow);
    lay->addWidget(bar);
    lay->addWidget(lblStatus);
}

void MainWidget::init_connect() {
    connect(btnOpen, &QPushButton::clicked, this, &MainWidget::doOpen);
    connect(btnWsChange, &QPushButton::clicked, this, &MainWidget::doChangeWorkspace);
    connect(btnHistory, &QPushButton::clicked, this, &MainWidget::doHistory);
    connect(btnModels, &QPushButton::clicked, this, &MainWidget::openModelDialog);// FR-8 片 B：模型管理入口
    connect(btnAudio, &QPushButton::clicked, this, &MainWidget::doExtractAudio);
    connect(btnVideo, &QPushButton::clicked, this, &MainWidget::doExtractVideo);
    connect(btnDenoise, &QPushButton::clicked, this, &MainWidget::doDenoiseWav);
    connect(btnAi, &QPushButton::clicked, this, &MainWidget::doDenoiseAiWav);
    connect(btnPitch, &QPushButton::clicked, this, &MainWidget::doPitchWav);
    connect(btnSep, &QPushButton::clicked, this, &MainWidget::doSepVocals);
    connect(btnInstrumental, &QPushButton::clicked, this, &MainWidget::doSepInstrumental);
    connect(btnPipeline, &QPushButton::clicked, this, &MainWidget::doPipeline);// 管线一键（FR-9）
    connect(btnBatch, &QPushButton::clicked, this, &MainWidget::openBatchDialog);// 批处理入口（FR-10）
    connect(btnCancel, &QPushButton::clicked, this, &MainWidget::doCancel);
    connect(chkHum, &QCheckBox::toggled, cmbHumBase, &QComboBox::setEnabled);// 不除电流声时制式选择无效
    connect(cmbHumBase, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QSettings().setValue(QStringLiteral("humBaseHz"), idx == 1 ? 60 : 50);// 换制式即记住，下次启动还原
    });
    connect(cmbAiTier, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QSettings().setValue(QString::fromUtf8(kSettingsAiTier),// FR-4：换档即记住，下次启动还原
                             idx == 1 ? QStringLiteral("quality") : QStringLiteral("fast"));
    });
}

void MainWidget::setMediaService(MediaService* svc) {
    media = svc;
    if (media) {
        connect(media, &MediaService::progress, this, &MainWidget::onProgress);
        connect(media, &MediaService::jobFinished, this, &MainWidget::onJobDone);
        connect(media, &MediaService::probeFinished, this, &MainWidget::onProbeDone);
        connect(media, &MediaService::probeInfoFinished, this, &MainWidget::onProbeInfoDone);// FR-1：结构化探测落地（建轨道下拉）
        connect(media, &MediaService::wsInfoFinished, this, &MainWidget::onWsInfoDone);
        connect(media, &MediaService::modelListFinished, this, &MainWidget::onModelListDone);// FR-8 片 B
        connect(media, &MediaService::modelOpFinished, this, &MainWidget::onModelOpDone);
        connect(media, &MediaService::batchFinished, this, &MainWidget::onBatchDone);// FR-10：批处理落地
        connect(media, &MediaService::modelMissing, this, &MainWidget::onModelMissing);// 结构化引导（D8）
        media->setWorkspaceRoot(workspaceRoot);// 启动即下传（QSettings 只在本层读，D7）
        media->setModelRoot(modelRoot);// FR-8 片 B：两级缺省解析与模型族请求的根
    }
}

// 轨道下拉重置（FR-1 D5）：清回单条「自动（默认音轨）」并隐藏。openFile 同步调用——probeInfoDone
// 异步晚到（用户极早期点任务 → 下拉未建 → trackValue 取「自动」，无害），真正的坑是换文件未重置
// （旧文件 track=3 对新文件即越界），故重置不等异步探测、失败探测同样清空（不留旧文件轨）
void MainWidget::resetTrackCombo() {
    cmbTrack->clear();
    cmbTrack->addItem(QStringLiteral("自动（默认音轨）"), -1);// 缺省项 = 组包不带 track 键（best-stream 零漂移，不偷换「音轨 0」）
    cmbTrack->setCurrentIndex(0);
    cmbTrack->hide();
}

void MainWidget::openFile(const QString& path) {
    curFile = path;
    lblFile->setText(QFileInfo(path).fileName());
    resetTrackCombo();// 同步重置「自动」（FR-1 D5：防跨文件 stale 索引，不等异步 probeInfoDone）
    if (!taskRunning) {// 任务运行中不解禁：wsInfo 迟到弹窗里「用此版本继续」不得击穿 busy 锁（按钮由 set_busy(false) 统一恢复），probe 排队任务后照常执行
        btnAudio->setEnabled(true);
        btnVideo->setEnabled(true);
        btnDenoise->setEnabled(true);
        btnAi->setEnabled(true);
        btnPitch->setEnabled(true);
        btnSep->setEnabled(true);
        btnInstrumental->setEnabled(true);
        btnPipeline->setEnabled(true);
        btnHistory->setEnabled(true);
        txtInfo->setPlainText(QStringLiteral("正在读取流信息…"));
        lblStatus->setText(QStringLiteral("读取中…"));
    }
    if (media) media->probe(path);
    else { txtInfo->setPlainText(QStringLiteral("服务未初始化")); lblStatus->setText(QStringLiteral("就绪")); }
}

void MainWidget::doOpen() {
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择音视频文件"), QString(), kMediaFilter);
    if (!file.isEmpty()) openFile(file);
}

void MainWidget::doChangeWorkspace() {
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择工作区根目录"), workspaceRoot);
    if (dir.isEmpty()) return;
    workspaceRoot = dir;
    QSettings().setValue(QString::fromUtf8(kSettingsWorkspaceRoot), dir);// 换根即记住，下次启动还原
    lblWs->setText(QStringLiteral("工作区：%1").arg(workspaceRoot));
    if (media) media->setWorkspaceRoot(dir);// 队列转发 worker，此后任务/历史查询都用新根
    lblStatus->setText(QStringLiteral("工作区已切换"));
}

void MainWidget::doChangeModelRoot() {
    if (!modelDlg) return;// 入口只存在于模型管理对话框内（对话框必已创建）
    const QString dir = QFileDialog::getExistingDirectory(modelDlg, QStringLiteral("选择模型库根目录"), modelRoot);
    if (dir.isEmpty()) return;
    modelRoot = dir;
    QSettings().setValue(QString::fromUtf8(kSettingsModelRoot), dir);// 换根即记住，下次启动还原（doChangeWorkspace 同型）
    lblModelRoot->setText(QStringLiteral("模型库：%1").arg(modelRoot));
    if (media) media->setModelRoot(dir);// 队列转发 worker，此后缺省解析/下载/导入都用新根
    lblStatus->setText(QStringLiteral("模型库已切换"));
    refreshModels();
}

void MainWidget::doHistory() {
    if (!media || curFile.isEmpty()) return;
    lblStatus->setText(QStringLiteral("正在查询处理历史…"));// 首查大文件哈希可能数秒（worker 线程，UI 不冻）
    media->wsInfo(curFile);
}

void MainWidget::onProbeDone(bool ok, const QString& info) {
    txtInfo->setPlainText(info);
    lblStatus->setText(ok ? QStringLiteral("已载入，可执行分离") : QStringLiteral("读取失败"));
}

// 结构化探测落地（FR-1 D5）：据 probe result 的 audio 流清单建轨道下拉——≥2 音轨才显示
//（单轨隐藏 = 单轨 UI 零漂移，OQ-2 拍板「隐藏」）；条目标签从 ProbeStream 渲染（语言/标题/
// 缺省旗标有则显示）；失败/无音轨 → resetTrackCombo 清空（openFile 已重置过，双保险防迟到旧文件轨）
void MainWidget::onProbeInfoDone(bool ok, const QString& json) {
    if (!ok) {
        resetTrackCombo();
        return;
    }
    const QJsonObject result = QJsonDocument::fromJson(json.toUtf8()).object();
    const QJsonArray streams = result.value(QStringLiteral("streams")).toArray();
    QJsonArray audio;// 音频流按流序入下拉（itemData = 容器内绝对流索引，与 track 参数同域）
    for (const QJsonValue& v : streams) {
        const QJsonObject s = v.toObject();
        if (s.value(QStringLiteral("type")).toString() == QStringLiteral("audio")) audio.append(s);
    }
    if (audio.size() < 2) {
        resetTrackCombo();
        return;
    }
    cmbTrack->clear();
    cmbTrack->addItem(QStringLiteral("自动（默认音轨）"), -1);
    for (const QJsonValue& v : audio) {
        const QJsonObject s = v.toObject();
        QString label = QStringLiteral("流#%1").arg(s.value(QStringLiteral("index")).toInt());
        const QString language = s.value(QStringLiteral("language")).toString();
        const QString title = s.value(QStringLiteral("title")).toString();
        if (!language.isEmpty()) label += QStringLiteral(" · %1").arg(language);
        if (!title.isEmpty()) label += QStringLiteral(" · %1").arg(title);
        label += QStringLiteral(" · %1 · %2Hz · %3ch")
                     .arg(s.value(QStringLiteral("codec")).toString())
                     .arg(s.value(QStringLiteral("sampleRate")).toInt())
                     .arg(s.value(QStringLiteral("channels")).toInt());
        if (s.value(QStringLiteral("defaultTrack")).toBool()) label += QStringLiteral(" · 默认");
        cmbTrack->addItem(label, s.value(QStringLiteral("index")).toInt());
    }
    cmbTrack->setCurrentIndex(0);// 缺省「自动」（不预标 best-stream 将选哪轨——自估即幻觉，D5 对抗 1.2）
    cmbTrack->show();
}

// 处理历史落地（D5 最小 UI）：wsInfo result 渲染版本清单对话框（序号/时间/动词/参数摘要/输出路径）
// +「打开目录」（版本目录）+「用此版本继续」（以产物为输入开新任务，OQ-3 拍板 = 等价 doOpen 该文件）
void MainWidget::onWsInfoDone(bool ok, const QString& json) {
    if (!taskRunning) lblStatus->setText(QStringLiteral("就绪"));// 查询在任务前排队的迟到到达不抹运行中状态行
    if (!ok) {// json 为 error message（worker 侧 wsInfoDone 失败语义）
        QMessageBox::warning(this, QString::fromUtf8(kAppTitle), json);
        return;
    }
    const QJsonObject result = QJsonDocument::fromJson(json.toUtf8()).object();
    if (!result.value(QStringLiteral("exists")).toBool()) {
        QMessageBox::information(this, QString::fromUtf8(kAppTitle), QStringLiteral("该文件还没有处理历史"));
        return;
    }
    const QString dir = result.value(QStringLiteral("dir")).toString();
    const QJsonArray versions = result.value(QStringLiteral("versions")).toArray();
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("处理历史"));
    auto* table = new QTableWidget(static_cast<int>(versions.size()), 5, &dlg);
    table->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("时间"), QStringLiteral("动词"),
                                      QStringLiteral("参数"), QStringLiteral("输出路径")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);// 整行选中，按钮作用于当前行
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);// 清单只读（版本记录 append-only）
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    QStringList products;// 行 → 版本产物绝对路径（result.dir + 记录内相对路径），与行号对齐
    for (int i = 0; i < versions.size(); ++i) {
        const QJsonObject v = versions.at(i).toObject();
        const QString abs = dir + "/" + v.value(QStringLiteral("output")).toObject()
                                       .value(QStringLiteral("path")).toString();
        products << abs;
        const QString params = QJsonDocument(v.value(QStringLiteral("params")).toObject())
                                  .toJson(QJsonDocument::Compact);
        table->setItem(i, 0, new QTableWidgetItem(QString::number(v.value(QStringLiteral("version")).toInt())));
        table->setItem(i, 1, new QTableWidgetItem(v.value(QStringLiteral("time")).toString()));
        table->setItem(i, 2, new QTableWidgetItem(v.value(QStringLiteral("cmd")).toString()));
        table->setItem(i, 3, new QTableWidgetItem(params));
        table->setItem(i, 4, new QTableWidgetItem(abs));
    }
    auto* btnOpenDir = new QPushButton(QStringLiteral("打开目录"), &dlg);
    auto* btnContinue = new QPushButton(QStringLiteral("用此版本继续"), &dlg);
    auto* btnClose = new QPushButton(QStringLiteral("关闭"), &dlg);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(btnOpenDir);
    btnRow->addWidget(btnContinue);
    btnRow->addStretch(1);
    btnRow->addWidget(btnClose);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(table);
    lay->addLayout(btnRow);
    if (!versions.isEmpty()) table->selectRow(static_cast<int>(versions.size()) - 1);// 缺省选中最新版本
    connect(btnOpenDir, &QPushButton::clicked, [&] {
        const int row = table->currentRow();
        if (row < 0 || row >= products.size()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(products.at(row)).absolutePath()));
    });
    connect(btnContinue, &QPushButton::clicked, [&] {
        const int row = table->currentRow();
        if (row < 0 || row >= products.size()) return;
        const QString product = products.at(row);
        if (!QFileInfo::exists(product)) {// 工作区产物可能已被用户清理，历史记录仍如实保留
            QMessageBox::warning(&dlg, QString::fromUtf8(kAppTitle),
                                 QStringLiteral("版本产物已不在盘上：%1").arg(product));
            return;
        }
        dlg.accept();
        openFile(product);
    });
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.resize(760, 320);
    dlg.exec();
}

// 打开模型管理对话框（FR-8 片 B，D8）：非模态 show()（主面板状态行/进度条仍可见，任务与模型
// 操作共用 busy 锁不受对话框模态影响），首次打开创建一次后复用；每次打开刷新列表（模型可能被
// 用户在库目录手动增删，列表以磁盘实况三视角为准）
void MainWidget::openModelDialog() {
    if (!modelDlg) buildModelDialog();
    modelDlg->show();
    modelDlg->raise();
    modelDlg->activateWindow();
    refreshModels();
}

// 搭模型管理对话框（一次性）：上排库根展示/修改 + 下载源 endpoint 下拉；中列表 id/用途/状态/
// 许可证/大小；下排下载/导入/打开目录/取消。objectName 供验收探针 findChild 定位
void MainWidget::buildModelDialog() {
    modelDlg = new QDialog(this);// 有父但 show() 非模态；随主窗析构
    modelDlg->setObjectName(QStringLiteral("modelDlg"));
    modelDlg->setWindowTitle(QStringLiteral("模型管理"));
    lblModelRoot = new QLabel(QStringLiteral("模型库：%1").arg(modelRoot), modelDlg);
    lblModelRoot->setObjectName(QStringLiteral("lblModelRoot"));
    btnModelRootChange = new QPushButton(QStringLiteral("修改"), modelDlg);
    btnModelRootChange->setObjectName(QStringLiteral("btnModelRootChange"));
    auto* rootRow = new QHBoxLayout;
    rootRow->addWidget(lblModelRoot, 1);
    rootRow->addWidget(btnModelRootChange);

    modelTree = new QTreeWidget(modelDlg);
    modelTree->setObjectName(QStringLiteral("modelTree"));
    modelTree->setColumnCount(5);
    modelTree->setHeaderLabels({QStringLiteral("id"), QStringLiteral("用途"), QStringLiteral("状态"),
                                QStringLiteral("许可证"), QStringLiteral("大小")});
    modelTree->setRootIsDecorated(false);// 平铺清单无层级
    modelTree->setSelectionBehavior(QAbstractItemView::SelectRows);// 整行选中，按钮作用于当前行
    modelTree->setSelectionMode(QAbstractItemView::SingleSelection);
    modelTree->setEditTriggers(QAbstractItemView::NoEditTriggers);// 清单只读（来源三视角，非编辑面）
    modelTree->header()->setStretchLastSection(true);
    modelTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    cmbModelEndpoint = new QComboBox(modelDlg);
    cmbModelEndpoint->setObjectName(QStringLiteral("cmbModelEndpoint"));
    cmbModelEndpoint->setEditable(true);// 预设之外允许手填自建源（D4：参数归参数，非白名单）
    cmbModelEndpoint->addItem(QString::fromUtf8(kDefaultModelEndpoint));
    cmbModelEndpoint->addItem(QStringLiteral("https://hf-mirror.com"));
    const QString savedEndpoint = QSettings().value(QString::fromUtf8(kSettingsModelEndpoint)).toString();
    if (!savedEndpoint.isEmpty()) cmbModelEndpoint->setCurrentText(savedEndpoint);// 记忆上次选择，首次 = 缺省 huggingface.co
    connect(cmbModelEndpoint, &QComboBox::currentIndexChanged, this, [this](int) {// 换选预设即记（D4）
        QSettings().setValue(QString::fromUtf8(kSettingsModelEndpoint), cmbModelEndpoint->currentText().trimmed());
    });
    connect(cmbModelEndpoint, &QComboBox::activated, this, [this](int) {// 补面：手填自定义后重选同号预设
        QSettings().setValue(QString::fromUtf8(kSettingsModelEndpoint), cmbModelEndpoint->currentText().trimmed());// （索引未变 currentIndexChanged 不发，activated 是用户选中恒发）
    });
    connect(cmbModelEndpoint->lineEdit(), &QLineEdit::editingFinished, this, [this] {// 手填不自入条目，索引不变不触发上行
        QSettings().setValue(QString::fromUtf8(kSettingsModelEndpoint), cmbModelEndpoint->currentText().trimmed());
    });

    modelBar = new QProgressBar(modelDlg);
    modelBar->setObjectName(QStringLiteral("modelBar"));
    modelBar->setRange(0, 100);
    modelBar->setValue(0);
    modelBar->hide();// 无操作时不占位（与主进度条同源联动）

    btnModelDownload = new QPushButton(QStringLiteral("下载"), modelDlg);
    btnModelDownload->setObjectName(QStringLiteral("btnModelDownload"));
    btnModelImport = new QPushButton(QStringLiteral("导入…"), modelDlg);
    btnModelImport->setObjectName(QStringLiteral("btnModelImport"));
    btnModelOpenDir = new QPushButton(QStringLiteral("打开目录"), modelDlg);
    btnModelOpenDir->setObjectName(QStringLiteral("btnModelOpenDir"));
    btnModelCancel = new QPushButton(QStringLiteral("取消"), modelDlg);
    btnModelCancel->setObjectName(QStringLiteral("btnModelCancel"));
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(btnModelDownload);
    btnRow->addWidget(btnModelImport);
    btnRow->addWidget(btnModelOpenDir);
    btnRow->addStretch(1);
    btnRow->addWidget(btnModelCancel);

    auto* lay = new QVBoxLayout(modelDlg);
    lay->addLayout(rootRow);
    lay->addWidget(modelTree);
    auto* epRow = new QHBoxLayout;
    epRow->addWidget(new QLabel(QStringLiteral("下载源"), modelDlg));
    epRow->addWidget(cmbModelEndpoint, 1);
    lay->addLayout(epRow);
    // 镜像适用性注记（D4 明文）：镜像前缀只改 hfSource 条目 URL；GTCRN 等 GitHub 直连条目
    // 换镜像无效——用户换 hf-mirror 后下载 gtcrn 仍走直连，无此注记则「镜像没生效」无从解释
    auto* lblMirrorNote = new QLabel(
        QStringLiteral("注：镜像前缀仅对 HuggingFace 源条目生效；GitHub 直连条目不受镜像影响。"), modelDlg);
    lblMirrorNote->setObjectName(QStringLiteral("lblMirrorNote"));
    lay->addWidget(lblMirrorNote);
    lay->addWidget(modelBar);
    lay->addLayout(btnRow);
    modelDlg->resize(720, 360);

    connect(btnModelRootChange, &QPushButton::clicked, this, &MainWidget::doChangeModelRoot);
    connect(btnModelDownload, &QPushButton::clicked, this, &MainWidget::doModelDownloadFromDialog);
    connect(btnModelImport, &QPushButton::clicked, this, &MainWidget::doModelImportFromDialog);
    connect(btnModelOpenDir, &QPushButton::clicked, this, [this] {
        QDir().mkpath(modelRoot);// 首次使用库可能还不存在，建空根让用户看到落点/可手放模型
        QDesktopServices::openUrl(QUrl::fromLocalFile(modelRoot));
    });
    connect(btnModelCancel, &QPushButton::clicked, this, &MainWidget::doCancel);// 复用服务取消通道（下载/导入与七槽同把锁）
    btnModelCancel->setEnabled(false);// 空闲态无任务可取消（set_busy 维护其后续显隐）
}

void MainWidget::refreshModels() {
    if (media) media->modelList();// 只读不占 busy：下载中也可刷新（wsInfo 先例）
}

void MainWidget::doModelDownloadFromDialog() {
    QTreeWidgetItem* item = modelTree ? modelTree->currentItem() : nullptr;
    if (!item || !media) return;// 无选中静默（列表为空时按钮点击无意义，不弹窗打扰）
    if (item->data(0, Qt::UserRole + 1).toString() == QStringLiteral("installed")) {
        QMessageBox::information(modelDlg, QString::fromUtf8(kAppTitle),
                                 QStringLiteral("该模型已安装：%1").arg(item->text(0)));
        return;
    }
    const QString id = item->data(0, Qt::UserRole).toString();
    const QString endpoint = cmbModelEndpoint->currentText().trimmed();// 空 = 不组键走命令层缺省源
    set_busy(true);
    lblStatus->setText(QStringLiteral("正在下载模型 %1…").arg(id));
    media->modelDownload(id, endpoint);
}

void MainWidget::doModelImportFromDialog() {
    if (!media) return;
    // FR-4：目录型模型（质量档）导入物 = tar.gz 压缩包，过滤器随收（单文件 onnx 仍主流形态）
    const QString path = QFileDialog::getOpenFileName(modelDlg, QStringLiteral("选择模型文件"), QString(),
        QStringLiteral("模型文件 (*.onnx *.tar.gz *.tgz);;所有文件 (*)"));
    if (path.isEmpty()) return;
    submitModelImport(path);
}

// 用途选择 + 许可提示后提交导入（§161 文案逐字）：自由导入不猜身份，用户声明该模型用于哪个
// 引擎动词，命令层按 for 做引擎试开校验。选完文件与探针直调共用此单一提交路径
void MainWidget::submitModelImport(const QString& path) {
    QDialog dlg(modelDlg);
    dlg.setObjectName(QStringLiteral("modelImportDlg"));
    dlg.setWindowTitle(QStringLiteral("选择导入用途"));
    auto* lbl = new QLabel(QStringLiteral("该模型用于哪个功能？"), &dlg);
    auto* combo = new QComboBox(&dlg);
    combo->setObjectName(QStringLiteral("modelImportCombo"));
    combo->addItem(QStringLiteral("AI 降噪"), QStringLiteral("aiDenoise"));
    combo->addItem(QStringLiteral("AI 降噪（质量档）"), QStringLiteral("aiDenoiseHq"));// FR-4：tar.gz 试开导入
    combo->addItem(QStringLiteral("人声分离"), QStringLiteral("separate"));
    auto* notice = new QLabel(QStringLiteral("请确认您有权使用该模型（来源与许可证由您自行核验）"), &dlg);
    notice->setObjectName(QStringLiteral("modelImportNotice"));
    notice->setWordWrap(true);
    auto* btnOk = new QPushButton(QStringLiteral("导入"), &dlg);
    btnOk->setObjectName(QStringLiteral("modelImportOk"));
    auto* btnNo = new QPushButton(QStringLiteral("取消"), &dlg);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(btnOk);
    row->addWidget(btnNo);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(lbl);
    lay->addWidget(combo);
    lay->addWidget(notice);
    lay->addLayout(row);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(btnNo, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("正在导入模型…"));
    media->modelImport(path, combo->currentData().toString());
}

// 模型列表落地：result.models[] 渲染进 QTreeWidget，状态/用途转中文展示；id/状态存 ItemData
// 供下载按钮判定（installed 拦截重复下载）；path 挂 tooltip（长路径不撑列宽）
void MainWidget::onModelListDone(bool ok, const QString& json) {
    if (!modelTree) return;// 刷新仅在对话框打开后发起，防御性判空
    // 刷新前选中 id：clear() 会销毁选中，重建后按 id 恢复（测试轮 D1——无条件回首行会让
    // 「选中→下载→刷新→再点下载」静默改目标；错击非镜像条目时还会对 GitHub 直连发起真外联）
    const QString keepId = modelTree->currentItem()
                               ? modelTree->currentItem()->data(0, Qt::UserRole).toString()
                               : QString();
    modelTree->clear();
    if (!ok) {// json 为 error message（worker 侧 modelListDone 失败语义）
        QMessageBox::warning(this, QString::fromUtf8(kAppTitle), json);
        return;
    }
    const QJsonObject result = QJsonDocument::fromJson(json.toUtf8()).object();
    const QJsonArray models = result.value(QStringLiteral("models")).toArray();
    static const QHash<QString, QString> kForText = {
        {QStringLiteral("aiDenoise"), QStringLiteral("AI 降噪")},
        {QStringLiteral("aiDenoiseHq"), QStringLiteral("AI 降噪（质量档）")},// FR-4
        {QStringLiteral("separate"), QStringLiteral("人声分离")}};
    static const QHash<QString, QString> kStatusText = {
        {QStringLiteral("installed"), QStringLiteral("已安装")},
        {QStringLiteral("available"), QStringLiteral("可下载")},
        {QStringLiteral("stale"), QStringLiteral("已失效（文件缺失）")}};
    for (const QJsonValue& v : models) {
        const QJsonObject m = v.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        const QString forVerb = m.value(QStringLiteral("for")).toString();
        const QString status = m.value(QStringLiteral("status")).toString();
        auto* item = new QTreeWidgetItem(modelTree);
        item->setText(0, id);
        item->setText(1, kForText.value(forVerb, forVerb));
        item->setText(2, kStatusText.value(status, status));
        item->setText(3, m.value(QStringLiteral("license")).toString());
        item->setText(4, modelSizeText(m.value(QStringLiteral("sizeBytes")).toDouble()));
        item->setData(0, Qt::UserRole, id);
        item->setData(0, Qt::UserRole + 1, status);
        const QString path = m.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) item->setToolTip(0, path);
    }
    if (modelTree->topLevelItemCount() > 0) {
        bool restored = false;
        if (!keepId.isEmpty())// 按 id 恢复原选中（列表刷新通常只是状态翻转，id 集不变）
            for (int i = 0; i < modelTree->topLevelItemCount(); ++i)
                if (modelTree->topLevelItem(i)->data(0, Qt::UserRole).toString() == keepId) {
                    modelTree->setCurrentItem(modelTree->topLevelItem(i));
                    restored = true;
                    break;
                }
        if (!restored)// 无原选中（首开）/原 id 已不在列表 → 缺省首行（目录序），下载按钮即有作用对象
            modelTree->setCurrentItem(modelTree->topLevelItem(0));
    }
}

// 模型下载/导入落地：放 busy、状态行直通 message；完成即刷新列表（安装状态已变）；「已取消」
// 静默（onJobDone 同型），其余失败弹警告（E_NET 网络错误等用户需感知）
void MainWidget::onModelOpDone(bool ok, const QString& message) {
    set_busy(false);
    lblStatus->setText(message);
    refreshModels();
    if (ok) QMessageBox::information(this, QString::fromUtf8(kAppTitle), message);
    else if (message != QStringLiteral("已取消")) QMessageBox::warning(this, QString::fromUtf8(kAppTitle), message);
}

// 缺模型引导（D8/OQ-7(a)，结构化信号触发，非文案前缀匹配）：问询打开模型管理，文案带用途
// （审查轮 P3-1：信号本载 forVerb——管线链中取 error.stepCmd（附裁 1）——壳此前丢参弹通用
// 文案，把「该下哪个用途的模型」留给用户猜）；附 F-2 等长篡改边界的解药文案——下载幂等只校验
// sizeBytes，等长翻字节命中不了重下，删除模型文件后重下才治愈（对话框无删除功能 D5——
// 「打开目录」即达：清单 stale → 重下治愈，审查轮 P2-1 改指向真实能力）
void MainWidget::onModelMissing(const QString& forVerb) {
    // 用途名映射（与七槽/预设文案同词）；未登记动词（未来注册表新引擎）退通用文案
    QString purpose;
    if (forVerb == QLatin1String("aiDenoise")) purpose = QStringLiteral("AI 降噪");
    else if (forVerb == QLatin1String("aiDenoiseHq")) purpose = QStringLiteral("AI 降噪（质量档）");// FR-4
    else if (forVerb == QLatin1String("separate")) purpose = QStringLiteral("人声分离");
    QMessageBox box(QMessageBox::Question, QString::fromUtf8(kAppTitle),
                    purpose.isEmpty()
                        ? QStringLiteral("模型不可用，是否打开模型管理下载？")
                        : QStringLiteral("%1模型不可用，是否打开模型管理下载？").arg(purpose),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(QStringLiteral("若下载后模型仍不可用，请在模型管理中打开库目录，删除该模型文件后重新下载。"));
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() == QMessageBox::Yes) openModelDialog();
}

// 打开批处理对话框（FR-10）：非模态 show()（openModelDialog 同型——主面板状态行/进度条仍可见，
// 批处理整批占 busy 与其他任务同锁），首次打开创建一次后复用；清单跨打开保留（对话框仅隐藏不销毁）
void MainWidget::openBatchDialog() {
    if (!batchDlg) buildBatchDialog();
    batchDlg->show();
    batchDlg->raise();
    batchDlg->activateWindow();
}

// 搭批处理对话框（一次性，FR-10 D8）：上排动作组合（8 条 = 5 引擎动词单步 + 3 管线预设，
// OQ-6(a)：无 transcode）；中清单（UserRole = 绝对路径）；下排添加/移除/清空/开始。
// objectName 供验收探针 findChild 定位（gui-slot-eqv 同族锚）
void MainWidget::buildBatchDialog() {
    batchDlg = new QDialog(this);// 有父但 show() 非模态；随主窗析构
    batchDlg->setObjectName(QStringLiteral("batchDlg"));
    batchDlg->setWindowTitle(QStringLiteral("批处理"));
    cmbBatchAction = new QComboBox(batchDlg);
    cmbBatchAction->setObjectName(QStringLiteral("cmbBatchAction"));
    for (const auto& a : kBatchSingleActions) cmbBatchAction->addItem(QString::fromUtf8(a.name));
    for (const auto& p : kPipePresets) cmbBatchAction->addItem(QString::fromUtf8(p.name));// 预设链与前 5 项同名不冲突（条目文案含动词链描述）
    auto* actRow = new QHBoxLayout;
    actRow->addWidget(new QLabel(QStringLiteral("动作"), batchDlg));
    actRow->addWidget(cmbBatchAction, 1);

    lstBatchFiles = new QListWidget(batchDlg);
    lstBatchFiles->setObjectName(QStringLiteral("lstBatchFiles"));
    lstBatchFiles->setSelectionBehavior(QAbstractItemView::SelectRows);
    lstBatchFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);// 移除可多选
    lstBatchFiles->setEditTriggers(QAbstractItemView::NoEditTriggers);// 清单只读（增删走按钮）

    btnBatchAdd = new QPushButton(QStringLiteral("添加文件…"), batchDlg);
    btnBatchAdd->setObjectName(QStringLiteral("btnBatchAdd"));
    btnBatchRemove = new QPushButton(QStringLiteral("移除选中"), batchDlg);
    btnBatchRemove->setObjectName(QStringLiteral("btnBatchRemove"));
    btnBatchClear = new QPushButton(QStringLiteral("清空"), batchDlg);
    btnBatchClear->setObjectName(QStringLiteral("btnBatchClear"));
    btnBatchStart = new QPushButton(QStringLiteral("开始"), batchDlg);
    btnBatchStart->setObjectName(QStringLiteral("btnBatchStart"));
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(btnBatchAdd);
    btnRow->addWidget(btnBatchRemove);
    btnRow->addWidget(btnBatchClear);
    btnRow->addStretch(1);
    btnRow->addWidget(btnBatchStart);

    auto* lay = new QVBoxLayout(batchDlg);
    lay->addLayout(actRow);
    lay->addWidget(lstBatchFiles);
    lay->addLayout(btnRow);
    batchDlg->resize(560, 360);

    connect(btnBatchAdd, &QPushButton::clicked, this, &MainWidget::doBatchAdd);
    connect(btnBatchRemove, &QPushButton::clicked, this, &MainWidget::doBatchRemove);
    connect(btnBatchClear, &QPushButton::clicked, this, &MainWidget::doBatchClear);
    connect(btnBatchStart, &QPushButton::clicked, this, &MainWidget::doBatchStart);
}

// 清单添加（FR-10 D2）：多选文件（过滤器与 doOpen 同一），QFileInfo::absoluteFilePath 字符串级
// 去重（与命令层 inputs 重复拒收同口径）；清单项文本显示文件名（路径长清单可读），UserRole 存绝对路径
void MainWidget::doBatchAdd() {
    const QStringList files = QFileDialog::getOpenFileNames(batchDlg, QStringLiteral("选择要批处理的文件"), QString(), kMediaFilter);
    for (const QString& f : files) {
        const QString abs = QFileInfo(f).absoluteFilePath();
        bool dup = false;
        for (int i = 0; i < lstBatchFiles->count(); ++i)
            if (lstBatchFiles->item(i)->data(Qt::UserRole).toString() == abs) { dup = true; break; }
        if (dup) continue;// 重复文件静默跳过（多选含已添加项是常态，弹窗打扰）
        auto* item = new QListWidgetItem(QFileInfo(abs).fileName(), lstBatchFiles);
        item->setData(Qt::UserRole, abs);
    }
}

void MainWidget::doBatchRemove() {
    const QList<QListWidgetItem*> sel = lstBatchFiles->selectedItems();
    for (QListWidgetItem* item : sel) delete item;
}

void MainWidget::doBatchClear() {
    lstBatchFiles->clear();
}

// 开始批处理（FR-10 D8）：≥2 件守卫（命令层同下限，GUI 前置给可读状态行）；动作组合取当前索引
// 组共享模板（单动词 = 1 步、预设 = 2 步链），动态参数取当前面板（doPipeline 同一面：denoise 步
// humBase/wind、pitch 步 semi）；组 {inputs, steps} 两 Compact JSON 交 MediaService::batch
//（模板模型步两级缺省解析在 worker 侧，与管线同源）。off+off、semi=0 等参数非法不在 GUI 重复判
//——命令层模板静态预校验给步骤级错误（薄壳原则，开工前零副作用）
void MainWidget::doBatchStart() {
    if (!media || !batchDlg) return;
    const int n = lstBatchFiles->count();
    if (n < 2) {
        lblStatus->setText(QStringLiteral("批处理至少需要 2 个文件"));
        return;
    }
    const int idx = cmbBatchAction->currentIndex();
    if (idx < 0) return;
    QJsonArray steps;
    if (idx < kBatchSingleCount) {// 前 5 项：单动词动作（separate 带 stem；denoise/pitch 注入面板动态参数）
        QJsonObject step;
        // FR-4：AI 动作跟随当前档位（aiVerb 同源——单动词动作与预设链同面）
        step.insert(QStringLiteral("cmd"),
                    QLatin1String(kBatchSingleActions[idx].verb) == QLatin1String("aiDenoise")
                        ? aiVerb()
                        : QString::fromLatin1(kBatchSingleActions[idx].verb));
        if (QLatin1String(kBatchSingleActions[idx].verb) == QLatin1String("denoise")) {
            step.insert(QStringLiteral("humBase"),
                        chkHum->isChecked()
                            ? (cmbHumBase->currentIndex() == 1 ? QStringLiteral("60") : QStringLiteral("50"))
                            : QStringLiteral("off"));
            step.insert(QStringLiteral("wind"), chkWind->isChecked());
        } else if (QLatin1String(kBatchSingleActions[idx].verb) == QLatin1String("pitch")) {
            step.insert(QStringLiteral("semi"), spinSemi->value());
        } else if (kBatchSingleActions[idx].stem) {
            step.insert(QStringLiteral("stem"), QString::fromLatin1(kBatchSingleActions[idx].stem));
        }
        steps.append(step);
    } else {// 后 3 项：管线预设（kPipePresets 顺序，动态参数注入与 doPipeline 同面）
        const int p = idx - kBatchSingleCount;
        if (p < 0 || p >= kPipePresetCount) return;
        for (const char* verb : kPipePresets[p].verbs) {
            QJsonObject step;
            // FR-4：预设链 AI 步跟随当前档位（doPipeline 同面）
            step.insert(QStringLiteral("cmd"),
                        QLatin1String(verb) == QLatin1String("aiDenoise") ? aiVerb()
                                                                          : QString::fromLatin1(verb));
            if (QLatin1String(verb) == QLatin1String("denoise")) {
                step.insert(QStringLiteral("humBase"),
                            chkHum->isChecked()
                                ? (cmbHumBase->currentIndex() == 1 ? QStringLiteral("60") : QStringLiteral("50"))
                                : QStringLiteral("off"));
                step.insert(QStringLiteral("wind"), chkWind->isChecked());
            } else if (QLatin1String(verb) == QLatin1String("pitch")) {
                step.insert(QStringLiteral("semi"), spinSemi->value());
            } else if (QLatin1String(verb) == QLatin1String("separate")) {
                step.insert(QStringLiteral("stem"), QStringLiteral("vocals"));// 预设链人声分离步主 stem 固定 vocals（kPipePresets 原语义）
            }
            steps.append(step);
        }
    }
    QJsonArray inputs;
    for (int i = 0; i < n; ++i) inputs.append(lstBatchFiles->item(i)->data(Qt::UserRole).toString());
    set_busy(true);
    lblStatus->setText(QStringLiteral("批处理中（共 %1 件）…").arg(n));
    media->batch(QString::fromUtf8(QJsonDocument(inputs).toJson(QJsonDocument::Compact)),
                 QString::fromUtf8(QJsonDocument(steps).toJson(QJsonDocument::Compact)));
}

// 批处理落地（FR-10 D8/附裁 5）：放锁；失败直通 error.message 弹警告——「已取消」静默特判
// （onJobDone 同型逐字命中，整批取消 message 即「已取消」）；成功弹汇总 information
// 「批处理完成：k/N 件成功」+ 清单逐件渲染（行文本 = 文件名 —— 成功（版本 N）/失败：CODE：message，
// 按 items[].index 对位——index 1 基与清单行同序，GUI 只在批启动后整批落定才收到本回调，行序稳定）
void MainWidget::onBatchDone(bool ok, const QString& jsonOrMessage) {
    set_busy(false);
    if (!ok) {
        bar->setValue(0);
        lblStatus->setText(jsonOrMessage);
        if (jsonOrMessage != QStringLiteral("已取消")) QMessageBox::warning(this, QString::fromUtf8(kAppTitle), jsonOrMessage);
        return;
    }
    bar->setValue(100);
    const QJsonObject result = QJsonDocument::fromJson(jsonOrMessage.toUtf8()).object();
    const int total = result.value(QStringLiteral("total")).toInt();
    const int okCount = result.value(QStringLiteral("okCount")).toInt();
    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& v : items) {
        const QJsonObject item = v.toObject();
        const int row = item.value(QStringLiteral("index")).toInt() - 1;
        if (row < 0 || row >= lstBatchFiles->count()) continue;
        QListWidgetItem* it = lstBatchFiles->item(row);
        const QString name = QFileInfo(it->data(Qt::UserRole).toString()).fileName();// 行文本可能已是上一轮结果，基名恒从 UserRole 取
        if (item.value(QStringLiteral("ok")).toBool()) {
            const int version = item.value(QStringLiteral("workspace")).toObject()
                                     .value(QStringLiteral("version")).toInt();
            it->setText(QStringLiteral("%1 —— 成功（版本 %2）").arg(name, QString::number(version)));
        } else {
            const QJsonObject err = item.value(QStringLiteral("error")).toObject();
            it->setText(QStringLiteral("%1 —— 失败：%2：%3")
                            .arg(name, err.value(QStringLiteral("code")).toString(),
                                 err.value(QStringLiteral("message")).toString()));
        }
    }
    lblStatus->setText(QStringLiteral("批处理完成：%1/%2 件成功").arg(okCount).arg(total));
    QMessageBox::information(this, QString::fromUtf8(kAppTitle),
                             QStringLiteral("批处理完成：%1/%2 件成功").arg(okCount).arg(total));
}

void MainWidget::doExtractAudio() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("正在提取音频…"));
    media->extractAudio(curFile, trackValue());// FR-1：轨参随行（「自动」= -1 = 不组键零漂移）
}

void MainWidget::doExtractVideo() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("正在提取无声视频…"));
    media->extractVideo(curFile);
}

void MainWidget::doDenoiseWav() {
    if (!media || curFile.isEmpty()) return;
    const bool hum = chkHum->isChecked();
    if (!hum && !chkWind->isChecked()) {
        lblStatus->setText(QStringLiteral("先勾选要除的噪声类型"));
        return;
    }
    const HumBase humBase = !hum ? HumBase::Off
                        : cmbHumBase->currentIndex() == 1 ? HumBase::Hz60 : HumBase::Hz50;
    set_busy(true);
    lblStatus->setText(QStringLiteral("正在降噪并导出 WAV…"));
    media->denoiseWav(curFile, humBase, chkWind->isChecked(), trackValue());// FR-1：轨参随行
}

// 当前 AI 降噪档位对应动词（FR-4）：七槽（doDenoiseAiWav）、管线预设步、批处理动作三面组包
// 同源取值——档位是面板动态状态（humBase/semi 同型），切档即下次任务生效
QString MainWidget::aiVerb() const {
    return cmbAiTier && cmbAiTier->currentIndex() == 1 ? QStringLiteral("aiDenoiseHq")
                                                       : QStringLiteral("aiDenoise");
}

// 当前轨道下拉取值（FR-1 D5）：缺省项「自动」= -1 = 组包不带 track 键（best-stream 语义零漂移）；
// 七槽（extractVideo 除外，其拒收 track）与管线一键单源取值（aiVerb 先例）。下拉未建/隐藏时
// 恒 -1（QComboBox 无选中 currentData 无效值 → 缺省 -1）
int MainWidget::trackValue() const {
    if (!cmbTrack) return -1;
    const QVariant d = cmbTrack->currentData();
    return d.isValid() ? d.toInt() : -1;// clear() 后 addItem 前的瞬态无效值按「自动」
}

void MainWidget::doDenoiseAiWav() {
    if (!media || curFile.isEmpty()) return;
    const bool quality = cmbAiTier && cmbAiTier->currentIndex() == 1;
    set_busy(true);
    lblStatus->setText(quality ? QStringLiteral("AI 降噪（质量档）中…")
                               : QStringLiteral("AI 降噪中…"));
    media->denoiseAiWav(curFile, quality, trackValue());// FR-1：轨参随行
}

void MainWidget::doPitchWav() {
    if (!media || curFile.isEmpty()) return;
    const double semi = spinSemi->value();
    if (qFuzzyIsNull(semi)) {
        lblStatus->setText(QStringLiteral("变调量为 0，无需处理"));
        return;
    }
    set_busy(true);
    lblStatus->setText(QStringLiteral("正在变调（%1 半音）…").arg(semi));
    media->pitchWav(curFile, semi, trackValue());// FR-1：轨参随行
}

void MainWidget::doSepVocals() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("人声分离中（CPU 推理较慢）…"));
    media->sepVocals(curFile, false, trackValue());// FR-1：轨参随行
}

void MainWidget::doSepInstrumental() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("伴奏提取中（CPU 推理较慢）…"));
    media->sepVocals(curFile, true, trackValue());// FR-1：轨参随行
}

// 管线一键（FR-9）：链形取预设表，动态参数取当前面板（denoise 步 humBase 跟随下拉/wind 跟随勾选、
// pitch 步 semi 跟随变调框）；组 stepsJson 交 worker（模型步两级缺省解析在 worker 侧，与七槽同源）。
// off+off、semi=0 等参数非法不在 GUI 重复判——命令层整链静态预校验给步骤级错误（薄壳原则）
void MainWidget::doPipeline() {
    if (!media || curFile.isEmpty()) return;
    const int idx = cmbPipeline->currentIndex();
    if (idx < 0 || idx >= kPipePresetCount) return;
    QJsonArray steps;
    for (const char* verb : kPipePresets[idx].verbs) {
        QJsonObject step;
        // FR-4：预设链 AI 步跟随当前档位（aiDenoise ↔ aiDenoiseHq）；denoise/pitch 动态参数同面注入
        step.insert(QStringLiteral("cmd"),
                    QLatin1String(verb) == QLatin1String("aiDenoise") ? aiVerb()
                                                                      : QString::fromLatin1(verb));
        if (QLatin1String(verb) == QLatin1String("denoise")) {
            step.insert(QStringLiteral("humBase"),
                        chkHum->isChecked()
                            ? (cmbHumBase->currentIndex() == 1 ? QStringLiteral("60") : QStringLiteral("50"))
                            : QStringLiteral("off"));
            step.insert(QStringLiteral("wind"), chkWind->isChecked());
        } else if (QLatin1String(verb) == QLatin1String("pitch")) {
            step.insert(QStringLiteral("semi"), spinSemi->value());
        }
        steps.append(step);
    }
    set_busy(true);
    lblStatus->setText(QStringLiteral("管线处理中（共 %1 步）…").arg(steps.size()));
    media->pipeline(curFile, QString::fromUtf8(QJsonDocument(steps).toJson(QJsonDocument::Compact)),
                    trackValue());// FR-1：轨参随行（命令层仅注步 1 源头文件选轨）
}

void MainWidget::doCancel() {
    if (media) media->cancelJob();
}

void MainWidget::onProgress(int pct) {
    bar->setValue(pct);
    if (modelBar) modelBar->setValue(pct);// 模型下载进度与主进度条同源联动（对话框内可见）
}

void MainWidget::onJobDone(bool ok, const QString& msg) {
    set_busy(false);
    bar->setValue(ok ? 100 : 0);
    lblStatus->setText(msg);
    if (ok) QMessageBox::information(this, QString::fromUtf8(kAppTitle), msg);
    else if (msg != QStringLiteral("已取消")) QMessageBox::warning(this, QString::fromUtf8(kAppTitle), msg);
}

void MainWidget::set_busy(bool on) {
    taskRunning = on;
    btnOpen->setEnabled(!on);
    btnAudio->setEnabled(!on);
    btnVideo->setEnabled(!on);
    btnDenoise->setEnabled(!on);
    btnAi->setEnabled(!on);
    btnPitch->setEnabled(!on);
    btnSep->setEnabled(!on);
    btnInstrumental->setEnabled(!on);
    btnPipeline->setEnabled(!on);
    cmbPipeline->setEnabled(!on);// 预设切换只在空闲态（运行中换预设无意义且易误读状态行）
    btnBatch->setEnabled(!on);// 批处理入口（FR-10）：对话框内启动键已随下行同锁，入口一并锁
    btnCancel->setEnabled(on);
    btnWsChange->setEnabled(!on);// 换根只对后续任务生效，任务期间不换（worker 单线程排队会延迟生效）
    btnHistory->setEnabled(!on && !curFile.isEmpty());// 查询排 worker 队，任务期间点了也要等任务跑完，干脆锁住
    btnModels->setEnabled(!on);// 对话框入口保留可用意义不大：对话框内按钮已随下行同锁，入口一并锁省半开状态
    if (modelDlg) {// 模型管理对话框同锁（busy 涵盖七槽任务与模型下载/导入）：操作键不可用、取消可用
        btnModelDownload->setEnabled(!on);
        btnModelImport->setEnabled(!on);
        btnModelOpenDir->setEnabled(!on);
        btnModelRootChange->setEnabled(!on);
        cmbModelEndpoint->setEnabled(!on);
        btnModelCancel->setEnabled(on);
        modelBar->setVisible(on);
        modelBar->setValue(0);
    }
    if (batchDlg) {// 批处理对话框同锁（FR-10 D5：busy 整批一锁 = 七槽/管线/模型操作同一把锁）
        cmbBatchAction->setEnabled(!on);
        btnBatchAdd->setEnabled(!on);
        btnBatchRemove->setEnabled(!on);
        btnBatchClear->setEnabled(!on);
        btnBatchStart->setEnabled(!on);
    }
    bar->setVisible(on);
    bar->setValue(0);
}
