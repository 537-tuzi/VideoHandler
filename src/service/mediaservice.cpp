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
    connect(this, &MediaService::modelRootRequested, worker, &FFExtractor::setModelRoot);// FR-8 片 B：模型库根注入（setWorkspaceRoot 同型）
    connect(this, &MediaService::modelListRequested, worker, &FFExtractor::doModelList);// 模型列表查询（wsInfo 同型只读不占 busy）
    connect(this, &MediaService::modelDownloadRequested, worker, &FFExtractor::doModelDownload);// 模型下载（占 busy，取消/进度经 Ctx）
    connect(this, &MediaService::modelImportRequested, worker, &FFExtractor::doModelImport);// 模型导入（占 busy）
    connect(this, &MediaService::pipelineRequested, worker, &FFExtractor::doPipeline);// 管线一键（FR-9：占 busy，取消/进度经 Ctx 链内透传）
    connect(this, &MediaService::batchRequested, worker, &FFExtractor::doBatch);// 批处理（FR-10：busy 整批一锁，worker 逐件组合 execute）
    connect(worker, &FFExtractor::progress, this, &MediaService::progress);
    connect(worker, &FFExtractor::jobDone, this, [this](bool ok, const QString& message) {
        busy = false;
        cancelFlag = false;
        emit jobFinished(ok, message);
    });
    connect(worker, &FFExtractor::probeDone, this, &MediaService::probeFinished);
    connect(worker, &FFExtractor::probeInfoDone, this, &MediaService::probeInfoFinished);// 结构化探测转发（FR-1：与 probeFinished 同遍双发射，GUI 建轨道下拉）
    connect(worker, &FFExtractor::wsInfoDone, this, &MediaService::wsInfoFinished);
    connect(worker, &FFExtractor::modelListDone, this, &MediaService::modelListFinished);
    connect(worker, &FFExtractor::modelOpDone, this, [this](bool ok, const QString& message) {
        busy = false;// 模型下载/导入与七槽任务共用一把 busy 锁（一次一任务语义涵盖模型操作）
        cancelFlag = false;
        emit modelOpFinished(ok, message);
    });
    connect(worker, &FFExtractor::modelMissing, this, &MediaService::modelMissing);// 结构化引导信号原样转发（D8）
    connect(worker, &FFExtractor::batchFinished, this, [this](bool ok, const QString& jsonOrMessage) {
        busy = false;// 整批结束才释放（FR-10 D5：与七槽/管线/模型操作同一把锁，件间无锁窗）
        cancelFlag = false;
        emit batchFinished(ok, jsonOrMessage);
    });
}

void MediaService::cancelJob() {
    cancelFlag = true;
}

void MediaService::setWorkspaceRoot(const QString& root) {
    emit workspaceRootRequested(root);// 排队进后台线程（worker 据此给任务请求组 workspace 键）
}

void MediaService::setModelRoot(const QString& root) {
    emit modelRootRequested(root);// 排队进后台线程（worker 据此做两级缺省解析与模型族请求组 root 键）
}

void MediaService::wsInfo(const QString& input) {
    emit wsInfoRequested(input);// 只读查询排队进后台线程，不占 busy（首查大文件哈希数秒不冻 UI）
}

void MediaService::modelList() {
    emit modelListRequested();// 只读查询排队进后台线程，不占 busy（wsInfo 先例：下载中也可刷新列表）
}

void MediaService::modelDownload(const QString& id, const QString& endpoint) {
    if (busy) return;
    busy = true;// 长任务占 busy（D5）：任务期间七槽与第二个模型操作均被锁
    cancelFlag = false;
    emit modelDownloadRequested(id, endpoint);// 队列投递到后台线程
}

void MediaService::modelImport(const QString& path, const QString& forVerb) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit modelImportRequested(path, forVerb);// 队列投递到后台线程
}

void MediaService::probe(const QString& inPath) {
    emit probeRequested(inPath);// 排队进后台线程，主线程不碰 FFmpeg
}

void MediaService::extractAudio(const QString& inPath, int track) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit audioRequested(inPath, track);// 队列投递到后台线程（FR-1 track 随行）
}

void MediaService::extractVideo(const QString& inPath) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit videoRequested(inPath);// 队列投递到后台线程
}

void MediaService::denoiseAiWav(const QString& inPath, bool quality, int track) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit aiDenoiseRequested(inPath, quality, track);// 队列投递到后台线程（FR-1 track 随行）
}

void MediaService::pitchWav(const QString& inPath, double semiTones, int track) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit pitchRequested(inPath, semiTones, track);// 队列投递到后台线程（FR-1 track 随行）
}

void MediaService::sepVocals(const QString& inPath, bool instrumental, int track) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit sepVocalsRequested(inPath, instrumental, track);// 队列投递到后台线程（FR-1 track 随行）
}

void MediaService::denoiseWav(const QString& inPath, HumBase humBase, bool removeWind, int track) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit denoiseRequested(inPath, humBase, removeWind, track);// 队列投递到后台线程（FR-1 track 随行）
}

void MediaService::pipeline(const QString& inPath, const QString& stepsJson, int track) {
    if (busy) return;
    busy = true;
    cancelFlag = false;
    emit pipelineRequested(inPath, stepsJson, track);// 队列投递到后台线程（stepsJson 为步骤数组 Compact JSON；FR-1 track=源头文件选轨）
}

void MediaService::batch(const QString& inputsJson, const QString& stepsJson) {
    if (busy) return;
    busy = true;// 整批占 busy 一次（D5）：worker 逐件 execute 期间无 jobDone，件间不存在锁窗
    cancelFlag = false;
    emit batchRequested(inputsJson, stepsJson);// 队列投递到后台线程（两 JSON 均 Compact）
}
