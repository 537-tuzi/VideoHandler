#include "mediaservice.h"
#include "ffextractor.h"

#include "model/data.h"

MediaService::MediaService(QObject* parent)
    : QObject(parent), busy(false), cancelFlag(false),
      thread(new QThread(this)), worker(new FFExtractor)// 无父对象供跨线程迁移
{
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
    connect(worker, &FFExtractor::progress, this, &MediaService::progress);
    connect(worker, &FFExtractor::jobDone, this, [this](bool ok, const QString& message) {
        busy = false;
        cancelFlag = false;
        emit jobFinished(ok, message);
    });
    connect(worker, &FFExtractor::probeDone, this, &MediaService::probeFinished);
}

void MediaService::cancelJob() {
    cancelFlag = true;
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

void MediaService::denoiseWav(const QString& inPath, bool removeHum, bool removeWind) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit denoiseRequested(inPath, removeHum, removeWind);// 队列投递到后台线程
}
