// Whole-second grid tests.
//
// The timeline draws a bold grey line on every whole second frame and widens
// the spacing through 1s -> 2s -> 5s -> 10s -> 30s -> 60s when zooming out, so
// the lines never merge into a grey wall.  The step is a pure function; the
// panel checks pin the frame geometry and the rendered pixels (offscreen).

#include "TestApp.h"
#include "TestRegistry.h"

#include <algorithm>
#include <cmath>

namespace {

bool isGrey(const QColor& color)
{
    const int maxChannel =
        std::max({color.red(), color.green(), color.blue()});
    const int minChannel =
        std::min({color.red(), color.green(), color.blue()});
    return maxChannel - minChannel <= 28;
}

int channelDistance(const QColor& a, const QColor& b)
{
    return std::max({std::abs(a.red() - b.red()),
                     std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

QColor samplePixel(const QImage& image, int x, int y)
{
    const double ratio = image.devicePixelRatio();
    const int px = std::clamp(int(std::lround(x * ratio)), 0,
                              image.width() - 1);
    const int py = std::clamp(int(std::lround(y * ratio)), 0,
                              image.height() - 1);
    return image.pixelColor(px, py);
}

// A two-pixel grid line is rasterised at x-1..x; accept either plus the
// neighbours so the check is not tied to the rasteriser's rounding.
bool hasGridLine(const QImage& image, int x, int y, const QColor& background)
{
    for (int offset = -1; offset <= 1; ++offset)
    {
        const QColor pixel = samplePixel(image, x + offset, y);
        if (isGrey(pixel) && channelDistance(pixel, background) > 16)
            return true;
    }
    return false;
}

bool hasNoGridLine(const QImage& image, int x, int y, const QColor& background)
{
    for (int offset = -1; offset <= 1; ++offset)
    {
        const QColor pixel = samplePixel(image, x + offset, y);
        if (channelDistance(pixel, background) > 8)
            return false;
    }
    return true;
}

int secondGridPureChecks()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const QString& label) {
        if (!ok)
        {
            qCritical() << "timeline-seconds-test:" << label;
            ++failures;
        }
        return ok;
    };

    // Normal zoom: one-second spacing for every supported project rate.
    for (int fps : {24, 30, 60})
    {
        check(pvf::TimelinePanel::secondGridStep(fps, 6.0) == 1,
              QStringLiteral("normal zoom did not use a 1s step at %1 fps")
                  .arg(fps));
        check(pvf::TimelinePanel::secondGridFrames(fps, 6.0) == fps,
              QStringLiteral("normal zoom step frames wrong at %1 fps")
                  .arg(fps));
    }

    // 12 px per second is the floor; one pixel less doubles the step.
    check(pvf::TimelinePanel::secondGridStep(24, 0.5) == 1,
          QStringLiteral("12 px/s must stay at a 1s step"));
    check(pvf::TimelinePanel::secondGridStep(24, 0.49) == 2,
          QStringLiteral("11.76 px/s must widen to a 2s step"));

    // Tiny zooms walk the 2/5/10/30/60 ladder (no 3s step).
    const auto stepAtPixelsPerSecond = [](int fps, double pixelsPerSecond) {
        return pvf::TimelinePanel::secondGridStep(fps, pixelsPerSecond / fps);
    };
    for (int fps : {24, 30, 60})
    {
        check(stepAtPixelsPerSecond(fps, 20.0) == 1,
              QStringLiteral("20 px/s should be 1s at %1 fps").arg(fps));
        check(stepAtPixelsPerSecond(fps, 8.0) == 2,
              QStringLiteral("8 px/s should be 2s at %1 fps").arg(fps));
        check(stepAtPixelsPerSecond(fps, 4.0) == 5,
              QStringLiteral("4 px/s should be 5s at %1 fps").arg(fps));
        check(stepAtPixelsPerSecond(fps, 1.5) == 10,
              QStringLiteral("1.5 px/s should be 10s at %1 fps").arg(fps));
        check(stepAtPixelsPerSecond(fps, 0.5) == 30,
              QStringLiteral("0.5 px/s should be 30s at %1 fps").arg(fps));
        check(stepAtPixelsPerSecond(fps, 0.25) == 60,
              QStringLiteral("0.25 px/s should be 60s at %1 fps").arg(fps));
    }
    // Past 60s the step keeps doubling instead of turning into a grey wall.
    check(pvf::TimelinePanel::secondGridStep(24, 0.001) == 960,
          QStringLiteral("extreme zoom should double past 60s"));

    // Whole-second frame sequences.  fps 24 -> 24/48/72...
    check(pvf::TimelinePanel::secondGridFrameList(24, 6.0, 0, 0, 100)
              == QVector<int>({0, 24, 48, 72, 96}),
          QStringLiteral("absolute fps=24 sequence is not 0/24/48/72/96"));
    check(pvf::TimelinePanel::secondGridFrames(24, 0.25) == 48,
          QStringLiteral("2s step at 24 fps must be 48 frames"));
    check(pvf::TimelinePanel::secondGridFrameList(24, 0.25, 0, 0, 110)
              == QVector<int>({0, 48, 96}),
          QStringLiteral("diluted sequence ignored the 2s step"));
    // Relative mode aligns the same 0/24/48 offsets to the parent clip start.
    check(pvf::TimelinePanel::secondGridFrameList(24, 6.0, 100, 100, 200)
              == QVector<int>({100, 124, 148, 172, 196}),
          QStringLiteral("relative fps=24 sequence is not parent+0/24/48"));
    check(pvf::TimelinePanel::secondGridFrameList(24, 6.0, 101, 101, 150)
              == QVector<int>({101, 125, 149}),
          QStringLiteral("relative sequence did not align to an odd origin"));
    // A mid-window query starts on the first whole second inside the range.
    check(pvf::TimelinePanel::secondGridFrameList(24, 6.0, 0, 30, 80)
              == QVector<int>({48, 72}),
          QStringLiteral("mid-window query clipped the wrong frames"));
    // The grid extends to the left of the origin too (relative private
    // timelines can scroll before the parent clip start).
    check(pvf::TimelinePanel::secondGridFrameList(24, 6.0, 100, 0, 100)
              == QVector<int>({4, 28, 52, 76, 100}),
          QStringLiteral("relative grid did not extend left of the origin"));

    return failures;
}

int secondGridPanelChecks(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool ok, const QString& label) {
        if (!ok)
        {
            qCritical() << "timeline-seconds-test:" << label;
            ++failures;
        }
        return ok;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("timeline-seconds.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("时间轴整秒网格");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 24;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-seconds-test: create failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1400, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !timeline)
    {
        qCritical() << "timeline-seconds-test: session window/timeline missing";
        return 1;
    }

    auto& project = live->mutableProject();
    pvf::model::Entity clip;
    clip.uuid = pvf::model::newUuid();
    clip.name = QStringLiteral("父片段");
    clip.start = 10;
    clip.end = 130;
    clip.row = 0;
    clip.content.kind = pvf::ContentKind::Text;
    const pvf::Uuid clipUuid = clip.uuid;
    project.entities.insert(clipUuid, clip);
    if (!document->commitEntity(clipUuid, QStringLiteral("timeline seconds"),
                                &error))
    {
        qCritical() << "timeline-seconds-test: setup commit failed" << error;
        return 1;
    }
    QString flushError;
    live->flush(&flushError);
    QApplication::processEvents();

    // Main timeline: absolute seconds, 8 px/frame = 192 px per second.
    timeline->setPixelsPerFrameForTest(8.0);
    timeline->setHorizontalScrollForTest(0);
    timeline->setDisplayFrames(false);
    QApplication::processEvents();

    check(timeline->secondGridOriginForTest() == 0,
          QStringLiteral("main timeline origin is not absolute 0"));
    check(timeline->frameXForTest(24) == 24 * 8,
          QStringLiteral("frame 24 is not at 192 px"));
    check(timeline->secondGridFrameListForTest(0, 80)
              == QVector<int>({0, 24, 48, 72}),
          QStringLiteral("main timeline whole-second frames are wrong"));

    const int y = std::min(timeline->bodyBottomForTest() - 20,
                           timeline->rulerHeightForTest() + 40);
    check(y > timeline->rulerHeightForTest() + 4,
          QStringLiteral("timeline body is too short for a pixel check"));

    const int x24 = timeline->frameXForTest(24);
    const int x48 = timeline->frameXForTest(48);
    QImage image = timeline->grab().toImage();
    check(image.width() > timeline->width() / 2,
          QStringLiteral("offscreen grab returned an empty image"));
    const QColor background = samplePixel(image, x24 + 20, y);
    check(hasGridLine(image, x24, y, background),
          QStringLiteral("no bold grey line at frame 24"));
    check(hasGridLine(image, x48, y, background),
          QStringLiteral("no bold grey line at frame 48"));
    check(hasNoGridLine(image, timeline->frameXForTest(12), y, background),
          QStringLiteral("unexpected line on the half-second frame 12"));

    // Frame mode keeps the same whole-second lines.
    timeline->setDisplayFrames(true);
    QApplication::processEvents();
    image = timeline->grab().toImage();
    check(hasGridLine(image, x24, y, background),
          QStringLiteral("frame mode lost the whole-second line"));
    timeline->setDisplayFrames(false);
    QApplication::processEvents();

    // Tiny zoom: 0.25 px/frame * 24 fps = 6 px/s -> 2s step (48-frame gaps).
    timeline->setPixelsPerFrameForTest(0.25);
    timeline->setHorizontalScrollForTest(0);
    QApplication::processEvents();
    check(timeline->secondGridFrameListForTest(0, 120)
              == QVector<int>({0, 48, 96}),
          QStringLiteral("tiny zoom did not dilute to a 2s step"));
    image = timeline->grab().toImage();
    const int x48Small = timeline->frameXForTest(48);
    const QColor smallBackground = samplePixel(image, x48Small + 3, y);
    check(hasGridLine(image, x48Small, y, smallBackground),
          QStringLiteral("diluted grid line at frame 48 is missing"));
    check(hasNoGridLine(image, timeline->frameXForTest(24), y, smallBackground),
          QStringLiteral("diluted grid still drew frame 24"));
    timeline->setPixelsPerFrameForTest(8.0);
    timeline->setHorizontalScrollForTest(0);
    QApplication::processEvents();

    // Private timeline: relative mode counts from the parent clip start,
    // absolute mode stays on project-wide whole seconds.
    const pvf::EntityId ownerId = timeline->entityIdForUuid(clipUuid);
    pvf::TimelinePanel* popup = timeline->openPopup(ownerId);
    if (!check(popup != nullptr,
               QStringLiteral("private timeline did not open")))
    {
        window->close();
        QApplication::processEvents();
        return failures == 0 ? 0 : 1;
    }
    popup->setPixelsPerFrameForTest(8.0);
    popup->setHorizontalScrollForTest(0);
    QApplication::processEvents();
    check(popup->relativeTimeForTest(),
          QStringLiteral("private timeline did not start in relative mode"));
    check(popup->secondGridOriginForTest() == 10,
          QStringLiteral("relative private timeline origin is not the parent "
                         "start"));
    check(popup->secondGridFrameListForTest(10, 130)
              == QVector<int>({10, 34, 58, 82, 106, 130}),
          QStringLiteral("relative private timeline seconds are not "
                         "parent+0/24/48"));

    popup->setRelativeTimeForTest(false);
    QApplication::processEvents();
    check(popup->secondGridOriginForTest() == 0,
          QStringLiteral("absolute private timeline origin is not 0"));
    check(popup->secondGridFrameListForTest(10, 130)
              == QVector<int>({24, 48, 72, 96, 120}),
          QStringLiteral("absolute private timeline seconds are not project "
                         "multiples of 24"));
    popup->setRelativeTimeForTest(true);
    QApplication::processEvents();

    timeline->closeAllPopups();
    window->close();
    QApplication::processEvents();

    if (failures == 0)
        qInfo() << "timeline-seconds-test: ok";
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerTimelineSecondsTests(Registry& registry)
{
    registry.add(QStringLiteral("--timeline-seconds-test"),
                 [](QApplication& app) {
                     return secondGridPureChecks() + secondGridPanelChecks(app);
                 });
}

} // namespace pvf::tests
