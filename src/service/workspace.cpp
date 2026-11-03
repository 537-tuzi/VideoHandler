#include "workspace.h"

#include "ffextractor.h"
#include "model/data.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

// 工作区版本化核心实现（FR-A4 片 A，spec §2 D1–D3）：登记器 owns 版本目录生命周期——
// plan 占位、commit 原子登记（失败自删）、discard 兜底清理，任一环失败索引不动（两段提交不变式）。

namespace Workspace {
namespace {

constexpr int kHashBufBytes = 64 * 1024;// 流式哈希缓冲（NFR-P4 同口径：禁整文件载入）
constexpr int kStemMaxChars = 48;// 工作区目录名 stem 助读上限（身份在 hash8，超长截断无碰撞风险）

// stem 净化：剔目录分隔符与各平台文件名非法字符（身份本就在 hash8，此处只为助读与跨平台安全）
QString sanitizeStem(const QString& name) {
    QString out;
    out.reserve(std::min<int>(name.size(), kStemMaxChars));
    for (const QChar c : name) {
        const bool ok = c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_');
        out.append(ok ? c : QLatin1Char('_'));
        if (out.size() >= kStemMaxChars) break;
    }
    while (out.endsWith(QLatin1Char('_'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("source") : out;
}

// 身份目录解析（D1 身份=内容哈希）：缓存 miss 时先扫根目录找同 hash8 既有工作区——
// 同内容换路径/换 stem 不另立门户（stem 只是助读，先登记者留名）；无命中才以输入 stem 铸新名。
// hash8 碰撞（32bit）在单根目录量级下可忽略，与目录名含 hash8 的既有取舍同源（spec §8）
QString resolveWsDir(const QString& root, const QString& hash8, const QString& stem) {
    const QStringList hits = QDir(root).entryList(QStringList{QStringLiteral("*.") + hash8},
                                                  QDir::Dirs | QDir::NoDotAndDotDot);
    if (!hits.isEmpty()) return root + QStringLiteral("/") + hits.first();
    return root + QStringLiteral("/") + stem + QStringLiteral(".") + hash8;
}

// 流式 sha256（64KiB 分块，内存有界）；open 失败/中途读错都归 File 域
bool streamSha256(const QString& path, QString* hex, ErrInfo* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *err = {ErrCode::File, QStringLiteral("打不开文件：%1").arg(path)};
        return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    char buf[kHashBufBytes];
    forever {
        const qint64 n = f.read(buf, sizeof buf);
        if (n < 0) {
            *err = {ErrCode::File, QStringLiteral("读源文件失败：%1").arg(path)};
            return false;
        }
        if (n == 0) break;
        h.addData(QByteArrayView(buf, static_cast<qsizetype>(n)));
    }
    *hex = QString::fromLatin1(h.result().toHex());
    return true;
}

QString rootCachePath(const QString& root) { return root + QStringLiteral("/") + QString::fromLatin1(kWsRootCacheFile); }
QString wsIndexPath(const QString& wsDir) { return wsDir + QStringLiteral("/") + QString::fromLatin1(kWsIndexFile); }
QString versionDirName(int n, const QString& cmd) {
    return QStringLiteral("v%1_%2").arg(n, 3, 10, QLatin1Char('0')).arg(cmd);
}

// JSON 文档原子写（QSaveFile：临时文件 + rename，跨平台覆盖语义内建）；失败时临时文件已自清
bool writeJsonAtomic(const QString& path, const QJsonDocument& doc) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = doc.toJson(QJsonDocument::Compact);
    return f.write(bytes) == bytes.size() && f.commit();
}

bool readJsonFile(const QString& path, QJsonDocument* doc) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError pe{};
    *doc = QJsonDocument::fromJson(f.readAll(), &pe);
    return pe.error == QJsonParseError::NoError;
}

// ---- 根级路径缓存（加速层：可丢弃可重建，损坏按空处理不报错；键 = 路径+size+mtimeMs，spec §8 风险 3 取舍）----

QJsonArray readRootCache(const QString& root) {
    QJsonDocument doc;
    if (!readJsonFile(rootCachePath(root), &doc) || !doc.isArray()) return {};
    return doc.array();
}

// 命中条件：路径+size+mtimeMs 三键一致，且缓存指向的工作区目录仍在（目录被删视为 miss 走哈希）
QString cacheLookup(const QJsonArray& cache, const QString& absPath, qint64 size, qint64 mtimeMs) {
    for (const QJsonValue v : cache) {
        const QJsonObject e = v.toObject();
        if (e.value(QStringLiteral("path")).toString() != absPath) continue;
        if (static_cast<qint64>(e.value(QStringLiteral("size")).toDouble()) != size) continue;
        if (static_cast<qint64>(e.value(QStringLiteral("mtimeMs")).toDouble()) != mtimeMs) continue;
        const QString ws = e.value(QStringLiteral("ws")).toString();
        if (!ws.isEmpty() && QFileInfo::exists(ws)) return ws;
        break;
    }
    return {};
}

// 回填缓存（upsert 按路径去重）后原子落盘；缓存写失败静默——可重建，删了只是下次多算哈希（spec D1）
void cacheStore(const QString& root, const QString& absPath, qint64 size, qint64 mtimeMs, const QString& ws) {
    QJsonArray cache = readRootCache(root);
    for (int i = 0; i < cache.size(); ++i) {
        if (cache.at(i).toObject().value(QStringLiteral("path")).toString() == absPath) {
            cache.removeAt(i);
            break;
        }
    }
    QJsonObject e;
    e.insert(QStringLiteral("path"), absPath);
    e.insert(QStringLiteral("size"), static_cast<double>(size));
    e.insert(QStringLiteral("mtimeMs"), static_cast<double>(mtimeMs));
    e.insert(QStringLiteral("ws"), ws);
    cache.append(e);
    writeJsonAtomic(rootCachePath(root), QJsonDocument(cache));
}

// ---- 权威索引（workspace.json：源登记 + 版本清单 + head）----

// 载入权威索引；不存在/损坏都归 File（这是任务链的真相源，坏索引不可静默重建——宁可任务失败重算）。
// 类型也须全检（审查 P1）：「合法 JSON、含键、错类型」形态（如 versions 是字符串）曾让 toArray() 返回
// 空数组后追加重写，一次任务即静默清空全部版本记录——append-only 不变式被击穿
bool loadWsIndex(const QString& wsDir, QJsonObject* idx, ErrInfo* err) {
    QJsonDocument doc;
    if (!readJsonFile(wsIndexPath(wsDir), &doc) || !doc.isObject()) {
        *err = {ErrCode::File, QStringLiteral("工作区索引读不了：%1").arg(wsIndexPath(wsDir))};
        return false;
    }
    const QJsonObject obj = doc.object();
    if (!obj.value(QStringLiteral("versions")).isArray() || !obj.value(QStringLiteral("head")).isDouble()
        || !obj.value(QStringLiteral("source")).isObject()) {
        *err = {ErrCode::File, QStringLiteral("工作区索引读不了：%1").arg(wsIndexPath(wsDir))};
        return false;
    }
    *idx = obj;
    return true;
}

// 源音频规格登记（探测近似即可：登记是元数据，输出契约真值在版本记录的 outputSpec）
QJsonObject probeInputSpec(const QString& path) {
    ProbeInfo info;
    QString err;
    QJsonObject spec;
    if (FFExtractor::probeInfo(path, &info, &err)) {
        for (const ProbeStream& s : info.streams) {
            if (s.mediaType != QStringLiteral("audio")) continue;
            spec.insert(QStringLiteral("sampleRate"), s.sampleRate);
            spec.insert(QStringLiteral("channels"), s.channels);
            break;
        }
    }
    return spec;
}

// 输入文件是否本工作区某版本产物（按版本记录的相对输出路径回拼绝对路径比对）→ 命中即父版本号
int findParentVersion(const QJsonObject& idx, const QString& wsDir, const QString& absInput) {
    const QString canonicalWs = QDir::cleanPath(wsDir);
    for (const QJsonValue v : idx.value(QStringLiteral("versions")).toArray()) {
        const QString rel = v.toObject().value(QStringLiteral("output")).toObject().value(QStringLiteral("path")).toString();
        if (!rel.isEmpty() && QDir::cleanPath(canonicalWs + QStringLiteral("/") + rel) == absInput)
            return v.toObject().value(QStringLiteral("version")).toInt();
    }
    return 0;
}

// 下一版本号：head 与清单内最大号取大后 +1；目录名已占（孤儿目录，kill -9 残迹）则继续递增不复用
int nextFreeVersion(const QJsonObject& idx, const QString& wsDir, const QString& cmd) {
    int n = idx.value(QStringLiteral("head")).toInt();
    for (const QJsonValue v : idx.value(QStringLiteral("versions")).toArray())
        n = qMax(n, v.toObject().value(QStringLiteral("version")).toInt());
    forever {
        ++n;
        if (!QFileInfo::exists(wsDir + QStringLiteral("/") + versionDirName(n, cmd))) return n;
    }
}

}// namespace

bool planVersion(const QString& root, const QString& input, const QString& cmd, VersionPlan* plan, ErrInfo* err) {
    const QFileInfo inFi(input);
    if (!inFi.exists() || !inFi.isFile()) {
        *err = {ErrCode::File, QStringLiteral("打不开文件：%1").arg(input)};
        return false;
    }
    const QString rootAbs = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    if (!QDir().mkpath(rootAbs)) {
        *err = {ErrCode::File, QStringLiteral("工作区根建不了：%1").arg(rootAbs)};
        return false;
    }
    const QString absInput = QDir::cleanPath(inFi.absoluteFilePath());

    // —— 身份解析：缓存命中（stat 三键一致）免哈希；miss 全量 sha256 定身份 ——
    QString sha;
    const QJsonArray cache = readRootCache(rootAbs);
    QString wsDir = cacheLookup(cache, absInput, inFi.size(), inFi.lastModified().toMSecsSinceEpoch());
    if (wsDir.isEmpty()) {
        if (!streamSha256(absInput, &sha, err)) return false;
        wsDir = resolveWsDir(rootAbs, sha.left(8), sanitizeStem(inFi.completeBaseName()));
    }

    // —— 工作区不存在则建（目录 + 初始权威索引：源登记）；索引写失败自清所建目录 ——
    if (!QFileInfo::exists(wsDir)) {
        if (!QDir().mkpath(wsDir)) {
            *err = {ErrCode::File, QStringLiteral("工作区目录建不了：%1").arg(wsDir)};
            return false;
        }
        QJsonObject source;
        source.insert(QStringLiteral("path"), absInput);
        source.insert(QStringLiteral("sha256"), sha);
        source.insert(QStringLiteral("sizeBytes"), static_cast<double>(inFi.size()));
        source.insert(QStringLiteral("spec"), probeInputSpec(absInput));
        QJsonObject idx;
        idx.insert(QStringLiteral("source"), source);
        idx.insert(QStringLiteral("head"), 0);
        idx.insert(QStringLiteral("versions"), QJsonArray());
        if (!writeJsonAtomic(wsIndexPath(wsDir), QJsonDocument(idx))) {
            QDir(wsDir).removeRecursively();
            *err = {ErrCode::File, QStringLiteral("工作区索引写不了：%1").arg(wsIndexPath(wsDir))};
            return false;
        }
        cacheStore(rootAbs, absInput, inFi.size(), inFi.lastModified().toMSecsSinceEpoch(), wsDir);
    }

    // —— 载入权威索引，补齐版本记录所需的输入登记 ——
    QJsonObject idx;
    if (!loadWsIndex(wsDir, &idx, err)) return false;
    const QJsonObject regSource = idx.value(QStringLiteral("source")).toObject();
    if (sha.isEmpty() && regSource.value(QStringLiteral("path")).toString() == absInput)
        sha = regSource.value(QStringLiteral("sha256")).toString();// 缓存命中且即源登记本身：身份哈希复用（size+mtime 键的文档化取舍）
    if (sha.isEmpty() && !streamSha256(absInput, &sha, err)) return false;// 链式输入（版本产物）：首次登记，现算哈希

    // —— 版本目录占位（vNNN_<cmd>，跳过孤儿不复用）——
    const int n = nextFreeVersion(idx, wsDir, cmd);
    const QString versionDir = wsDir + QStringLiteral("/") + versionDirName(n, cmd);
    if (!QDir().mkpath(versionDir)) {
        *err = {ErrCode::File, QStringLiteral("版本目录建不了：%1").arg(versionDir)};
        return false;
    }

    plan->root = rootAbs;
    plan->wsDir = wsDir;
    plan->versionDir = versionDir;
    plan->cmd = cmd;
    plan->version = n;
    QJsonObject inputRec;
    inputRec.insert(QStringLiteral("path"), absInput);
    inputRec.insert(QStringLiteral("sha256"), sha);
    inputRec.insert(QStringLiteral("sizeBytes"), static_cast<double>(inFi.size()));
    plan->input = inputRec;
    plan->parentVersion = findParentVersion(idx, wsDir, absInput);
    return true;
}

bool commitVersion(const VersionPlan& plan, const QJsonObject& params, const QJsonValue& model,
                   const QJsonObject& taskResult, ErrInfo* err) {
    // 重载权威索引（单写者假设下的最新态；到不了这里带旧 head 的是并发提交，D7 未定义域）
    QJsonObject idx;
    if (!loadWsIndex(plan.wsDir, &idx, err)) {
        discardVersion(plan);
        return false;
    }

    // 版本记录（D3 来源链）：filled 参数 + 输入登记 + 模型摘要 + 父链 + 输出真值（自 result 如实转录）
    QJsonObject out;
    const QString product = taskResult.value(QStringLiteral("output")).toString();
    out.insert(QStringLiteral("path"), QDir(plan.wsDir).relativeFilePath(product));
    for (const QLatin1String k : {QLatin1String("outputSpec"), QLatin1String("frames"),
                                  QLatin1String("durationSec"), QLatin1String("truncated")})
        if (taskResult.contains(k)) out.insert(k, taskResult.value(k));
    QJsonObject rec;
    rec.insert(QStringLiteral("version"), plan.version);
    rec.insert(QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    rec.insert(QStringLiteral("cmd"), plan.cmd);
    rec.insert(QStringLiteral("params"), params);
    rec.insert(QStringLiteral("input"), plan.input);
    if (model.isNull()) rec.insert(QStringLiteral("model"), QJsonValue(QJsonValue::Null));
    else rec.insert(QStringLiteral("model"), model);
    rec.insert(QStringLiteral("parentVersion"),
               plan.parentVersion > 0 ? QJsonValue(plan.parentVersion) : QJsonValue(QJsonValue::Null));
    if (!plan.pipeline.isEmpty())// 管线标记按存在性插键（FR-9 D5/附裁 4）：非管线任务不含 → 逐字节零漂移
        rec.insert(QStringLiteral("pipeline"), plan.pipeline);
    rec.insert(QStringLiteral("output"), out);

    QJsonArray versions = idx.value(QStringLiteral("versions")).toArray();
    versions.append(rec);
    idx.insert(QStringLiteral("versions"), versions);
    idx.insert(QStringLiteral("head"), plan.version);
    // 同内容换路径（哈希已证同身份）：刷新源登记的路径/大小，链不断、身份不动
    if (plan.input.value(QStringLiteral("sha256")).toString() == idx.value(QStringLiteral("source")).toObject().value(QStringLiteral("sha256")).toString()) {
        QJsonObject source = idx.value(QStringLiteral("source")).toObject();
        source.insert(QStringLiteral("path"), plan.input.value(QStringLiteral("path")).toString());
        source.insert(QStringLiteral("sizeBytes"), plan.input.value(QStringLiteral("sizeBytes")).toDouble());
        idx.insert(QStringLiteral("source"), source);
    }

    // 原子登记；写失败 = 任务失败（删版本目录含产物，索引不动——与「失败不留半成品」同构）
    if (!writeJsonAtomic(wsIndexPath(plan.wsDir), QJsonDocument(idx))) {
        discardVersion(plan);
        *err = {ErrCode::File, QStringLiteral("工作区索引写不了：%1").arg(wsIndexPath(plan.wsDir))};
        return false;
    }

    // 回填路径缓存：源（若本次输入即源）与产物（链式输入的定位加速）；缓存写失败静默（可重建）
    const QFileInfo srcFi(plan.input.value(QStringLiteral("path")).toString());
    if (plan.parentVersion == 0)
        cacheStore(plan.root, srcFi.absoluteFilePath(), srcFi.size(), srcFi.lastModified().toMSecsSinceEpoch(), plan.wsDir);
    const QFileInfo prodFi(product);
    if (prodFi.exists())
        cacheStore(plan.root, QDir::cleanPath(prodFi.absoluteFilePath()), prodFi.size(),
                   prodFi.lastModified().toMSecsSinceEpoch(), plan.wsDir);
    return true;
}

void discardVersion(const VersionPlan& plan) {
    if (!plan.versionDir.isEmpty()) QDir(plan.versionDir).removeRecursively();
}

QJsonObject queryInfo(const QString& root, const QString& input, ErrInfo* err) {
    const QFileInfo inFi(input);
    if (!inFi.exists() || !inFi.isFile()) {
        *err = {ErrCode::File, QStringLiteral("打不开文件：%1").arg(input)};
        return {};
    }
    const QString rootAbs = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString absInput = QDir::cleanPath(inFi.absoluteFilePath());

    // 只读定位：缓存命中免哈希；miss 现算哈希（只读，查询不落盘、不建目录）
    QString wsDir = cacheLookup(readRootCache(rootAbs), absInput, inFi.size(), inFi.lastModified().toMSecsSinceEpoch());
    if (wsDir.isEmpty()) {
        QString sha;
        if (!streamSha256(absInput, &sha, err)) return {};
        wsDir = resolveWsDir(rootAbs, sha.left(8), sanitizeStem(inFi.completeBaseName()));
    }

    QJsonObject result;
    result.insert(QStringLiteral("root"), rootAbs);
    result.insert(QStringLiteral("dir"), wsDir);
    QJsonObject idx;
    if (!QFileInfo::exists(wsDir)) {// 未登记：空清单（查询不落盘，绝不顺手建目录）
        result.insert(QStringLiteral("exists"), false);
        result.insert(QStringLiteral("source"), QJsonValue(QJsonValue::Null));
        result.insert(QStringLiteral("head"), 0);
        result.insert(QStringLiteral("versions"), QJsonArray());
        return result;
    }
    if (!loadWsIndex(wsDir, &idx, err)) return {};// 目录在而索引坏：File 域报错
    result.insert(QStringLiteral("exists"), true);
    result.insert(QStringLiteral("source"), idx.value(QStringLiteral("source")));
    result.insert(QStringLiteral("head"), idx.value(QStringLiteral("head")));
    result.insert(QStringLiteral("versions"), idx.value(QStringLiteral("versions")));
    return result;
}

}// namespace Workspace
