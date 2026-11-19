// Security tests for issue #123: an AI cannot self-approve an import from
// outside the project, and preview base64 output refuses assets whose metadata
// says no human approved them. Kept in a separate translation unit so
// mcp2_tests.cpp stays under the code-size ratchet; the methods belong to
// Mcp2Tests.

#include "mcp2/Service.h"

#include "mcp2_tests.h"

#include "model/Model.h"
#include "store/Store.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <optional>

using namespace pvf;

namespace {

struct SecurityFixture
{
    QTemporaryDir temp;
    std::optional<store::ProjectStore> store;
    std::unique_ptr<mcp2::Service> service;

    bool init()
    {
        if (!temp.isValid())
            return false;
        QString error;
        auto created = store::ProjectStore::create(
            temp.filePath(QStringLiteral("Demo.pvproj")),
            model::ProjectSettings(), &error);
        if (!created)
        {
            qWarning("ProjectStore::create failed: %s", qPrintable(error));
            return false;
        }
        store = std::move(*created);
        service = std::make_unique<mcp2::Service>(&*store, &store->assets());
        return true;
    }

    QJsonObject call(const QString& tool, const QJsonObject& args = QJsonObject())
    {
        return service->execute(tool, args);
    }
};

QString errorCode(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
        .toString();
}

} // namespace

void Mcp2Tests::assetImportOutsideNeedsConfirm()
{
    SecurityFixture fixture;
    QVERIFY(fixture.init());

    const QString outsidePath = fixture.temp.filePath(QStringLiteral("outside.png"));
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::cyan);
    QVERIFY(image.save(outsidePath));

    QJsonObject result =
        fixture.call(QStringLiteral("asset.import"),
                     QJsonObject{{QStringLiteral("path"), outsidePath}});
    QCOMPARE(errorCode(result), QStringLiteral("confirm_required"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("path")).toString(),
             QFileInfo(outsidePath).canonicalFilePath());
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("reason")).toString(),
             QStringLiteral("no_human_available"));
    QVERIFY(fixture.store->assets().list().isEmpty());

    // An AI-supplied confirm flag is not a parameter and cannot approve.
    result = fixture.call(QStringLiteral("asset.import"),
                          QJsonObject{{QStringLiteral("path"), outsidePath},
                                      {QStringLiteral("confirm"), true}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    QVERIFY(fixture.store->assets().list().isEmpty());

    // A human can deny; the file never enters the project.
    QString confirmedPath;
    fixture.service->setExternalImportConfirmer(
        [&confirmedPath](const QString& path) {
            confirmedPath = path;
            return false;
        });
    result = fixture.call(QStringLiteral("asset.import"),
                          QJsonObject{{QStringLiteral("path"), outsidePath}});
    QCOMPARE(errorCode(result), QStringLiteral("permission_denied"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("reason")).toString(),
             QStringLiteral("human_denied"));
    QCOMPARE(confirmedPath, QFileInfo(outsidePath).canonicalFilePath());
    QVERIFY(fixture.store->assets().list().isEmpty());

    // Only the human callback approves the copy.
    fixture.service->setExternalImportConfirmer([](const QString&) { return true; });
    result = fixture.call(QStringLiteral("asset.import"),
                          QJsonObject{{QStringLiteral("path"), outsidePath}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QString outsideHash = result.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("asset")).toObject()
                                    .value(QStringLiteral("hash")).toString();
    QCOMPARE(outsideHash.size(), 64);
    const auto imported = fixture.store->assets().get(outsideHash);
    QVERIFY(imported.has_value());
    QCOMPARE(imported->metadata.value(QStringLiteral("importedBy")).toString(),
             QStringLiteral("mcp"));
    QVERIFY(imported->metadata.value(QStringLiteral("humanApproved")).toBool());

    // Credentials and system directories are refused before the dialog.
    bool sensitiveAsked = false;
    fixture.service->setExternalImportConfirmer(
        [&sensitiveAsked](const QString&) {
            sensitiveAsked = true;
            return true;
        });
    result = fixture.call(
        QStringLiteral("asset.import"),
        QJsonObject{{QStringLiteral("path"), QStringLiteral("/etc/hostname")}});
    QCOMPARE(errorCode(result), QStringLiteral("permission_denied"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("reason")).toString(),
             QStringLiteral("sensitive_path"));
    QVERIFY(!sensitiveAsked);

    // A file inside the project root imports without confirmation.
    const QString insidePath = fixture.store->root() + QStringLiteral("/inside.png");
    QImage inside(4, 4, QImage::Format_ARGB32);
    inside.fill(Qt::yellow);
    QVERIFY(inside.save(insidePath));
    result = fixture.call(QStringLiteral("asset.import"),
                          QJsonObject{{QStringLiteral("path"), insidePath}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(fixture.store->assets().list().size(), 2);
}

void Mcp2Tests::previewRefusesUnapprovedBase64()
{
    SecurityFixture fixture;
    QVERIFY(fixture.init());

    const QString imagePath = fixture.store->root() + QStringLiteral("/pic.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    QVERIFY(image.save(imagePath));

    QJsonObject imported = fixture.call(
        QStringLiteral("asset.import"),
        QJsonObject{{QStringLiteral("path"), imagePath}});
    QVERIFY2(imported.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(imported)));
    const QString hash = imported.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("asset")).toObject()
                             .value(QStringLiteral("hash")).toString();
    QVERIFY(!hash.isEmpty());

    QJsonObject entityArgs;
    entityArgs.insert(QStringLiteral("name"), QStringLiteral("图片"));
    entityArgs.insert(QStringLiteral("start"), 0);
    entityArgs.insert(QStringLiteral("end"), 60);
    const QString uuid = fixture.call(QStringLiteral("entity.create"), entityArgs)
                             .value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("uuid")).toString();
    QVERIFY(!uuid.isEmpty());
    QJsonObject assigned =
        fixture.call(QStringLiteral("asset.assign"),
                     QJsonObject{{QStringLiteral("entityUuid"), uuid},
                                 {QStringLiteral("assetHash"), hash}});
    QVERIFY2(assigned.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(assigned)));

    // Simulate an asset that entered the project without human approval: the
    // base64 preview channel must refuse to copy its pixels.
    const QString metadataPath =
        fixture.store->root() + QStringLiteral("/assets/") + hash
        + QStringLiteral("/asset.json");
    QFile file(metadataPath);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QJsonObject metadata = QJsonDocument::fromJson(file.readAll()).object();
    metadata.insert(QStringLiteral("importedBy"), QStringLiteral("mcp"));
    metadata.insert(QStringLiteral("humanApproved"), false);
    file.resize(0);
    file.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    file.close();

    QJsonObject result = fixture.call(
        QStringLiteral("preview.render_frame"),
        QJsonObject{{QStringLiteral("frame"), 0},
                    {QStringLiteral("includeBase64"), true}});
    QCOMPARE(errorCode(result), QStringLiteral("permission_denied"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("reason")).toString(),
             QStringLiteral("unapproved_import"));

    // Without base64 the local PNG path is still available, inside the private
    // per-user directory and unreadable by other local users.
    result = fixture.call(QStringLiteral("preview.render_frame"),
                          QJsonObject{{QStringLiteral("frame"), 0}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QString path = result.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("path")).toString();
    QVERIFY2(path.contains(QStringLiteral("/pvforge-previews-")), qPrintable(path));
#ifdef Q_OS_UNIX
    // Only the owner may read the frame (mode 0600) and enter the directory
    // (0700); group/other bits must be clear.
    const QFile::Permissions foreign =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther
        | QFileDevice::ExeOther;
    QCOMPARE(QFile::permissions(path) & foreign, QFile::Permissions{});
    const QFile::Permissions directoryMode =
        QFile::permissions(QFileInfo(path).absolutePath());
    QCOMPARE(directoryMode & foreign, QFile::Permissions{});
    QVERIFY(directoryMode & QFileDevice::ReadOwner);
    QVERIFY(directoryMode & QFileDevice::ExeOwner);
#endif
}

void Mcp2Tests::previewTempFilesArePruned()
{
    SecurityFixture fixture;
    QVERIFY(fixture.init());

    QJsonObject result = fixture.call(
        QStringLiteral("preview.render_frame"), QJsonObject{{QStringLiteral("frame"), 0}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QString firstPath = result.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("path")).toString();
    const QDir directory = QFileInfo(firstPath).absoluteDir();
    QVERIFY(directory.exists());

    QImage pixel(2, 2, QImage::Format_ARGB32);
    pixel.fill(Qt::black);

    // Older than the one-hour TTL: must be deleted regardless of the count.
    const QString expired = directory.filePath(
        QStringLiteral("pvforge-preview-expired-") + model::newUuid()
        + QStringLiteral(".png"));
    QVERIFY(pixel.save(expired, "PNG"));
    {
        QFile file(expired);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(
            QDateTime::currentDateTimeUtc().addSecs(-2 * 60 * 60),
            QFileDevice::FileModificationTime));
    }

    // More files than the LRU cap (32). The first few are the oldest and must
    // be dropped to keep the directory bounded.
    QVector<QString> recent;
    recent.reserve(40);
    for (int i = 0; i < 40; ++i)
    {
        const QString path = directory.filePath(
            QStringLiteral("pvforge-preview-lru-") + QString::number(i)
            + QStringLiteral("-") + model::newUuid() + QStringLiteral(".png"));
        QVERIFY(pixel.save(path, "PNG"));
        // Distinct mtimes: filesystem timestamp granularity can otherwise make
        // all 40 files tie and the "oldest" assertion non-deterministic.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(
            QDateTime::currentDateTimeUtc().addSecs(-40 + i),
            QFileDevice::FileModificationTime));
        file.close();
        recent.append(path);
    }
    const QString oldest = recent.first();
    const QString newest = recent.last();

    result = fixture.call(QStringLiteral("preview.render_frame"),
                          QJsonObject{{QStringLiteral("frame"), 0}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));

    QVERIFY2(!QFileInfo::exists(expired), "preview files older than the TTL must be removed");
    QVERIFY2(!QFileInfo::exists(oldest), "preview LRU must drop the oldest files");
    QVERIFY2(QFileInfo::exists(newest), "preview LRU must keep the newest files");

    const int remaining = directory.entryList({QStringLiteral("*.png")}, QDir::Files).size();
    QVERIFY2(remaining <= 33,
             qPrintable(QStringLiteral("preview directory grew to %1 files (cap 32 + 1 new)")
                            .arg(remaining)));

    for (const QString& path : recent)
        QFile::remove(path);
    QFile::remove(expired);
}
