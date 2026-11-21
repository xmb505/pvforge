#include "app/EditorSession.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

QString q(const char* text)
{
    return QString::fromUtf8(text);
}

// Minimal HTML bundle.  The prefetch tests never render it: they assert that
// EditorSession asks MediaServices::prefetchHtml() for the right frame on the
// open / edit / playhead paths.  MediaServices then drops the batch itself
// because no browser is running (MediaServicesTests covers the real renders).
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

} // namespace

class HtmlPrefetchTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void openProjectPrefetchesCurrentFrame();
    void playheadMovePrefetchesWithoutForce();
    void projectEditPrefetchesAfterDebounce();
    void playbackStartPrefetches();

private:
    // Reopens the session, so a test can observe the open-time prefetch.
    void reopenSession();

    std::unique_ptr<QTemporaryDir> m_temp;
    std::unique_ptr<pvf::EditorSession> m_session;
    pvf::AssetHash m_htmlHash;
};

void HtmlPrefetchTests::initTestCase()
{
    pvf::EditorSession::setMcpPortOverrideForTest(0);
}

void HtmlPrefetchTests::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    pvf::EditorSession::setMcpConfigPathForTest(
        m_temp->filePath(QStringLiteral("mcp.json")));

    pvf::model::ProjectSettings settings;
    settings.name = q("html-prefetch-tests");
    settings.fps = 30;
    QString error;
    m_session = pvf::EditorSession::createProject(
        m_temp->filePath(QStringLiteral("prefetch.pvproj")), settings, &error,
        false);
    QVERIFY2(m_session != nullptr, qPrintable(error));

    const QString bundle = makeBundle(m_temp->filePath(QStringLiteral("bundle")));
    QVERIFY(!bundle.isEmpty());
    const auto stored = m_session->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    m_htmlHash = stored->hash;

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = q("HTML");
    entity.start = 10;
    entity.end = 60;
    entity.content.kind = pvf::ContentKind::Asset;
    entity.content.asset.hash = m_htmlHash;
    entity.content.asset.type = pvf::AssetType::Html;
    m_session->document()->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(m_session->document()
                 ->commitEntity(entity.uuid, q("setup"), &error),
             qPrintable(error));
    m_session->flush();
}

void HtmlPrefetchTests::cleanup()
{
    m_session.reset();
    m_temp.reset();
}

void HtmlPrefetchTests::reopenSession()
{
    const QString path = m_session->root();
    m_session->flush();
    m_session.reset();
    QString error;
    m_session = pvf::EditorSession::openProject(path, &error, false);
    QVERIFY2(m_session != nullptr, qPrintable(error));
}

void HtmlPrefetchTests::openProjectPrefetchesCurrentFrame()
{
    m_session->setPlayhead(25);
    m_session->flush();

    reopenSession();

    // Opening (after the HTML service is up) warms the saved playhead once and
    // forces the batch, because nothing is cached for this session yet.
    QCOMPARE(m_session->htmlPrefetchCountForTest(), 1);
    QCOMPARE(m_session->htmlPrefetchLastFrameForTest(), 25);
    QVERIFY(m_session->htmlPrefetchLastForceForTest());
}

void HtmlPrefetchTests::playheadMovePrefetchesWithoutForce()
{
    const int before = m_session->htmlPrefetchCountForTest();

    m_session->setPlayhead(30);
    QCOMPARE(m_session->htmlPrefetchCountForTest(), before + 1);
    QCOMPARE(m_session->htmlPrefetchLastFrameForTest(), 30);
    // Scrubbing never forces: a repeat of the same frame stays a no-op.
    QVERIFY(!m_session->htmlPrefetchLastForceForTest());

    m_session->setPlayhead(35);
    QCOMPARE(m_session->htmlPrefetchCountForTest(), before + 2);
    QCOMPARE(m_session->htmlPrefetchLastFrameForTest(), 35);
}

void HtmlPrefetchTests::projectEditPrefetchesAfterDebounce()
{
    m_session->setPlayhead(40);
    m_session->flushHtmlPrefetchDebounceForTest();
    const int before = m_session->htmlPrefetchCountForTest();

    // An edit does not prefetch synchronously: the timer coalesces a burst.
    pvf::model::Entity edited = m_session->project().entities.values().first();
    edited.name = q("HTML renamed");
    QString error;
    QVERIFY2(m_session->document()->commitEntity(edited.uuid, q("rename"),
                                                 &error),
             qPrintable(error));
    QCOMPARE(m_session->htmlPrefetchCountForTest(), before);

    // A second edit inside the window is folded into the same run.
    edited.name = q("HTML renamed twice");
    QVERIFY2(m_session->document()->commitEntity(edited.uuid, q("rename 2"),
                                                 &error),
             qPrintable(error));
    QCOMPARE(m_session->htmlPrefetchCountForTest(), before);

    m_session->flushHtmlPrefetchDebounceForTest();
    QCOMPARE(m_session->htmlPrefetchCountForTest(), before + 1);
    QCOMPARE(m_session->htmlPrefetchLastFrameForTest(), 40);
    // Edits invalidate the cache: this run must rebuild the batch.
    QVERIFY(m_session->htmlPrefetchLastForceForTest());

    // The debounce also fires on its own (120 ms), without the test flush.
    edited.name = q("HTML renamed by timer");
    QVERIFY2(m_session->document()->commitEntity(edited.uuid, q("rename 3"),
                                                 &error),
             qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(m_session->htmlPrefetchCountForTest(), before + 2,
                              2000);
    QVERIFY(m_session->htmlPrefetchLastForceForTest());
}

void HtmlPrefetchTests::playbackStartPrefetches()
{
    m_session->setPlayhead(12);
    const int before = m_session->htmlPrefetchCountForTest();

    pvf::next::PlaybackController* playback = m_session->playback();
    QVERIFY(playback != nullptr);
    playback->play();
    QVERIFY(playback->isPlaying());
    QCOMPARE(m_session->htmlPrefetchCountForTest(), before + 1);
    QCOMPARE(m_session->htmlPrefetchLastFrameForTest(), 12);
    QVERIFY(!m_session->htmlPrefetchLastForceForTest());
    playback->stop();
}

QTEST_MAIN(HtmlPrefetchTests)
#include "HtmlPrefetchTests.moc"
