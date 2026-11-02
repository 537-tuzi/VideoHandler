#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>

class FFExtractor;
enum class HumBase;// hum 基频制式（定义在 dspdenoiser.h，信号参数用）

// 媒体服务：调度后台线程执行分离/降噪任务并转发进度。
// FR-A4 片 B：工作区根经 setWorkspaceRoot 队列转发 worker（七对既有业务信号槽签名零改）；
// 处理历史查询 wsInfo 与 probe 同型（只读不占 busy），大文件首查哈希在 worker 线程不冻 UI
class MediaService : public QObject
{
    Q_OBJECT
public:
    explicit MediaService(QObject* parent = nullptr);
    ~MediaService() override;
    void cancelJob();// 请求取消当前任务
private:
    void init_connect();// 集中连接信号槽
    bool busy;// 任务进行中标记，防并发提交
    std::atomic<bool> cancelFlag;// 任务取消标志，worker 分块循环检查
    QThread* thread;// 后台工作线程
    FFExtractor* worker;// 任务执行者，无父对象供跨线程迁移
signals:
    void audioRequested(const QString& inPath);// 音轨任务请求
    void videoRequested(const QString& inPath);// 无声视频任务请求
    void probeRequested(const QString& inPath);// 文件探测请求
    void denoiseRequested(const QString& inPath, HumBase humBase, bool removeWind);// 降噪任务请求
    void aiDenoiseRequested(const QString& inPath);// AI 降噪任务请求
    void pitchRequested(const QString& inPath, double semiTones);// 变调任务请求
    void sepVocalsRequested(const QString& inPath, bool instrumental);// 人声分离/伴奏提取任务请求
    void workspaceRootRequested(const QString& root);// 工作区根注入请求（FR-A4 片 B，队列转发 worker）
    void wsInfoRequested(const QString& input);// 处理历史查询请求（FR-A4 片 B，只读不占 busy）
    void progress(int pct);// 任务进度百分比
    void jobFinished(bool ok, const QString& message);// 任务完成通知
    void probeFinished(bool ok, const QString& info);// 文件探测完成
    void wsInfoFinished(bool ok, const QString& json);// 处理历史查询完成（ok 时 json 为 result 的 Compact JSON）
public slots:
    void setWorkspaceRoot(const QString& root);// 注入工作区根到 worker（GUI 线程读 QSettings 后调用，D7 边界）
    void wsInfo(const QString& input);// 提交处理历史查询（异步，结果走信号，probe 同型不占 busy）
    void probe(const QString& inPath);// 提交文件探测任务（异步，结果走信号）
    void extractAudio(const QString& inPath);// 提交音轨提取任务
    void extractVideo(const QString& inPath);// 提交无声视频提取任务
    void denoiseWav(const QString& inPath, HumBase humBase, bool removeWind);// 提交降噪任务，输出到工作区版本目录（根未注入时落源目录）
    void denoiseAiWav(const QString& inPath);// 提交 AI 降噪任务，输出到工作区版本目录（根未注入时落源目录）
    void pitchWav(const QString& inPath, double semiTones);// 提交变调任务，输出到工作区版本目录（根未注入时落源目录）
    void sepVocals(const QString& inPath, bool instrumental);// 提交人声分离/伴奏提取任务，输出到工作区版本目录（根未注入时落源目录）
};
