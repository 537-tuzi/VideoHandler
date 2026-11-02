#pragma once

#include <QWidget>
#include <QString>

class QPushButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class MediaService;

// 主窗口：文件导入 + 音视频分离操作面板 + 工作区根展示/修改与处理历史（FR-A4 片 B）。
// QSettings 只在本层（GUI 线程）读写（D7 边界）：工作区根读入后经 MediaService 下传 worker，
// 七槽任务缺省走工作区（无开关硬切，OQ-2 拍板），历史对话框数据来自 worker 线程的 wsInfo 查询
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
    void openFile(const QString& path);// 载入文件并异步刷新流信息（doOpen 与「用此版本继续」共用，OQ-3）
    MediaService* media;// 媒体服务，组合根 setter 初始化
    QString curFile;// 当前已载入文件路径
    QString workspaceRoot;// 工作区根（QSettings 读入，缺省 ~/VideoHandler-Workspace，FR-A4 OQ-1）
    bool taskRunning = false;// 任务运行标志（set_busy 维护；openFile/onWsInfoDone 据此避免迟到交互击穿 busy 锁）
    QPushButton* btnOpen;
    QPushButton* btnAudio;
    QPushButton* btnVideo;
    QPushButton* btnDenoise;
    QPushButton* btnAi;
    QPushButton* btnPitch;
    QPushButton* btnSep;
    QPushButton* btnInstrumental;
    QPushButton* btnCancel;
    QPushButton* btnWsChange;// 修改工作区根（QFileDialog + QSettings 持久化）
    QPushButton* btnHistory;// 处理历史（wsInfo 查询 → 版本清单对话框），无文件时禁用
    QCheckBox* chkHum;
    QComboBox* cmbHumBase;// hum 陷波基频制式（50/60Hz，勾选除电流声时使能）
    QCheckBox* chkWind;
    QDoubleSpinBox* spinSemi;
    QLabel* lblFile;
    QLabel* lblWs;// 工作区根展示
    QLabel* lblStatus;
    QPlainTextEdit* txtInfo;
    QProgressBar* bar;
public slots:
    void doOpen();// 选文件并异步刷新流信息
    void doChangeWorkspace();// 修改工作区根并下传 worker
    void doHistory();// 查询当前文件的处理历史（worker 线程，结果走 onWsInfoDone）
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
    void onWsInfoDone(bool ok, const QString& json);// 处理历史查询落地（渲染版本清单对话框）
};
