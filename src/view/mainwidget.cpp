#include "mainwidget.h"
#include "service/mediaservice.h"

#include "model/data.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "service/dspdenoiser.h"// HumBase 制式枚举

MainWidget::MainWidget(QWidget* parent)
    : QWidget(parent), media(nullptr), curFile(),
      btnOpen(nullptr), btnAudio(nullptr), btnVideo(nullptr),
      btnDenoise(nullptr), btnAi(nullptr), btnPitch(nullptr), btnSep(nullptr), btnInstrumental(nullptr), btnCancel(nullptr),
      chkHum(nullptr), cmbHumBase(nullptr), chkWind(nullptr), spinSemi(nullptr),
      lblFile(nullptr), lblStatus(nullptr), txtInfo(nullptr), bar(nullptr)
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
    bar->hide();// 没任务时不占位

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(btnOpen);
    topRow->addWidget(lblFile, 1);
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
    lay->addWidget(txtInfo);
    lay->addLayout(actRow);
    lay->addLayout(denRow);
    lay->addLayout(pitchRow);
    lay->addWidget(bar);
    lay->addWidget(lblStatus);
}

void MainWidget::init_connect() {
    connect(btnOpen, &QPushButton::clicked, this, &MainWidget::doOpen);
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
    }
}

void MainWidget::doOpen() {
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择音视频文件"), QString(),
        QStringLiteral("媒体文件 (*.mp4 *.mkv *.mov *.avi *.flv *.webm *.ts *.m4a *.mp3 *.wav *.aac *.flac *.ogg *.opus);;所有文件 (*)"));
    if (file.isEmpty()) return;
    curFile = file;
    lblFile->setText(QFileInfo(file).fileName());
    btnAudio->setEnabled(true);
    btnVideo->setEnabled(true);
    btnDenoise->setEnabled(true);
    btnAi->setEnabled(true);
    btnPitch->setEnabled(true);
    btnSep->setEnabled(true);
    btnInstrumental->setEnabled(true);
    txtInfo->setPlainText(QStringLiteral("正在读取流信息…"));
    lblStatus->setText(QStringLiteral("读取中…"));
    if (media) media->probe(file);
    else { txtInfo->setPlainText(QStringLiteral("服务未初始化")); lblStatus->setText(QStringLiteral("就绪")); }
}

void MainWidget::onProbeDone(bool ok, const QString& info) {
    txtInfo->setPlainText(info);
    lblStatus->setText(ok ? QStringLiteral("已载入，可执行分离") : QStringLiteral("读取失败"));
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
    btnOpen->setEnabled(!on);
    btnAudio->setEnabled(!on);
    btnVideo->setEnabled(!on);
    btnDenoise->setEnabled(!on);
    btnAi->setEnabled(!on);
    btnPitch->setEnabled(!on);
    btnSep->setEnabled(!on);
    btnInstrumental->setEnabled(!on);
    btnCancel->setEnabled(on);
    bar->setVisible(on);
    bar->setValue(0);
}
