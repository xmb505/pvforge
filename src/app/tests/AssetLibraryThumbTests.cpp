#include "app/EditorSession.h"
#include "next/app/ProjectDocument.h"
#include "ui/AssetLibraryPanel.h"

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QImage>
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

QImage solidImage(const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    return image;
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

private:
    std::unique_ptr<QTemporaryDir> m_temp;
    std::unique_ptr<pvf::EditorSession> m_session;
    std::unique_ptr<pvf::AssetLibraryPanel> m_panel;
    pvf::AssetHash m_htmlHash;
};

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

    m_panel = std::make_unique<pvf::AssetLibraryPanel>(m_session.get());
    m_panel->refresh();
    QCOMPARE(m_panel->assetCount(), 1);
    QCOMPARE(m_panel->assetHashAt(0), m_htmlHash);
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
    // asked the renderer once for a low-resolution frame.
    const QPixmap icon = m_panel->assetThumbnailAt(0);
    QVERIFY(!icon.isNull());
    QVERIFY(renders >= 1);
    QCOMPARE(requestedHash, m_htmlHash);
    QVERIFY(requested.width() >= 48 && requested.height() >= 48);
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

    QVERIFY(m_panel->assetThumbnailAt(0).toImage()
            != solidImage(QSize(48, 48), QColor(QStringLiteral("#ff0000"))));
    QVERIFY(renders >= 1);

    const int refreshes = m_panel->htmlThumbnailRefreshCountForTest();
    frame = solidImage(QSize(240, 36), QColor(QStringLiteral("#ff0000")));
    // The frameReady callback drops the memoised icons and rebuilds the list,
    // so the next lookup returns the real image instead of the placeholder.
    m_panel->handleHtmlFrameReadyForTest();
    QVERIFY(m_panel->htmlThumbnailRefreshCountForTest() > refreshes);

    const QPixmap real = m_panel->assetThumbnailAt(0);
    QVERIFY(renders >= 2);
    QVERIFY(!real.isNull());

    const QImage got = real.toImage();
    const QColor center = got.pixelColor(got.width() / 2, got.height() / 2);
    QVERIFY2(center.red() > 200 && center.green() < 60 && center.blue() < 60,
             qPrintable(center.name()));

    // Cached: another icon lookup does not ask the renderer again.
    const int before = renders;
    m_panel->assetThumbnailAt(0);
    QCOMPARE(renders, before);
}

void AssetLibraryThumbTests::htmlHoverPreviewRefreshesAfterRender()
{
    QImage frame;
    int renders = 0;
    m_panel->setHtmlThumbnailRendererForTest(
        [&](const pvf::AssetHash&, const QSize&) {
            ++renders;
            return frame; // already the requested size
        });

    m_panel->showHoverPreviewAtForTest(0);
    QFrame* hover = m_panel->hoverPreviewForTest();
    QVERIFY(hover != nullptr);
    const int refreshes = m_panel->htmlThumbnailRefreshCountForTest();

    // The render completes and htmlFrameReady fires: the list is rebuilt and
    // the open hover card is re-asked for its preview.  The card is still
    // alive and now holds the finished thumbnail.
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
    m_panel->showHoverPreviewAtForTest(0);

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

QTEST_MAIN(AssetLibraryThumbTests)
#include "AssetLibraryThumbTests.moc"
