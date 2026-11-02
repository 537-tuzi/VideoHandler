#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>

#include "errinfo.h"

enum class HumBase;// hum 基频制式（定义在 dspdenoiser.h，槽参数用）

// 结构化探测结果：probeText（GUI 中文渲染）与命令层 JSON result 各取所需，单一 FFmpeg 遍历实现
struct ProbeStream {
    int index = -1;
    QString mediaType;// "video" / "audio" / "other"
    QString codec;
    int width = 0, height = 0;// 视频流才有
    int sampleRate = 0, channels = 0;// 音频流才有
};

struct ProbeInfo {
    QString container;
    qint64 sizeBytes = 0;
    qint64 durationUs = 0;// AV_TIME_BASE 微秒；<=0 未知
    QList<ProbeStream> streams;
};

// FFmpeg 任务执行者：提取/探测静态核心供命令层复用，slots 为 GUI worker 壳——组 JSON 请求走
// CommandLayer::execute（FR-24 片 D），取消/进度经 Ctx 注入，响应拆包重建 GUI 文案（逐字不变）
class FFExtractor : public QObject
{
    Q_OBJECT
public:
    explicit FFExtractor(QObject* parent = nullptr);
    void setCancelFlag(std::atomic<bool>* flag);// 初始化取消标志（由服务装配）
    static bool probe(const QString& inPath, QString* info);// 探测文件流信息（中文渲染文本），false 时 info 为错误说明；槽/GUI 兼容面
    static bool probeInfo(const QString& inPath, ProbeInfo* info, QString* err);// 结构化探测，false 时 err 为错误说明
    static QString probeText(const ProbeInfo& info);// 结构化信息 → 现有中文渲染（逐字平移，GUI 展示不变）
    static QString extractAudio(const QString& inPath, ErrInfo* err, const std::function<void(int)>& onProgress = {},
                                const std::atomic<bool>* cancel = nullptr,
                                const QString& outPath = {});// 提取音轨，成功返回输出路径；outPath 空 = 源目录自动命名
    static QString extractVideo(const QString& inPath, ErrInfo* err, const std::function<void(int)>& onProgress = {},
                                const std::atomic<bool>* cancel = nullptr,
                                const QString& outPath = {});// 提取无声视频；outPath 空 = 源目录自动命名
private:
    void runRequest(const QJsonObject& req, const QString& okMsg);// 统一命令层收尾：请求交 execute（取消/进度经 Ctx 注入），响应拆包 → jobDone（成功文案由壳重建，spec D6）
    std::atomic<bool>* cancelFlag = nullptr;// 服务装配的取消标志，经 Ctx 透传给命令层分块循环检查
signals:
    void progress(int pct);// 任务进度百分比
    void jobDone(bool ok, const QString& message);// 任务完成通知
    void probeDone(bool ok, const QString& info);// 文件探测完成
public slots:
    void doExtractAudio(const QString& inPath);// 后台提取音轨
    void doExtractVideo(const QString& inPath);// 后台提取无声视频
    void doProbe(const QString& inPath);// 后台探测文件流信息
    void doDenoiseWav(const QString& inPath, HumBase humBase, bool removeWind);// 后台降噪转码
    void doDenoiseAiWav(const QString& inPath);// 后台 AI 降噪转码（GTCRN）
    void doPitchWav(const QString& inPath, double semiTones);// 后台变调转码（SoundTouch）
    void doSepVocals(const QString& inPath, bool instrumental);// 后台人声分离/伴奏提取转码（MDX-Net）
};
