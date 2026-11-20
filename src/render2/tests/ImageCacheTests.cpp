// Image decode cache and AssetStore metadata cache behaviour (issue #131).
//
// Separate executable so the existing render2 test files stay inside the
// code-size ratchet.

#include "assets/Assets.h"
#include "render2/ImageCache.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

using namespace pvf;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    ++gChecks;
    if (condition)
        return;

    ++gFailures;
    QTextStream(stderr) << QStringLiteral("FAIL %1:%2  %3")
                                .arg(QString::fromLatin1(file))
                                .arg(line)
                                .arg(QString::fromLatin1(expression))
                        << '\n';
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

QString writePng(const QString& path, int size, const QColor& color)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(color);
    return image.save(path) ? path : QString();
}

// Two calls for the same asset decode once; a new asset decodes again.
void imageCacheHits()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    assets::AssetStore store(temp.path());
    const QString first =
        writePng(temp.filePath(QStringLiteral("a.png")), 4, QColor(255, 0, 0));
    const QString second =
        writePng(temp.filePath(QStringLiteral("b.png")), 4, QColor(0, 0, 255));
    const auto importedFirst = store.importFile(first);
    const auto importedSecond = store.importFile(second);
    CHECK(importedFirst.has_value());
    CHECK(importedSecond.has_value());
    if (!importedFirst || !importedSecond)
        return;

    render2::clearImageCache();
    const render2::Picture one =
        render2::loadPicture(store, importedFirst->hash);
    CHECK(one.isValid());
    CHECK(one.size == QSize(4, 4));

    const render2::Picture again =
        render2::loadPicture(store, importedFirst->hash);
    CHECK(again.isValid());
    CHECK(again.image.cacheKey() == one.image.cacheKey());

    const render2::ImageCacheStats afterHits = render2::imageCacheStats();
    CHECK(afterHits.hits == 1);
    CHECK(afterHits.decodes == 1);
    CHECK(afterHits.entries == 1);

    const render2::Picture other =
        render2::loadPicture(store, importedSecond->hash);
    CHECK(other.isValid());
    const render2::ImageCacheStats afterOther = render2::imageCacheStats();
    CHECK(afterOther.decodes == 2);
    CHECK(afterOther.entries == 2);
}

// Replacing the content file invalidates the decoded picture: the key carries
// path + size + mtime, so a re-import cannot serve stale pixels.
void replacingContentInvalidates()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    assets::AssetStore store(temp.path());
    const QString source =
        writePng(temp.filePath(QStringLiteral("c.png")), 4, QColor(0, 255, 0));
    const auto imported = store.importFile(source);
    CHECK(imported.has_value());
    if (!imported)
        return;

    render2::clearImageCache();
    const render2::Picture before = render2::loadPicture(store, imported->hash);
    CHECK(before.isValid());

    const QStringList files = QDir(imported->directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    CHECK(!files.isEmpty());
    if (files.isEmpty())
        return;
    const QString content =
        imported->directory + QLatin1Char('/') + files.first();
    QFile::remove(content);
    QImage replacement(6, 6, QImage::Format_ARGB32);
    replacement.fill(QColor(255, 255, 0));
    CHECK(replacement.save(content));
    // The adapter key includes size+mtime.  A real replacement always lands at
    // a different time; force it so the test does not depend on filesystem
    // timestamp resolution (two writes may share the same millisecond).
    {
        QFile stamp(content);
        CHECK(stamp.open(QIODevice::ReadWrite));
        CHECK(stamp.setFileTime(QDateTime::currentDateTime().addSecs(2),
                                QFileDevice::FileModificationTime));
    }

    const render2::Picture after = render2::loadPicture(store, imported->hash);
    CHECK(after.isValid());
    CHECK(after.image.size() == QSize(6, 6));
    CHECK(render2::imageCacheStats().decodes == 2);
}

// Editing asset.json out of band must be seen by the next get(); the metadata
// cache is keyed by size+mtime, not by hash alone.
void metadataCacheSeesExternalEdits()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    assets::AssetStore store(temp.path());
    const QString source =
        writePng(temp.filePath(QStringLiteral("d.png")), 4, QColor(10, 20, 30));
    const auto imported = store.importFile(source);
    CHECK(imported.has_value());
    if (!imported)
        return;

    const auto before = store.get(imported->hash);
    CHECK(before.has_value());
    CHECK(before->name != QStringLiteral("renamed externally"));
    // Fill the cache once more so the entry is definitely stored.
    CHECK(store.get(imported->hash).has_value());

    const QString metadataPath =
        imported->directory + QStringLiteral("/asset.json");
    QFile file(metadataPath);
    CHECK(file.open(QIODevice::ReadOnly));
    QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    object.insert(QStringLiteral("name"),
                  QStringLiteral("renamed externally"));
    file.setFileName(metadataPath + QStringLiteral(".tmp"));
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray bytes =
        QJsonDocument(object).toJson(QJsonDocument::Indented);
    CHECK(file.write(bytes) == bytes.size());
    file.close();
    QFile::remove(metadataPath);
    CHECK(QFile::rename(metadataPath + QStringLiteral(".tmp"), metadataPath));

    const auto after = store.get(imported->hash);
    CHECK(after.has_value());
    CHECK(after && after->name == QStringLiteral("renamed externally"));
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    imageCacheHits();
    replacingContentInvalidates();
    metadataCacheSeesExternalEdits();

    QTextStream(stdout) << "render2 cache tests: " << gChecks << " checks, "
                        << gFailures << " failures\n";
    return gFailures == 0 ? 0 : 1;
}
