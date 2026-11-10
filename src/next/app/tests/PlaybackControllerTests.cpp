#include "next/app/PlaybackController.h"

#include "next/core/Document.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;
using namespace pvf::next;
using namespace pvf::model;

namespace {

// Minimal Document with a real AssetStore so PlaybackController can build its
// (best-effort, silent) audio object without a project store or git.
class FakeDocument : public Document
{
public:
    explicit FakeDocument(const QString& root)
        : m_assets(root)
    {
    }

    const Project& project() const override { return m_project; }
    Project& mutableProject() override { return m_project; }
    assets::AssetStore& assets() override { return m_assets; }

    int playhead() const override { return m_playhead; }
    void setPlayhead(int frame) override
    {
        const int clamped = frame < 0 ? 0 : frame;
        if (clamped == m_playhead)
            return;
        m_playhead = clamped;
        emit playheadChanged();
    }

    Uuid selection() const override { return m_selection; }
    void setSelection(const Uuid& uuid) override { m_selection = uuid; }

    bool commitEntity(const Uuid&, const QString&, QString* error) override
    {
        if (error)
            error->clear();
        return true;
    }

    bool commitProject(const QString&, QString* error) override
    {
        if (error)
            error->clear();
        return true;
    }

    bool undo(QString*) override { return false; }
    bool redo(QString*) override { return false; }

private:
    assets::AssetStore m_assets;
    Project m_project;
    int m_playhead = 0;
    Uuid m_selection;
};

void addLongClip(FakeDocument& document)
{
    Entity entity;
    entity.uuid = newUuid();
    entity.name = QStringLiteral("clip");
    entity.start = 0;
    entity.end = 10000;
    entity.row = 0;
    document.mutableProject().entities.insert(entity.uuid, entity);
}

} // namespace

class PlaybackControllerTests : public QObject
{
    Q_OBJECT

private slots:
    void stallsWithoutPresentedFrames()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(100);

        QSignalSpy stalled(&playback, &PlaybackController::stalled);
        playback.play();
        QVERIFY(playback.isPlaying());

        // The clock timer keeps advancing the playhead, but without a real
        // viewport frame the watchdog must still pause playback.
        QTRY_VERIFY_WITH_TIMEOUT(!playback.isPlaying(), 2000);
        QCOMPARE(stalled.count(), 1);
        QCOMPARE(stalled.at(0).at(0).toString(),
                 QStringLiteral("帧渲染超时，已暂停"));

        // The watchdog sleeps while paused: no repeated signal.
        QTest::qWait(200);
        QCOMPARE(stalled.count(), 1);
    }

    void presentedFramesKeepPlaying()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(2000);

        QSignalSpy stalled(&playback, &PlaybackController::stalled);
        const int before = document.playhead();
        playback.play();
        for (int i = 0; i < 5; ++i) {
            QTest::qWait(100);
            playback.noteFramePresented();
        }

        QVERIFY(playback.isPlaying());
        QVERIFY(document.playhead() > before);
        QCOMPARE(stalled.count(), 0);
    }

    void framePresentedResetsWatchdog()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(1500);

        QSignalSpy stalled(&playback, &PlaybackController::stalled);
        playback.play();
        QTest::qWait(500);
        playback.noteFramePresented();
        QTest::qWait(500);

        // 1000 ms since play but only 500 ms since the presented frame.
        QVERIFY(playback.isPlaying());
        QCOMPARE(stalled.count(), 0);
    }

    void pausedWatchdogIsSilent()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(100);

        QSignalSpy stalled(&playback, &PlaybackController::stalled);
        playback.play();
        playback.pause();
        QTest::qWait(250);

        QVERIFY(!playback.isPlaying());
        QCOMPARE(stalled.count(), 0);
    }

    void manualSeekResetsStallClock()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(1000);

        QSignalSpy stalled(&playback, &PlaybackController::stalled);
        playback.play();
        QTest::qWait(400);
        document.setPlayhead(500);

        // Only 400 ms have passed since the seek, not since play().
        QTest::qWait(400);
        QVERIFY(playback.isPlaying());
        QCOMPARE(stalled.count(), 0);

        // With no new frame after the seek the watchdog eventually fires.
        QTRY_VERIFY_WITH_TIMEOUT(!playback.isPlaying(), 3000);
        QCOMPARE(stalled.count(), 1);
    }

    void watchdogMsIsSynchronised()
    {
        PlaybackController playback;
        QCOMPARE(playback.watchdogMs(), 3000);

        playback.setWatchdogMs(1500);
        QCOMPARE(playback.watchdogMs(), 1500);

        playback.setWatchdogMs(100);
        QCOMPARE(playback.watchdogMs(), 100);
    }
};

QTEST_MAIN(PlaybackControllerTests)
#include "PlaybackControllerTests.moc"
