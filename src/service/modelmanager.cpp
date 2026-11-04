#include "modelmanager.h"

#include "engineregistry.h"
#include "model/data.h"
#include "streamengine.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>

#include <memory>

// 模型管理核心实现（FR-8 片 A，spec §2 D1–D6 + FR-4 目录型模型）：目录表/清单/三视角合并在此；
// 下载器按 resolve 契约五步走（HEAD 解析 → ETag 对照 → Range 续传 → 增量写盘 → sha256 收尾落位
// 登记），QNAM 异步模型由局部 QEventLoop 同步化。失败清理纪律与 FFmpeg 胶水同款：失败路径零残留
// （.part 续传缓存豁免，OQ-6 拍板：R1 约束任务产物，下载缓存不是任务产物——sha256 不符/200 强制
// 重下时仍删）。FR-4 目录型（dfn3）：下载物 = tar.gz → 系统 tar 解包 → 整目录同卷 rename 落位
// 登记（幂等 = keyFiles 完整 + 登记；tar 包不驻库）。

namespace ModelManager {
namespace {

constexpr int kHashBufBytes = 64 * 1024;// 流式哈希/下载写盘缓冲（NFR-P4 同口径：禁整文件载入）
constexpr int kRetryExtra = 2;// 连接级失败额外重试次数（fetch-deps.sh --retry 3 同款；HTTP 4xx/5xx 不重试）
constexpr int kTransferTimeoutMs = 30 * 1000;// 传输停滞超时（无字节交换即断，D3）
constexpr auto kPartSuffix = ".part";// 下载/导入在途临时文件后缀（同卷 rename 原子落位）

// ---- 内置模型目录（D2：编译期静态表，目录变更 = 发版；与 vocalseparator 内置 UVR 参数表同型先例）----

struct CatalogEntry {
    QString id;// "gtcrn" / "uvr-mdx-voc-ft" / "dfn3"（ASCII 小写连字符，命令面引用键）
    QString name;// 显示名（中文）
    QString forVerb;// 关联引擎动词（单向声明：条目指向注册表，注册表零改，spec D2）
    QString relPath;// 库内相对路径 "<用途>/<文件>"（bundled 随包路径 = appDir/models/<同布局>，对齐 data.h）
    bool hfSource;// true = HF resolve 契约（endpoint 可换前缀，镜像只对此类生效，D4）
    QString hfRepoPath;// hfSource 时 <repo>/resolve/<rev>/<file>（endpoint + 此段拼 URL）
    QString directUrl;// 非 HF 源完整 URL（GTCRN 走 GitHub release，镜像字段对其无效——文档/UI 注记）
    QString sha256;// 期望指纹（校验 + ETag 对照）
    QString license;// 许可证（NFR-C2 THIRD-PARTY 清单数据源，§6 红线表如实转录）
    qint64 sizeBytes;// 展示与磁盘占用预估
    bool dirModel = false;// 目录型模型（FR-4 dfn3）：下载物 = tar.gz，解包成目录登记（引擎 model 参数 = 目录）
    QStringList keyFiles = {};// dirModel 的完整性判据文件名（config.ini + 各 onnx；引擎 setup 同清单权威）
};

// sha256/URL 与 scripts/fetch-deps.sh:14-19 单一事实源对齐（脚本属 CI 域不动；并存期各自维护、值相同）
const QList<CatalogEntry>& catalog() {
    static const QList<CatalogEntry> table = {
        CatalogEntry{
            .id = QStringLiteral("gtcrn"),
            .name = QStringLiteral("GTCRN 人声降噪（gtcrn_simple）"),
            .forVerb = QStringLiteral("aiDenoise"),
            .relPath = QStringLiteral("denoiser/gtcrn_simple.onnx"),
            .hfSource = false,
            .hfRepoPath = {},
            .directUrl = QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/speech-enhancement-models/gtcrn_simple.onnx"),
            .sha256 = QStringLiteral("e77603ac0c23dac3227dd2d7135b3a585cbee2679048aecfa886657d3ae1b534"),
            .license = QStringLiteral("MIT（倾向，发版前官方书面确认）"),
            .sizeBytes = 535638,
        },
        CatalogEntry{
            .id = QStringLiteral("uvr-mdx-voc-ft"),
            .name = QStringLiteral("UVR-MDX 人声分离（Voc_FT）"),
            .forVerb = QStringLiteral("separate"),
            .relPath = QStringLiteral("separator/UVR-MDX-NET-Voc_FT.onnx"),
            .hfSource = true,
            .hfRepoPath = QStringLiteral("Politrees/UVR_resources/resolve/main/models/MDXNet/UVR-MDX-NET-Voc_FT.onnx"),
            .directUrl = {},
            .sha256 = QStringLiteral("534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111"),
            .license = QStringLiteral("UVR 生态：免费再分发，须标注来源"),
            .sizeBytes = 66762490,
        },
        CatalogEntry{// FR-4 质量档：目录型（tar.gz 下载 → 系统 tar 解包 → 整目录登记）；URL/指纹与
                     // scripts/fetch-deps.sh DFN3_URL/DFN3_SHA 同源对齐（并存期各自维护、值相同）
            .id = QStringLiteral("dfn3"),
            .name = QStringLiteral("DeepFilterNet3 人声降噪（质量档）"),
            .forVerb = QStringLiteral("aiDenoiseHq"),
            .relPath = QStringLiteral("denoiser/dfn3"),// bundled 布局 = kDfn3ModelRelPath（models/denoiser/dfn3）
            .hfSource = false,
            .hfRepoPath = {},
            .directUrl = QStringLiteral("https://raw.githubusercontent.com/Rikorose/DeepFilterNet/main/models/DeepFilterNet3_onnx.tar.gz"),
            .sha256 = QStringLiteral("c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616"),
            .license = QStringLiteral("MIT（DeepFilterNet 官方仓库随源码发布）"),
            .sizeBytes = 7983136,// tar.gz 包体；解包后目录以 dirSize 展示
            .dirModel = true,
            .keyFiles = {QStringLiteral("config.ini"), QStringLiteral("enc.onnx"),
                         QStringLiteral("erb_dec.onnx"), QStringLiteral("df_dec.onnx")},
        },
    };
    Q_ASSERT([] {// 开发期一致性防御（engineregistry id 唯一性断言同型）：forVerb 须落在注册表
                 // requiresModel 动词集——静态表由维护者保证，运行期不做降级逻辑
        for (const CatalogEntry& e : table) {
            bool hit = false;
            for (const EngineRegistry::EngineEntry& r : EngineRegistry::engineTable())
                if (r.requiresModel && r.id == e.forVerb) { hit = true; break; }
            if (!hit) return false;
        }
        return true;
    }());
    return table;
}

const CatalogEntry* findCatalog(const QString& id) {
    for (const CatalogEntry& e : catalog())
        if (e.id == id) return &e;
    return nullptr;
}

// ---- 目录型模型（FR-4 dfn3）：tar.gz 下载解包 / 整目录登记 ----

// 目录完整性 = keyFiles 全在（引擎 setup 同清单，此处只做存在性快速判定）
bool dirComplete(const QString& dirPath, const QStringList& keyFiles) {
    for (const QString& f : keyFiles)
        if (!QFileInfo::exists(dirPath + QLatin1Char('/') + f)) return false;
    return true;
}

// 展示口径的目录占用（递归求和；keyFiles 级小目录，遍历开销可忽略）
qint64 dirSize(const QString& dirPath) {
    qint64 total = 0;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) total += QFileInfo(it.next()).size();
    return total;
}

// tar 包语干（去 .tar.gz/.tgz 后交 sanitizeId；"x.tar.gz" → "x"，普通名原样）
QString archiveStem(const QString& fileName) {
    if (fileName.endsWith(QLatin1String(".tar.gz"), Qt::CaseInsensitive))
        return fileName.left(fileName.size() - 7);
    if (fileName.endsWith(QLatin1String(".tgz"), Qt::CaseInsensitive))
        return fileName.left(fileName.size() - 4);
    return fileName;
}

// 该动词的目录型目录条目（自由导入走压缩包形态的分派依据；无则按单文件试开）
const CatalogEntry* dirEntryForVerb(const QString& forVerb) {
    for (const CatalogEntry& e : catalog())
        if (e.forVerb == forVerb && e.dirModel) return &e;
    return nullptr;
}

// 系统 tar 解包到暂存目录并定位模型根：官方包文件在 tmp/export/ 前缀下——自外向内
// 逐层下钻（仅单层子目录时）直至 config.ini，兼容无前缀 / DeepFilterNet3/ 单层目录等布局。
// Windows 10+ 系统自带 bsdtar（System32\tar.exe），Linux 发行版必有 tar。
bool stageExtract(const QString& tarGzPath, const QString& stageDir, const QStringList& keyFiles,
                  QString* innerDir, ErrInfo* err) {
    if (QFileInfo::exists(stageDir) && !QDir(stageDir).removeRecursively()) {
        *err = {ErrCode::File, QStringLiteral("清不掉历史暂存目录：%1").arg(stageDir)};
        return false;
    }
    if (!QDir().mkpath(stageDir)) {
        *err = {ErrCode::File, QStringLiteral("暂存目录建不了：%1").arg(stageDir)};
        return false;
    }
    QProcess tar;
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-xzf"), tarGzPath, QStringLiteral("-C"), stageDir});
    if (!tar.waitForStarted(5000) || !tar.waitForFinished(120 * 1000)
        || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::File,
                QStringLiteral("解包模型失败（tar %1，需系统带 tar：Windows 10+ 自带）：%2")
                    .arg(tar.error() == QProcess::FailedToStart
                             ? QStringLiteral("不可用")
                             : QStringLiteral("退出码 %1").arg(tar.exitCode()),
                         tarGzPath)};
        return false;
    }
    QString cur = stageDir;
    forever {// 下钻定位 config.ini
        if (QFileInfo::exists(cur + QLatin1Char('/') + keyFiles.front())) break;
        const QDir d(cur);
        const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (subs.size() == 1) {
            cur += QLatin1Char('/') + subs.first();
            continue;
        }
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::Model, QStringLiteral("压缩包内找不到 %1（不是该引擎的模型包）")
                                         .arg(keyFiles.front())};
        return false;
    }
    for (const QString& f : keyFiles) {
        if (QFileInfo::exists(cur + QLatin1Char('/') + f)) continue;
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::Model, QStringLiteral("模型目录不完整：缺 %1").arg(f)};
        return false;
    }
    *innerDir = cur;
    return true;
}

// ---- 路径与清单 ----

// 模型库根解析（OQ-3：缺省 ~/VideoHandler-Models；root 可选覆盖——模型库是应用级单一约定，
// 与 wsInfo 的 root 必填不对称的理由见 spec D5）
QString resolveRoot(const QString& root) {
    const QString given = root.isEmpty()
        ? QDir::homePath() + QStringLiteral("/") + QString::fromLatin1(kModelsDefaultRootName)
        : root;
    return QDir::cleanPath(QFileInfo(given).absoluteFilePath());
}

QString manifestPath(const QString& rootAbs) {
    return rootAbs + QStringLiteral("/") + QString::fromLatin1(kModelsIndexFile);
}

// appDir bundled 随包模型路径（D1：内置随包不迁移不复制，modelList 并列展示）；无应用实例
// （无头 DUT）时无 bundled 视角
QString bundledPath(const CatalogEntry& e) {
    if (!QCoreApplication::instance()) return {};
    return QCoreApplication::applicationDirPath() + QStringLiteral("/models/") + e.relPath;
}

// 流式 sha256（workspace 同款 64KiB 分块）；open 失败/中途读错归 File 域
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

// models.json 读（D1/D2：缓存性登记不是真相源——不存在/损坏/非数组都按空处理；读取侧以磁盘
// 实况为准压住删除侧漂移[stale 语义]，写入侧以「登记失败=任务失败」压住写入漂移）
QJsonArray readManifest(const QString& rootAbs) {
    QFile f(manifestPath(rootAbs));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray()) return {};
    return doc.array();
}

bool writeManifest(const QString& rootAbs, const QJsonArray& manifest, ErrInfo* err) {
    QSaveFile f(manifestPath(rootAbs));
    if (!f.open(QIODevice::WriteOnly)) {
        *err = {ErrCode::File, QStringLiteral("模型清单写不了：%1").arg(manifestPath(rootAbs))};
        return false;
    }
    const QByteArray bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        *err = {ErrCode::File, QStringLiteral("模型清单写不了：%1").arg(manifestPath(rootAbs))};
        return false;
    }
    return true;
}

QJsonObject findEntry(const QJsonArray& manifest, const QString& id) {
    for (const QJsonValue v : manifest)
        if (v.toObject().value(QStringLiteral("id")).toString() == id) return v.toObject();
    return {};
}

// 登记收编（upsert by id）：[{id,for,path,sha256,sizeBytes,license,source,time}]
bool registerModel(const QString& rootAbs, QJsonArray* manifest, const QString& id, const QString& forVerb,
                   const QString& path, const QString& sha256, qint64 sizeBytes, const QString& license,
                   const QString& source, ErrInfo* err) {
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), id);
    entry.insert(QStringLiteral("for"), forVerb);
    entry.insert(QStringLiteral("path"), path);
    entry.insert(QStringLiteral("sha256"), sha256);
    entry.insert(QStringLiteral("sizeBytes"), static_cast<double>(sizeBytes));
    entry.insert(QStringLiteral("license"), license);
    entry.insert(QStringLiteral("source"), source);
    entry.insert(QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    bool replaced = false;
    for (int i = 0; i < manifest->size(); ++i) {
        if (manifest->at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            manifest->replace(i, entry);
            replaced = true;
            break;
        }
    }
    if (!replaced) manifest->append(entry);
    return writeManifest(rootAbs, *manifest, err);
}

QJsonObject okResult(const QString& id, const QString& path, const QString& sha256, qint64 sizeBytes) {
    QJsonObject r;
    r.insert(QStringLiteral("id"), id);
    r.insert(QStringLiteral("path"), path);
    r.insert(QStringLiteral("sha256"), sha256);
    r.insert(QStringLiteral("sizeBytes"), static_cast<double>(sizeBytes));
    return r;
}

// ---- QNAM 同步化（spec 风险 2）：局部事件循环驱动；退出条件只挂 finished（error 必随之）——
// 传输停滞由 setTransferTimeout 兜住，连接级失败与 HTTP 状态错误靠属性区分（无状态码 = 连接级）----

// ETag 规范化：剥可选弱前缀 W/ 与引号（LFS/Xet 文件的 ETag 即 sha256，D3 步骤 2 对照口径）
QString normalizeEtag(const QByteArray& raw) {
    QByteArray t = raw.trimmed();
    if (t.startsWith("W/")) t.remove(0, 2);
    if (t.size() >= 2 && t.startsWith('"') && t.endsWith('"')) {
        t.chop(1);
        t.remove(0, 1);
    }
    return QString::fromLatin1(t).toLower();
}

QString httpStatusText(int status) {
    if (status == 429)
        return QStringLiteral("下载失败：HTTP 429（请求过于频繁，稍后再试或换镜像 endpoint）");
    return QStringLiteral("下载失败：HTTP %1").arg(status);
}

struct HttpMeta {
    int status = 0;// HTTP 状态码；0 = 无状态可达（连接级失败）
    QNetworkReply::NetworkError netError = QNetworkReply::NoError;
    QString errorString;
    qint64 contentLength = -1;// HEAD Content-Length（-1 = 未给；进度分母 + 陈旧 .part 判定）
    QString etagHex;// 规范化后的 ETag（空 = 服务端未给，免对照、收尾哈希兜底）
};

void drainReply(QNetworkReply* reply) {
    reply->deleteLater();
    // 同步化下的资源纪律：就地冲洗延迟删除（壳可能不再进事件循环——CLI 单发后即返回）
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

HttpMeta doHead(QNetworkAccessManager& qnam, const QUrl& url) {
    QNetworkRequest req(url);
    // 重定向策略显式设置（NFR-C1 防降级）。Qt 6.10 起移除 setRedirectPolicy，改经属性承载
    // （RedirectPolicyAttribute 取 RedirectPolicy 枚举 int 值；缺省本即 NoLessSafe，显式钉住语义）
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     static_cast<int>(QNetworkRequest::NoLessSafeRedirectPolicy));
    req.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = qnam.head(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    HttpMeta m;
    const QVariant st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (st.isValid()) m.status = st.toInt();
    m.netError = reply->error();
    if (m.netError != QNetworkReply::NoError) m.errorString = reply->errorString();
    const QVariant cl = reply->header(QNetworkRequest::ContentLengthHeader);
    if (cl.isValid()) m.contentLength = cl.toLongLong();
    m.etagHex = normalizeEtag(reply->rawHeader(QByteArrayLiteral("ETag")));
    drainReply(reply);
    return m;
}

// 单轮 GET：Range 续 .part（offset>0 时）；readyRead 增量落盘（64KiB 上界）+ 块间取消检查。
// 206 先验 Content-Range 起点（协议违约不硬凑）；200 = 服务器不理 Range（不支持/文件已变）
// → 弃 .part 从头重写（D3 步骤 3）；416 = .part 已不短于全量（上轮下完未收尾的残局），零写入。
struct GetRound {
    int status = 0;
    QNetworkReply::NetworkError netError = QNetworkReply::NoError;
    QString errorString;
    bool cancelled = false;
    bool ioError = false;// 写盘失败（*err 已置 File 域）
    bool rangeMismatch = false;// 206 的 Content-Range 起点与请求不符
};

GetRound doGetRound(QNetworkAccessManager& qnam, const QUrl& url, const QString& partPath, qint64 offset,
                    qint64 total, const Ctx& ctx, ErrInfo* err) {
    GetRound g;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     static_cast<int>(QNetworkRequest::NoLessSafeRedirectPolicy));// 同 doHead（Qt 6.10 属性承载）
    req.setTransferTimeout(kTransferTimeoutMs);
    if (offset > 0)
        req.setRawHeader(QByteArrayLiteral("Range"),
                         QByteArrayLiteral("bytes=") + QByteArray::number(offset) + '-');
    QNetworkReply* reply = qnam.get(req);

    QFile part(partPath);
    bool opened = false;
    qint64 base = offset;// 本轮进度口径起点（200 重写时归零）
    const auto httpStatus = [&reply]() {
        const QVariant st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        return st.isValid() ? st.toInt() : 0;
    };
    // 响应头一到即定位写入模式并验 Content-Range：206 → append 续写；200 → truncate 弃 .part
    QObject::connect(reply, &QNetworkReply::metaDataChanged, [&]() {
        if (opened) return;
        const int status = httpStatus();
        if (status != 206 && status != 200) return;// 错误状态无体可写，交收尾判定
        const bool append = status == 206;
        if (append) {
            const QString cr = QString::fromLatin1(reply->rawHeader(QByteArrayLiteral("Content-Range")));
            qint64 start = -1;
            if (cr.startsWith(QLatin1String("bytes "))) {
                const QString rest = cr.mid(6);
                const int dash = rest.indexOf('-');
                if (dash > 0) start = rest.left(dash).toLongLong();
            }
            if (start >= 0 && start != offset) {// 起点 ≠ 已落盘量：不硬凑，交收尾报协议错
                g.rangeMismatch = true;
                reply->abort();
                return;
            }
        } else {
            base = 0;
        }
        if (!part.open(append ? QIODevice::Append : (QIODevice::WriteOnly | QIODevice::Truncate))) {
            g.ioError = true;
            *err = {ErrCode::File, QStringLiteral("写模型临时文件失败：%1").arg(partPath)};
            reply->abort();
            return;
        }
        opened = true;
    });
    QObject::connect(reply, &QIODevice::readyRead, [&]() {
        if (g.cancelled || g.ioError || g.rangeMismatch) return;
        if (ctx.cancel && ctx.cancel->load()) {// 块间取消检查（D3 步骤 4）：abort + .part 保留（OQ-6）
            g.cancelled = true;
            reply->abort();
            return;
        }
        if (!opened) return;// 头未到（理论不可达：metaDataChanged 先于 readyRead）
        forever {
            const QByteArray chunk = reply->read(kHashBufBytes);
            if (chunk.isEmpty()) break;
            if (part.write(chunk) != chunk.size()) {
                g.ioError = true;
                *err = {ErrCode::File, QStringLiteral("写模型临时文件失败：%1").arg(partPath)};
                reply->abort();
                return;
            }
            if (ctx.onProgress && total > 0)
                ctx.onProgress(static_cast<int>(
                    qBound(qint64(0), 100 * (base + part.size()) / total, qint64(99))));
        }
    });
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    part.close();
    g.status = httpStatus();
    g.netError = reply->error();
    if (g.netError != QNetworkReply::NoError) g.errorString = reply->errorString();
    drainReply(reply);
    return g;
}

// ---- 下载源 URL 组装（单文件/目录型共用）----

// 镜像只对 hfSource 生效（directUrl 直连，endpoint 对其无效，D4 注记）。组装+校验先于一切
// 落盘副作用（测试轮 P3：非法 endpoint 曾在 E_PARAMS 拒绝前留下空目录）
QUrl buildSourceUrl(const CatalogEntry& e, const QString& endpoint, ErrInfo* err) {
    const QString effectiveEndpoint = endpoint.isEmpty()
        ? QString::fromLatin1(kDefaultModelEndpoint) : endpoint;
    const QUrl url = e.hfSource
        ? QUrl(effectiveEndpoint + QStringLiteral("/") + e.hfRepoPath)
        : QUrl(e.directUrl);
    if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        *err = {ErrCode::Params, QStringLiteral("endpoint 不是合法的 http(s) 地址：%1").arg(effectiveEndpoint)};
        return {};
    }
    return url;
}

// ---- 网络下载到已验 .part（单文件/目录型共用，D3 五步的 1–4 步 + sha256 全量校验）----

bool fetchVerified(const CatalogEntry& e, QNetworkAccessManager& qnam, const QUrl& url,
                   const QString& partPath, const Ctx& ctx, ErrInfo* err) {
    // 步骤 1/2：HEAD 解析（跟随重定向）+ ETag 指纹对照；连接级失败重试至多 kRetryExtra 次
    HttpMeta head;
    for (int attempt = 0;; ++attempt) {
        head = doHead(qnam, url);
        if (head.status >= 200 && head.status < 300) break;
        if (head.status > 0) {// HTTP 4xx/5xx：不重试（D3 步骤 4）
            *err = {ErrCode::Net, httpStatusText(head.status)};
            return false;
        }
        if (attempt >= kRetryExtra) {
            *err = {ErrCode::Net, QStringLiteral("网络连接失败：%1（已重试 %2 次）")
                                       .arg(head.errorString).arg(kRetryExtra)};
            return false;
        }
    }
    if (e.hfSource && !head.etagHex.isEmpty() && head.etagHex != e.sha256) {
        // ETag 对照仅 HF resolve 契约（LFS/Xet 文件 ETag 即 sha256）；直连源 ETag 是主机侧不透明
        // 指纹（GitHub release 资产 0x8DD5… 型、raw.githubusercontent 为 git 对象 64 位指纹，均非
        // 内容 sha256——实测），对照必假阳，收尾流式 sha256 全量校验才是权威
        *err = {ErrCode::Net, QStringLiteral("远端文件指纹与目录不符（ETag=%1，期望 %2）——目录条目过期或远端文件被改")
                                   .arg(head.etagHex, e.sha256)};
        return false;
    }
    if (head.contentLength > 0 && QFileInfo(partPath).size() > head.contentLength)
        QFile::remove(partPath);// 陈旧 .part 比远端全量还长（本地缓存异常）：无续传价值，弃之从头

    // 步骤 3/4：Range 续传 + 增量写盘 + 块间取消；连接级失败重试（每轮从 .part 现量续传）
    for (int attempt = 0;; ++attempt) {
        const qint64 offset = QFileInfo(partPath).size();
        const GetRound g = doGetRound(qnam, url, partPath, offset, head.contentLength, ctx, err);
        if (g.cancelled || (ctx.cancel && ctx.cancel->load())) {
            *err = {ErrCode::Cancelled, QStringLiteral("已取消")};// .part 保留（OQ-6 续传缓存豁免）
            return false;
        }
        if (g.ioError) return false;// *err 已置（File 域）；.part 保留（已写字节仍有效）
        if (g.rangeMismatch) {
            *err = {ErrCode::Net, QStringLiteral("服务器续传响应与请求不符（Content-Range 起点不等于已落盘量）")};
            return false;
        }
        if (g.netError == QNetworkReply::NoError) break;// 本轮干净收尾：声明量到齐（200/206 同等待遇）
        if (g.status == 416) break;// .part 已不短于全量（上轮下完未收尾的残局）：免传输交哈希收尾
        if (g.status >= 400) {// HTTP 状态错误（4xx/5xx）不重试；注意掐断类连接错误的状态属性残留 2xx，
            *err = {ErrCode::Net, httpStatusText(g.status)};// 只能凭 ≥400 认 HTTP 错误，否则误吞掐断（实测教训）
            return false;// .part 保留（HTTP 错误后远端仍在，续传缓存不失效）
        }
        if (attempt >= kRetryExtra) {
            *err = {ErrCode::Net, QStringLiteral("网络连接失败：%1（已重试 %2 次）")
                                       .arg(g.errorString).arg(kRetryExtra)};
            return false;// .part 保留：跨运行续传是本包差异点（HF 官方已不做，issue #4196，D3 步骤 3）
        }
    }

    // sha256 全量校验（内容坏删 .part——续传无意义，与「取消/断流保留」的区分点）
    QString gotSha;
    if (!streamSha256(partPath, &gotSha, err)) return false;
    if (gotSha != e.sha256) {
        QFile::remove(partPath);
        *err = {ErrCode::Net, QStringLiteral("下载内容 sha256 不符（期望 %1，实得 %2）——已删临时文件")
                                   .arg(e.sha256.left(16), gotSha.left(16))};
        return false;
    }
    return true;
}

// ---- 目录型下载（FR-4 dfn3）：tar.gz 验指纹下载 → 系统 tar 解包到同卷暂存 → 定位模型根 →
// 整目录 rename 落位（原子）→ 登记删暂存/包体。幂等 = 登记 + 目录完整即信（解包后无法从目录
// 反推内容指纹，keyFiles 完整性即引擎可用性权威）；tar 包不驻库，库内只留目录 ----

QJsonObject downloadDirModel(const CatalogEntry& e, const QString& rootAbs, const QString& endpoint,
                             const Ctx& ctx, ErrInfo* err) {
    const QString dest = rootAbs + QStringLiteral("/") + e.relPath;
    const QString destDir = QFileInfo(dest).absolutePath();
    const QString partPath = dest + QStringLiteral(".tar.gz.part");
    const QString stageDir = destDir + QStringLiteral("/.") + QFileInfo(dest).fileName()
                             + QStringLiteral("-stage");
    {
        const QJsonArray manifest = readManifest(rootAbs);
        if (dirComplete(dest, e.keyFiles)) {
            const QJsonObject reg = findEntry(manifest, e.id);
            if (reg.value(QStringLiteral("path")).toString() == dest
                && reg.value(QStringLiteral("sha256")).toString() == e.sha256)
                return okResult(e.id, dest, e.sha256, e.sizeBytes);// 已装已登记：零网络（D5 同口径）
            QJsonArray m2 = manifest;// 目录在位未登记（用户手放库内）：keyFiles 完整即收编，
            if (registerModel(rootAbs, &m2, e.id, e.forVerb, dest, e.sha256, e.sizeBytes,// 指纹记档 tar 包
                              e.license, QStringLiteral("local"), err))
                return okResult(e.id, dest, e.sha256, e.sizeBytes);
            return {};
        }// 清单有条目但目录没了/不完整：清单非真理，往下重下自愈（单文件口径同）
    }
    {
        const QString bp = bundledPath(e);
        if (!bp.isEmpty() && dirComplete(bp, e.keyFiles))
            return okResult(e.id, bp, e.sha256, e.sizeBytes);// appDir bundled 在位（两级解析第二级）
    }

    const QUrl url = buildSourceUrl(e, endpoint, err);
    if (!url.isValid()) return {};
    if (!QDir().mkpath(destDir)) {
        *err = {ErrCode::File, QStringLiteral("模型库目录建不了：%1").arg(destDir)};
        return {};
    }
    QNetworkAccessManager qnam;
    if (!fetchVerified(e, qnam, url, partPath, ctx, err)) return {};
    if (ctx.cancel && ctx.cancel->load()) {// 解包前最后一查（解包+落位不中断，代价 8MB 级）
        QFile::remove(partPath);
        *err = {ErrCode::Cancelled, QStringLiteral("已取消")};
        return {};
    }

    QString inner;
    if (!stageExtract(partPath, stageDir, e.keyFiles, &inner, err)) return {};// .part 已验内容，保留待重试
    if (QFileInfo::exists(dest) && !QDir(dest).removeRecursively()) {// 不完整旧目录：弃之翻新
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::File, QStringLiteral("旧模型目录删不掉：%1").arg(dest)};
        return {};
    }
    if (!QDir().rename(inner, dest)) {// 同卷目录 rename = 原子落位
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::File, QStringLiteral("模型目录落位失败：%1 → %2").arg(inner, dest)};
        return {};
    }
    QDir(stageDir).removeRecursively();// 清掉壳层（官方包 tmp/export 的上级）
    {
        QJsonArray m2 = readManifest(rootAbs);
        if (!registerModel(rootAbs, &m2, e.id, e.forVerb, dest, e.sha256, e.sizeBytes,
                           e.license, QStringLiteral("download"), err)) {
            QDir(dest).removeRecursively();// 登记失败删产物不留孤儿
            QFile::remove(partPath);
            return {};
        }
    }
    QFile::remove(partPath);// 包体不驻库（指纹已记档，库内只留目录）
    if (ctx.onProgress) ctx.onProgress(100);
    return okResult(e.id, dest, e.sha256, e.sizeBytes);
}

// ---- 导入辅助 ----

// id 净化（自由导入）：字母（含中文等 Unicode 字母）/数字/连字符/下划线，超长截断（48 字符）；
// 净化后为空（全标点/符号名）才回落 "model"——中文名 isLetterOrNumber 为 true，原样保留为 id
QString sanitizeId(const QString& stem) {
    QString out;
    for (const QChar c : stem) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) out.append(c);
        if (out.size() >= 48) break;
    }
    while (out.endsWith(QLatin1Char('_'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("model") : out;
}

// 自由导入落库目录：取该动词目录条目的用途目录（denoiser/separator）；目录暂无该动词条目时以
// 动词名兜底（FR-4 质量档等后续条目落目录后自动归位，spec §5 留缝）
QString purposeDir(const QString& forVerb) {
    for (const CatalogEntry& e : catalog())
        if (e.forVerb == forVerb) return QFileInfo(e.relPath).path();
    return forVerb;
}

// requiresModel 动词集文案（顿号连接，engineregistry enumValuesText 同款渲染；表在注册表侧此处只聚合）
QString modelVerbList() {
    QString text;
    for (const EngineRegistry::EngineEntry& r : EngineRegistry::engineTable()) {
        if (!r.requiresModel) continue;
        if (!text.isEmpty()) text += QStringLiteral("、");
        text += r.id;
    }
    return text;
}

// ---- 目录型导入（FR-4）：源 = tar.gz，解包 → 整目录落位登记（两种形态共用落位段）----

// 形态 1（按 id）：指纹对目录条目（防「同名不同内容」混入，同单文件口径）
QJsonObject importDirById(const CatalogEntry& e, const QFileInfo& src, const QString& rootAbs,
                          const Ctx& ctx, ErrInfo* err) {
    QString sha;
    if (!streamSha256(src.absoluteFilePath(), &sha, err)) return {};
    if (sha != e.sha256) {
        *err = {ErrCode::Model, QStringLiteral("模型指纹与目录条目不符：%1（期望 %2，实得 %3）")
                                     .arg(e.id, e.sha256.left(16), sha.left(16))};
        return {};
    }
    const QString dest = rootAbs + QStringLiteral("/") + e.relPath;
    const QString stageDir = QFileInfo(dest).absolutePath() + QStringLiteral("/.")
                             + QFileInfo(dest).fileName() + QStringLiteral("-import-stage");
    QString inner;
    if (!stageExtract(src.absoluteFilePath(), stageDir, e.keyFiles, &inner, err)) return {};
    if (QFileInfo::exists(dest) && !QDir(dest).removeRecursively()) {// 同 id 翻新：内容已验，弃旧换新
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::File, QStringLiteral("旧模型目录删不掉：%1").arg(dest)};
        return {};
    }
    if (!QDir().rename(inner, dest)) {
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::File, QStringLiteral("模型目录落位失败：%1 → %2").arg(inner, dest)};
        return {};
    }
    QDir(stageDir).removeRecursively();
    {
        QJsonArray manifest = readManifest(rootAbs);
        if (!registerModel(rootAbs, &manifest, e.id, e.forVerb, dest, e.sha256, e.sizeBytes,
                           e.license, QStringLiteral("import"), err)) {
            QDir(dest).removeRecursively();// 登记失败删产物不留孤儿
            return {};
        }
    }
    if (ctx.onProgress) ctx.onProgress(100);
    QJsonObject result = okResult(e.id, dest, e.sha256, e.sizeBytes);
    result.insert(QStringLiteral("license"), e.license);
    return result;
}

// 形态 2（自由导入，OQ-5）：解包到暂存 → 引擎 setup 试开（权威校验器，试开即弃）→ id/路径
// 占用检查 → 整目录落位。id = tar 包语干净化；指纹/大小记档口径 = 源 tar.gz（目录无法反推内容指纹）
QJsonObject importDirFree(const CatalogEntry& de, const EngineRegistry::EngineEntry& verbEntry,
                          const QFileInfo& src, const QString& rootAbs, const Ctx& ctx, ErrInfo* err) {
    const QString purpose = purposeDir(verbEntry.id);
    const QString stageDir = rootAbs + QStringLiteral("/") + purpose + QStringLiteral("/.")
                             + verbEntry.id + QStringLiteral("-import-stage");
    QString inner;
    if (!stageExtract(src.absoluteFilePath(), stageDir, de.keyFiles, &inner, err)) return {};
    const auto bail = [&](ErrInfo e2) {
        QDir(stageDir).removeRecursively();
        *err = e2;
        return QJsonObject{};
    };
    QJsonObject args;
    args.insert(QStringLiteral("model"), inner);
    QString createErr;
    {
        const std::unique_ptr<IStreamEngine> engine = verbEntry.create(args, 0, 0, &createErr);
        if (!engine)// 试开败按条目 setupErrorClass 归类（aiDenoiseHq 为 Model，D5 同口径）
            return bail({verbEntry.setupErrorClass != ErrCode::None ? verbEntry.setupErrorClass
                                                                    : ErrCode::Engine,
                         createErr});
    }// engine 随作用域析构（试开即弃，不驻 ORT 会话）
    const QString useId = sanitizeId(archiveStem(src.fileName()));
    if (findCatalog(useId))
        return bail({ErrCode::Params,
                     QStringLiteral("导入 id 与内置目录条目冲突：%1（请改文件名）").arg(useId)});
    QString sha;
    if (!streamSha256(src.absoluteFilePath(), &sha, err)) return bail(*err);
    const QString destRel = purpose + QStringLiteral("/") + useId;
    const QString dest = rootAbs + QStringLiteral("/") + destRel;
    // 路径/id 占用检查（与单文件自由导入同根修复）：静默覆盖会造「双 id 同路径/同 id 遮蔽」，
    // 毒化 downloadModel 幂等的「登记即信」信任链；同 id 同 sha 重复导入 → 放行翻新幂等
    {
        const QJsonArray manifest = readManifest(rootAbs);
        for (const CatalogEntry& ce : catalog()) {
            if (rootAbs + QStringLiteral("/") + ce.relPath == dest)
                return bail({ErrCode::Params,
                             QStringLiteral("导入目标与内置目录条目路径冲突：%1（请改文件名后重导）").arg(destRel)});
        }
        const QJsonObject taken = findEntry(manifest, useId);
        if (!taken.isEmpty()) {
            if (taken.value(QStringLiteral("sha256")).toString() != sha)
                return bail({ErrCode::Params,
                             QStringLiteral("导入 id 已被占用：%1（不同内容的模型请改文件名后重导）").arg(useId)});
        } else {
            for (const QJsonValue v : manifest) {
                if (v.toObject().value(QStringLiteral("path")).toString() == dest)
                    return bail({ErrCode::Params,
                                 QStringLiteral("导入目标已被其他模型占用：%1（请改文件名后重导）").arg(destRel)});
            }
        }
    }
    if (QFileInfo::exists(dest) && !QDir(dest).removeRecursively()) {// 同 id 同 sha：原地翻新
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::File, QStringLiteral("旧模型目录删不掉：%1").arg(dest)};
        return {};
    }
    if (!QDir().rename(inner, dest)) {
        QDir(stageDir).removeRecursively();
        *err = {ErrCode::File, QStringLiteral("模型目录落位失败：%1 → %2").arg(inner, dest)};
        return {};
    }
    QDir(stageDir).removeRecursively();
    {
        QJsonArray manifest = readManifest(rootAbs);
        if (!registerModel(rootAbs, &manifest, useId, verbEntry.id, dest, sha, src.size(),
                           QStringLiteral("unknown（用户自行核验）"), QStringLiteral("import"), err)) {
            QDir(dest).removeRecursively();// 登记失败删产物不留孤儿
            return {};
        }
    }
    if (ctx.onProgress) ctx.onProgress(100);
    QJsonObject result = okResult(useId, dest, sha, src.size());
    result.insert(QStringLiteral("license"), QStringLiteral("unknown（用户自行核验）"));
    return result;
}

}// namespace

QJsonObject listModels(const QString& root) {
    const QString rootAbs = resolveRoot(root);
    const QJsonArray manifest = readManifest(rootAbs);// 不存在/损坏按空；查询不建目录不落盘

    QJsonArray models;
    // 视角合并（D2）：内置目录（可下载什么）× models.json（装了什么）× 磁盘实况（现场验证）
    for (const CatalogEntry& e : catalog()) {
        QJsonObject m;
        m.insert(QStringLiteral("id"), e.id);
        m.insert(QStringLiteral("name"), e.name);
        m.insert(QStringLiteral("for"), e.forVerb);
        m.insert(QStringLiteral("bundled"), false);
        m.insert(QStringLiteral("sha256"), e.sha256);
        m.insert(QStringLiteral("sizeBytes"), static_cast<double>(e.sizeBytes));
        m.insert(QStringLiteral("license"), e.license);
        m.insert(QStringLiteral("status"), QStringLiteral("available"));
        if (e.dirModel) {// 目录型（FR-4）：在位 = keyFiles 全在（登记/bundled 同判据）；实占递归求和展示
            const QJsonObject reg = findEntry(manifest, e.id);
            const QString libPath = reg.value(QStringLiteral("path")).toString();
            if (!libPath.isEmpty() && dirComplete(libPath, e.keyFiles)) {
                m.insert(QStringLiteral("status"), QStringLiteral("installed"));
                m.insert(QStringLiteral("path"), libPath);
                m.insert(QStringLiteral("sizeBytes"), static_cast<double>(dirSize(libPath)));
            } else {
                const QString bp = bundledPath(e);
                if (!bp.isEmpty() && dirComplete(bp, e.keyFiles)) {
                    m.insert(QStringLiteral("status"), QStringLiteral("installed"));
                    m.insert(QStringLiteral("bundled"), true);
                    m.insert(QStringLiteral("path"), bp);
                    m.insert(QStringLiteral("sizeBytes"), static_cast<double>(dirSize(bp)));
                } else if (!reg.isEmpty()) {
                    m.insert(QStringLiteral("status"), QStringLiteral("stale"));// 清单非真理，同单文件口径
                }
            }
            models.append(m);
            continue;
        }
        const QJsonObject reg = findEntry(manifest, e.id);
        const QString libPath = reg.value(QStringLiteral("path")).toString();
        if (!libPath.isEmpty() && QFileInfo::exists(libPath)) {// 库内在位（两级解析第一级，D1）
            m.insert(QStringLiteral("status"), QStringLiteral("installed"));
            m.insert(QStringLiteral("path"), libPath);
            m.insert(QStringLiteral("sizeBytes"), static_cast<double>(QFileInfo(libPath).size()));
        } else {
            const QString bp = bundledPath(e);
            const QFileInfo bf(bp);
            if (!bp.isEmpty() && bf.isFile() && bf.size() == e.sizeBytes) {
                // bundled 在位 = 存在性 + size 匹配（D1：不逐次算 sha256——每次全量读 20–70MB 不可取；
                // 损坏检测交给引擎 setup 的 E_MODEL 路径，运行期发现 → 引导重下，正合 NFR-R4）
                m.insert(QStringLiteral("status"), QStringLiteral("installed"));
                m.insert(QStringLiteral("bundled"), true);
                m.insert(QStringLiteral("path"), bp);
            } else if (!reg.isEmpty()) {
                // 清单有条目但库文件没了（bundled 也不在）→ stale：不静默当已装（models.json 非真理）
                m.insert(QStringLiteral("status"), QStringLiteral("stale"));
            }
        }
        models.append(m);
    }
    // 目录外清单条目（自由导入的社区模型——RVC/Qwen 的前门，spec §5 留缝）
    for (const QJsonValue v : manifest) {
        const QJsonObject reg = v.toObject();
        const QString rid = reg.value(QStringLiteral("id")).toString();
        if (rid.isEmpty() || findCatalog(rid)) continue;
        QJsonObject m;
        m.insert(QStringLiteral("id"), rid);
        m.insert(QStringLiteral("name"), rid);
        m.insert(QStringLiteral("for"), reg.value(QStringLiteral("for")).toString());
        m.insert(QStringLiteral("bundled"), false);
        m.insert(QStringLiteral("sha256"), reg.value(QStringLiteral("sha256")).toString());
        m.insert(QStringLiteral("sizeBytes"), reg.value(QStringLiteral("sizeBytes")));
        m.insert(QStringLiteral("license"), reg.value(QStringLiteral("license")).toString());
        const QString p = reg.value(QStringLiteral("path")).toString();
        if (QFileInfo::exists(p)) {
            m.insert(QStringLiteral("status"), QStringLiteral("installed"));
            m.insert(QStringLiteral("path"), p);
            m.insert(QStringLiteral("sizeBytes"), static_cast<double>(QFileInfo(p).size()));
        } else {
            m.insert(QStringLiteral("status"), QStringLiteral("stale"));
        }
        models.append(m);
    }

    QJsonObject result;
    result.insert(QStringLiteral("root"), rootAbs);
    result.insert(QStringLiteral("models"), models);
    return result;
}

QJsonObject downloadModel(const QString& id, const QString& root, const QString& endpoint,
                          const Ctx& ctx, ErrInfo* err) {
    const CatalogEntry* e = findCatalog(id);
    if (!e) {
        *err = {ErrCode::Params, QStringLiteral("未知模型 id：%1").arg(id)};
        return {};
    }
    if (ctx.cancel && ctx.cancel->load()) {
        *err = {ErrCode::Cancelled, QStringLiteral("已取消")};
        return {};
    }
    const QString rootAbs = resolveRoot(root);
    if (e->dirModel) return downloadDirModel(*e, rootAbs, endpoint, ctx, err);// FR-4 目录型整流程

    const QString dest = rootAbs + QStringLiteral("/") + e->relPath;
    const QString destDir = QFileInfo(dest).absolutePath();
    const QString partPath = dest + QLatin1String(kPartSuffix);

    // —— 幂等（D5）：库或 appDir 已在位且指纹/大小符合 → ok 返回现路径零网络请求 ——
    const QJsonArray manifest = readManifest(rootAbs);
    if (QFileInfo::exists(dest)) {
        const QJsonObject reg = findEntry(manifest, id);
        bool verified = false;
        if (reg.value(QStringLiteral("path")).toString() == dest
            && reg.value(QStringLiteral("sha256")).toString() == e->sha256
            && static_cast<qint64>(reg.value(QStringLiteral("sizeBytes")).toDouble())
                   == QFileInfo(dest).size()) {
            verified = true;// 下载/导入时已全量校验并登记（清单即校验记录），免重读大文件；
            // size 与登记一致才信（与 D1 bundled「存在+size」同精度）：登记后文件被截断/替换
            // 不再命中 → 落 else 现算指纹 → 不符重下治愈（测试 F-2：否则 D8 引导重下恒 ok 死循环）。
            // 等长翻字节仍过——与 bundled 同边界，恶意/罕见损坏交给引擎 setup E_MODEL 兜底。
        } else {
            QString got;// 无登记（用户手放库内）：现算一次指纹，符合即收编登记
            if (!streamSha256(dest, &got, err)) return {};
            if (got == e->sha256) {
                QJsonArray m2 = manifest;
                if (registerModel(rootAbs, &m2, id, e->forVerb, dest, got, QFileInfo(dest).size(),
                                  e->license, QStringLiteral("local"), err))
                    return okResult(id, dest, got, QFileInfo(dest).size());
                return {};
            }
        }
        if (verified) return okResult(id, dest, e->sha256, QFileInfo(dest).size());
        // 指纹不符的旧件：不当已装，往下重下（收尾 rename 前移除）
    }
    {
        const QString bp = bundledPath(*e);
        const QFileInfo bf(bp);
        if (!bp.isEmpty() && bf.isFile() && bf.size() == e->sizeBytes)
            return okResult(id, bp, e->sha256, bf.size());// appDir bundled 在位（两级解析第二级）
    }

    // —— 网络面：URL 组装（镜像只对 hfSource 生效——GTCRN/DFN3 直连 GitHub，endpoint 对其无效，D4 注记）。
    //    组装+校验先于一切落盘副作用（测试轮 P3：非法 endpoint 曾在 E_PARAMS 拒绝前留下空目录）——
    const QUrl url = buildSourceUrl(*e, endpoint, err);
    if (!url.isValid()) return {};

    if (!QDir().mkpath(destDir)) {
        *err = {ErrCode::File, QStringLiteral("模型库目录建不了：%1").arg(destDir)};
        return {};
    }
    QNetworkAccessManager qnam;
    if (!fetchVerified(*e, qnam, url, partPath, ctx, err)) return {};

    // —— 收尾：同卷 rename 原子落位 → QSaveFile 原子登记 ——
    if (QFileInfo::exists(dest)) QFile::remove(dest);// 幂等段放行的指纹不符旧件
    if (!QFile::rename(partPath, dest)) {
        *err = {ErrCode::File, QStringLiteral("模型落位失败：%1 → %2").arg(partPath, dest)};
        return {};
    }
    {
        QJsonArray m2 = readManifest(rootAbs);
        if (!registerModel(rootAbs, &m2, id, e->forVerb, dest, e->sha256, QFileInfo(dest).size(),
                           e->license, QStringLiteral("download"), err)) {
            QFile::remove(dest);// 登记失败 = 下载失败删产物（与工作区「索引写失败=任务失败」同构）
            return {};
        }
    }
    if (ctx.onProgress) ctx.onProgress(100);
    return okResult(id, dest, e->sha256, QFileInfo(dest).size());
}

QJsonObject importModel(const QString& id, const QString& forVerb, const QString& path,
                        const QString& root, const Ctx& ctx, ErrInfo* err) {
    const QFileInfo src(path);
    if (!src.exists() || !src.isFile()) {
        *err = {ErrCode::File, QStringLiteral("打不开文件：%1").arg(path)};
        return {};
    }
    if (ctx.cancel && ctx.cancel->load()) {
        *err = {ErrCode::Cancelled, QStringLiteral("已取消")};
        return {};
    }
    const QString rootAbs = resolveRoot(root);

    QString useId, useFor, license, sha, destRel;
    if (!id.isEmpty()) {
        // —— 形态 1（按 id）：指纹对目录条目（防「同名不同内容」混入），按条目登记（D6）——
        const CatalogEntry* e = findCatalog(id);
        if (!e) {
            *err = {ErrCode::Params, QStringLiteral("未知模型 id：%1").arg(id)};
            return {};
        }
        if (e->dirModel) return importDirById(*e, src, rootAbs, ctx, err);// FR-4：源 = tar.gz
        if (!streamSha256(src.absoluteFilePath(), &sha, err)) return {};
        if (sha != e->sha256) {
            *err = {ErrCode::Model, QStringLiteral("模型指纹与目录条目不符：%1（期望 %2，实得 %3）")
                                         .arg(id, e->sha256.left(16), sha.left(16))};
            return {};
        }
        useId = e->id;
        useFor = e->forVerb;
        license = e->license;
        destRel = e->relPath;
    } else if (!forVerb.isEmpty()) {
        // —— 形态 2（自由导入，OQ-5）：for 必带 + create 闭包 setup 试开（setup 即权威校验器，
        //    零新增校验代码且比 sha256 匹配更强——能收目录外模型；免 for 猜用途会让废件蒙混）——
        const EngineRegistry::EngineEntry* entry = nullptr;
        for (const EngineRegistry::EngineEntry& r : EngineRegistry::engineTable())
            if (r.id == forVerb) { entry = &r; break; }
        if (!entry || !entry->requiresModel) {
            *err = {ErrCode::Params, QStringLiteral("for 只接受 %1").arg(modelVerbList())};
            return {};
        }
        if (const CatalogEntry* de = dirEntryForVerb(forVerb))// FR-4：目录型动词（aiDenoiseHq）走压缩包形态
            return importDirFree(*de, *entry, src, rootAbs, ctx, err);
        QJsonObject args;
        args.insert(QStringLiteral("model"), src.absoluteFilePath());
        QString createErr;
        const std::unique_ptr<IStreamEngine> engine = entry->create(args, 0, 0, &createErr);
        if (!engine) {// 试开败按条目 setupErrorClass 归类（aiDenoise/separate 皆 Model，D5）
            *err = {entry->setupErrorClass != ErrCode::None ? entry->setupErrorClass : ErrCode::Engine,
                    createErr};
            return {};
        }
        useId = sanitizeId(src.completeBaseName());
        if (findCatalog(useId)) {
            *err = {ErrCode::Params,
                    QStringLiteral("导入 id 与内置目录条目冲突：%1（请改文件名）").arg(useId)};
            return {};
        }
        useFor = forVerb;
        license = QStringLiteral("unknown（用户自行核验）");// 许可义务在如实呈现，核验责任转移用户（OQ-5）
        destRel = purposeDir(forVerb) + QStringLiteral("/") + src.fileName();
        if (!streamSha256(src.absoluteFilePath(), &sha, err)) return {};// 记档指纹
        // 路径/id 占用检查（审查 P1-1/P2-1 同根修复）：冲突时静默覆盖会造出「双 id 同路径」
        // 或「同 id 遮蔽旧登记」，毒化 downloadModel 幂等的登记信任链（登记 sha 即信、不读文件）。
        // 同 id 同 sha（重复导入同一文件）翻新幂等保留——内容已验，文件与清单同步翻新无害。
        {
            const QJsonArray manifest = readManifest(rootAbs);
            const QString destPath = rootAbs + QStringLiteral("/") + destRel;
            for (const CatalogEntry& ce : catalog()) {
                if (rootAbs + QStringLiteral("/") + ce.relPath == destPath) {
                    *err = {ErrCode::Params,
                            QStringLiteral("导入目标与内置目录条目路径冲突：%1（请改文件名后重导）").arg(destRel)};
                    return {};
                }
            }
            const QJsonObject taken = findEntry(manifest, useId);
            if (!taken.isEmpty()) {
                if (taken.value(QStringLiteral("sha256")).toString() != sha) {
                    *err = {ErrCode::Params,
                            QStringLiteral("导入 id 已被占用：%1（不同内容的模型请改文件名后重导）").arg(useId)};
                    return {};
                }
            } else {
                for (const QJsonValue v : manifest) {
                    if (v.toObject().value(QStringLiteral("path")).toString() == destPath) {
                        *err = {ErrCode::Params,
                                QStringLiteral("导入目标已被其他模型占用：%1（请改文件名后重导）").arg(destRel)};
                        return {};
                    }
                }
            }
        }
    } else {
        *err = {ErrCode::Params, QStringLiteral("导入须带 id（按目录指纹）或 for（自由导入试开）")};
        return {};
    }

    // —— 复制进库（原件只读：复制绝不移动不改写）。.part 中转 + 同卷 rename（D3 同款原子性）；
    //    本地源重拷廉价——失败/取消即删 .part，无续传价值（OQ-6 豁免只覆盖网络下载缓存）——
    const QString dest = rootAbs + QStringLiteral("/") + destRel;
    const QString destDir = QFileInfo(dest).absolutePath();
    if (!QDir().mkpath(destDir)) {
        *err = {ErrCode::File, QStringLiteral("模型库目录建不了：%1").arg(destDir)};
        return {};
    }
    const QString partPath = dest + QLatin1String(kPartSuffix);
    const qint64 importTotal = src.size();// 进度分母（D5：导入同占 busy+进度条；0–99 按已复制字节）
    {
        QFile in(src.absoluteFilePath());
        QFile out(partPath);
        if (!in.open(QIODevice::ReadOnly)
            || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QFile::remove(partPath);
            *err = {ErrCode::File, QStringLiteral("写模型临时文件失败：%1").arg(partPath)};
            return {};
        }
        char buf[kHashBufBytes];
        qint64 copied = 0;
        forever {
            if (ctx.cancel && ctx.cancel->load()) {
                QFile::remove(partPath);
                *err = {ErrCode::Cancelled, QStringLiteral("已取消")};
                return {};
            }
            const qint64 n = in.read(buf, sizeof buf);
            if (n < 0) {
                QFile::remove(partPath);
                *err = {ErrCode::File, QStringLiteral("读源文件失败：%1").arg(src.absoluteFilePath())};
                return {};
            }
            if (n == 0) break;
            if (out.write(buf, n) != n) {
                QFile::remove(partPath);
                *err = {ErrCode::File, QStringLiteral("写模型临时文件失败：%1").arg(partPath)};
                return {};
            }
            copied += n;
            if (ctx.onProgress && importTotal > 0)// 试开/sha 阶段无粒度，进度只在复制段（与下载器 0–99 同口径）
                ctx.onProgress(static_cast<int>(copied * 99 / importTotal));// copied≤total → 恒在 0–99
        }
    }
    if (QFileInfo::exists(dest)) QFile::remove(dest);// 重复导入同 id：内容已验，原地翻新
    if (!QFile::rename(partPath, dest)) {
        QFile::remove(partPath);
        *err = {ErrCode::File, QStringLiteral("模型落位失败：%1 → %2").arg(partPath, dest)};
        return {};
    }
    {
        QJsonArray manifest = readManifest(rootAbs);
        if (!registerModel(rootAbs, &manifest, useId, useFor, dest, sha, QFileInfo(dest).size(),
                           license, QStringLiteral("import"), err)) {
            QFile::remove(dest);// 登记失败删产物不留孤儿
            return {};
        }
    }
    if (ctx.onProgress) ctx.onProgress(100);// 收尾 100（登记成功才算完，与下载器同口径）
    QJsonObject result = okResult(useId, dest, sha, QFileInfo(dest).size());
    result.insert(QStringLiteral("license"), license);
    return result;
}

}// namespace ModelManager
