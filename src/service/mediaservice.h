#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>

class FFExtractor;

// 媒体服务：调度后台线程执行分离/降噪任务并转发进度
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
    void denoiseRequested(const QString& inPath, bool removeHum, bool removeWind);// 降噪任务请求
    void aiDenoiseRequested(const QString& inPath);// AI 降噪任务请求
    void pitchRequested(const QString& inPath, double semiTones);// 变调任务请求
    void sepVocalsRequested(const QString& inPath, bool instrumental);// 人声分离/伴奏提取任务请求
    void progress(int pct);// 任务进度百分比
    void jobFinished(bool ok, const QString& message);// 任务完成通知
    void probeFinished(bool ok, const QString& info);// 文件探测完成
public slots:
    void probe(const QString& inPath);// 提交文件探测任务（异步，结果走信号）
    void extractAudio(const QString& inPath);// 提交音轨提取任务
    void extractVideo(const QString& inPath);// 提交无声视频提取任务
    void denoiseWav(const QString& inPath, bool removeHum, bool removeWind);// 提交降噪任务，输出到源目录
    void denoiseAiWav(const QString& inPath);// 提交 AI 降噪任务，输出到源目录
    void pitchWav(const QString& inPath, double semiTones);// 提交变调任务，输出到源目录
    void sepVocals(const QString& inPath, bool instrumental);// 提交人声分离/伴奏提取任务，输出到源目录
};
