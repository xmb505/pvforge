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
    qCritical() << "timeline-edge-snap-test:" << message;
    return false;
}

pvf::model::Entity makeClip(const QString& name, int start, int end, int row,
                            pvf::ContentKind kind)
{
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = name;
    entity.start = start;
    entity.end = end;
    entity.row = row;
    entity.content.kind = kind;
    if (kind == pvf::ContentKind::Camera)
    {
        entity.content.camera = {};
    }
    else if (kind == pvf::ContentKind::Asset)
    {
        entity.content.asset.type = pvf::AssetType::Audio;
        entity.content.asset.hash = QStringLiteral("edge-snap-audio");
    }
    else
    {
        entity.content.text.text = name;
    }
    return entity;
}

int timelineEdgeSnapTest(QApplication& app)
{
    Q_UNUSED(app);
    gFailures = 0;

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Timeline Edge Snap");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(
        QDir(temp.path()).filePath(QStringLiteral("timeline-edge-snap.pvproj")),
        settings, &error);
    if (!session)
    {
        qCritical() << "timeline-edge-snap-test: create failed" << error;
        return 1;
    }

    // Row 0 (bottom): the audio clip whose edges become snap lines.
    // Row 1: the camera clip that is dragged/trimmed onto those edges.
    auto& project = session->mutableProject();
    const pvf::model::Entity audio = makeClip(
        QStringLiteral("音频"), 10, 299, 0, pvf::ContentKind::Asset);
    const pvf::model::Entity camera = makeClip(
        QStringLiteral("摄像机"), 50, 120, 1, pvf::ContentKind::Camera);
    // On its own track (rowIds() shows one entity per row, so it cannot share
    // the camera's row).  Its start edge sits inside the gap between the
    // camera and the audio right edge, so it can pull the camera's end without
    // shrinking the camera below a single frame.
    pvf::model::Entity shadow = makeClip(
        QStringLiteral("影子"), 200, 260, 3, pvf::ContentKind::Text);
    // A folder with two children covers the private timeline: the same-parent
    // edge snapping must also work inside the floating child timeline.
    const pvf::model::Entity folder =
        makeClip(QStringLiteral("容器"), 0, 400, 2, pvf::ContentKind::Empty);
    pvf::model::Entity childA =
        makeClip(QStringLiteral("子片段A"), 20, 40, 0, pvf::ContentKind::Text);
    childA.parentUuid = folder.uuid;
    pvf::model::Entity childB =
        makeClip(QStringLiteral("子片段B"), 100, 150, 1, pvf::ContentKind::Text);
    childB.parentUuid = folder.uuid;
    project.entities.insert(audio.uuid, audio);
    project.entities.insert(camera.uuid, camera);
    project.entities.insert(shadow.uuid, shadow);
    project.entities.insert(folder.uuid, folder);
    project.entities.insert(childA.uuid, childA);
    project.entities.insert(childB.uuid, childB);

    if (!session->document()->commitEntities(
            {audio.uuid, camera.uuid, shadow.uuid, folder.uuid, childA.uuid,
             childB.uuid},
            QStringLiteral("edge snap setup"), &error)
        || !session->commitProject(QStringLiteral("edge snap project"), &error))
    {
        qCritical() << "timeline-edge-snap-test: setup commit failed" << error;
        return 1;
    }
    session->flush(&error);
    if (!error.isEmpty())
    {
        qCritical() << "timeline-edge-snap-test: setup flush failed" << error;
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

    const pvf::Uuid audioUuid = audio.uuid;
    const pvf::Uuid cameraUuid = camera.uuid;
    const pvf::Uuid shadowUuid = shadow.uuid;
    const pvf::Uuid folderUuid = folder.uuid;
    const pvf::Uuid childBUuid = childB.uuid;
    auto entity = [live](const pvf::Uuid& uuid) {
        return live->project().entities.value(uuid);
    };
    const pvf::EntityId audioId = timeline->entityIdForUuid(audioUuid);
    const pvf::EntityId cameraId = timeline->entityIdForUuid(cameraUuid);
    const int audioRow = timeline->rowForEntityForTest(audioId);
    const int cameraRow = timeline->rowForEntityForTest(cameraId);
    if (!check(audioRow >= 0 && cameraRow >= 0,
               QStringLiteral("clip rows missing (%1/%2)")
                   .arg(audioRow)
                   .arg(cameraRow)))
        return 1;

    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         ++gFailures;
                         qCritical() << "timeline-edge-snap-test: edit failed"
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
        live->flush(&error);
        check(live->document()->undo(&error),
              QStringLiteral("%1 undo failed: %2").arg(what, error));
    };
    auto expectRange = [&](const QString& what, const pvf::Uuid& uuid,
                           int start, int end) {
        const pvf::model::Entity value = entity(uuid);
        check(value.start == start && value.end == end,
              QStringLiteral("%1 range is [%2,%3], expected [%4,%5]")
                  .arg(what)
                  .arg(value.start)
                  .arg(value.end)
                  .arg(start)
                  .arg(end));
    };

    // 2 px/frame keeps [10,300] inside the panel without scrolling; the 8 px
    // threshold therefore spans 4 frames.  The playhead starts far away so
    // only the audio edges can attract the camera.
    live->document()->setPlayhead(1000);
    timeline->setPixelsPerFrameForTest(2.0);
    timeline->setHorizontalScrollForTest(0);
    QApplication::processEvents();
    if (!check(timeline->snapThresholdPxForTest() == 8,
               QStringLiteral("snap threshold changed")))
        return 1;

    const int cameraRowY = timeline->rowCenterYForTest(cameraRow);

    QRect cameraRect = timeline->clipRectFor(cameraId);
    if (!check(!cameraRect.isEmpty(),
               QStringLiteral("camera clip rect missing")))
        return 1;
    const QPoint trimStartTo(timeline->frameXForTest(10) + 3, cameraRowY);
    press(QPoint(cameraRect.left() + 1, cameraRowY), Qt::NoModifier);
    move(trimStartTo, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 10,
          QStringLiteral("TrimStart highlighted %1, expected 10")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(cameraRow) == 10,
          QStringLiteral("TrimStart preview is %1, expected 10")
              .arg(timeline->rowStartForTest(cameraRow)));
    release(trimStartTo, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("TrimStart"), cameraUuid, 10, 120);
    undo(QStringLiteral("TrimStart"));
    expectRange(QStringLiteral("TrimStart undo"), cameraUuid, 50, 120);

    // TrimEnd: the audio visual right edge is end + 1 = 300.  A sibling edge
    // is a boundary rather than a frame, so the painted edge parks on the line
    // and the stored end is 299: both clips end on the same frame.
    cameraRect = timeline->clipRectFor(cameraId);
    const QPoint trimEndTo(timeline->frameXForTest(300), cameraRowY);
    press(QPoint(cameraRect.right() - 1, cameraRowY), Qt::NoModifier);
    move(trimEndTo, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 300,
          QStringLiteral("TrimEnd highlighted %1, expected 300")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowEndForTest(cameraRow) == 299,
          QStringLiteral("TrimEnd preview is %1, expected 299")
              .arg(timeline->rowEndForTest(cameraRow)));
    release(trimEndTo, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("TrimEnd"), cameraUuid, 50, 299);
    undo(QStringLiteral("TrimEnd"));
    expectRange(QStringLiteral("TrimEnd undo"), cameraUuid, 50, 120);

    // TrimEnd onto a same-row sibling's *start* (200): the sibling edge is a
    // boundary, not a frame, so the painted edge parks on it with end == 199.
    // The two clips end up adjacent (camera [50,199], shadow [200,260]) with
    // no shared frame, which is the no-overlap guarantee.
    cameraRect = timeline->clipRectFor(cameraId);
    const QPoint trimEndStartTo(timeline->frameXForTest(200), cameraRowY);
    press(QPoint(cameraRect.right() - 1, cameraRowY), Qt::NoModifier);
    move(trimEndStartTo, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 200,
          QStringLiteral("TrimEnd(start edge) highlighted %1, expected 200")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowEndForTest(cameraRow) == 199,
          QStringLiteral("TrimEnd(start edge) preview is %1, expected 199")
              .arg(timeline->rowEndForTest(cameraRow)));
    release(trimEndStartTo, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("TrimEnd(start edge)"), cameraUuid, 50, 199);
    // Adjacent, not overlapping: the sibling still owns frame 200.
    expectRange(QStringLiteral("TrimEnd(start edge) sibling"), shadowUuid, 200,
                260);
    undo(QStringLiteral("TrimEnd(start edge)"));
    expectRange(QStringLiteral("TrimEnd(start edge) undo"), cameraUuid, 50,
                120);

    // MoveClip right edge onto 300 => right aligned with the audio [10,299].
    // A 178-frame drag parks the free right edge at 299, 2 px short of the
    // target line; the snap pulls the delta to 179 and stores end == 299.
    cameraRect = timeline->clipRectFor(cameraId);
    const QPoint moveBody(cameraRect.left() + 20, cameraRowY);
    const QPoint moveRightEdge(moveBody.x() + 178 * 2, cameraRowY);
    press(moveBody, Qt::NoModifier);
    move(moveRightEdge, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 300,
          QStringLiteral("MoveClip right edge highlighted %1, expected 300")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(cameraRow) == 229
              && timeline->rowEndForTest(cameraRow) == 299,
          QStringLiteral("MoveClip right preview is [%1,%2], expected [229,299]")
              .arg(timeline->rowStartForTest(cameraRow))
              .arg(timeline->rowEndForTest(cameraRow)));
    release(moveRightEdge, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("MoveClip right edge"), cameraUuid, 229, 299);
    undo(QStringLiteral("MoveClip right edge"));
    expectRange(QStringLiteral("MoveClip right edge undo"), cameraUuid, 50,
                120);

    // MoveClip left edge onto 300 => starts exactly after the audio ends.
    // A 247-frame drag puts the free left edge 3 frames (6 px) before the
    // line; the snap pulls it onto 300.
    cameraRect = timeline->clipRectFor(cameraId);
    const QPoint moveLeftEdge(cameraRect.left() + 20 + 247 * 2, cameraRowY);
    press(QPoint(cameraRect.left() + 20, cameraRowY), Qt::NoModifier);
    move(moveLeftEdge, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 300,
          QStringLiteral("MoveClip left edge highlighted %1, expected 300")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(cameraRow) == 300
              && timeline->rowEndForTest(cameraRow) == 370,
          QStringLiteral("MoveClip left preview is [%1,%2], expected [300,370]")
              .arg(timeline->rowStartForTest(cameraRow))
              .arg(timeline->rowEndForTest(cameraRow)));
    release(moveLeftEdge, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("MoveClip left edge"), cameraUuid, 300, 370);
    undo(QStringLiteral("MoveClip left edge"));
    expectRange(QStringLiteral("MoveClip left edge undo"), cameraUuid, 50, 120);

    // Dragging the audio itself must ignore its own old edges.  A two-frame
    // nudge puts both edges 4 px from their old positions, inside the 8 px
    // threshold; only the exclusion keeps the move free.
    const QRect audioRect = timeline->clipRectFor(audioId);
    const QPoint audioBody(audioRect.left() + 180,
                           timeline->rowCenterYForTest(audioRow));
    const QPoint audioNudge(audioBody.x() + 4, audioBody.y());
    press(audioBody, Qt::NoModifier);
    move(audioNudge, Qt::NoModifier);
    check(timeline->snapFrameForTest() == -1,
          QStringLiteral("audio snapped to its own old edge (%1)")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(audioRow) == 12
              && timeline->rowEndForTest(audioRow) == 301,
          QStringLiteral("audio nudge is [%1,%2], expected [12,301]")
              .arg(timeline->rowStartForTest(audioRow))
              .arg(timeline->rowEndForTest(audioRow)));
    release(audioNudge, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("audio self-drag"), audioUuid, 12, 301);
    undo(QStringLiteral("audio self-drag"));
    expectRange(QStringLiteral("audio self-drag undo"), audioUuid, 10, 299);

    // Ctrl disables sibling-edge snapping for the whole gesture; the release
    // has no Ctrl, so only the cached modifier can keep the drag free.
    cameraRect = timeline->clipRectFor(cameraId);
    const QPoint ctrlTrimTo(timeline->frameXForTest(10) + 3, cameraRowY);
    press(QPoint(cameraRect.left() + 1, cameraRowY), Qt::ControlModifier);
    move(ctrlTrimTo, Qt::ControlModifier);
    check(timeline->snapFrameForTest() == -1,
          QStringLiteral("Ctrl TrimStart snapped to %1")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(cameraRow) == 12,
          QStringLiteral("Ctrl TrimStart preview is %1, expected 12")
              .arg(timeline->rowStartForTest(cameraRow)));
    release(ctrlTrimTo, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("Ctrl TrimStart"), cameraUuid, 12, 120);
    undo(QStringLiteral("Ctrl TrimStart"));
    expectRange(QStringLiteral("Ctrl TrimStart undo"), cameraUuid, 50, 120);

    // Dedupe: the playhead and the audio start share frame 10, which must stay
    // one candidate; the audio visual right edge contributes 300.
    live->document()->setPlayhead(10);
    QApplication::processEvents();
    QVector<int> targets = timeline->snapTargetsForTest();
    check(std::count(targets.begin(), targets.end(), 10) == 1,
          QStringLiteral("frame 10 was not de-duplicated"));
    check(targets.contains(10) && targets.contains(300)
              && targets.contains(50) && targets.contains(121),
          QStringLiteral("sibling edges missing from snap targets"));

    // Equal pixel distance: at 0.5 px/frame, playhead 6 sits 1 px from the
    // mouse and audio start 10 sits 1 px on the other side.  The playhead
    // priority must win over the sibling edge.
    live->document()->setPlayhead(6);
    timeline->setPixelsPerFrameForTest(0.5);
    timeline->setHorizontalScrollForTest(0);
    QApplication::processEvents();
    cameraRect = timeline->clipRectFor(cameraId);
    const QPoint priorityTo(timeline->frameXForTest(6) + 1, cameraRowY);
    press(QPoint(cameraRect.left() + 1, cameraRowY), Qt::NoModifier);
    move(priorityTo, Qt::NoModifier);
    check(timeline->snapFrameForTest() == 6,
          QStringLiteral("equal-distance tie picked %1, expected playhead 6")
              .arg(timeline->snapFrameForTest()));
    check(timeline->rowStartForTest(cameraRow) == 6,
          QStringLiteral("priority preview is %1, expected 6")
              .arg(timeline->rowStartForTest(cameraRow)));
    release(priorityTo, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("playhead priority"), cameraUuid, 6, 120);
    undo(QStringLiteral("playhead priority"));
    expectRange(QStringLiteral("playhead priority undo"), cameraUuid, 50, 120);

    // Private timeline: child B trims its start onto child A's start edge (20).
    // The same helper filters siblings by m_owner, so only the folder's
    // children are candidates inside the floating panel.
    const pvf::EntityId folderId = timeline->entityIdForUuid(folderUuid);
    pvf::TimelinePanel* popup = timeline->openPopup(folderId);
    if (!check(popup != nullptr,
               QStringLiteral("private timeline did not open")))
        return 1;
    QApplication::processEvents();
    popup->setPixelsPerFrameForTest(2.0);
    popup->setHorizontalScrollForTest(0);
    QApplication::processEvents();
    const pvf::EntityId childBId = popup->entityIdForUuid(childBUuid);
    const int childBRow = popup->rowForEntityForTest(childBId);
    const QRect childBRect = popup->clipRectFor(childBId);
    if (!check(childBRow >= 0 && !childBRect.isEmpty(),
               QStringLiteral("private child rect missing")))
        return 1;
    const int childBRowY = popup->rowCenterYForTest(childBRow);
    const QPoint privateTrimTo(popup->frameXForTest(20) + 3, childBRowY);
    auto popupPress = [popup](const QPoint& point,
                              Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseButtonPress, QPointF(point),
                          QPointF(popup->mapToGlobal(point)),
                          Qt::LeftButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(popup, &event);
    };
    auto popupMove = [popup](const QPoint& point,
                             Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseMove, QPointF(point),
                          QPointF(popup->mapToGlobal(point)), Qt::NoButton,
                          Qt::LeftButton, modifiers);
        QApplication::sendEvent(popup, &event);
        QApplication::processEvents();
    };
    auto popupRelease = [popup](const QPoint& point,
                                Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseButtonRelease, QPointF(point),
                          QPointF(popup->mapToGlobal(point)),
                          Qt::LeftButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(popup, &event);
        QApplication::processEvents();
    };
    popupPress(QPoint(childBRect.left() + 1, childBRowY), Qt::NoModifier);
    popupMove(privateTrimTo, Qt::NoModifier);
    check(popup->snapFrameForTest() == 20,
          QStringLiteral("private TrimStart highlighted %1, expected 20")
              .arg(popup->snapFrameForTest()));
    check(popup->rowStartForTest(childBRow) == 20,
          QStringLiteral("private TrimStart preview is %1, expected 20")
              .arg(popup->rowStartForTest(childBRow)));
    popupRelease(privateTrimTo, Qt::NoModifier);
    live->flush(&error);
    expectRange(QStringLiteral("private TrimStart"), childBUuid, 20, 150);
    undo(QStringLiteral("private TrimStart"));
    expectRange(QStringLiteral("private TrimStart undo"), childBUuid, 100,
                150);

    window->close();
    QApplication::processEvents();
    if (gFailures == 0)
    {
        qInfo() << "timeline-edge-snap-test OK: sibling start/end+1 edges,"
                   " adjacent trim end, move alignment, self exclusion, Ctrl,"
                   " dedupe, playhead priority, private timeline";
    }
    return gFailures == 0 ? 0 : 1;
}

} // namespace

void pvf::tests::registerTimelineEdgeSnapTests(Registry& registry)
{
    registry.add(QStringLiteral("--timeline-edge-snap-test"),
                 [](QApplication& app) { return timelineEdgeSnapTest(app); });
}
