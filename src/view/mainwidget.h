#pragma once

// 主窗口：文件导入 + 音视频分离操作面板 + 工作区根展示/修改与处理历史（FR-A4 片 B）+
// 模型管理对话框（FR-8 片 B）+ 管线一键（FR-9：静态预设三条下拉 + 一键执行）。
// QSettings 只在本层（GUI 线程）读写（D7 边界）：工作区根读入后经 MediaService 下传 worker，
// 七槽任务缺省走工作区（无开关硬切，OQ-2 拍板），历史对话框数据来自 worker 线程的 wsInfo 查询；
// 模型库根/下载源 endpoint 同型承载（FR-8 D1/D4：modelRoot/modelEndpoint 两键，缺省根
// ~/VideoHandler-Models、缺省源 huggingface.co）；模型下载/导入占 busy（对话框内进度条与
// 主进度条同源联动，spec D8）；管线预设只定链形，denoise/pitch 步动态参数组包时取当前面板

#include <QWidget>
#include <QString>

class QPushButton;
class QCheckBox;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QTreeWidget;
class MediaService;

class MainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MainWidget(QWidget* parent = nullptr);
    void setMediaService(MediaService* svc);// 初始化媒体服务（组合根装配）
public slots:
    void openModelDialog();// 打开模型管理对话框（非模态，按需创建；「模型管理」按钮与 modelMissing 引导共用入口，FR-8 D8）
    void doOpen();// 选文件并异步刷新流信息
    void doChangeWorkspace();// 修改工作区根并下传 worker
    void doChangeModelRoot();// 修改模型库根并下传 worker（FR-8 片 B）
    void doHistory();// 查询当前文件的处理历史（worker 线程，结果走 onWsInfoDone）
    void doExtractAudio();// 提交音轨提取
    void doExtractVideo();// 提交无声视频提取
    void doDenoiseWav();// 提交降噪转码
    void doDenoiseAiWav();// 提交 AI 降噪转码
    void doPitchWav();// 提交变调转码
    void doSepVocals();// 提交人声分离转码
    void doSepInstrumental();// 提交伴奏提取转码
    void doPipeline();// 提交管线一键（FR-9：预设链形 + 面板动态参数组 stepsJson，worker 侧模型步补缺省）
    void doCancel();// 请求取消当前任务
    void onProgress(int pct);// 刷新进度条
    void onJobDone(bool ok, const QString& msg);// 任务结果落地
    void onProbeDone(bool ok, const QString& info);// 探测结果落地
    void onWsInfoDone(bool ok, const QString& json);// 处理历史查询落地（渲染版本清单对话框）
    void onModelListDone(bool ok, const QString& json);// 模型列表落地（渲染模型管理对话框表格）
    void onModelOpDone(bool ok, const QString& message);// 模型下载/导入落地（放锁 + 刷新列表）
    void onModelMissing(const QString& forVerb);// 缺模型引导：问询打开模型管理对话框（D8/OQ-7，结构化信号触发；文案带用途——管线链 forVerb 取 error.stepCmd，FR-9 附裁 1）
private:
    void build_ui();// 搭界面
    void buildModelDialog();// 搭模型管理对话框（首次打开创建一次：列表/下载/导入/进度/endpoint/库根）
    void refreshModels();// 发起模型列表刷新（modelList 只读查询，worker 线程回传 onModelListDone）
    void submitModelImport(const QString& path);// 用途选择 + 许可提示对话框后提交导入（doModelImportFromDialog 选完文件走此，单一提交路径）
    void init_connect();// 集中连接信号槽
    void set_busy(bool on);// 任务期间锁按钮并显隐进度条
    void openFile(const QString& path);// 载入文件并异步刷新流信息（doOpen 与「用此版本继续」共用，OQ-3）
    MediaService* media;// 媒体服务，组合根 setter 初始化
    QString curFile;// 当前已载入文件路径
    QString workspaceRoot;// 工作区根（QSettings 读入，缺省 ~/VideoHandler-Workspace，FR-A4 OQ-1）
    QString modelRoot;// 模型库根（QSettings 读入，缺省 ~/VideoHandler-Models，FR-8 OQ-3；经 setModelRoot 下传 worker 供两级缺省解析与模型族请求）
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
    QPushButton* btnModels;// 模型管理入口（FR-8 片 B）
    QCheckBox* chkHum;
    QComboBox* cmbHumBase;// hum 陷波基频制式（50/60Hz，勾选除电流声时使能）
    QCheckBox* chkWind;
    QDoubleSpinBox* spinSemi;
    QComboBox* cmbPipeline;// 管线预设下拉（FR-9，三条静态预设）
    QPushButton* btnPipeline;// 一键执行管线（FR-9，占 busy；成功文案「管线完成：<终产物路径>」）
    QLabel* lblFile;
    QLabel* lblWs;// 工作区根展示
    QLabel* lblStatus;
    QPlainTextEdit* txtInfo;
    QProgressBar* bar;
    QDialog* modelDlg = nullptr;// 模型管理对话框（非模态，首次打开创建后复用；关闭仅隐藏不销毁）
    QTreeWidget* modelTree = nullptr;// 模型列表（id/用途/状态/许可证/大小，数据 = modelList result）
    QProgressBar* modelBar = nullptr;// 对话框内下载进度条（与主进度条同源联动）
    QPushButton* btnModelDownload = nullptr;// 下载选中模型（占 busy 可取消）
    QPushButton* btnModelImport = nullptr;// 导入模型（QFileDialog + 用途选择 + 许可提示，D6/§161）
    QPushButton* btnModelCancel = nullptr;// 取消下载/导入（复用服务取消通道）
    QPushButton* btnModelOpenDir = nullptr;// 打开模型库目录（文件管理器）
    QPushButton* btnModelRootChange = nullptr;// 修改模型库根（QSettings 持久化 + 下传 worker）
    QComboBox* cmbModelEndpoint = nullptr;// 下载源 endpoint 下拉（缺省 huggingface.co，QSettings 记忆，D4）
    QLabel* lblModelRoot = nullptr;// 模型库根展示
private slots:
    void doModelDownloadFromDialog();// 下载当前选中模型（endpoint 取下拉当前值）
    void doModelImportFromDialog();// 导入模型文件（选文件 → 选用途 → 提交，自由导入形态）
};
