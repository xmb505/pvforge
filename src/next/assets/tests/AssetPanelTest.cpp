#include "next/assets/AssetPanel.h"
#include "next/core/Document.h"
#include "video2/VideoExtractionQueue.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMimeData>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
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

QString makeVideo(const QString& path, int seconds)
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        return {};

    QProcess process;
    process.start(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
         QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
         QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc=size=160x90:rate=30:duration=%1")
             .arg(seconds),
         QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"),
         QStringLiteral("2"), QStringLiteral("-pix_fmt"),
         QStringLiteral("yuv420p"), path});
    if (!process.waitForFinished(120000))
    {
        process.kill();
        process.waitForFinished();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QFileInfo::exists(path) ? path : QString();
}

QString indexFileFor(const QString& projectRoot, const QString& hash,
                     int fps = 30, int maxWidth = 960)
{
    return QDir(projectRoot).filePath(
        QStringLiteral(".cache/video/") + hash + QLatin1Char('/')
        + QString::number(fps) + QLatin1Char('/')
        + QString::number(maxWidth) + QStringLiteral("/index.json"));
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

class AssetPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void importPngAppears();
    void searchFilters();
    void tagsShowInListAndSearch();
    void tagFilterShowsMatchingOnly();
    void editTagsCommitsOnce();
    void missingAssetTagsNotEditable();
    void dragOutMimeHasHash();
    void renameUpdatesName();
    void deleteRemovesFromList();
    void missingAssetShowsPlaceholder();
    void videoImportShowsProgressAndCancel();
    void videoImportCompletesAndHides();
    void videoExtractionFailuresStackAndClose();
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

void AssetPanelTest::tagsShowInListAndSearch()
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

    auto rowFor = [&panel](const QString& name) {
        for (int i = 0; i < panel.assetCount(); ++i)
        {
            if (panel.assetNameAt(i) == name)
                return i;
        }
        return -1;
    };

    const int catRow = rowFor(QStringLiteral("cat.png"));
    QVERIFY(catRow >= 0);
    const QString catHash = panel.assetHashAt(catRow);
    QVERIFY2(panel.setAssetTags(catHash, {QStringLiteral("宠物"), QStringLiteral("hero")},
                                &error),
             qPrintable(error));

    const int taggedRow = rowFor(QStringLiteral("cat.png"));
    QVERIFY(taggedRow >= 0);
    QCOMPARE(panel.assetTagsAt(taggedRow),
             QStringList({QStringLiteral("宠物"), QStringLiteral("hero")}));

    auto* list = panel.findChild<QListWidget*>();
    QVERIFY(list);
    QVERIFY(list->item(taggedRow)->text().contains(QStringLiteral("#宠物")));
    QVERIFY(list->item(taggedRow)->text().contains(QStringLiteral("hero")));

    panel.setSearchText(QStringLiteral("宠物"));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("cat.png"));

    panel.setSearchText(QStringLiteral("HERO"));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("cat.png"));

    panel.setSearchText(QStringLiteral("dog"));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("dog.png"));

    panel.setSearchText(QString());
    QCOMPARE(panel.assetCount(), 2);

    FakeDocument reopened(directory.path());
    const auto stored = reopened.assets().get(catHash);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->tags, QStringList({QStringLiteral("宠物"), QStringLiteral("hero")}));
}

void AssetPanelTest::tagFilterShowsMatchingOnly()
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

    QString catHash;
    QString dogHash;
    for (int i = 0; i < panel.assetCount(); ++i)
    {
        if (panel.assetNameAt(i) == QStringLiteral("cat.png"))
            catHash = panel.assetHashAt(i);
        else if (panel.assetNameAt(i) == QStringLiteral("dog.png"))
            dogHash = panel.assetHashAt(i);
    }
    QVERIFY(!catHash.isEmpty());
    QVERIFY(!dogHash.isEmpty());

    QVERIFY2(panel.setAssetTags(catHash, {QStringLiteral("宠物")}, &error), qPrintable(error));
    QVERIFY2(panel.setAssetTags(dogHash, {QStringLiteral("动物")}, &error), qPrintable(error));

    QVERIFY(panel.tagFilters().contains(QStringLiteral("宠物")));
    QVERIFY(panel.tagFilters().contains(QStringLiteral("动物")));
    QCOMPARE(panel.tagFilters().size(), 2);

    panel.setTagFilter(QStringLiteral("宠物"));
    QCOMPARE(panel.tagFilter(), QStringLiteral("宠物"));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("cat.png"));

    panel.setTagFilter(QStringLiteral("动物"));
    QCOMPARE(panel.assetCount(), 1);
    QCOMPARE(panel.assetNameAt(0), QStringLiteral("dog.png"));

    // Search and tag filter combine.
    panel.setSearchText(QStringLiteral("cat"));
    QCOMPARE(panel.assetCount(), 0);

    panel.setSearchText(QString());
    panel.setTagFilter(QString());
    QCOMPARE(panel.tagFilter(), QString());
    QCOMPARE(panel.assetCount(), 2);

    // An unknown tag falls back to "all tags".
    panel.setTagFilter(QStringLiteral("不存在"));
    QCOMPARE(panel.tagFilter(), QString());
    QCOMPARE(panel.assetCount(), 2);
}

void AssetPanelTest::editTagsCommitsOnce()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    AssetPanel panel(&document);

    const QString path = directory.path() + QStringLiteral("/tag.png");
    QVERIFY(writePng(path, Qt::magenta));
    QString error;
    QVERIFY2(panel.importPaths({path}, &error), qPrintable(error));

    const QString hash = panel.assetHashAt(0);
    const int before = document.commits().size();
    QVERIFY2(panel.setAssetTags(hash, {QStringLiteral("a"), QStringLiteral("b")}, &error),
             qPrintable(error));

    QCOMPARE(document.commits().size(), before + 1);
    const QString message = document.commits().last();
    QVERIFY(message.startsWith(QStringLiteral("pvf: edit asset tags \"")));
    QVERIFY(message.contains(QStringLiteral("tag.png")));
    QCOMPARE(panel.assetTagsAt(0),
             QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY(document.assets().get(hash)->tags
            == QStringList({QStringLiteral("a"), QStringLiteral("b")}));
}

void AssetPanelTest::missingAssetTagsNotEditable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());

    const QString hash = QStringLiteral("b").repeated(64);
    pvf::model::Entity entity;
    entity.uuid = QStringLiteral("entity-missing-tags");
    entity.name = QStringLiteral("missing");
    entity.content.kind = pvf::ContentKind::Asset;
    entity.content.asset.hash = hash;
    entity.content.asset.type = pvf::AssetType::Image;
    document.mutableProject().entities.insert(entity.uuid, entity);

    AssetPanel panel(&document);
    QCOMPARE(panel.assetCount(), 1);
    QVERIFY(panel.assetMissingAt(0));

    QString error;
    QVERIFY(!panel.setAssetTags(hash, {QStringLiteral("x")}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(document.commits().isEmpty());

    bool foundTagButton = false;
    for (QPushButton* button : panel.findChildren<QPushButton*>())
    {
        if (button->text().startsWith(QStringLiteral("标签")))
        {
            foundTagButton = true;
            QVERIFY(!button->isEnabled());
        }
    }
    QVERIFY(foundTagButton);
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

void AssetPanelTest::videoImportShowsProgressAndCancel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString video = makeVideo(
        directory.path() + QStringLiteral("/clip.mp4"), 8);
    if (video.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成测试视频");

    FakeDocument document(directory.path());
    document.mutableProject().settings.fps = 30;
    AssetPanel panel(&document);
    panel.resize(320, 480);

    QString error;
    QVERIFY2(panel.importPaths({video}, &error), qPrintable(error));
    QCOMPARE(panel.assetCount(), 1);

    QTRY_VERIFY_WITH_TIMEOUT(panel.progressRowCount() > 0, 15000);
    QVERIFY(panel.progressTextAt(0).contains(QStringLiteral("抽帧")));
    QVERIFY(panel.findChild<QProgressBar*>() != nullptr);

    QPushButton* cancel = panel.progressCancelButtonAt(0);
    QVERIFY(cancel);
    QVERIFY(cancel->isEnabled());
    QVERIFY(QFileInfo(directory.path() + QStringLiteral("/.cache/video")).isDir());

    cancel->click();
    QTRY_COMPARE_WITH_TIMEOUT(panel.progressRowCount(), 0, 20000);
}

void AssetPanelTest::videoImportCompletesAndHides()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString video = makeVideo(
        directory.path() + QStringLiteral("/short.mp4"), 1);
    if (video.isEmpty())
        QSKIP("ffmpeg 不可用或无法生成测试视频");

    FakeDocument document(directory.path());
    document.mutableProject().settings.fps = 30;
    AssetPanel panel(&document);

    QString error;
    QVERIFY2(panel.importPaths({video}, &error), qPrintable(error));
    QCOMPARE(panel.assetCount(), 1);
    const QString hash = panel.assetHashAt(0);

    QTRY_VERIFY_WITH_TIMEOUT(panel.progressRowCount() > 0, 15000);

    const QString index = indexFileFor(directory.path(), hash);
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo(index).isFile()
            && readJson(index).value(QStringLiteral("frames")).toObject().size()
                   == 30,
        60000);

    // Completed rows stay visible briefly, then disappear.
    QTRY_COMPARE_WITH_TIMEOUT(panel.progressRowCount(), 0, 6000);

    const QJsonObject root = readJson(index);
    QCOMPARE(root.value(QStringLiteral("totalFrames")).toInt(), 30);
    QCOMPARE(root.value(QStringLiteral("projectFps")).toInt(), 30);
    QCOMPARE(root.value(QStringLiteral("maxWidth")).toInt(), 960);
}

void AssetPanelTest::videoExtractionFailuresStackAndClose()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    AssetPanel panel(&document);

    auto* queue = panel.findChild<pvf::video2::VideoExtractionQueue*>();
    QVERIFY(queue);

    pvf::video2::ExtractionJob first;
    first.assetHash = QStringLiteral("failurehash1");
    first.videoPath = directory.path() + QStringLiteral("/missing-1.mp4");
    first.cacheRoot = directory.path() + QStringLiteral("/.cache/video");
    first.maxWidth = 160;

    pvf::video2::ExtractionJob second = first;
    second.assetHash = QStringLiteral("failurehash2");
    second.videoPath = directory.path() + QStringLiteral("/missing-2.mp4");

    queue->enqueue(first);
    queue->enqueue(second);

    QTRY_COMPARE_WITH_TIMEOUT(panel.progressRowCount(), 2, 30000);
    QVERIFY(panel.progressTextAt(0).contains(QStringLiteral("抽帧失败")));
    QVERIFY(panel.progressTextAt(1).contains(QStringLiteral("抽帧失败")));

    QPushButton* close = panel.progressCancelButtonAt(0);
    QVERIFY(close);
    QCOMPARE(close->text(), QStringLiteral("关闭"));
    close->click();
    QTRY_COMPARE(panel.progressRowCount(), 1);

    QPushButton* closeLast = panel.progressCancelButtonAt(0);
    QVERIFY(closeLast);
    closeLast->click();
    QTRY_COMPARE(panel.progressRowCount(), 0);
}

QTEST_MAIN(AssetPanelTest)

#include "AssetPanelTest.moc"
