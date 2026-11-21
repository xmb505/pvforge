#include "TimelineEditTests.h"

#include "TestApp.h"
#include "TimelineRowMoveChecks.h"

#include <QDockWidget>
#include <QThread>

namespace {

struct TimelineEditIds
{
    pvf::Uuid parent;
    pvf::Uuid child;
    pvf::Uuid audio;
    pvf::Uuid video;
    pvf::Uuid lockedRoot;
    pvf::Uuid lockedChild;
    pvf::Uuid target;
    pvf::Uuid worldSource;
    pvf::Uuid localSource;
    pvf::Uuid solverRoot;
    pvf::Uuid trashRoot;
    pvf::Uuid trashChild;
    pvf::Uuid followParent;
    pvf::Uuid followChild;
    pvf::Uuid followPeer;
    pvf::Uuid followGrand;
    pvf::Uuid followTarget;
    pvf::Uuid negativeChild;
};

struct TimelineEditOps
{
    std::function<bool(bool, const QString&)> check;
    std::function<void()> flush;
    std::function<bool()> undo;
    std::function<int()> depth;
    std::function<pvf::model::Entity(const pvf::Uuid&)> entity;
};

// EditActions model checks: subtree move, trim cascade/keys/sourceIn, locks,
// solver pin, reparent and cascade delete.
int editModelChecks(pvf::EditorSession* session, pvf::EditActions* actions,
                    const TimelineEditIds& ids, const TimelineEditOps& ops,
                    QString& error)
{
    const pvf::Uuid& parent = ids.parent;
    const pvf::Uuid& child = ids.child;
    const pvf::Uuid& audio = ids.audio;
    const pvf::Uuid& video = ids.video;
    const pvf::Uuid& lockedRoot = ids.lockedRoot;
    const pvf::Uuid& lockedChild = ids.lockedChild;
    const pvf::Uuid& target = ids.target;
    const pvf::Uuid& worldSource = ids.worldSource;
    const pvf::Uuid& localSource = ids.localSource;
    const pvf::Uuid& solverRoot = ids.solverRoot;
    const pvf::Uuid& trashRoot = ids.trashRoot;
    const pvf::Uuid& trashChild = ids.trashChild;
    const pvf::Uuid& followParent = ids.followParent;
    const pvf::Uuid& followChild = ids.followChild;
    const pvf::Uuid& followPeer = ids.followPeer;
    const pvf::Uuid& followGrand = ids.followGrand;
    const pvf::Uuid& followTarget = ids.followTarget;
    const auto& check = ops.check;
    const auto& flush = ops.flush;
    const auto& undo = ops.undo;
    const auto& depth = ops.depth;
    const auto& entity = ops.entity;

    // Move a parent: the whole subtree keeps its local offsets.
    int beforeDepth = depth();
    pvf::EditPlan movePlan;
    if (!check(actions->planMove({parent}, 10, {}, &movePlan, &error),
               QStringLiteral("planMove failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).start == 0 && entity(child).start == 10,
               QStringLiteral("planMove mutated the document")))
        return 1;
    if (!check(actions->moveClips({parent}, 10, &error),
               QStringLiteral("moveClips failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).start == 10 && entity(parent).end == 70
                   && entity(child).start == 20 && entity(child).end == 50,
               QStringLiteral("parent move did not shift the child")))
        return 1;
    flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("moveClips did not commit exactly once")))
        return 1;
    if (!undo())
        return 1;

    // Trim start: whole-frame keys shift/crop and the child is clipped by the
    // parent.
    const int childKeyCount = entity(child).keyframes.size();
    if (!check(childKeyCount == 2, QStringLiteral("setup child keys wrong")))
        return 1;
    if (!check(actions->trimClip(child, 15, 40, &error),
               QStringLiteral("trim start failed: %1").arg(error)))
        return 1;
    {
        if (!check(entity(child).start == 15 && entity(child).end == 40
                       && entity(child).keyframes.size() == 1
                       && entity(child).keyframes.first().frame == 24,
                   QStringLiteral("trim start did not shift/crop keys")))
            return 1;
    }
    if (!undo())
        return 1;

    // Trim end: out-of-range keys disappear.
    if (!check(actions->trimClip(child, 10, 30, &error),
               QStringLiteral("trim end failed: %1").arg(error)))
        return 1;
    {
        if (!check(entity(child).end == 30 && entity(child).keyframes.size() == 1
                       && entity(child).keyframes.first().frame == 0,
                   QStringLiteral("trim end did not drop the late key")))
            return 1;
    }
    if (!undo())
        return 1;

    // Audio sourceIn follows the start; crossing the material start fails.
    if (!check(actions->trimClip(audio, 25, 50, &error),
               QStringLiteral("audio trim start failed: %1").arg(error)))
        return 1;
    if (!check(entity(audio).start == 25
                   && entity(audio).content.asset.sourceIn == 5,
               QStringLiteral("audio sourceIn was not shifted")))
        return 1;
    if (!undo())
        return 1;
    if (!check(!actions->trimClip(audio, 10, 50, &error),
               QStringLiteral("audio trim below sourceIn should fail")))
        return 1;
    if (!check(entity(audio).start == 20
                   && entity(audio).content.asset.sourceIn == 0,
               QStringLiteral("failed audio trim changed the project")))
        return 1;
    if (!check(!actions->trimClip(audio, 20, 90, &error),
               QStringLiteral("audio trim past the material should fail")))
        return 1;

    // Video sourceIn shifts too.
    if (!check(actions->trimClip(video, 2, 30, &error),
               QStringLiteral("video trim start failed: %1").arg(error)))
        return 1;
    if (!check(entity(video).content.asset.sourceIn == 7,
               QStringLiteral("video sourceIn was not shifted")))
        return 1;
    if (!undo())
        return 1;

    // Locked ancestors reject moves, trims and deletes.
    if (!check(!actions->moveClips({lockedChild}, 5, &error),
               QStringLiteral("locked child move should fail")))
        return 1;
    if (!check(!actions->trimClip(lockedChild, 12, 20, &error),
               QStringLiteral("locked child trim should fail")))
        return 1;
    if (!check(!actions->deleteEntities({lockedRoot}, false, nullptr, &error),
               QStringLiteral("locked cascade delete should fail")))
        return 1;
    if (!check(entity(lockedChild).start == 10,
               QStringLiteral("locked edit changed the project")))
        return 1;

    // Trim end pins an open solver segment to the new last frame.
    if (!check(actions->trimClip(solverRoot, 0, 40, &error),
               QStringLiteral("solver trim failed: %1").arg(error)))
        return 1;
    if (!check(entity(solverRoot).solvers.size() == 1
                   && entity(solverRoot).solvers.first().endFrame == 40,
               QStringLiteral("open solver segment was not pinned")))
        return 1;
    if (!undo())
        return 1;

    // Reparent keeps either world time or parent-local time.
    if (!check(actions->reparent(worldSource, target, true, &error),
               QStringLiteral("keepWorld reparent failed: %1").arg(error)))
        return 1;
    if (!check(entity(worldSource).parentUuid == target
                   && entity(worldSource).start == 120
                   && entity(worldSource).end == 140,
               QStringLiteral("keepWorld changed the absolute range")))
        return 1;
    if (!undo())
        return 1;
    if (!check(actions->reparent(localSource, target, false, &error),
               QStringLiteral("keepLocal reparent failed: %1").arg(error)))
        return 1;
    if (!check(entity(localSource).parentUuid == target
                   && entity(localSource).start == 120
                   && entity(localSource).end == 140,
               QStringLiteral("keepLocal did not add the parent offset")))
        return 1;
    if (!undo())
        return 1;
    if (!check(!actions->reparent(localSource, target, true, &error),
               QStringLiteral("keepWorld outside the new parent should fail")))
        return 1;

    // Parent-range following: enabling rewrites the child to the parent's
    // range, rebases keys to keep absolute frames and cascades to following
    // grandchildren.
    auto keyFrames = [&](const pvf::Uuid& uuid, const QString&) {
        QVector<int> frames;
        for (const auto& key : entity(uuid).keyframes)
            frames.append(key.frame);
        return frames;
    };
    auto expectFrames = [&](const QVector<int>& actual,
                            const QVector<int>& expected,
                            const QString& message) {
        return check(actual == expected, message);
    };

    int followDepth = depth();
    if (!check(actions->setFollowParentRange(followChild, true, &error),
               QStringLiteral("enable follow failed: %1").arg(error)))
        return 1;
    if (!check(entity(followChild).start == 0
                   && entity(followChild).end == 20
                   && entity(followChild).followParentRange,
               QStringLiteral("enable follow did not rewrite the child range")))
        return 1;
    if (!expectFrames(keyFrames(followChild, QStringLiteral("opacity")),
                      {7, 13, 17},
                      QStringLiteral("enable follow changed absolute key frames")))
        return 1;
    if (!check(entity(followGrand).start == 0
                   && entity(followGrand).end == 20
                   && entity(followGrand).followParentRange,
               QStringLiteral("enable follow did not cascade to the grandchild")))
        return 1;
    if (!expectFrames(keyFrames(followGrand, QStringLiteral("opacity")), {5},
                      QStringLiteral("grandchild key lost its absolute frame")))
        return 1;
    flush();
    if (!check(depth() == followDepth + 1,
               QStringLiteral("enable follow did not commit exactly once")))
        return 1;
    if (!undo())
        return 1;
    if (!check(entity(followChild).start == 5
                   && entity(followChild).end == 18
                   && !entity(followChild).followParentRange,
               QStringLiteral("undo did not restore the follow state")))
        return 1;

    // Re-enable and move the parent: absolute keys move with the entity while
    // relative frames stay put.
    if (!check(actions->setFollowParentRange(followChild, true, &error),
               QStringLiteral("re-enable follow failed: %1").arg(error)))
        return 1;
    flush();
    if (!check(actions->moveClips({followParent}, 10, &error),
               QStringLiteral("follow parent move failed: %1").arg(error)))
        return 1;
    if (!check(entity(followParent).start == 10
                   && entity(followChild).start == 10
                   && entity(followChild).end == 30,
               QStringLiteral("parent move did not carry the following child")))
        return 1;
    if (!expectFrames(keyFrames(followChild, QStringLiteral("opacity")),
                      {7, 13, 17},
                      QStringLiteral("parent move changed relative key frames")))
        return 1;
    if (!undo())
        return 1;

    // Parent trim start: the child follows the new range and keys before the
    // cut disappear (the example's absolute frame 13 key).
    if (!check(actions->trimClip(followParent, 15, 20, &error),
               QStringLiteral("follow parent trim failed: %1").arg(error)))
        return 1;
    if (!check(entity(followChild).start == 15
                   && entity(followChild).end == 20,
               QStringLiteral("trim did not move the following child")))
        return 1;
    if (!expectFrames(keyFrames(followChild, QStringLiteral("opacity")), {2},
                      QStringLiteral("trim did not drop the out-of-range keys")))
        return 1;
    if (!undo())
        return 1;

    // Parent stretch end: the child expands and keeps every key.
    if (!check(actions->trimClip(followParent, 0, 30, &error),
               QStringLiteral("follow parent stretch failed: %1").arg(error)))
        return 1;
    if (!check(entity(followChild).end == 30,
               QStringLiteral("stretch did not expand the following child")))
        return 1;
    if (!expectFrames(keyFrames(followChild, QStringLiteral("opacity")),
                      {7, 13, 17},
                      QStringLiteral("stretch dropped or moved keys")))
        return 1;
    if (!undo())
        return 1;

    // Multiple following children all take the same parent range; moving one
    // on its own is rejected.
    if (!check(actions->setFollowParentRange(followPeer, true, &error),
               QStringLiteral("enable peer follow failed: %1").arg(error)))
        return 1;
    if (!check(entity(followPeer).start == entity(followParent).start
                   && entity(followPeer).end == entity(followParent).end
                   && entity(followChild).start == entity(followParent).start,
               QStringLiteral("multiple following children disagree")))
        return 1;
    if (!undo())
        return 1;
    if (!check(!actions->moveClips({followChild}, 5, &error),
               QStringLiteral("moving a following child alone should fail")))
        return 1;
    if (!check(!actions->trimClip(followChild, 1, 10, &error),
               QStringLiteral("trimming a following child alone should fail")))
        return 1;

    // Reparent a following child: it immediately takes the new parent's range
    // and keys are rebased to keep absolute frames.
    if (!check(actions->reparent(followChild, followTarget, true, &error),
               QStringLiteral("follow reparent failed: %1").arg(error)))
        return 1;
    if (!check(entity(followChild).parentUuid == followTarget
                   && entity(followChild).start == 5
                   && entity(followChild).end == 25,
               QStringLiteral("follow reparent did not take the new range")))
        return 1;
    if (!expectFrames(keyFrames(followChild, QStringLiteral("opacity")),
                      {2, 8, 12},
                      QStringLiteral("follow reparent did not rebase keys")))
        return 1;
    if (!check(entity(followGrand).start == 5
                   && entity(followGrand).end == 25,
               QStringLiteral("follow reparent did not cascade to the grandchild")))
        return 1;
    if (!undo())
        return 1;
    if (!check(entity(followChild).parentUuid == followParent
                   && entity(followChild).start == 0
                   && entity(followChild).end == 20,
               QStringLiteral("follow reparent undo did not restore the tree")))
        return 1;

    // Turning follow off keeps the current range.
    if (!check(actions->setFollowParentRange(followChild, false, &error),
               QStringLiteral("disable follow failed: %1").arg(error)))
        return 1;
    if (!check(!entity(followChild).followParentRange
                   && entity(followChild).start == 0
                   && entity(followChild).end == 20,
               QStringLiteral("disable follow changed the range")))
        return 1;
    if (!undo())
        return 1;

    // Delete cascades and undo restores the subtree.
    const int entityCount = session->project().entities.size();
    if (!check(actions->deleteEntities({trashRoot}, false, nullptr, &error),
               QStringLiteral("cascade delete failed: %1").arg(error)))
        return 1;
    if (!check(!session->project().entities.contains(trashRoot)
                   && !session->project().entities.contains(trashChild)
                   && session->project().entities.size() == entityCount - 2,
               QStringLiteral("cascade delete left children behind")))
        return 1;
    if (!undo())
        return 1;
    if (!check(session->project().entities.contains(trashChild),
               QStringLiteral("undo did not restore the deleted child")))
        return 1;

    // Batch clip helpers from the multi-selection UI: duplicate, paste and
    // group each produce one undoable commit and never touch the source clip.
    const int beforeDuplicate = session->project().entities.size();
    const QVector<pvf::Uuid> duplicated =
        actions->duplicateEntities({video}, 0, pvf::Uuid(), &error);
    if (!check(duplicated.size() == 1
                   && session->project().entities.contains(duplicated.first())
                   && entity(duplicated.first()).start == entity(video).start
                   && entity(duplicated.first()).end == entity(video).end
                   && session->project().entities.size() == beforeDuplicate + 1,
               QStringLiteral("duplicateEntities failed: %1").arg(error)))
        return 1;
    if (!undo()
        || !check(!session->project().entities.contains(duplicated.first()),
                  QStringLiteral("duplicate undo left the copy")))
        return 1;

    QVector<pvf::model::Entity> clipboard;
    clipboard.append(entity(video));
    const QVector<pvf::Uuid> pasted =
        actions->pasteEntities(clipboard, pvf::Uuid(), 100, &error);
    if (!check(pasted.size() == 1 && session->project().entities.contains(pasted.first())
                   && entity(pasted.first()).start == entity(video).start + 100,
               QStringLiteral("pasteEntities failed: %1").arg(error)))
        return 1;
    if (!undo())
        return 1;

    pvf::Uuid folder;
    if (!check(actions->groupIntoFolder({audio, video}, &folder, &error),
               QStringLiteral("groupIntoFolder failed: %1").arg(error)))
        return 1;
    if (!check(!folder.isEmpty()
                   && entity(audio).parentUuid == folder
                   && entity(video).parentUuid == folder
                   && entity(folder).start == std::min(entity(audio).start,
                                                       entity(video).start)
                   && entity(folder).end == std::max(entity(audio).end,
                                                     entity(video).end),
               QStringLiteral("group did not wrap both clips")))
        return 1;
    if (!undo() || !check(entity(audio).parentUuid.isEmpty()
                              && entity(video).parentUuid.isEmpty(),
                          QStringLiteral("group undo did not restore parents")))
        return 1;

    return 0;
}

// Timeline panel gestures: one drag = one changed()/commit/undo step, Esc
// cancels, row swaps, right-edge trim, the private overlay, drag reparent.
int editPanelChecks(std::unique_ptr<pvf::EditorSession> session,
                    const TimelineEditIds& ids,
                    const std::function<bool(bool, const QString&)>& check,
                    QString& error)
{
    const pvf::Uuid& parent = ids.parent;
    const pvf::Uuid& child = ids.child;
    const pvf::Uuid& video = ids.video;
    const pvf::Uuid& target = ids.target;
    const pvf::Uuid& worldSource = ids.worldSource;

    // Panel gesture: one drag = one changed()/commit/undo step.  Esc cancels.
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline, QStringLiteral("session window/timeline missing")))
        return 1;
    // Same offscreen layout as --timeline-test: the default split leaves the
    // timeline under three rows tall, which would put row 2 and the reparent
    // rows under the ruler (a drag there means "above everything").  Resize
    // the dock so every row this test drives is a real, clickable row.
    if (auto* stageDock =
            window->findChild<QDockWidget*>(QStringLiteral("stageDock")))
    {
        if (auto* timelineDock =
                window->findChild<QDockWidget*>(QStringLiteral("timelineDock")))
            window->resizeDocks({stageDock, timelineDock}, {320, 560},
                                Qt::Vertical);
    }
    QApplication::processEvents();
    auto rowForName = [&](const QString& name) {
        for (int row = 0; row < timeline->entityRowCountForTest(); ++row)
            if (timeline->rowNameForTest(row) == name)
                return row;
        return -1;
    };
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         qCritical() << "timeline-edit-test: panel edit failed"
                                     << message;
                     });

    pvf::EditorSession* sessionPtr = live;
    auto entity = [sessionPtr](const pvf::Uuid& uuid) {
        return sessionPtr->project().entities.value(uuid);
    };

    const int playheadBefore = live->playhead();
    const int panelRow = rowForName(QStringLiteral("父级"));
    if (!check(panelRow >= 0, QStringLiteral("parent row missing")))
        return 1;
    const QPoint startPoint(timeline->frameXForTest(20),
                            timeline->rowCenterYForTest(panelRow));
    const QPoint movePoint(timeline->frameXForTest(35),
                           timeline->rowCenterYForTest(panelRow));
    int changedSignals = 0;
    QObject::connect(live->document(), &pvf::next::Document::changed,
                     [&changedSignals] { ++changedSignals; });
    int panelDepth = live->document()->store()->undoDepth();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(startPoint),
                      QPointF(timeline->mapToGlobal(startPoint)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(movePoint),
                     QPointF(timeline->mapToGlobal(movePoint)), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(movePoint),
                        QPointF(timeline->mapToGlobal(movePoint)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &press);
    QApplication::sendEvent(timeline, &move);
    QApplication::sendEvent(timeline, &release);
    QApplication::processEvents();
    if (!check(changedSignals == 1,
               QStringLiteral("one drag emitted %1 changed signals")
                   .arg(changedSignals)))
        return 1;
    live->flush();
    if (!check(live->document()->store()->undoDepth() == panelDepth + 1,
               QStringLiteral("one drag did not create exactly one undo step")))
        return 1;
    if (!check(entity(parent).start == 15 && entity(parent).end == 75
                   && entity(child).start == 25 && entity(child).end == 55,
               QStringLiteral("panel drag did not move parent and child")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("panel drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).start == 0 && entity(child).start == 10,
               QStringLiteral("panel undo did not restore the subtree")))
        return 1;
    if (!check(live->playhead() == playheadBefore,
               QStringLiteral("panel drag changed the playhead")))
        return 1;

    // Esc during a drag restores the preview and creates no commit.
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    QMouseEvent cancelPress(QEvent::MouseButtonPress, QPointF(startPoint),
                            QPointF(timeline->mapToGlobal(startPoint)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent cancelMove(QEvent::MouseMove,
                           QPointF(timeline->frameXForTest(30),
                                   timeline->rowCenterYForTest(panelRow)),
                           QPointF(timeline->mapToGlobal(
                               QPoint(timeline->frameXForTest(30),
                                      timeline->rowCenterYForTest(panelRow)))),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &cancelPress);
    QApplication::sendEvent(timeline, &cancelMove);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(timeline, &escape);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 0, QStringLiteral("Esc emitted changed"))
        || !check(live->document()->store()->undoDepth() == panelDepth,
                  QStringLiteral("Esc created an undo step"))
        || !check(entity(parent).start == 0,
                  QStringLiteral("Esc did not restore the drag preview")))
        return 1;

    // Vertical drag between occupied rows.  Tracks are layers: an overlapping
    // drop is rejected without moving either clip.
    const int videoRow = rowForName(QStringLiteral("视频"));
    const int parentRow = rowForName(QStringLiteral("父级"));
    if (!check(videoRow >= 0 && parentRow >= 0,
               QStringLiteral("row reorder rows missing")))
        return 1;
    if (!check(timeline->rowCenterYForTest(videoRow)
                       > timeline->rulerHeightForTest()
                   && timeline->rowCenterYForTest(parentRow)
                          > timeline->rulerHeightForTest(),
               QStringLiteral("row drag target is under the ruler (%1/%2)")
                   .arg(timeline->rowCenterYForTest(videoRow))
                   .arg(timeline->rulerHeightForTest())))
        return 1;
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint rowFrom(timeline->frameXForTest(30),
                         timeline->rowCenterYForTest(parentRow));
    const QPoint rowTo(timeline->frameXForTest(30),
                       timeline->rowCenterYForTest(videoRow));
    QMouseEvent rowPress(QEvent::MouseButtonPress, QPointF(rowFrom),
                         QPointF(timeline->mapToGlobal(rowFrom)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rowMove(QEvent::MouseMove, QPointF(rowTo),
                        QPointF(timeline->mapToGlobal(rowTo)), Qt::NoButton,
                        Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rowRelease(QEvent::MouseButtonRelease, QPointF(rowTo),
                           QPointF(timeline->mapToGlobal(rowTo)),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &rowPress);
    QApplication::sendEvent(timeline, &rowMove);
    QApplication::sendEvent(timeline, &rowRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 0,
               QStringLiteral("overlapping row drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth,
                  QStringLiteral("overlapping row drag created an undo step"))
        || !check(entity(parent).row == parentRow && entity(video).row == videoRow,
                  QStringLiteral("overlapping row drag moved a clip (%1/%2)")
                      .arg(entity(parent).row)
                      .arg(entity(video).row)))
        return 1;

    // The same drop succeeds once the ranges no longer overlap: drag the
    // parent 60 frames right on the video's row.
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint sharedFrom(timeline->frameXForTest(30),
                            timeline->rowCenterYForTest(parentRow));
    const QPoint sharedTo(timeline->frameXForTest(90),
                          timeline->rowCenterYForTest(videoRow));
    QMouseEvent sharedPress(QEvent::MouseButtonPress, QPointF(sharedFrom),
                            QPointF(timeline->mapToGlobal(sharedFrom)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent sharedMove(QEvent::MouseMove, QPointF(sharedTo),
                           QPointF(timeline->mapToGlobal(sharedTo)), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QMouseEvent sharedRelease(QEvent::MouseButtonRelease, QPointF(sharedTo),
                              QPointF(timeline->mapToGlobal(sharedTo)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &sharedPress);
    QApplication::sendEvent(timeline, &sharedMove);
    QApplication::sendEvent(timeline, &sharedRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("shared-row drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("shared-row drag did not create one undo step"))
        || !check(entity(parent).row == videoRow && entity(video).row == videoRow,
                  QStringLiteral("shared-row drag did not share the track (%1/%2)")
                      .arg(entity(parent).row)
                      .arg(entity(video).row))
        || !check(entity(parent).start == 60,
                  QStringLiteral("shared-row drag did not shift time (%1)")
                      .arg(entity(parent).start)))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("shared-row drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).row == parentRow && entity(parent).start == 0,
               QStringLiteral("shared-row undo did not restore the row")))
        return 1;

    // Right-edge drag trims the parent and clips the child in one commit.
    const int trimRow = rowForName(QStringLiteral("父级"));
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    // Grab the painted right edge (inside the clip rect).  frameXForTest(60)
    // is the first pixel after the clip and hit-tests as an empty row, so it
    // would start a box select instead of the trim gesture.
    const QRect trimClipRect =
        timeline->clipRectFor(timeline->entityIdForUuid(parent));
    if (!check(!trimClipRect.isEmpty() && trimRow >= 0,
               QStringLiteral("trim target clip missing")))
        return 1;
    const QPoint trimFrom(trimClipRect.right() - 1,
                          timeline->rowCenterYForTest(trimRow));
    const QPoint trimTo(timeline->frameXForTest(30),
                        timeline->rowCenterYForTest(trimRow));
    QMouseEvent trimPress(QEvent::MouseButtonPress, QPointF(trimFrom),
                          QPointF(timeline->mapToGlobal(trimFrom)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent trimMove(QEvent::MouseMove, QPointF(trimTo),
                         QPointF(timeline->mapToGlobal(trimTo)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
    QMouseEvent trimRelease(QEvent::MouseButtonRelease, QPointF(trimTo),
                            QPointF(timeline->mapToGlobal(trimTo)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &trimPress);
    QApplication::sendEvent(timeline, &trimMove);
    QApplication::sendEvent(timeline, &trimRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("trim drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("trim drag did not create one undo step"))
        || !check(entity(parent).end == 30 && entity(child).end == 30,
                  QStringLiteral("panel trim did not cascade to the child")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("trim drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).end == 60 && entity(child).end == 40,
               QStringLiteral("trim drag undo did not restore the range")))
        return 1;

    // The private timeline overlay edits the same model and commits once.
    auto* popup = timeline->openPopup(timeline->entityIdForUuid(parent));
    QApplication::processEvents();
    if (!check(popup && popup->entityRowCountForTest() == 1,
               QStringLiteral("private timeline did not open for editing")))
        return 1;
    // Negative tracks are real hit targets, not just decoration.
    const pvf::EntityId negativeId =
        popup->entityIdForUuid(ids.negativeChild);
    const QRect negativeClip = popup->clipRectFor(negativeId);
    if (!check(negativeId != pvf::kInvalidEntity
                   && popup->rowForEntityForTest(negativeId) == -1
                   && !negativeClip.isEmpty(),
               QStringLiteral("negative track clip is not rendered")))
        return 1;
    {
        const QPoint center = negativeClip.center();
        QMouseEvent negativePress(QEvent::MouseButtonPress, QPointF(center),
                                  QPointF(popup->mapToGlobal(center)),
                                  Qt::LeftButton, Qt::LeftButton,
                                  Qt::NoModifier);
        QMouseEvent negativeRelease(QEvent::MouseButtonRelease, QPointF(center),
                                    QPointF(popup->mapToGlobal(center)),
                                    Qt::LeftButton, Qt::NoButton,
                                    Qt::NoModifier);
        QApplication::sendEvent(popup, &negativePress);
        QApplication::sendEvent(popup, &negativeRelease);
        QApplication::processEvents();
    }
    if (!check(popup->entitySelectionCountForTest() == 1
                   && live->document()->selection() == ids.negativeChild,
               QStringLiteral("negative track click did not select the clip")))
        return 1;
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint popupFrom(popup->frameXForTest(20),
                           popup->rowCenterYForTest(0));
    const QPoint popupTo(popup->frameXForTest(25),
                         popup->rowCenterYForTest(0));
    QMouseEvent popupPress(QEvent::MouseButtonPress, QPointF(popupFrom),
                           QPointF(popup->mapToGlobal(popupFrom)),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent popupMove(QEvent::MouseMove, QPointF(popupTo),
                          QPointF(popup->mapToGlobal(popupTo)), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
    QMouseEvent popupRelease(QEvent::MouseButtonRelease, QPointF(popupTo),
                             QPointF(popup->mapToGlobal(popupTo)),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(popup, &popupPress);
    QApplication::sendEvent(popup, &popupMove);
    QApplication::sendEvent(popup, &popupRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("popup drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("popup drag did not create one undo step"))
        || !check(entity(child).start == 15 && entity(child).end == 45,
                  QStringLiteral("popup drag did not move the child")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("popup drag undo failed: %1").arg(error)))
        return 1;
    timeline->closeAllPopups();
    QApplication::processEvents();

    // Property panel follow checkbox: one toggle is one undo step, enabling
    // takes the parent's range and disabling keeps the current range.
    auto* properties = window->findChild<pvf::PropertiesPanel*>();
    auto* followBox = properties
                          ? properties->findChild<QCheckBox*>(
                                QStringLiteral("entityFollowParentRange"))
                          : nullptr;
    if (!check(properties && followBox,
               QStringLiteral("follow checkbox missing")))
        return 1;
    live->document()->setSelection(child);
    QApplication::processEvents();
    if (!check(followBox->isEnabled() && !followBox->isChecked(),
               QStringLiteral("follow checkbox should start enabled and off")))
        return 1;
    panelDepth = live->document()->store()->undoDepth();
    followBox->setChecked(true);
    QApplication::processEvents();
    live->flush();
    if (!check(entity(child).followParentRange
                   && entity(child).start == entity(parent).start
                   && entity(child).end == entity(parent).end,
               QStringLiteral("follow checkbox did not adopt the parent range")))
        return 1;
    if (!check(live->document()->store()->undoDepth() == panelDepth + 1,
               QStringLiteral("follow toggle did not create one undo step")))
        return 1;
    followBox->setChecked(false);
    QApplication::processEvents();
    live->flush();
    if (!check(!entity(child).followParentRange
                   && entity(child).start == entity(parent).start
                   && entity(child).end == entity(parent).end,
               QStringLiteral("disabling follow should keep the current range")))
        return 1;
    if (!check(live->document()->store()->undoDepth() == panelDepth + 2,
               QStringLiteral("disabling follow did not create one undo step")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("follow toggle undo failed: %1").arg(error))
        || !check(live->document()->undo(&error),
                  QStringLiteral("follow toggle undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(child).start == 10 && entity(child).end == 40
                   && !entity(child).followParentRange,
               QStringLiteral("follow toggle undo did not restore the range")))
        return 1;
    live->document()->setSelection(parent);
    QApplication::processEvents();
    if (!check(!followBox->isEnabled(),
               QStringLiteral("the follow checkbox must be disabled for roots")))
        return 1;
    live->document()->setSelection(pvf::Uuid());
    QApplication::processEvents();

    // Alt + box select selects whole clips; the normal layer box-selects
    // keyframes instead.
    auto sendAlt = [timeline](QEvent::Type type) {
        QKeyEvent alt(type, Qt::Key_Alt,
                      type == QEvent::KeyPress ? Qt::AltModifier
                                               : Qt::NoModifier);
        QApplication::sendEvent(timeline, &alt);
        QApplication::processEvents();
    };
    auto dragBox = [&](const QPoint& from, const QPoint& to,
                       Qt::KeyboardModifiers modifiers) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          QPointF(timeline->mapToGlobal(from)), Qt::LeftButton,
                          Qt::LeftButton, modifiers);
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         QPointF(timeline->mapToGlobal(to)), Qt::NoButton,
                         Qt::LeftButton, modifiers);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to),
                            QPointF(timeline->mapToGlobal(to)), Qt::LeftButton,
                            Qt::NoButton, modifiers);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &move);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
    };
    auto clickAt = [&](const QPoint& pos, Qt::KeyboardModifiers modifiers) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos),
                          QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                          Qt::LeftButton, modifiers);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos),
                            QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                            Qt::NoButton, modifiers);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
    };
    const int selectionAudioRow = rowForName(QStringLiteral("音频"));
    const int selectionVideoRow = rowForName(QStringLiteral("视频"));
    if (!check(selectionAudioRow >= 0 && selectionVideoRow >= 0,
               QStringLiteral("multi-select rows missing")))
        return 1;
    sendAlt(QEvent::KeyPress);
    dragBox(QPoint(timeline->frameXForTest(80),
                   timeline->rowCenterYForTest(selectionVideoRow)),
            QPoint(timeline->frameXForTest(20),
                   timeline->rowCenterYForTest(selectionAudioRow)),
            Qt::AltModifier);
    sendAlt(QEvent::KeyRelease);
    if (!check(timeline->entitySelectionCountForTest() == 2,
               QStringLiteral("Alt box select selected %1 clips")
                   .arg(timeline->entitySelectionCountForTest())))
        return 1;

    const int selectionParentRow = rowForName(QStringLiteral("父级"));
    const QPoint ctrlPoint(timeline->frameXForTest(30),
                           timeline->rowCenterYForTest(selectionParentRow));
    QMouseEvent ctrlPress(QEvent::MouseButtonPress, QPointF(ctrlPoint),
                          QPointF(timeline->mapToGlobal(ctrlPoint)),
                          Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QMouseEvent ctrlRelease(QEvent::MouseButtonRelease, QPointF(ctrlPoint),
                            QPointF(timeline->mapToGlobal(ctrlPoint)),
                            Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(timeline, &ctrlPress);
    QApplication::sendEvent(timeline, &ctrlRelease);
    QApplication::processEvents();
    if (!check(timeline->entitySelectionCountForTest() == 3,
               QStringLiteral("Ctrl-click left %1 selected clips")
                   .arg(timeline->entitySelectionCountForTest())))
        return 1;
    live->document()->setSelection(pvf::Uuid());
    QApplication::processEvents();

    // There is no keyframe-select button; clicking a blank area clears both
    // layers, and the normal empty-box drag selects diamonds only.
    if (!check(timeline->rowButtonRect(videoRow, 2).isEmpty(),
               QStringLiteral("the blue select-all keyframes button survived")))
        return 1;
    if (!check(live->editActions()->addKeyframe(video, 10, &error),
               QStringLiteral("keyframe box setup failed: %1").arg(error)))
        return 1;
    live->flush();
    auto* objectTab = window->findChild<QWidget*>(QStringLiteral("objectTab"));
    if (!check(objectTab != nullptr, QStringLiteral("object tab missing")))
        return 1;
    dragBox(QPoint(timeline->frameXForTest(80),
                   timeline->rowCenterYForTest(videoRow)),
            QPoint(timeline->frameXForTest(5),
                   timeline->rowCenterYForTest(videoRow)),
            Qt::NoModifier);
    if (!check(timeline->selectedKeyframeCount() == 1
                   && live->document()->selection() == video,
               QStringLiteral("normal box select did not take the keyframe (%1)")
                   .arg(timeline->selectedKeyframeCount())))
        return 1;
    clickAt(QPoint(timeline->frameXForTest(60),
                   timeline->rowCenterYForTest(videoRow)),
            Qt::NoModifier);
    if (!check(live->document()->selection().isEmpty()
                   && timeline->entitySelectionCountForTest() == 0
                   && timeline->selectedKeyframeCount() == 0
                   && !objectTab->isVisible(),
               QStringLiteral("blank click did not clear every selection")))
        return 1;

    // Two separate slow clicks never rename; Alt + fast double-click does.
    const QPoint clipPoint(timeline->frameXForTest(15),
                           timeline->rowCenterYForTest(videoRow));
    clickAt(clipPoint, Qt::NoModifier);
    QThread::msleep(450);
    clickAt(clipPoint, Qt::NoModifier);
    if (!check(!timeline->inlineRenameActiveForTest(),
               QStringLiteral("slow double-click still started a rename")))
        return 1;
    const int keysBeforeRename = int(entity(video).keyframes.size());
    sendAlt(QEvent::KeyPress);
    QMouseEvent renameDouble(QEvent::MouseButtonDblClick, QPointF(clipPoint),
                             QPointF(timeline->mapToGlobal(clipPoint)),
                             Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    QApplication::sendEvent(timeline, &renameDouble);
    QApplication::processEvents();
    if (!check(timeline->inlineRenameActiveForTest(),
               QStringLiteral("Alt + double-click did not open the rename edit"))
        || !check(int(entity(video).keyframes.size()) == keysBeforeRename,
                  QStringLiteral("Alt + double-click also created a key")))
        return 1;
    auto* renameEdit =
        timeline->findChild<QLineEdit*>(QStringLiteral("clipRenameEdit"));
    if (!check(renameEdit != nullptr,
               QStringLiteral("rename editor widget missing")))
        return 1;
    renameEdit->setText(QStringLiteral("视频改名"));
    timeline->commitInlineRenameForTest();
    QApplication::processEvents();
    live->flush();
    if (!check(entity(video).name == QStringLiteral("视频改名"),
               QStringLiteral("Alt + double-click rename did not commit")))
        return 1;
    sendAlt(QEvent::KeyRelease);
    QApplication::processEvents();

    // Dragging onto a folder clip asks world/local and commits the choice.
    const int reparentSourceRow = rowForName(QStringLiteral("世界位置"));
    const int reparentTargetRow = rowForName(QStringLiteral("目标父级"));
    if (!check(reparentSourceRow >= 0 && reparentTargetRow >= 0,
               QStringLiteral("reparent rows missing")))
        return 1;
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint reparentFrom(timeline->frameXForTest(130),
                              timeline->rowCenterYForTest(reparentSourceRow));
    const QPoint reparentTo(timeline->frameXForTest(130),
                            timeline->rowCenterYForTest(reparentTargetRow));
    QTimer::singleShot(0, [] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (QAbstractButton* button : box->buttons())
        {
            if (button->text().contains(QStringLiteral("保持世界位置")))
            {
                button->click();
                return;
            }
        }
        box->reject();
    });
    QTimer::singleShot(2000, [] {
        if (auto* box =
                qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->reject();
    });
    QMouseEvent reparentPress(QEvent::MouseButtonPress, QPointF(reparentFrom),
                              QPointF(timeline->mapToGlobal(reparentFrom)),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent reparentMove(QEvent::MouseMove, QPointF(reparentTo),
                             QPointF(timeline->mapToGlobal(reparentTo)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent reparentRelease(
        QEvent::MouseButtonRelease, QPointF(reparentTo),
        QPointF(timeline->mapToGlobal(reparentTo)), Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(timeline, &reparentPress);
    QApplication::sendEvent(timeline, &reparentMove);
    QApplication::sendEvent(timeline, &reparentRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("reparent drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("reparent drag did not create one undo step"))
        || !check(entity(worldSource).parentUuid == target
                      && entity(worldSource).start == 120
                      && entity(worldSource).end == 140,
                  QStringLiteral("drag reparent keepWorld result wrong")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("reparent drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(worldSource).parentUuid.isEmpty()
                   && rowForName(QStringLiteral("世界位置")) >= 0,
               QStringLiteral("reparent undo did not restore the root row")))
        return 1;

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!check(window->grab().save(
                   QStringLiteral("/tmp/opencode/pvforge-p2b1-edit.png")),
               QStringLiteral("screenshot failed")))
        return 1;

    window->close();
    QApplication::processEvents();
    return 0;
}

} // namespace

namespace pvf::tests {

int runTimelineEditTest(QApplication& app)
{
    // Multi-clip vertical move lives in its own translation unit to keep this
    // file under the size budget; it still runs as part of this test case.
    if (runTimelineRowMoveChecks(app) != 0)
        return 1;

    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b1-edit.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b1 时间轴编辑");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-edit-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    pvf::EditActions* actions = session->editActions();
    if (!actions)
    {
        qCritical() << "timeline-edit-test: no EditActions";
        return 1;
    }

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-edit-test:" << message;
        return condition;
    };

    const QString audioHash(64, QLatin1Char('a'));
    const QString videoHash(64, QLatin1Char('b'));
    auto writeFakeAsset = [&](const QString& hash, const QString& type, int frames) {
        const QString directory =
            projectDir + QStringLiteral("/assets/") + hash;
        if (!QDir().mkpath(directory))
            return false;
        QJsonObject metadata;
        metadata.insert(QStringLiteral("hash"), hash);
        metadata.insert(QStringLiteral("type"), type);
        metadata.insert(QStringLiteral("name"), type + QStringLiteral(".bin"));
        metadata.insert(QStringLiteral("durationFrames"), frames);
        metadata.insert(QStringLiteral("durationSeconds"), frames / 30.0);
        QFile metadataFile(directory + QStringLiteral("/asset.json"));
        if (!metadataFile.open(QIODevice::WriteOnly))
            return false;
        metadataFile.write(QJsonDocument(metadata).toJson());
        metadataFile.close();
        QFile content(directory + QStringLiteral("/content.bin"));
        if (!content.open(QIODevice::WriteOnly))
            return false;
        content.write("fake");
        return true;
    };
    if (!check(writeFakeAsset(audioHash, QStringLiteral("audio"), 60)
                   && writeFakeAsset(videoHash, QStringLiteral("video"), 40),
               QStringLiteral("fake asset write failed")))
        return 1;

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row,
                                 const pvf::Uuid& parent = pvf::Uuid()) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.parentUuid = parent;
        entity.content.kind = pvf::ContentKind::Empty;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    auto makeAsset = [&](const QString& name, int start, int end, int row,
                         pvf::AssetType type, const QString& hash,
                         int sourceIn) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Asset;
        entity.content.asset.type = type;
        entity.content.asset.hash = hash;
        entity.content.asset.sourceIn = sourceIn;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid parent = makeEntity(QStringLiteral("父级"), 0, 60, 0);
    const pvf::Uuid child = makeEntity(QStringLiteral("子级"), 10, 40, 0, parent);
    project.entities[child].content.kind = pvf::ContentKind::Text;
    {
        pvf::model::Keyframe first;
        first.frame = 0;
        pvf::model::Keyframe second;
        second.frame = 29;
        second.opacity = 0.5;
        project.entities[child].keyframes = {first, second};
    }

    // A grandchild under the child makes a parent trim drop a whole subtree
    // while the parent itself stays in the same plan.
    const pvf::Uuid childGrand =
        makeEntity(QStringLiteral("子级孙层"), 15, 20, 0, child);
    const pvf::Uuid negativeChild =
        makeEntity(QStringLiteral("负轨子级"), 12, 18, -1, parent);

    const pvf::Uuid audio = makeAsset(QStringLiteral("音频"), 20, 50, 1,
                                      pvf::AssetType::Audio, audioHash, 0);
    const pvf::Uuid video = makeAsset(QStringLiteral("视频"), 0, 30, 2,
                                      pvf::AssetType::Video, videoHash, 5);
    const pvf::Uuid lockedRoot = makeEntity(QStringLiteral("锁定父级"), 0, 60, 3);
    const pvf::Uuid lockedChild =
        makeEntity(QStringLiteral("锁定子级"), 10, 20, 0, lockedRoot);
    const pvf::Uuid target = makeEntity(QStringLiteral("目标父级"), 100, 200, 4);
    const pvf::Uuid worldSource =
        makeEntity(QStringLiteral("世界位置"), 120, 140, 5);
    const pvf::Uuid localSource =
        makeEntity(QStringLiteral("局部坐标"), 20, 40, 6);
    const pvf::Uuid solverRoot =
        makeEntity(QStringLiteral("求解段"), 0, 60, 7);
    pvf::model::SolverSegment segment;
    segment.id = pvf::model::newUuid();
    segment.effect = pvf::SolverEffect::Gravity;
    segment.startFrame = 0;
    segment.endFrame = -1;
    segment.params.insert(QStringLiteral("targetUuid"), parent);
    project.entities[solverRoot].solvers.append(segment);
    const pvf::Uuid trashRoot = makeEntity(QStringLiteral("待删除"), 200, 220, 8);
    const pvf::Uuid trashChild =
        makeEntity(QStringLiteral("待删除子级"), 205, 210, 0, trashRoot);

    const pvf::Uuid followParent =
        makeEntity(QStringLiteral("跟随父级"), 0, 20, 9);
    const pvf::Uuid followChild =
        makeEntity(QStringLiteral("跟随子级"), 5, 18, 0, followParent);
    const pvf::Uuid followPeer =
        makeEntity(QStringLiteral("跟随同伴"), 5, 15, 1, followParent);
    const pvf::Uuid followGrand =
        makeEntity(QStringLiteral("跟随孙级"), 5, 18, 0, followChild);
    project.entities[followGrand].followParentRange = true;
    const pvf::Uuid followTarget =
        makeEntity(QStringLiteral("新跟随父级"), 5, 25, 10);
    for (const auto& pair : QVector<QPair<int, double>>{{2, 0.0}, {8, 1.0},
                                                        {12, 0.5}})
    {
        pvf::model::Keyframe key;
        key.frame = pair.first;
        key.opacity = pair.second;
        project.entities[followChild].keyframes.append(key);
    }
    {
        pvf::model::Keyframe key;
        key.frame = 0;
        project.entities[followGrand].keyframes.append(key);
    }

    const QVector<pvf::Uuid> all = {
        parent,      child,       childGrand,  audio,      video,
        lockedRoot,  lockedChild, target,      worldSource, localSource,
        solverRoot,  trashRoot,   trashChild,  followParent, followChild,
        followPeer,  followGrand, followTarget, negativeChild};
    if (!document->commitEntities(all, QStringLiteral("p2b1 setup"), &error))
    {
        qCritical() << "timeline-edit-test: setup commit failed" << error;
        return 1;
    }
    // Lock the parent only after the tree exists: an ancestor-locked upsert is
    // rejected on purpose, which is exactly the guard the checks below cover.
    project.entities[lockedRoot].locked = true;
    if (!document->commitEntity(lockedRoot, QStringLiteral("p2b1 lock"), &error))
    {
        qCritical() << "timeline-edit-test: lock commit failed" << error;
        return 1;
    }
    session->flush();

    auto flush = [&] {
        QString flushError;
        session->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-edit-test: flush failed" << flushError;
    };
    auto undo = [&] {
        flush();
        QString undoError;
        if (!document->undo(&undoError))
        {
            qCritical() << "timeline-edit-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto depth = [&] { return document->store()->undoDepth(); };
    pvf::EditorSession* sessionPtr = session.get();
    auto entity = [sessionPtr](const pvf::Uuid& uuid) {
        return sessionPtr->project().entities.value(uuid);
    };

    // Trimming a parent can keep the parent while dropping a whole child
    // subtree.  The plan then has upserts plus a parent-and-child delete list;
    // the document must delete the subtree once instead of failing on the
    // child whose parent was already removed.
    if (!check(actions->trimClip(parent, 45, 60, &error),
               QStringLiteral("subtree trim failed: %1").arg(error)))
        return 1;
    if (!check(session->project().entities.contains(parent)
                   && !session->project().entities.contains(child)
                   && !session->project().entities.contains(childGrand),
               QStringLiteral("subtree trim left the child subtree behind")))
        return 1;
    if (!undo())
        return 1;
    if (!check(entity(child).start == 10 && entity(child).end == 40
                   && session->project().entities.contains(childGrand),
               QStringLiteral("subtree trim undo did not restore the child")))
        return 1;

    const TimelineEditIds ids{parent,        child,       audio,
                              video,         lockedRoot,  lockedChild,
                              target,        worldSource, localSource,
                              solverRoot,    trashRoot,   trashChild,
                              followParent,  followChild, followPeer,
                              followGrand,   followTarget, negativeChild};
    const TimelineEditOps ops{check, flush, undo, depth, entity};
    if (editModelChecks(session.get(), actions, ids, ops, error) != 0)
        return 1;
    if (editPanelChecks(std::move(session), ids, check, error) != 0)
        return 1;

    qInfo() << "timeline-edit-test OK: subtree move, trim cascade/keys/sourceIn,"
               " solver pin, locks, row/trim/popup gestures, one gesture one"
               " undo, drag reparent world/local, cascade delete";
    return 0;
}

int runTimelineEditE2eTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir = QDir(temp.path()).filePath(
        QStringLiteral("timeline-edit-e2e.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("时间轴编辑端到端");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    // Self-contained setup: create the project inside the temporary directory,
    // commit two clips, flush them to disk and reopen the directory project.
    // Nothing here depends on /tmp/pvfe2e or any user path.
    QString error;
    {
        auto created =
            pvf::EditorSession::createProject(projectDir, settings, &error);
        if (!created)
        {
            qCritical() << "timeline-edit-e2e: create failed" << error;
            return 1;
        }
        auto& project = created->mutableProject();
        auto makeAsset = [&project](const QString& name, int start, int end,
                                    int row, pvf::AssetType type,
                                    const QString& hash) {
            pvf::model::Entity entity;
            entity.uuid = pvf::model::newUuid();
            entity.name = name;
            entity.start = start;
            entity.end = end;
            entity.row = row;
            entity.content.kind = pvf::ContentKind::Asset;
            entity.content.asset.type = type;
            entity.content.asset.hash = hash;
            project.entities.insert(entity.uuid, entity);
            return entity.uuid;
        };
        const pvf::Uuid audio =
            makeAsset(QStringLiteral("音频"), 20, 80, 1, pvf::AssetType::Audio,
                      QString(64, QLatin1Char('d')));
        const pvf::Uuid image =
            makeAsset(QStringLiteral("图片"), 10, 50, 2, pvf::AssetType::Image,
                      QString(64, QLatin1Char('e')));
        if (!created->document()->commitEntities(
                {audio, image}, QStringLiteral("e2e setup"), &error))
        {
            qCritical() << "timeline-edit-e2e: setup commit failed" << error;
            return 1;
        }
        QString flushError;
        created->flush(&flushError);
        if (!flushError.isEmpty())
        {
            qCritical() << "timeline-edit-e2e: setup flush failed" << flushError;
            return 1;
        }
    }
    auto session = pvf::EditorSession::openProject(projectDir, &error);
    if (!session)
    {
        qCritical() << "timeline-edit-e2e: open failed" << error;
        return 1;
    }
    auto findUuid = [&](const QString& name) {
        for (auto it = session->project().entities.constBegin();
             it != session->project().entities.constEnd(); ++it)
            if (it.value().name == name)
                return it.key();
        return pvf::Uuid();
    };
    const pvf::Uuid audio = findUuid(QStringLiteral("音频"));
    const pvf::Uuid image = findUuid(QStringLiteral("图片"));
    if (audio.isEmpty() || image.isEmpty())
    {
        qCritical() << "timeline-edit-e2e: audio/image entity missing";
        return 1;
    }
    const int audioStartBefore = session->project().entities.value(audio).start;
    const int audioEndBefore = session->project().entities.value(audio).end;
    const int imageStartBefore = session->project().entities.value(image).start;
    const int imageEndBefore = session->project().entities.value(image).end;

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 820);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !timeline)
    {
        qCritical() << "timeline-edit-e2e: window has no session timeline";
        return 1;
    }
    if (auto* stageDock =
            window->findChild<QDockWidget*>(QStringLiteral("stageDock")))
    {
        if (auto* timelineDock =
                window->findChild<QDockWidget*>(QStringLiteral("timelineDock")))
            window->resizeDocks({stageDock, timelineDock}, {320, 520},
                                Qt::Vertical);
    }
    QApplication::processEvents();
    auto rowForUuid = [&](const pvf::Uuid& uuid) {
        return timeline->rowForEntityForTest(timeline->entityIdForUuid(uuid));
    };
    auto dragClip = [&](const pvf::Uuid& uuid, int grabFrame, int delta) {
        const int row = rowForUuid(uuid);
        if (row < 0)
            return false;
        const QPoint from(timeline->frameXForTest(grabFrame),
                          timeline->rowCenterYForTest(row));
        const QPoint to(timeline->frameXForTest(grabFrame + delta),
                        timeline->rowCenterYForTest(row));
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          QPointF(timeline->mapToGlobal(from)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         QPointF(timeline->mapToGlobal(to)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to),
                            QPointF(timeline->mapToGlobal(to)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &move);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
        return true;
    };
    if (!dragClip(audio, audioStartBefore + 10, 30)
        || !dragClip(image, imageStartBefore + 10, 20))
    {
        qCritical() << "timeline-edit-e2e: drag failed";
        return 1;
    }
    live->flush();

    auto readRange = [&](const pvf::Uuid& uuid, int* start, int* end) {
        QFile file(live->root() + QStringLiteral("/entities/") + uuid
                   + QStringLiteral(".json"));
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QJsonObject object =
            QJsonDocument::fromJson(file.readAll()).object();
        *start = object.value(QStringLiteral("start")).toInt();
        *end = object.value(QStringLiteral("end")).toInt();
        return true;
    };
    int audioStart = 0;
    int audioEnd = 0;
    int imageStart = 0;
    int imageEnd = 0;
    if (!readRange(audio, &audioStart, &audioEnd)
        || !readRange(image, &imageStart, &imageEnd))
    {
        qCritical() << "timeline-edit-e2e: cannot read entity JSON";
        return 1;
    }
    if (audioStart != audioStartBefore + 30
        || audioEnd != audioEndBefore + 30
        || imageStart != imageStartBefore + 20
        || imageEnd != imageEndBefore + 20)
    {
        qCritical() << "timeline-edit-e2e: ranges wrong"
                    << "audio" << audioStartBefore << audioEndBefore << "->"
                    << audioStart << audioEnd << "image" << imageStartBefore
                    << imageEndBefore << "->" << imageStart << imageEnd;
        return 1;
    }

    const QString shot =
        QDir(temp.path()).filePath(QStringLiteral("timeline-edit-e2e.png"));
    if (!window->grab().save(shot))
    {
        qCritical() << "timeline-edit-e2e: screenshot failed" << shot;
        return 1;
    }
    qInfo() << "timeline-edit-e2e OK: audio"
            << audioStartBefore << "->" << audioStart << " image"
            << imageStartBefore << "->" << imageStart << projectDir;
    window->close();
    QApplication::processEvents();
    return 0;
}

} // namespace pvf::tests
