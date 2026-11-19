#include "next/app/PlaybackController.h"

#include "next/core/Document.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

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
    entity.end = 1000000;
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

    // Issue #130: the playhead is derived from monotonic elapsed time, not from
    // one frame per timer tick.  Five minutes of deliberately irregular ticks
    // must stay within one frame of the wall clock at 60 and 24 fps.
    void clockTracksElapsedTimeAt60Fps()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);
        document.mutableProject().settings.fps = 60;

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(600000);

        qint64 nowNs = 0;
        playback.setClockForTest([&nowNs] { return nowNs; });
        playback.play();
        QVERIFY(playback.isPlaying());
        QCOMPARE(document.playhead(), 0);

        constexpr int fps = 60;
        constexpr qint64 fiveMinutesNs = 5LL * 60 * 1000000000LL;
        const qint64 irregularTicks[] = {16666667LL, 17000000LL, 15000000LL,
                                         33333333LL, 8000000LL};
        int tick = 0;
        qint64 lastReported = 0;
        while (nowNs < fiveMinutesNs)
        {
            nowNs += irregularTicks[tick++ % 5];
            playback.tickForTest();
            const int expected = int((nowNs * fps) / 1000000000LL);
            QVERIFY2(std::abs(document.playhead() - expected) <= 1,
                     qPrintable(QStringLiteral("t=%1ms expected %2 got %3")
                                    .arg(nowNs / 1000000)
                                    .arg(expected)
                                    .arg(document.playhead())));
            lastReported = document.playhead();
        }
        QVERIFY(std::abs(lastReported - int((nowNs * fps) / 1000000000LL)) <= 1);
        QVERIFY(playback.isPlaying());
    }

    void clockTracksElapsedTimeAt24Fps()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);
        document.mutableProject().settings.fps = 24;

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(600000);

        qint64 nowNs = 0;
        playback.setClockForTest([&nowNs] { return nowNs; });
        playback.play();

        constexpr int fps = 24;
        for (int second = 0; second < 300; ++second)
        {
            // 41 ms / 42 ms alternating: the old integer interval gave 24.39
            // fps and drifted ~5 s over five minutes.
            nowNs += (second % 2 == 0 ? 41000000LL : 42000000LL);
            playback.tickForTest();
            const int expected = int((nowNs * fps) / 1000000000LL);
            QVERIFY(std::abs(document.playhead() - expected) <= 1);
        }
        const int expected =
            int((nowNs * fps) / 1000000000LL);
        QVERIFY(std::abs(document.playhead() - expected) <= 1);
    }

    void stallSkipsFramesInsteadOfSlowingDown()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);
        document.mutableProject().settings.fps = 60;

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(600000);

        qint64 nowNs = 0;
        playback.setClockForTest([&nowNs] { return nowNs; });
        playback.play();

        nowNs = 500000000LL; // 0.5 s stall
        playback.tickForTest();
        QCOMPARE(document.playhead(), 30); // 60 fps: 30 frames in 500 ms
    }

    void manualSeekReanchorsTheClock()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);
        document.mutableProject().settings.fps = 60;

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(600000);

        qint64 nowNs = 0;
        playback.setClockForTest([&nowNs] { return nowNs; });
        playback.play();

        nowNs = 2000000000LL; // 2 s in
        playback.tickForTest();
        QCOMPARE(document.playhead(), 120);

        document.setPlayhead(500);
        nowNs += 1000000000LL; // 1 s after the seek
        playback.tickForTest();
        QCOMPARE(document.playhead(), 560);
    }

    void clockNeverRewindsThePlayhead()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(600000);

        qint64 nowNs = 0;
        playback.setClockForTest([&nowNs] { return nowNs; });
        playback.play();

        nowNs = 1000000000LL;
        playback.tickForTest();
        QCOMPARE(document.playhead(), 30);
        nowNs = 100000000LL; // bogus backwards reading
        playback.tickForTest();
        QCOMPARE(document.playhead(), 30);
    }

    void clockStopsAtContentEnd()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FakeDocument document(temp.path());
        addLongClip(document);

        PlaybackController playback;
        playback.setDocument(&document);
        playback.setWatchdogMs(600000);

        qint64 nowNs = 0;
        playback.setClockForTest([&nowNs] { return nowNs; });
        playback.play();

        nowNs = 10000000LL * 1000000000LL; // far past the clip end
        playback.tickForTest();
        QVERIFY(!playback.isPlaying());
        QCOMPARE(document.playhead(), 1000000);
    }
};

QTEST_MAIN(PlaybackControllerTests)
#include "PlaybackControllerTests.moc"
