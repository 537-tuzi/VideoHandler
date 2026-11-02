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
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "service/dspdenoiser.h"// HumBase 制式枚举

MainWidget::MainWidget(QWidget* parent)
    : QWidget(parent), media(nullptr), curFile(), workspaceRoot(),
      btnOpen(nullptr), btnAudio(nullptr), btnVideo(nullptr),
      btnDenoise(nullptr), btnAi(nullptr), btnPitch(nullptr), btnSep(nullptr), btnInstrumental(nullptr), btnCancel(nullptr),
      btnWsChange(nullptr), btnHistory(nullptr),
      chkHum(nullptr), cmbHumBase(nullptr), chkWind(nullptr), spinSemi(nullptr),
      lblFile(nullptr), lblWs(nullptr), lblStatus(nullptr), txtInfo(nullptr), bar(nullptr)
{
    build_ui();
    init_connect();
}

void MainWidget::build_ui() {
    btnOpen = new QPushButton(QStringLiteral("选择文件"), this);
    btnAudio = new QPushButton(QStringLiteral("提取音频"), this);
    btnVideo = new QPushButton(QStringLiteral("提取无声视频"), this);
    btnDenoise = new QPushButton(QStringLiteral("DSP降噪导出WAV"), this);
    btnAi = new QPushButton(QStringLiteral("AI降噪导出WAV"), this);
    btnPitch = new QPushButton(QStringLiteral("变调导出WAV"), this);
    btnSep = new QPushButton(QStringLiteral("人声分离导出WAV"), this);
    btnInstrumental = new QPushButton(QStringLiteral("伴奏提取导出WAV"), this);
    btnCancel = new QPushButton(QStringLiteral("取消任务"), this);
    chkHum = new QCheckBox(QStringLiteral("除电流声"), this);
    cmbHumBase = new QComboBox(this);
    cmbHumBase->addItem(QStringLiteral("50Hz"));
    cmbHumBase->addItem(QStringLiteral("60Hz"));// 电网制式：50/60Hz 陷波基频
    cmbHumBase->setCurrentIndex(QSettings().value(QStringLiteral("humBaseHz"), 50).toInt() == 60 ? 1 : 0);// 记住上次选择，缺省 50Hz
    cmbHumBase->setEnabled(true);// chkHum 缺省勾选；随勾选状态联动见 init_connect
    chkWind = new QCheckBox(QStringLiteral("除风声(低频)"), this);
    chkHum->setChecked(true);
    chkWind->setChecked(true);
    spinSemi = new QDoubleSpinBox(this);
    spinSemi->setRange(-12.0, 12.0);
    spinSemi->setSingleStep(1.0);
    spinSemi->setDecimals(1);
    spinSemi->setValue(0.0);
    spinSemi->setSuffix(QStringLiteral(" 半音"));
    lblFile = new QLabel(QStringLiteral("未选择文件"), this);
    // 工作区根（FR-A4 片 B，D5/OQ-1）：QSettings 只在此读（GUI 线程，D7），缺省 ~/VideoHandler-Workspace
    workspaceRoot = QSettings().value(QString::fromUtf8(kSettingsWorkspaceRoot),
                                      QDir::home().filePath(QString::fromUtf8(kWsDefaultRootName))).toString();
    if (workspaceRoot.isEmpty())// 键存过空串（手改 QSettings 等）→ 归一缺省，消除「任务 direct 而历史查询报 root 不能为空」两副面孔
        workspaceRoot = QDir::home().filePath(QString::fromUtf8(kWsDefaultRootName));
    lblWs = new QLabel(QStringLiteral("工作区：%1").arg(workspaceRoot), this);
    btnWsChange = new QPushButton(QStringLiteral("修改"), this);
    btnHistory = new QPushButton(QStringLiteral("处理历史"), this);
    lblStatus = new QLabel(QStringLiteral("就绪"), this);
    txtInfo = new QPlainTextEdit(this);
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
    btnCancel->setEnabled(false);
    btnHistory->setEnabled(false);// wsInfo 契约 = root+input，未载入文件时无查询对象
    bar->hide();// 没任务时不占位

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(btnOpen);
    topRow->addWidget(lblFile, 1);
    auto* wsRow = new QHBoxLayout;// 工作区根展示/可改 + 处理历史入口（D5 最小 UI）
    wsRow->addWidget(lblWs, 1);
    wsRow->addWidget(btnWsChange);
    wsRow->addWidget(btnHistory);
    auto* actRow = new QHBoxLayout;
    actRow->addWidget(btnAudio);
    actRow->addWidget(btnVideo);
    auto* denRow = new QHBoxLayout;
    denRow->addWidget(chkHum);
    denRow->addWidget(cmbHumBase);
    denRow->addWidget(chkWind);
    denRow->addWidget(btnDenoise);
    denRow->addWidget(btnAi);
    auto* pitchRow = new QHBoxLayout;
    pitchRow->addWidget(spinSemi);
    pitchRow->addWidget(btnPitch);
    pitchRow->addWidget(btnSep);
    pitchRow->addWidget(btnInstrumental);
    pitchRow->addWidget(btnCancel);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(topRow);
    lay->addLayout(wsRow);
    lay->addWidget(txtInfo);
    lay->addLayout(actRow);
    lay->addLayout(denRow);
    lay->addLayout(pitchRow);
    lay->addWidget(bar);
    lay->addWidget(lblStatus);
}

void MainWidget::init_connect() {
    connect(btnOpen, &QPushButton::clicked, this, &MainWidget::doOpen);
    connect(btnWsChange, &QPushButton::clicked, this, &MainWidget::doChangeWorkspace);
    connect(btnHistory, &QPushButton::clicked, this, &MainWidget::doHistory);
    connect(btnAudio, &QPushButton::clicked, this, &MainWidget::doExtractAudio);
    connect(btnVideo, &QPushButton::clicked, this, &MainWidget::doExtractVideo);
    connect(btnDenoise, &QPushButton::clicked, this, &MainWidget::doDenoiseWav);
    connect(btnAi, &QPushButton::clicked, this, &MainWidget::doDenoiseAiWav);
    connect(btnPitch, &QPushButton::clicked, this, &MainWidget::doPitchWav);
    connect(btnSep, &QPushButton::clicked, this, &MainWidget::doSepVocals);
    connect(btnInstrumental, &QPushButton::clicked, this, &MainWidget::doSepInstrumental);
    connect(btnCancel, &QPushButton::clicked, this, &MainWidget::doCancel);
    connect(chkHum, &QCheckBox::toggled, cmbHumBase, &QComboBox::setEnabled);// 不除电流声时制式选择无效
    connect(cmbHumBase, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QSettings().setValue(QStringLiteral("humBaseHz"), idx == 1 ? 60 : 50);// 换制式即记住，下次启动还原
    });
}

void MainWidget::setMediaService(MediaService* svc) {
    media = svc;
    if (media) {
        connect(media, &MediaService::progress, this, &MainWidget::onProgress);
        connect(media, &MediaService::jobFinished, this, &MainWidget::onJobDone);
        connect(media, &MediaService::probeFinished, this, &MainWidget::onProbeDone);
        connect(media, &MediaService::wsInfoFinished, this, &MainWidget::onWsInfoDone);
        media->setWorkspaceRoot(workspaceRoot);// 启动即下传（QSettings 只在本层读，D7）
    }
}

void MainWidget::openFile(const QString& path) {
    curFile = path;
    lblFile->setText(QFileInfo(path).fileName());
    if (!taskRunning) {// 任务运行中不解禁：wsInfo 迟到弹窗里「用此版本继续」不得击穿 busy 锁（按钮由 set_busy(false) 统一恢复），probe 排队任务后照常执行
        btnAudio->setEnabled(true);
        btnVideo->setEnabled(true);
        btnDenoise->setEnabled(true);
        btnAi->setEnabled(true);
        btnPitch->setEnabled(true);
        btnSep->setEnabled(true);
        btnInstrumental->setEnabled(true);
        btnHistory->setEnabled(true);
        txtInfo->setPlainText(QStringLiteral("正在读取流信息…"));
        lblStatus->setText(QStringLiteral("读取中…"));
    }
    if (media) media->probe(path);
    else { txtInfo->setPlainText(QStringLiteral("服务未初始化")); lblStatus->setText(QStringLiteral("就绪")); }
}

void MainWidget::doOpen() {
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择音视频文件"), QString(),
        QStringLiteral("媒体文件 (*.mp4 *.mkv *.mov *.avi *.flv *.webm *.ts *.m4a *.mp3 *.wav *.aac *.flac *.ogg *.opus);;所有文件 (*)"));
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

void MainWidget::doHistory() {
    if (!media || curFile.isEmpty()) return;
    lblStatus->setText(QStringLiteral("正在查询处理历史…"));// 首查大文件哈希可能数秒（worker 线程，UI 不冻）
    media->wsInfo(curFile);
}

void MainWidget::onProbeDone(bool ok, const QString& info) {
    txtInfo->setPlainText(info);
    lblStatus->setText(ok ? QStringLiteral("已载入，可执行分离") : QStringLiteral("读取失败"));
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

void MainWidget::doExtractAudio() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("正在提取音频…"));
    media->extractAudio(curFile);
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
    media->denoiseWav(curFile, humBase, chkWind->isChecked());
}

void MainWidget::doDenoiseAiWav() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("AI 降噪中…"));
    media->denoiseAiWav(curFile);
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
    media->pitchWav(curFile, semi);
}

void MainWidget::doSepVocals() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("人声分离中（CPU 推理较慢）…"));
    media->sepVocals(curFile, false);
}

void MainWidget::doSepInstrumental() {
    if (!media || curFile.isEmpty()) return;
    set_busy(true);
    lblStatus->setText(QStringLiteral("伴奏提取中（CPU 推理较慢）…"));
    media->sepVocals(curFile, true);
}

void MainWidget::doCancel() {
    if (media) media->cancelJob();
}

void MainWidget::onProgress(int pct) {
    bar->setValue(pct);
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
    btnCancel->setEnabled(on);
    btnWsChange->setEnabled(!on);// 换根只对后续任务生效，任务期间不换（worker 单线程排队会延迟生效）
    btnHistory->setEnabled(!on && !curFile.isEmpty());// 查询排 worker 队，任务期间点了也要等任务跑完，干脆锁住
    bar->setVisible(on);
    bar->setValue(0);
}
