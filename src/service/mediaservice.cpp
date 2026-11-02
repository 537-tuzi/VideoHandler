#include "mediaservice.h"
#include "dspdenoiser.h"
#include "ffextractor.h"

#include "model/data.h"

#include <QMetaType>

MediaService::MediaService(QObject* parent)
    : QObject(parent), busy(false), cancelFlag(false),
      thread(new QThread(this)), worker(new FFExtractor)// 无父对象供跨线程迁移
{
    qRegisterMetaType<HumBase>();// denoiseRequested 队列投递到后台线程，enum 须注册元类型否则静默投递失败
    worker->setCancelFlag(&cancelFlag);
    worker->moveToThread(thread);
    thread->start();
    init_connect();
}

MediaService::~MediaService() {
    cancelFlag = true;// 先请求取消，否则 wait() 要等长任务自然跑完，关窗假死
    thread->quit();
    thread->wait();
    delete worker;// 线程已停，直接删安全
}

void MediaService::init_connect() {
    connect(this, &MediaService::audioRequested, worker, &FFExtractor::doExtractAudio);
    connect(this, &MediaService::videoRequested, worker, &FFExtractor::doExtractVideo);
    connect(this, &MediaService::probeRequested, worker, &FFExtractor::doProbe);
    connect(this, &MediaService::denoiseRequested, worker, &FFExtractor::doDenoiseWav);
    connect(this, &MediaService::aiDenoiseRequested, worker, &FFExtractor::doDenoiseAiWav);
    connect(this, &MediaService::pitchRequested, worker, &FFExtractor::doPitchWav);
    connect(this, &MediaService::sepVocalsRequested, worker, &FFExtractor::doSepVocals);
    connect(this, &MediaService::workspaceRootRequested, worker, &FFExtractor::setWorkspaceRoot);// FR-A4 片 B：根注入（装配期 setCancelFlag 同型，队列投递到 worker 线程）
    connect(this, &MediaService::wsInfoRequested, worker, &FFExtractor::doWsInfo);// 处理历史查询（probe 同型）
    connect(worker, &FFExtractor::progress, this, &MediaService::progress);
    connect(worker, &FFExtractor::jobDone, this, [this](bool ok, const QString& message) {
        busy = false;
        cancelFlag = false;
        emit jobFinished(ok, message);
    });
    connect(worker, &FFExtractor::probeDone, this, &MediaService::probeFinished);
    connect(worker, &FFExtractor::wsInfoDone, this, &MediaService::wsInfoFinished);
}

void MediaService::cancelJob() {
    cancelFlag = true;
}

void MediaService::setWorkspaceRoot(const QString& root) {
    emit workspaceRootRequested(root);// 排队进后台线程（worker 据此给任务请求组 workspace 键）
}

void MediaService::wsInfo(const QString& input) {
    emit wsInfoRequested(input);// 只读查询排队进后台线程，不占 busy（首查大文件哈希数秒不冻 UI）
}

void MediaService::probe(const QString& inPath) {
    emit probeRequested(inPath);// 排队进后台线程，主线程不碰 FFmpeg
}

void MediaService::extractAudio(const QString& inPath) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit audioRequested(inPath);// 队列投递到后台线程
}

void MediaService::extractVideo(const QString& inPath) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit videoRequested(inPath);// 队列投递到后台线程
}

void MediaService::denoiseAiWav(const QString& inPath) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit aiDenoiseRequested(inPath);// 队列投递到后台线程
}

void MediaService::pitchWav(const QString& inPath, double semiTones) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit pitchRequested(inPath, semiTones);// 队列投递到后台线程
}

void MediaService::sepVocals(const QString& inPath, bool instrumental) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit sepVocalsRequested(inPath, instrumental);// 队列投递到后台线程
}

void MediaService::denoiseWav(const QString& inPath, HumBase humBase, bool removeWind) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit denoiseRequested(inPath, humBase, removeWind);// 队列投递到后台线程
}
