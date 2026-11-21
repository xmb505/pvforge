#include "TestApp.h"
#include "TestRegistry.h"

#include <QDockWidget>

#include <algorithm>

namespace {

int gFailures = 0;

bool check(bool condition, const QString& message)
{
    if (condition)
        return true;
    ++gFailures;
    qCritical() << "timeline-snap-test:" << message;
    return false;
}

pvf::model::Entity makeClip(const QString& name, int start, int end, int row)
{
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = name;
    entity.start = start;
    entity.end = end;
    entity.row = row;
    entity.content.kind = pvf::ContentKind::Text;
    entity.content.text.text = name;
    return entity;
}

int timelineSnapTest(QApplication& app)
{
    Q_UNUSED(app);
    gFailures = 0;

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Timeline Snap");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(
        QDir(temp.path()).filePath(QStringLiteral("timeline-snap.pvproj")),
        settings, &error);
    if (!session)
    {
        qCritical() << "timeline-snap-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    const pvf::model::Entity longEntity =
        makeClip(QStringLiteral("长片段"), 0, 5399, 0);
    const pvf::model::Entity shortEntity =
        makeClip(QStringLiteral("细调片段"), 90, 110, 1);
    project.entities.insert(longEntity.uuid, longEntity);
    project.entities.insert(shortEntity.uuid, shortEntity);

    // A marker on the playhead must be de-duplicated; a second marker gives
    // the marker-snap case a distinct target.
    pvf::model::Marker sameFrame;
    sameFrame.id = QStringLiteral("snap-playhead-marker");
    sameFrame.frame = 3000;
    sameFrame.title = QStringLiteral("同帧");
    sameFrame.color = QStringLiteral("#ffbe3c");
    pvf::model::Marker marker;
    marker.id = QStringLiteral("snap-marker");
    marker.frame = 3100;
    marker.title = QStringLiteral("吸附标记");
    marker.color = QStringLiteral("#4cc9f0");
    project.markers = {sameFrame, marker};

    if (!session->document()->commitEntities({longEntity.uuid, shortEntity.uuid},
                                             QStringLiteral("snap setup"),
                                             &error)
        || !session->commitProject(QStringLiteral("snap markers"), &error))
    {
        qCritical() << "timeline-snap-test: setup commit failed" << error;
        return 1;
    }
    session->flush(&error);
    if (!error.isEmpty())
    {
        qCritical() << "timeline-snap-test: setup flush failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline, QStringLiteral("window/timeline missing")))
        return 1;
    if (auto* stageDock =
            window->findChild<QDockWidget*>(QStringLiteral("stageDock")))
    {
        if (auto* timelineDock =
                window->findChild<QDockWidget*>(QStringLiteral("timelineDock")))
            window->resizeDocks({stageDock, timelineDock}, {320, 560},
                                Qt::Vertical);
    }
    QApplication::processEvents();

    const pvf::Uuid longUuid = longEntity.uuid;
    const pvf::Uuid shortUuid = shortEntity.uuid;
    auto entity = [live](const pvf::Uuid& uuid) {
        return live->project().entities.value(uuid);
    };
    const pvf::EntityId longId = timeline->entityIdForUuid(longUuid);
    const pvf::EntityId shortId = timeline->entityIdForUuid(shortUuid);
    const int longRow = timeline->rowForEntityForTest(longId);
    const int shortRow = timeline->rowForEntityForTest(shortId);
    if (!check(longRow >= 0 && shortRow >= 0,
               QStringLiteral("clip rows missing (%1/%2)")
                   .arg(longRow)
                   .arg(shortRow)))
        return 1;

    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         ++gFailures;
                         qCritical() << "timeline-snap-test: panel edit failed"
                                     << message;
                     });

    auto press = [&](const QPoint& point, Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseButtonPress, QPointF(point),
                          QPointF(timeline->mapToGlobal(point)),
                          Qt::LeftButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(timeline, &event);
    };
    auto move = [&](const QPoint& point, Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseMove, QPointF(point),
                          QPointF(timeline->mapToGlobal(point)), Qt::NoButton,
                          Qt::LeftButton, modifiers);
        QApplication::sendEvent(timeline, &event);
        QApplication::processEvents();
    };
    auto release = [&](const QPoint& point, Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseButtonRelease, QPointF(point),
                          QPointF(timeline->mapToGlobal(point)),
                          Qt::LeftButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(timeline, &event);
        QApplication::processEvents();
    };
    auto undo = [&](const QString& what) {
        live->flush();
        check(live->document()->undo(&error),
              QStringLiteral("%1 undo failed: %2").arg(what, error));
    };

    // Low zoom: 5400 frames span 270 px, so the shrink-to-fit case gives the
    // 8 px threshold a wide frame window while the mouse still lands on the
    // playhead line.
    live->document()->setPlayhead(3000);
    timeline->setPixelsPerFrameForTest(0.05);
    timeline->setHorizontalScrollForTest(0);
    QApplication::processEvents();

    const QVector<int> targets = timeline->snapTargetsForTest();
    check(timeline->snapThresholdPxForTest() == 8,
          QStringLiteral("default snap threshold is %1 px")
              .arg(timeline->snapThresholdPxForTest()));
    check(std::count(targets.begin(), targets.end(), 3000) == 1,
          QStringLiteral("playhead marker was not de-duplicated"));
    check(targets.contains(3000) && targets.contains(3100),
          QStringLiteral("snap targets missing playhead/marker"));

    const int playheadX = timeline->frameXForTest(3000);
    const int markerX = timeline->frameXForTest(3100);
    const int longRowY = timeline->rowCenterYForTest(longRow);

    // TrimStart at low zoom: 1 px inside the left edge, drag to the playhead.
    QRect clip = timeline->clipRectFor(longId);
    if (!check(!clip.isEmpty(), QStringLiteral("long clip rect missing")))
        return 1;
    const QPoint trimStartFrom(clip.left() + 1, longRowY);
    const QPoint trimStartTo(playheadX, longRowY);
    press(trimStartFrom, Qt::NoModifier);
    move(trimStartTo, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 3000,
          QStringLiteral("TrimStart did not highlight the playhead (%1)")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(longRow) == 3000,
          QStringLiteral("TrimStart preview is %1, expected 3000")
              .arg(timeline->rowStartForTest(longRow)));
    // Leaving the threshold clears the line immediately, without releasing.
    move(QPoint(playheadX + 50, longRowY), Qt::NoModifier);
    check(timeline->snapFrameForTest() == -1,
          QStringLiteral("TrimStart left the snap highlight active"));
    move(trimStartTo, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 3000,
          QStringLiteral("TrimStart did not re-acquire the playhead"));
    release(trimStartTo, Qt::NoModifier);
    live->flush();
    check(entity(longUuid).start == 3000 && entity(longUuid).end == 5399,
          QStringLiteral("TrimStart committed [%1,%2], expected [3000,5399]")
              .arg(entity(longUuid).start)
              .arg(entity(longUuid).end));
    check(timeline->snapFrameForTest() == -1,
          QStringLiteral("snap highlight survived the release"));
    undo(QStringLiteral("TrimStart"));
    check(entity(longUuid).start == 0,
          QStringLiteral("TrimStart undo did not restore the start"));

    // TrimEnd onto the playhead: the playhead frame must end up *inside* the
    // clip (end == 3000), otherwise releasing on the playhead deletes the
    // frame the user is standing on.  Approach from the left first.
    clip = timeline->clipRectFor(longId);
    const QPoint trimEndFrom(clip.right() - 1, longRowY);
    const QPoint trimEndLeft(playheadX - 1, longRowY);
    press(trimEndFrom, Qt::NoModifier);
    move(trimEndLeft, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 3000,
          QStringLiteral("TrimEnd highlights %1, expected 3000")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowEndForTest(longRow) == 3000,
          QStringLiteral("TrimEnd preview is %1, expected 3000")
              .arg(timeline->rowEndForTest(longRow)));
    // Ctrl can be toggled mid-drag without moving the mouse: the preview
    // updates on the key event and the cached state decides the release.
    QKeyEvent ctrlPress(QEvent::KeyPress, Qt::Key_Control,
                        Qt::ControlModifier);
    QApplication::sendEvent(timeline, &ctrlPress);
    QApplication::processEvents();
    check(timeline->snapFrameForTest() == -1,
          QStringLiteral("Ctrl key press did not clear the snap line"));
    // Ctrl drops back to the freely dragged end for the same pointer x.
    check(timeline->rowEndForTest(longRow) == 2980,
          QStringLiteral("Ctrl key press preview is %1, expected 2980")
              .arg(timeline->rowEndForTest(longRow)));
    QKeyEvent ctrlRelease(QEvent::KeyRelease, Qt::Key_Control,
                          Qt::NoModifier);
    QApplication::sendEvent(timeline, &ctrlRelease);
    QApplication::processEvents();
    check(timeline->snapFrameForTest() == 3000,
          QStringLiteral("Ctrl release did not restore the snap line"));
    check(timeline->rowEndForTest(longRow) == 3000,
          QStringLiteral("Ctrl release preview is %1, expected 3000")
              .arg(timeline->rowEndForTest(longRow)));
    release(trimEndLeft, Qt::NoModifier);
    live->flush();
    check(entity(longUuid).end == 3000,
          QStringLiteral("TrimEnd(playhead) committed %1, expected 3000")
              .arg(entity(longUuid).end));
    undo(QStringLiteral("TrimEnd(playhead)"));
    check(entity(longUuid).end == 5399,
          QStringLiteral("TrimEnd(playhead) undo did not restore the end"));

    // The same snap approached from the right side of the playhead line still
    // includes frame 3000: the playhead never truncates the clip by a frame.
    clip = timeline->clipRectFor(longId);
    const QPoint trimEndRight(playheadX + 1, longRowY);
    press(QPoint(clip.right() - 1, longRowY), Qt::NoModifier);
    move(trimEndRight, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 3000,
          QStringLiteral("TrimEnd(playhead, right) highlights %1, expected"
                         " 3000")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowEndForTest(longRow) == 3000,
          QStringLiteral("TrimEnd(playhead, right) preview is %1, expected"
                         " 3000")
              .arg(timeline->rowEndForTest(longRow)));
    // Release at the same position: the commit must keep the snapped end.
    release(trimEndRight, Qt::NoModifier);
    live->flush();
    check(entity(longUuid).end == 3000,
          QStringLiteral("TrimEnd(playhead, right) committed %1, expected"
                         " 3000")
              .arg(entity(longUuid).end));
    undo(QStringLiteral("TrimEnd(playhead, right)"));
    check(entity(longUuid).end == 5399,
          QStringLiteral("TrimEnd(playhead, right) undo did not restore the"
                         " end"));

    // Ctrl during the same trim disables snapping.  The release has no Ctrl
    // so the cached modifier is the only thing that can keep it free.
    clip = timeline->clipRectFor(longId);
    const QPoint ctrlTo(playheadX + 1, longRowY);
    press(QPoint(clip.right() - 1, longRowY), Qt::ControlModifier);
    move(ctrlTo, Qt::ControlModifier);
    check(timeline->snapFrameForTest() == -1,
          QStringLiteral("Ctrl did not disable TrimEnd snapping"));
    release(ctrlTo, Qt::NoModifier);
    live->flush();
    check(entity(longUuid).end == 3020,
          QStringLiteral("Ctrl TrimEnd committed %1, expected free frame 3020")
              .arg(entity(longUuid).end));
    undo(QStringLiteral("Ctrl TrimEnd"));
    check(entity(longUuid).end == 5399,
          QStringLiteral("Ctrl TrimEnd undo did not restore the end"));

    // MoveClip: the left edge snaps to the playhead while dragging the body.
    clip = timeline->clipRectFor(longId);
    press(QPoint(clip.left() + 100, longRowY), Qt::NoModifier);
    move(QPoint(clip.left() + 251, longRowY), Qt::NoModifier);
    check(timeline->snapFrameForTest() == 3000,
          QStringLiteral("MoveClip did not snap the left edge (%1)")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(longRow) == 3000,
          QStringLiteral("MoveClip preview starts at %1, expected 3000")
              .arg(timeline->rowStartForTest(longRow)));
    release(QPoint(clip.left() + 251, longRowY), Qt::NoModifier);
    live->flush();
    check(entity(longUuid).start == 3000 && entity(longUuid).end == 8399,
          QStringLiteral("MoveClip committed [%1,%2], expected [3000,8399]")
              .arg(entity(longUuid).start)
              .arg(entity(longUuid).end));
    undo(QStringLiteral("MoveClip"));
    check(entity(longUuid).start == 0,
          QStringLiteral("MoveClip undo did not restore the start"));

    // Marker snap: the marker line is closer than the playhead line, and the
    // free frame (3120) is different from the snapped one (3100).
    clip = timeline->clipRectFor(longId);
    press(QPoint(clip.left() + 1, longRowY), Qt::NoModifier);
    move(QPoint(markerX + 1, longRowY), Qt::NoModifier);
    check(timeline->snapFrameForTest() == 3100,
          QStringLiteral("marker snap highlighted %1, expected 3100")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(longRow) == 3100,
          QStringLiteral("marker snap preview starts at %1, expected 3100")
              .arg(timeline->rowStartForTest(longRow)));
    release(QPoint(markerX + 1, longRowY), Qt::NoModifier);
    live->flush();
    check(entity(longUuid).start == 3100,
          QStringLiteral("marker snap committed %1, expected 3100")
              .arg(entity(longUuid).start));
    undo(QStringLiteral("marker snap"));
    check(entity(longUuid).start == 0,
          QStringLiteral("marker snap undo did not restore the start"));

    // High zoom: 16 px/frame.  Two frames (32 px) from the playhead is outside
    // the 8 px threshold, so the right edge stays at the freely chosen frame.
    live->document()->setPlayhead(100);
    timeline->setPixelsPerFrameForTest(16.0);
    timeline->setHorizontalScrollForTest(100 * 16 - 600);
    QApplication::processEvents();
    const int highPlayheadX = timeline->frameXForTest(100);
    if (!check(highPlayheadX == 600,
               QStringLiteral("high-zoom playhead x is %1, expected 600")
                   .arg(highPlayheadX)))
        return 1;
    const int shortRowY = timeline->rowCenterYForTest(shortRow);
    const QRect shortClip = timeline->clipRectFor(shortId);
    if (!check(!shortClip.isEmpty(), QStringLiteral("short clip rect missing")))
        return 1;
    const QPoint highFrom(shortClip.right() - 1, shortRowY);
    const QPoint highTo(timeline->frameXForTest(102), shortRowY);
    press(highFrom, Qt::NoModifier);
    move(highTo, Qt::NoModifier);
    check(timeline->snapFrameForTest() == -1,
          QStringLiteral("high-zoom 32 px drag snapped to %1")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowEndForTest(shortRow) == 102,
          QStringLiteral("high-zoom preview end is %1, expected 102")
              .arg(timeline->rowEndForTest(shortRow)));
    release(highTo, Qt::NoModifier);
    live->flush();
    check(entity(shortUuid).end == 102,
          QStringLiteral("high-zoom committed end %1, expected 102")
              .arg(entity(shortUuid).end));
    undo(QStringLiteral("high-zoom trim"));
    check(entity(shortUuid).end == 110,
          QStringLiteral("high-zoom undo did not restore the end"));

    // Double-click on the painted right edge must write a keyframe inside the
    // clip's own closed range [90,110].  clipEndOf() is the exclusive edge 111
    // and clip.right() sits on x(end + 1), so frameFromX() there returns 111;
    // the old clamp passed that straight to canWriteKeyAt(), which rejected it
    // as out of range.  The "+" affordance covers the frame *inside* the edge,
    // so the free spot that still resolves to 111 is the edge pixel itself.
    const pvf::Uuid keyUuid = shortEntity.uuid;
    const int keyRowY = shortRowY;
    const int endFrame = entity(keyUuid).end;
    auto keyframes = [live, keyUuid] {
        return live->project().entities.value(keyUuid).keyframes.size();
    };
    // Only the double-click event: the press half of the gesture would start
    // (and the release half commit) a right-edge trim on the very pixel under
    // test, which is a different code path.
    auto doubleClick = [&](const QPoint& point) {
        QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(point),
                          QPointF(timeline->mapToGlobal(point)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &event);
        QApplication::processEvents();
    };
    const QRect keyClip = timeline->clipRectFor(shortId);
    if (!check(!keyClip.isEmpty(),
               QStringLiteral("end-frame clip rect missing")))
        return 1;
    doubleClick(QPoint(keyClip.right(), keyRowY));
    live->flush();
    check(timeline->currentFrameForTest() == endFrame,
          QStringLiteral("edge double-click parked the playhead on %1,"
                         " expected %2")
              .arg(timeline->currentFrameForTest())
              .arg(endFrame));
    check(keyframes() == 1,
          QStringLiteral("edge double-click wrote %1 keyframes, expected 1")
              .arg(keyframes()));
    check(pvf::model::keyframeAt(
              live->project().entities.value(keyUuid).keyframes,
              double(endFrame - shortEntity.start))
              != nullptr,
          QStringLiteral("edge double-click did not key the end frame %1")
              .arg(endFrame));
    for (const pvf::model::Keyframe& key :
         live->project().entities.value(keyUuid).keyframes)
        check(key.frame >= 0 && key.frame <= endFrame - shortEntity.start,
              QStringLiteral("edge double-click wrote out-of-range relative"
                             " frame %1")
                  .arg(key.frame));
    undo(QStringLiteral("end-frame keyframe"));
    check(keyframes() == 0,
          QStringLiteral("end-frame keyframe undo left %1 keyframes")
              .arg(keyframes()));

    window->close();
    QApplication::processEvents();
    if (gFailures == 0)
    {
        qInfo() << "timeline-snap-test OK: pixel threshold, playhead-inclusive"
                   " TrimEnd, Ctrl, MoveClip, markers, high zoom, end-frame"
                   " double-click";
    }
    return gFailures == 0 ? 0 : 1;
}

} // namespace

void pvf::tests::registerTimelineSnapTests(Registry& registry)
{
    registry.add(QStringLiteral("--timeline-snap-test"),
                 [](QApplication& app) { return timelineSnapTest(app); });
}
