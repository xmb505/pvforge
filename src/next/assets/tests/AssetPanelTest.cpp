#include "next/assets/AssetPanel.h"
#include "next/core/Document.h"

#include <QColor>
#include <QImage>
#include <QListWidget>
#include <QMimeData>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using pvf::next::AssetPanel;

namespace {

class FakeDocument : public pvf::next::Document
{
public:
    explicit FakeDocument(const QString& root)
        : m_assets(root)
    {
    }

    const pvf::model::Project& project() const override { return m_project; }
    pvf::model::Project& mutableProject() override { return m_project; }
    pvf::assets::AssetStore& assets() override { return m_assets; }

    int playhead() const override { return m_playhead; }
    void setPlayhead(int frame) override { m_playhead = frame; }

    pvf::Uuid selection() const override { return m_selection; }
    void setSelection(const pvf::Uuid& uuid) override { m_selection = uuid; }

    bool commitEntity(const pvf::Uuid&, const QString& message,
                      QString* error) override
    {
        m_commits.append(message);
        emit changed();
        if (error)
            error->clear();
        return true;
    }

    bool commitProject(const QString& message, QString* error) override
    {
        m_commits.append(message);
        emit changed();
        if (error)
            error->clear();
        return true;
    }

    bool undo(QString*) override { return false; }
    bool redo(QString*) override { return false; }

    QStringList commits() const { return m_commits; }

private:
    pvf::assets::AssetStore m_assets;
    pvf::model::Project m_project;
    int m_playhead = 0;
    pvf::Uuid m_selection;
    QStringList m_commits;
};

bool writePng(const QString& path, const QColor& color)
{
    QImage image(64, 40, QImage::Format_ARGB32);
    image.fill(color);
    return image.save(path, "PNG");
}

} // namespace

class AssetPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void importPngAppears();
    void searchFilters();
    void dragOutMimeHasHash();
    void renameUpdatesName();
    void deleteRemovesFromList();
    void missingAssetShowsPlaceholder();
};

void AssetPanelTest::importPngAppears()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    AssetPanel panel(&document);

    const QString path = directory.path() + QStringLiteral("/background.png");
    QVERIFY(writePng(path, QColor(QStringLiteral("#336699"))));

    QString error;
    QVERIFY2(panel.importPaths({path}, &error), qPrintable(error));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("background.png"));
    QCOMPARE(panel.assetHashAt(0).size(), 64);
    QVERIFY(!panel.assetMissingAt(0));
    QVERIFY(document.assets().get(panel.assetHashAt(0)).has_value());
    QVERIFY(!document.commits().isEmpty());
    QVERIFY(document.commits().last().startsWith(QStringLiteral("pvf: import asset")));
}

void AssetPanelTest::searchFilters()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    AssetPanel panel(&document);

    const QString cat = directory.path() + QStringLiteral("/cat.png");
    const QString dog = directory.path() + QStringLiteral("/dog.png");
    QVERIFY(writePng(cat, Qt::red));
    QVERIFY(writePng(dog, Qt::blue));

    QString error;
    QVERIFY2(panel.importPaths({cat, dog}, &error), qPrintable(error));
    QCOMPARE(panel.assetCount(), 2);

    panel.setSearchText(QStringLiteral("cat"));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("cat.png"));

    panel.setSearchText(QStringLiteral("DOG"));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("dog.png"));

    panel.setSearchText(QStringLiteral("nothing"));
    QCOMPARE(panel.assetCount(), 0);

    panel.setSearchText(QString());
    QCOMPARE(panel.assetCount(), 2);
}

void AssetPanelTest::dragOutMimeHasHash()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    AssetPanel panel(&document);

    const QString path = directory.path() + QStringLiteral("/drag.png");
    QVERIFY(writePng(path, Qt::green));
    QString error;
    QVERIFY2(panel.importPaths({path}, &error), qPrintable(error));

    const QString hash = panel.assetHashAt(0);
    std::unique_ptr<QMimeData> mime(panel.assetMimeData(hash));
    QVERIFY(mime);
    QVERIFY(mime->hasFormat(QStringLiteral("application/x-pvforge-asset")));
    QCOMPARE(mime->data(QStringLiteral("application/x-pvforge-asset")), hash.toUtf8());
    QCOMPARE(mime->text(), hash);
}

void AssetPanelTest::renameUpdatesName()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    AssetPanel panel(&document);

    const QString path = directory.path() + QStringLiteral("/old.png");
    QVERIFY(writePng(path, Qt::yellow));
    QString error;
    QVERIFY2(panel.importPaths({path}, &error), qPrintable(error));

    const QString hash = panel.assetHashAt(0);
    QVERIFY2(panel.renameAsset(hash, QStringLiteral("renamed.png"), &error),
             qPrintable(error));
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("renamed.png"));
    QCOMPARE(panel.assetHashAt(0), hash);

    const auto stored = document.assets().get(hash);
    QVERIFY(stored);
    QCOMPARE(stored->name, QStringLiteral("renamed.png"));

    FakeDocument reopened(directory.path());
    const auto reloaded = reopened.assets().get(hash);
    QVERIFY(reloaded);
    QCOMPARE(reloaded->name, QStringLiteral("renamed.png"));

    QVERIFY(document.commits().last().startsWith(QStringLiteral("pvf: rename asset")));
}

void AssetPanelTest::deleteRemovesFromList()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    AssetPanel panel(&document);

    const QString path = directory.path() + QStringLiteral("/gone.png");
    QVERIFY(writePng(path, Qt::cyan));
    QString error;
    QVERIFY2(panel.importPaths({path}, &error), qPrintable(error));

    const QString hash = panel.assetHashAt(0);
    QVERIFY2(panel.deleteAsset(hash, &error), qPrintable(error));
    QCOMPARE(panel.assetCount(), 0);
    QVERIFY(!document.assets().get(hash).has_value());
    QVERIFY(document.commits().last().startsWith(QStringLiteral("pvf: delete asset")));
}

void AssetPanelTest::missingAssetShowsPlaceholder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());

    const QString hash = QStringLiteral("a").repeated(64);
    pvf::model::Entity entity;
    entity.uuid = QStringLiteral("entity-1");
    entity.name = QStringLiteral("missing");
    entity.content.kind = pvf::ContentKind::Asset;
    entity.content.asset.hash = hash;
    entity.content.asset.type = pvf::AssetType::Video;
    document.mutableProject().entities.insert(entity.uuid, entity);

    AssetPanel panel(&document);
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetHashAt(0), hash);
    QVERIFY(panel.assetMissingAt(0));
    QCOMPARE(panel.assetNameAt(0), hash.left(8));

    auto* list = panel.findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("素材缺失")));

    QVERIFY(panel.assetMimeData(hash) == nullptr);

    QString error;
    QVERIFY(!panel.deleteAsset(hash, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(panel.assetCount(), 1);

    panel.setSearchText(QStringLiteral("zzz"));
    QCOMPARE(panel.assetCount(), 0);
    panel.setSearchText(QString());
    QCOMPARE(panel.assetCount(), 1);
}

QTEST_MAIN(AssetPanelTest)

#include "AssetPanelTest.moc"
