#pragma once

#include <QWidget>
#include <QString>

class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class MediaService;

// 主窗口：文件导入 + 音视频分离操作面板
class MainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MainWidget(QWidget* parent = nullptr);
    void setMediaService(MediaService* svc);// 初始化媒体服务（组合根装配）
private:
    void build_ui();// 搭界面
    void init_connect();// 集中连接信号槽
    void set_busy(bool on);// 任务期间锁按钮并显隐进度条
    MediaService* media;// 媒体服务，组合根 setter 初始化
    QString curFile;// 当前已载入文件路径
    QPushButton* btnOpen;
    QPushButton* btnAudio;
    QPushButton* btnVideo;
    QPushButton* btnDenoise;
    QPushButton* btnAi;
    QPushButton* btnPitch;
    QPushButton* btnSep;
    QPushButton* btnInstrumental;
    QPushButton* btnCancel;
    QCheckBox* chkHum;
    QCheckBox* chkWind;
    QDoubleSpinBox* spinSemi;
    QLabel* lblFile;
    QLabel* lblStatus;
    QPlainTextEdit* txtInfo;
    QProgressBar* bar;
public slots:
    void doOpen();// 选文件并异步刷新流信息
    void doExtractAudio();// 提交音轨提取
    void doExtractVideo();// 提交无声视频提取
    void doDenoiseWav();// 提交降噪转码
    void doDenoiseAiWav();// 提交 AI 降噪转码
    void doPitchWav();// 提交变调转码
    void doSepVocals();// 提交人声分离转码
    void doSepInstrumental();// 提交伴奏提取转码
    void doCancel();// 请求取消当前任务
    void onProgress(int pct);// 刷新进度条
    void onJobDone(bool ok, const QString& msg);// 任务结果落地
    void onProbeDone(bool ok, const QString& info);// 探测结果落地
};
