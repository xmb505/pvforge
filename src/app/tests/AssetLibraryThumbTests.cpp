#include "app/EditorSession.h"
#include "next/app/ProjectDocument.h"
#include "ui/AssetLibraryPanel.h"

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QListWidgetItem>
#include <QPalette>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

QString q(const char* text)
{
    return QString::fromUtf8(text);
}

// Minimal HTML bundle: the panel never renders it in these tests, the asset
// store only needs a content file for the session-mode row to appear.
QString makeBundle(const QString& directory)
{
    if (!QDir().mkpath(directory))
        return {};
    QFile file(QDir(directory).filePath(QStringLiteral("index.html")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write("<!doctype html><html><body></body></html>");
    file.close();
    return directory;
}

// A real image file for the non-HTML row: iconFor() reads it synchronously, so
// the row has an icon the refresh must leave completely alone.
QString makeImage(const QString& path)
{
    QImage image(32, 24, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#204080")));
    return image.save(path, "PNG") ? path : QString();
}

QImage solidImage(const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

// #c02020 is r=192: a clearly red, low-green frame; a scaled placeholder or a
// grey HTML card never matches.
bool isReddish(const QColor& color)
{
    return color.red() > 150 && color.green() < 90 && color.blue() < 90;
}

} // namespace

class AssetLibraryThumbTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void htmlIconFallsBackToPlaceholderWithoutRender();
    void htmlIconUsesThumbnailOnceRendered();
    void htmlHoverPreviewRefreshesAfterRender();
    void hoverPreviewUsesNativePalette();
    void htmlThumbnailRendersAtHoverSizeOnce();
    void frameReadyKeepsRowsAndForeignIcons();

private:
    // Row order follows the asset store listing; look rows up by hash instead
    // of assuming one.
    int rowOf(const pvf::AssetHash& hash) const;
    QPixmap iconOf(const pvf::AssetHash& hash) const;

    std::unique_ptr<QTemporaryDir> m_temp;
    std::unique_ptr<pvf::EditorSession> m_session;
    std::unique_ptr<pvf::AssetLibraryPanel> m_panel;
    pvf::AssetHash m_htmlHash;
    pvf::AssetHash m_imageHash;
};

int AssetLibraryThumbTests::rowOf(const pvf::AssetHash& hash) const
{
    for (int row = 0; row < m_panel->assetCount(); ++row)
        if (m_panel->assetHashAt(row) == hash)
            return row;
    return -1;
}

QPixmap AssetLibraryThumbTests::iconOf(const pvf::AssetHash& hash) const
{
    const int row = rowOf(hash);
    return row < 0 ? QPixmap() : m_panel->assetThumbnailAt(row);
}

void AssetLibraryThumbTests::initTestCase()
{
    pvf::EditorSession::setMcpPortOverrideForTest(0);
}

void AssetLibraryThumbTests::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    pvf::EditorSession::setMcpConfigPathForTest(
        m_temp->filePath(QStringLiteral("mcp.json")));

    pvf::model::ProjectSettings settings;
    settings.name = q("asset-thumb-tests");
    settings.fps = 30;
    QString error;
    m_session = pvf::EditorSession::createProject(
        m_temp->filePath(QStringLiteral("thumb.pvproj")), settings, &error, false);
    QVERIFY2(m_session != nullptr, qPrintable(error));

    const QString bundle = makeBundle(m_temp->filePath(QStringLiteral("bundle")));
    QVERIFY(!bundle.isEmpty());
    const auto stored = m_session->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    m_htmlHash = stored->hash;

    // One non-HTML row: its icon proves the HTML refresh leaves other entries
    // and the rest of the icon cache untouched.
    const QString imagePath =
        makeImage(m_temp->filePath(QStringLiteral("base.png")));
    QVERIFY(!imagePath.isEmpty());
    const auto image = m_session->assets().importFile(imagePath, &error);
    QVERIFY2(image.has_value(), qPrintable(error));
    m_imageHash = image->hash;
    QVERIFY(!m_imageHash.isEmpty());

    m_panel = std::make_unique<pvf::AssetLibraryPanel>(m_session.get());
    m_panel->refresh();
    QCOMPARE(m_panel->assetCount(), 2);
    QVERIFY(rowOf(m_htmlHash) >= 0);
    QVERIFY(rowOf(m_imageHash) >= 0);
}

void AssetLibraryThumbTests::cleanup()
{
    m_panel.reset();
    m_session.reset();
    m_temp.reset();
}

void AssetLibraryThumbTests::htmlIconFallsBackToPlaceholderWithoutRender()
{
    QImage requested;
    QString requestedHash;
    int renders = 0;
    m_panel->setHtmlThumbnailRendererForTest(
        [&](const pvf::AssetHash& hash, const QSize& size) -> QImage {
            requestedHash = hash;
            requested = QImage(size.expandedTo(QSize(1, 1)),
                               QImage::Format_ARGB32);
            requested.fill(Qt::black);
            ++renders;
            return QImage(); // still rendering
        });

    // No finished frame yet: the row shows the HTML placeholder, and the miss
    // asked the renderer once for the 240x150 base frame.
    const QPixmap icon = iconOf(m_htmlHash);
    QVERIFY(!icon.isNull());
    QVERIFY(renders >= 1);
    QCOMPARE(requestedHash, m_htmlHash);
    QCOMPARE(requested.size(), QSize(240, 150));
    // The placeholder carries the "HTML" caption; a rendered frame does not
    // (checked below), so this is the observable "still waiting" state.
    const QImage placeholder = icon.toImage();
    QVERIFY(placeholder.pixelColor(placeholder.width() / 2,
                                   placeholder.height() / 2)
                != QColor(Qt::black));
}

void AssetLibraryThumbTests::htmlIconUsesThumbnailOnceRendered()
{
    QImage frame;
    int renders = 0;
    m_panel->setHtmlThumbnailRendererForTest(
        [&](const pvf::AssetHash&, const QSize&) {
            ++renders;
            return frame; // null until the test "finishes" the render
        });

    QVERIFY(!iconOf(m_htmlHash).isNull());
    QVERIFY(renders >= 1);

    const int refreshes = m_panel->htmlThumbnailRefreshCountForTest();
    frame = solidImage(QSize(240, 36), QColor(QStringLiteral("#ff0000")));
    // The frameReady callback re-icons just the finished HTML row, so the next
    // lookup returns the real image instead of the placeholder.
    m_panel->handleHtmlFrameReadyForTest();
    QVERIFY(m_panel->htmlThumbnailRefreshCountForTest() > refreshes);

    const QPixmap real = iconOf(m_htmlHash);
    QVERIFY(renders >= 2);
    QVERIFY(!real.isNull());

    const QImage got = real.toImage();
    const QColor center = got.pixelColor(got.width() / 2, got.height() / 2);
    QVERIFY2(isReddish(center), qPrintable(center.name()));

    // Cached: another icon lookup does not ask the renderer again.
    const int before = renders;
    iconOf(m_htmlHash);
    QCOMPARE(renders, before);
}

void AssetLibraryThumbTests::htmlHoverPreviewRefreshesAfterRender()
{
    QImage frame;
    int renders = 0;
    m_panel->setHtmlThumbnailRendererForTest(
        [&](const pvf::AssetHash&, const QSize& size) {
            ++renders;
            // "already the requested size"
            return frame.isNull() ? frame : frame.scaled(size);
        });

    m_panel->showHoverPreviewAtForTest(rowOf(m_htmlHash));
    QFrame* hover = m_panel->hoverPreviewForTest();
    QVERIFY(hover != nullptr);
    const int refreshes = m_panel->htmlThumbnailRefreshCountForTest();

    // The render completes and htmlFrameReady fires: the open hover card is
    // re-asked for its preview.  The card is still alive and now holds the
    // finished thumbnail.
    frame = solidImage(QSize(240, 36), QColor(QStringLiteral("#00c000")));
    m_panel->handleHtmlFrameReadyForTest();
    QVERIFY(m_panel->htmlThumbnailRefreshCountForTest() > refreshes);
    QCOMPARE(m_panel->hoverPreviewForTest(), hover);
    QVERIFY(renders >= 2);
}

void AssetLibraryThumbTests::hoverPreviewUsesNativePalette()
{
    m_panel->setHtmlThumbnailRendererForTest(
        [](const pvf::AssetHash&, const QSize& size) {
            return solidImage(size.expandedTo(QSize(1, 1)),
                              QColor(QStringLiteral("#3366cc")));
        });
    m_panel->showHoverPreviewAtForTest(rowOf(m_htmlHash));

    QFrame* hover = m_panel->hoverPreviewForTest();
    QVERIFY(hover != nullptr);
    // No hardcoded dark sheet any more: the card is a plain native frame and
    // its colours come from the application palette.
    QVERIFY2(hover->styleSheet().isEmpty(), qPrintable(hover->styleSheet()));
    QCOMPARE(hover->frameShape(), QFrame::StyledPanel);
    QVERIFY(hover->autoFillBackground());

    const QPalette palette = hover->palette();
    const QPalette appPalette = QGuiApplication::palette();
    QCOMPARE(palette.color(QPalette::Window),
             appPalette.color(QPalette::ToolTipBase));
    QCOMPARE(palette.color(QPalette::WindowText),
             appPalette.color(QPalette::ToolTipText));
    QCOMPARE(palette.color(QPalette::Mid), appPalette.color(QPalette::Mid));
}

// §2: the list icon used to ask for 48x48, so Chrome cached a 48x27 frame that
// the 240x150 hover card then upscaled five times.  Both views must share one
// 240x150 render, requested exactly once.
void AssetLibraryThumbTests::htmlThumbnailRendersAtHoverSizeOnce()
{
    int renders = 0;
    QList<QSize> requests;
    m_panel->setHtmlThumbnailRendererForTest(
        [&](const pvf::AssetHash&, const QSize& size) {
            ++renders;
            requests.append(size);
            return solidImage(size, QColor(QStringLiteral("#c02020")));
        });

    const QPixmap icon = iconOf(m_htmlHash);
    QVERIFY(!icon.isNull());
    QCOMPARE(renders, 1);
    QCOMPARE(requests.first(), QSize(240, 150));

    m_panel->showHoverPreviewAtForTest(rowOf(m_htmlHash));
    QVERIFY(m_panel->hoverPreviewForTest() != nullptr);
    // The hover card reads the memoised 240x150 frame: no second render, and
    // no 48x48 request was ever made.
    QCOMPARE(renders, 1);
    QCOMPARE(requests.size(), 1);
    for (const QSize& size : requests)
        QCOMPARE(size, QSize(240, 150));
}

// §1: htmlFrameReady fires on every rendered frame while an HTML project
// plays.  The panel must never answer it by dropping the icon cache and
// rebuilding the list: the list items keep their identity, a frameReady that
// finds nothing finished keeps the memo intact, and only the row that gained a
// frame is re-iconed.
void AssetLibraryThumbTests::frameReadyKeepsRowsAndForeignIcons()
{
    QImage frame; // null: the first render is still in flight
    int renders = 0;
    m_panel->setHtmlThumbnailRendererForTest(
        [&](const pvf::AssetHash&, const QSize& size) {
            ++renders;
            return frame.isNull() ? frame : frame.scaled(size);
        });
    const int htmlRow = rowOf(m_htmlHash);
    const int imageRow = rowOf(m_imageHash);
    QListWidgetItem* htmlItem = m_panel->assetItemAtForTest(htmlRow);
    QListWidgetItem* imageItem = m_panel->assetItemAtForTest(imageRow);
    QVERIFY(htmlItem != nullptr);
    QVERIFY(imageItem != nullptr);

    // Warm both rows while the HTML frame is still rendering: the image row
    // gets a real icon, the HTML row keeps its placeholder.
    QVERIFY(!iconOf(m_imageHash).isNull());
    QVERIFY(!iconOf(m_htmlHash).isNull());
    QCOMPARE(renders, 1);
    const QImage imageIcon = m_panel->assetThumbnailAt(imageRow).toImage();
    QCOMPARE(imageIcon.pixelColor(16, 12), QColor(QStringLiteral("#204080")));
    const int cacheBefore = m_panel->iconCacheSizeForTest();
    const int refreshes = m_panel->htmlThumbnailRefreshCountForTest();

    // The frame arrives and htmlFrameReady fires for it.
    frame = solidImage(QSize(240, 150), QColor(QStringLiteral("#c02020")));
    m_panel->handleHtmlFrameReadyForTest();
    QCOMPARE(m_panel->htmlThumbnailRefreshCountForTest(), refreshes + 1);
    // Patched in place: neither row was recreated...
    QCOMPARE(m_panel->assetItemAtForTest(htmlRow), htmlItem);
    QCOMPARE(m_panel->assetItemAtForTest(imageRow), imageItem);
    // ...the non-HTML icon is still memoised and unchanged...
    QCOMPARE(m_panel->iconCacheSizeForTest(), cacheBefore);
    QCOMPARE(m_panel->assetThumbnailAt(imageRow).toImage().pixelColor(16, 12),
             QColor(QStringLiteral("#204080")));
    // ...and the HTML row carries the finished frame, still as a 48x48 icon
    // downscaled from the single 240x150 render.
    const QImage htmlNow = m_panel->assetThumbnailAt(htmlRow).toImage();
    QCOMPARE(htmlNow.size(), QSize(48, 48));
    const QColor htmlCenter =
        htmlNow.pixelColor(htmlNow.width() / 2, htmlNow.height() / 2);
    QVERIFY2(isReddish(htmlCenter), qPrintable(htmlCenter.name()));
    QCOMPARE(renders, 2); // the miss, then the icon rebuild after the frame

    // Every HTML row is cached now: each later frame is a no-op.  It must not
    // touch the icon cache, the rows, or re-ask the renderer.
    const int settled = m_panel->htmlThumbnailRefreshCountForTest();
    const int cacheSettled = m_panel->iconCacheSizeForTest();
    for (int i = 0; i < 5; ++i) // five frames, ~1/6 s of playback at 30 fps
        m_panel->handleHtmlFrameReadyForTest();
    QCOMPARE(m_panel->htmlThumbnailRefreshCountForTest(), settled);
    QCOMPARE(m_panel->iconCacheSizeForTest(), cacheSettled);
    QCOMPARE(m_panel->assetItemAtForTest(htmlRow), htmlItem);
    QCOMPARE(m_panel->assetItemAtForTest(imageRow), imageItem);
    QCOMPARE(renders, 2);
}

QTEST_MAIN(AssetLibraryThumbTests)
#include "AssetLibraryThumbTests.moc"
