#include "mcp2/Internal.h"
#include "mcp2/Service.h"

#include "store/Store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;

namespace {

struct Fixture
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
        // These tests exercise data safety, not the human-confirmation dialog.
        service->setExternalImportConfirmer([](const QString&) { return true; });
        return true;
    }

    QJsonObject call(const QString& tool, const QJsonObject& args = QJsonObject())
    {
        return service->execute(tool, args);
    }

    int historySize() { return store->history(0).size(); }
};

QString errorCode(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
        .toString();
}

QJsonObject entityArgs(const QString& name, int start, int end)
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("start"), start},
                       {QStringLiteral("end"), end}};
}

QString uuidOf(const QJsonObject& result)
{
    return result.value(QStringLiteral("data"))
        .toObject()
        .value(QStringLiteral("uuid"))
        .toString();
}

} // namespace

class Mcp2SafetyTests : public QObject
{
    Q_OBJECT

private slots:
    void historyUndoFlushesPendingHumanEdit();
    void commitFailureRollsBackCleanly();
    void timedOutFinishWritesNothing();
    void assetDeleteTrashesAndUndoRestores();
};

void Mcp2SafetyTests::historyUndoFlushesPendingHumanEdit()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    // A human edit saved to disk but still inside the debounce window: no git
    // commit and no undo entry yet.
    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("人类正在改");
    entity.start = 0;
    entity.end = 30;
    fixture.store->mutableProject().entities.insert(entity.uuid, entity);
    QString saveError;
    QVERIFY2(fixture.store->saveEntity(entity.uuid, &saveError),
             qPrintable(saveError));
    QVERIFY(fixture.store->hasPendingWrites());
    QCOMPARE(fixture.store->undoDepth(), 0);
    const int gitBefore = fixture.historySize();

    const QJsonObject result = fixture.call(
        QStringLiteral("history.undo"),
        QJsonObject{{QStringLiteral("steps"), 1}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    // The pending edit was flushed and then undone: only that edit disappears,
    // and it is preserved as a git commit.
    QVERIFY(result.value(QStringLiteral("data"))
                .toObject()
                .value(QStringLiteral("flushedPending"))
                .toBool());
    QVERIFY(fixture.store->project().entities.isEmpty());
    QCOMPARE(fixture.historySize(), gitBefore + 1);
    QCOMPARE(fixture.store->history(1).first().message,
             QStringLiteral("pvf: flush pending edit"));
    // The flushed entry was the one undo consumed; the restored state stays
    // pending for the next human save.
    QCOMPARE(fixture.store->undoDepth(), 0);
}

void Mcp2SafetyTests::commitFailureRollsBackCleanly()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString first = uuidOf(fixture.call(
        QStringLiteral("entity.create"), entityArgs(QStringLiteral("保留"), 0, 30)));
    QVERIFY(!first.isEmpty());
    const int historyBefore = fixture.historySize();

    // A stale index.lock is an environmental commit failure that the
    // hardened Git path cannot bypass. Project hooks are deliberately ignored
    // now (issue #121), so they can no longer be used as the failure injection.
    const QString lockPath =
        fixture.store->root() + QStringLiteral("/.git/index.lock");
    QFile lock(lockPath);
    QVERIFY(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
    lock.write("lock");
    lock.close();

    const QJsonObject result = fixture.call(
        QStringLiteral("entity.create"),
        entityArgs(QStringLiteral("不应留下"), 0, 30));
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(errorCode(result), QStringLiteral("internal_error"));
    const QString message = result.value(QStringLiteral("error"))
                                .toObject()
                                .value(QStringLiteral("message"))
                                .toString();
    QVERIFY2(message.contains(QStringLiteral("已完整回滚")), qPrintable(message));

    // No half-written entity file, no extra commit, no read-only lock-out.
    QCOMPARE(fixture.store->project().entities.size(), 1);
    QVERIFY(!fixture.store->readOnly());
    QCOMPARE(fixture.historySize(), historyBefore);
    const QStringList files =
        QDir(fixture.store->root() + QStringLiteral("/entities"))
            .entryList({QStringLiteral("*.json")}, QDir::Files);
    QCOMPARE(files.size(), 1);

    QVERIFY(QFile::remove(lockPath));
    const QJsonObject retry = fixture.call(
        QStringLiteral("entity.create"),
        entityArgs(QStringLiteral("重试成功"), 0, 30));
    QVERIFY2(retry.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(retry)));
    QCOMPARE(fixture.store->project().entities.size(), 2);
}

void Mcp2SafetyTests::timedOutFinishWritesNothing()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    // Drive the transaction directly: the tool deadline is enforced at
    // finish(), the only place that writes.
    mcp2::detail::Tx tx(&*fixture.store);
    tx.deadlineCheck = [] { return true; };
    const QJsonObject result =
        mcp2::detail::entityCreate(tx, entityArgs(QStringLiteral("超时"), 0, 30))
            .toJson();
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(errorCode(result), QStringLiteral("request_timeout"));
    QVERIFY(fixture.store->project().entities.isEmpty());
    QVERIFY(QDir(fixture.store->root() + QStringLiteral("/entities"))
                .entryList({QStringLiteral("*.json")}, QDir::Files)
                .isEmpty());
    QCOMPARE(fixture.historySize(), 1); // only the initial commit
}

void Mcp2SafetyTests::assetDeleteTrashesAndUndoRestores()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString imagePath = fixture.temp.filePath(QStringLiteral("trash.png"));
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));

    const QJsonObject imported = fixture.call(
        QStringLiteral("asset.import"),
        QJsonObject{{QStringLiteral("path"), imagePath}});
    QVERIFY2(imported.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(imported)));
    const QString hash = imported.value(QStringLiteral("data"))
                             .toObject()
                             .value(QStringLiteral("asset"))
                             .toObject()
                             .value(QStringLiteral("hash"))
                             .toString();
    QVERIFY(!hash.isEmpty());

    const QJsonObject deleted = fixture.call(
        QStringLiteral("asset.delete"),
        QJsonObject{{QStringLiteral("hash"), hash},
                    {QStringLiteral("confirm"), true}});
    QVERIFY2(deleted.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(deleted)));
    QVERIFY(deleted.value(QStringLiteral("data"))
                .toObject()
                .value(QStringLiteral("trashed"))
                .toBool());
    QVERIFY(!fixture.store->assets().get(hash).has_value());
    QVERIFY(QFileInfo::exists(fixture.store->root() + QStringLiteral("/.trash/")
                              + hash + QStringLiteral("/asset.json")));

    const QJsonObject undo = fixture.call(
        QStringLiteral("history.undo"),
        QJsonObject{{QStringLiteral("steps"), 1}});
    QVERIFY2(undo.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(undo)));
    QVERIFY2(fixture.store->assets().get(hash).has_value(),
             "undo did not restore the asset");
    QVERIFY(!QFileInfo::exists(fixture.store->root() + QStringLiteral("/.trash/")
                               + hash));
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    Mcp2SafetyTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "mcp2_safety_tests.moc"
