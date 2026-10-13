#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>

class IStreamEngine;

// FFmpeg 任务执行者：分离/转码/降噪的静态核心供 CLI 复用，slots 供后台线程调度
class FFExtractor : public QObject
{
    Q_OBJECT
public:
    explicit FFExtractor(QObject* parent = nullptr);
    void setCancelFlag(std::atomic<bool>* flag);// 初始化取消标志（由服务装配）
    static bool probe(const QString& inPath, QString* info);// 探测文件流信息，false 时 info 为错误说明
    static QString extractAudio(const QString& inPath, QString* err, const std::function<void(int)>& onProgress = {});// 提取音轨，成功返回输出路径
    static QString extractVideo(const QString& inPath, QString* err, const std::function<void(int)>& onProgress = {});// 提取无声视频，成功返回输出路径
private:
    void runJob(const QString& inPath, IStreamEngine& engine, const char* suffix, const QString& okMsg);// 统一流式引擎任务收尾
    std::atomic<bool>* cancelFlag = nullptr;// 服务装配的取消标志，转码分块循环检查
signals:
    void progress(int pct);// 任务进度百分比
    void jobDone(bool ok, const QString& message);// 任务完成通知
    void probeDone(bool ok, const QString& info);// 文件探测完成
public slots:
    void doExtractAudio(const QString& inPath);// 后台提取音轨
    void doExtractVideo(const QString& inPath);// 后台提取无声视频
    void doProbe(const QString& inPath);// 后台探测文件流信息
    void doDenoiseWav(const QString& inPath, bool removeHum, bool removeWind);// 后台降噪转码
    void doDenoiseAiWav(const QString& inPath);// 后台 AI 降噪转码（GTCRN）
    void doPitchWav(const QString& inPath, double semiTones);// 后台变调转码（SoundTouch）
    void doSepVocals(const QString& inPath, bool instrumental);// 后台人声分离/伴奏提取转码（MDX-Net）
};
