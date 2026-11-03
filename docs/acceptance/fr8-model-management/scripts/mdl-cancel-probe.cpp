// mdl-cancel-probe.cpp — FR-8 验收探针（MDL-CANCEL 族 + bundled 视角）。
// 取消通道在 CLI --json 壳里不可达（Ctx 传空、单发无并发方），取消语义由本探针直调
// ModelManager 注入 cancel 标志验证；bundled 视角需要可控 appDir（把本探针与 models/
// 布局放进同一临时目录），CLI 侧 appDir 固定为 build-linux 不可控，同由探针承载。
// 用法：
//   mdl-cancel-probe bundled <临时根>          打 listModels(<临时根>) 的紧凑 JSON（壳层断言 bundled 键）
//   mdl-cancel-probe cancel-mid <id> <root> <endpoint>   下载首个进度回调后置 cancel
//   mdl-cancel-probe cancel-pre <id> <root> <endpoint>   预置 cancel（期待零网络请求）
// cancel-* 输出固定行供壳断言：CODE <E_*> / PART <size> <path>（无则 NOPART）/ DONE
#include "service/errinfo.h"
#include "service/modelmanager.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QJsonDocument>
#include <atomic>
#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication a(argc, argv);// QNAM 事件循环前提（modelmanager.h 契约）
    const QString mode = QString::fromLocal8Bit(argv[1]);

    if (mode == QStringLiteral("bundled")) {
        const QJsonObject r = ModelManager::listModels(QString::fromLocal8Bit(argv[2]));
        std::fwrite(QJsonDocument(r).toJson(QJsonDocument::Compact).constData(), 1,
                    QJsonDocument(r).toJson(QJsonDocument::Compact).size(), stdout);
        std::fputc('\n', stdout);
        return 0;
    }

    if (mode == QStringLiteral("cancel-mid") || mode == QStringLiteral("cancel-pre")) {
        const QString id = QString::fromLocal8Bit(argv[2]);
        const QString root = QString::fromLocal8Bit(argv[3]);
        const QString endpoint = QString::fromLocal8Bit(argv[4]);
        std::atomic<bool> cancel{mode == QStringLiteral("cancel-pre")};
        ModelManager::Ctx ctx;
        ctx.cancel = &cancel;
        // cancel-mid：首个进度回调即置位 → 下一个块间检查点中止（对传输速度不敏感）
        ctx.onProgress = [&cancel](int) { cancel.store(true); };
        ErrInfo err;
        const QJsonObject r = ModelManager::downloadModel(id, root, endpoint, ctx, &err);
        if (!r.isEmpty()) {
            std::printf("DONE\n");
            return 0;
        }
        std::printf("CODE %s\n", errCodeText(err.code));
        // .part 现场（OQ-6：取消保留续传缓存）：扫根目录报首个 .part
        QDirIterator it(root, {"*.part"}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            const QFileInfo fi(it.next());
            std::printf("PART %lld %s\n", static_cast<long long>(fi.size()),
                        fi.absoluteFilePath().toUtf8().constData());
        } else {
            std::printf("NOPART\n");
        }
        return 0;
    }

    std::fprintf(stderr, "用法: %s bundled <临时根> | cancel-mid <id> <root> <endpoint> | cancel-pre <id> <root> <endpoint>\n",
                 argv[0]);
    return 2;
}
