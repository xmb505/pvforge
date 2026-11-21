#include "TimelineRowMoveChecks.h"

#include "TestApp.h"

#include <QDockWidget>
#include <QMouseEvent>

namespace {

struct RowMoveIds
{
    pvf::Uuid rowA;
    pvf::Uuid rowB;
    pvf::Uuid clampA;
    pvf::Uuid clampB;
    pvf::Uuid blocker;
    pvf::Uuid privateParent;
    pvf::Uuid privateA;
    pvf::Uuid privateB;
    pvf::Uuid privateGrand;
};

int rowMoveModelChecks(pvf::EditorSession* session, pvf::EditActions* actions,
                       const RowMoveIds& ids,
                       const std::function<bool(bool, const QString&)>& check,
                       QString& error)
{
    auto entity = [session](const pvf::Uuid& uuid) {
        return session->project().entities.value(uuid);
    };
    auto flush = [session] {
        QString flushError;
        session->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-row-move-test: flush failed" << flushError;
    };
    auto undo = [session, &flush] {
        flush();
        QString undoError;
        if (!session->document()->undo(&undoError))
        {
            qCritical() << "timeline-row-move-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto depth = [session] { return session->document()->store()->undoDepth(); };

    // Two roots on rows 0/1 move up two rows and 50 frames as one block.
    const int blockDepth = depth();
    pvf::EditPlan blockPlan;
    if (!check(actions->planMoveClipsToRows({ids.rowA, ids.rowB}, 50, 2,
                                            &blockPlan, &error),
               QStringLiteral("multi row plan failed: %1").arg(error)))
        return 1;
    if (!check(blockPlan.project.entities.value(ids.rowA).row == 2
                   && blockPlan.project.entities.value(ids.rowB).row == 3,
               QStringLiteral("multi row plan broke relative row spacing")))
        return 1;
    if (!check(blockPlan.project.entities.value(ids.rowA).start == 150
                   && blockPlan.project.entities.value(ids.rowB).end == 160,
               QStringLiteral("multi row plan did not shift time")))
        return 1;
    if (!check(actions->apply(blockPlan, &error),
               QStringLiteral("multi row apply failed: %1").arg(error)))
        return 1;
    flush();
    if (!check(depth() == blockDepth + 1,
               QStringLiteral("multi row move was not one commit")))
        return 1;
    if (!check(entity(ids.rowA).row == 2 && entity(ids.rowB).row == 3,
               QStringLiteral("multi row move did not reach rows 2/3")))
        return 1;
    if (!undo()
        || !check(entity(ids.rowA).row == 0 && entity(ids.rowB).row == 1
                      && entity(ids.rowA).start == 100,
                  QStringLiteral("multi row undo did not restore rows/time")))
        return 1;

    // Dragging the block below row 0 clamps the whole block at row 0 instead
    // of producing negative root rows.
    pvf::EditPlan clampPlan;
    if (!check(actions->planMoveClipsToRows({ids.clampA, ids.clampB}, 0, -10,
                                            &clampPlan, &error),
               QStringLiteral("clamped row plan failed: %1").arg(error)))
        return 1;
    if (!check(clampPlan.project.entities.value(ids.clampA).row == 0
                   && clampPlan.project.entities.value(ids.clampB).row == 1,
               QStringLiteral("clamp did not pin the block at rows 0/1")))
        return 1;
    const int clampDepth = depth();
    if (!check(actions->apply(clampPlan, &error),
               QStringLiteral("clamped row apply failed: %1").arg(error)))
        return 1;
    flush();
    if (!check(depth() == clampDepth + 1,
               QStringLiteral("clamped row move was not one commit")))
        return 1;
    if (!undo()
        || !check(entity(ids.clampA).row == 2
                      && entity(ids.clampB).row == 3,
                  QStringLiteral("clamped row undo did not restore rows")))
        return 1;

    // The target row belongs to an unselected clip with an overlapping time
    // range: reject the plan and leave the project untouched.
    pvf::EditPlan conflictPlan;
    if (!check(!actions->planMoveClipsToRows({ids.rowA, ids.rowB}, 0, 4,
                                             &conflictPlan, &error),
               QStringLiteral("overlapping target row was accepted")))
        return 1;
    if (!check(error.contains(QStringLiteral("重叠")),
               QStringLiteral("overlap rejection lacks a clear reason: %1")
                   .arg(error)))
        return 1;
    if (!check(entity(ids.rowA).row == 0 && entity(ids.rowB).row == 1,
               QStringLiteral("rejected row plan moved the clips")))
        return 1;

    // Root and private child have no shared row axis: reject only the vertical
    // part instead of guessing a layer mapping.
    pvf::EditPlan mixedPlan;
    if (!check(!actions->planMoveClipsToRows({ids.rowA, ids.privateA}, 0, 1,
                                             &mixedPlan, &error),
               QStringLiteral("mixed-parent row move was accepted")))
        return 1;
    if (!check(error.contains(QStringLiteral("不同父级")),
               QStringLiteral("mixed-parent rejection lacks a clear reason: %1")
                   .arg(error)))
        return 1;

    // Private timeline: two children shift across the positive/negative
    // divider together and keep their layer order.
    const int privateDepth = depth();
    pvf::EditPlan privatePlan;
    if (!check(actions->planMoveClipsToRows(
                   {ids.privateA, ids.privateB}, 2, -1, &privatePlan, &error),
               QStringLiteral("private row plan failed: %1").arg(error)))
        return 1;
    if (!check(privatePlan.project.entities.value(ids.privateA).row == -1
                   && privatePlan.project.entities.value(ids.privateB).row == -2,
               QStringLiteral("private block did not cross the divider")))
        return 1;
    if (!check(privatePlan.project.entities.value(ids.privateGrand).row == 0
                   && privatePlan.project.entities.value(ids.privateGrand).start
                          == 17,
               QStringLiteral("private move did not carry the grandchild time")))
        return 1;
    if (!check(actions->apply(privatePlan, &error),
               QStringLiteral("private row apply failed: %1").arg(error)))
        return 1;
    flush();
    if (!check(depth() == privateDepth + 1,
               QStringLiteral("private row move was not one commit")))
        return 1;
    if (!check(entity(ids.privateA).row == -1
                   && entity(ids.privateA).start == 12
                   && entity(ids.privateB).row == -2
                   && entity(ids.privateB).start == 22,
               QStringLiteral("private row move result is wrong")))
        return 1;
    if (!undo()
        || !check(entity(ids.privateA).row == 0
                      && entity(ids.privateB).row == -1
                      && entity(ids.privateA).start == 10,
                  QStringLiteral("private row undo did not restore rows/time")))
        return 1;

    return 0;
}

int rowMovePanelChecks(std::unique_ptr<pvf::EditorSession> session,
                       const RowMoveIds& ids,
                       const std::function<bool(bool, const QString&)>& check,
                       QString& error)
{
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline,
               QStringLiteral("row move window/timeline missing")))
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

    auto entity = [live](const pvf::Uuid& uuid) {
        return live->project().entities.value(uuid);
    };
    auto rowFor = [timeline](const pvf::Uuid& uuid) {
        return timeline->rowForEntityForTest(
            timeline->entityIdForUuid(uuid));
    };

    const int rowA = rowFor(ids.rowA);
    const int rowB = rowFor(ids.rowB);
    if (!check(rowA == 0 && rowB == 1,
               QStringLiteral("row move setup rows are wrong (%1/%2)")
                   .arg(rowA)
                   .arg(rowB)))
        return 1;
    if (!check(timeline->rowCenterYForTest(4)
                   > timeline->rulerHeightForTest(),
               QStringLiteral("row 4 is under the ruler")))
        return 1;

    auto sendAlt = [timeline](QEvent::Type type) {
        QKeyEvent alt(type, Qt::Key_Alt,
                      type == QEvent::KeyPress ? Qt::AltModifier
                                               : Qt::NoModifier);
        QApplication::sendEvent(timeline, &alt);
        QApplication::processEvents();
    };
    auto dragBox = [timeline](const QPoint& from, const QPoint& to,
                              Qt::KeyboardModifiers modifiers) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          QPointF(timeline->mapToGlobal(from)),
                          Qt::LeftButton, Qt::LeftButton, modifiers);
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
    auto dragClip = [timeline](const QPoint& from, const QPoint& to) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          QPointF(timeline->mapToGlobal(from)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         QPointF(timeline->mapToGlobal(to)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to),
                            QPointF(timeline->mapToGlobal(to)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &move);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
    };

    int changedSignals = 0;
    QObject::connect(live->document(), &pvf::next::Document::changed,
                     [&changedSignals] { ++changedSignals; });
    int failedSignals = 0;
    QString failure;
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [&failedSignals, &failure](const QString& message) {
                         ++failedSignals;
                         failure = message;
                     });

    sendAlt(QEvent::KeyPress);
    dragBox(QPoint(timeline->frameXForTest(115),
                   timeline->rowCenterYForTest(rowA)),
            QPoint(timeline->frameXForTest(95),
                   timeline->rowCenterYForTest(rowB)),
            Qt::AltModifier);
    sendAlt(QEvent::KeyRelease);
    if (!check(timeline->entitySelectionCountForTest() == 2,
               QStringLiteral("Alt box selected %1 clips")
                   .arg(timeline->entitySelectionCountForTest())))
        return 1;

    // Dragging the block below row 0 clamps at the boundary: the preview
    // restores and the release is a no-op, not an empty "commit".
    const int clampDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    failedSignals = 0;
    dragClip(QPoint(timeline->frameXForTest(105),
                    timeline->rowCenterYForTest(rowB)),
             QPoint(timeline->frameXForTest(105),
                    timeline->rowCenterYForTest(rowA)));
    live->flush();
    if (!check(changedSignals == 0,
               QStringLiteral("clamped row drag emitted %1 changes")
                   .arg(changedSignals))
        || !check(failedSignals == 0,
                  QStringLiteral("clamped row drag reported a failure"))
        || !check(live->document()->store()->undoDepth() == clampDepth,
                  QStringLiteral("clamped row drag created an undo step"))
        || !check(entity(ids.rowA).row == 0 && entity(ids.rowB).row == 1,
                  QStringLiteral("clamped row drag moved the clips")))
        return 1;

    // A conflicting vertical drop restores the baseline preview and reports
    // once on release; no commit is created.
    const int conflictDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    failedSignals = 0;
    dragClip(QPoint(timeline->frameXForTest(105),
                    timeline->rowCenterYForTest(rowA)),
             QPoint(timeline->frameXForTest(105),
                    timeline->rowCenterYForTest(4)));
    live->flush();
    if (!check(changedSignals == 0,
               QStringLiteral("conflicting row drag emitted %1 changes")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == conflictDepth,
                  QStringLiteral("conflicting row drag created an undo step"))
        || !check(failedSignals == 1
                      && failure.contains(QStringLiteral("重叠")),
                  QStringLiteral("conflicting row drag did not report once"))
        || !check(entity(ids.rowA).row == 0 && entity(ids.rowB).row == 1,
                  QStringLiteral("conflicting row drag moved the clips")))
        return 1;

    // The same block drops successfully two rows up and stays one block.
    const int moveDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    failedSignals = 0;
    dragClip(QPoint(timeline->frameXForTest(105),
                    timeline->rowCenterYForTest(rowA)),
             QPoint(timeline->frameXForTest(105),
                    timeline->rowCenterYForTest(2)));
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("multi row drag emitted %1 changes")
                   .arg(changedSignals))
        || !check(failedSignals == 0,
                  QStringLiteral("multi row drag reported a failure"))
        || !check(live->document()->store()->undoDepth() == moveDepth + 1,
                  QStringLiteral("multi row drag was not one commit"))
        || !check(entity(ids.rowA).row == 2 && entity(ids.rowB).row == 3,
                  QStringLiteral("multi row drag did not reach rows 2/3"))
        || !check(entity(ids.rowA).start == 100
                      && entity(ids.rowB).start == 100,
                  QStringLiteral("multi row drag changed time unexpectedly")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("multi row drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(ids.rowA).row == 0 && entity(ids.rowB).row == 1,
               QStringLiteral("multi row drag undo did not restore rows")))
        return 1;

    window->close();
    QApplication::processEvents();
    return 0;
}

} // namespace

namespace pvf::tests {

int runTimelineRowMoveChecks(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("row-move.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("多选跨行移动");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-row-move-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    pvf::EditActions* actions = session->editActions();
    if (!document || !actions)
    {
        qCritical() << "timeline-row-move-test: no document/EditActions";
        return 1;
    }

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-row-move-test:" << message;
        return condition;
    };

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end,
                                 int row, const pvf::Uuid& parent = {}) {
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

    RowMoveIds ids;
    ids.rowA = makeEntity(QStringLiteral("上行 A"), 100, 110, 0);
    ids.rowB = makeEntity(QStringLiteral("上行 B"), 100, 110, 1);
    ids.clampA = makeEntity(QStringLiteral("夹紧 A"), 200, 210, 2);
    ids.clampB = makeEntity(QStringLiteral("夹紧 B"), 200, 210, 3);
    ids.blocker = makeEntity(QStringLiteral("目标阻挡"), 100, 200, 4);
    ids.privateParent = makeEntity(QStringLiteral("私有父级"), 0, 100, 5);
    ids.privateA =
        makeEntity(QStringLiteral("私有多选 A"), 10, 40, 0, ids.privateParent);
    ids.privateB =
        makeEntity(QStringLiteral("私有多选 B"), 20, 30, -1, ids.privateParent);
    ids.privateGrand =
        makeEntity(QStringLiteral("私有孙级"), 15, 20, 0, ids.privateA);

    const QVector<pvf::Uuid> all = {
        ids.rowA,     ids.rowB,      ids.clampA,     ids.clampB,
        ids.blocker,  ids.privateParent, ids.privateA, ids.privateB,
        ids.privateGrand};
    if (!document->commitEntities(all, QStringLiteral("row move setup"),
                                  &error))
    {
        qCritical() << "timeline-row-move-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    if (rowMoveModelChecks(session.get(), actions, ids, check, error) != 0)
        return 1;
    if (rowMovePanelChecks(std::move(session), ids, check, error) != 0)
        return 1;

    qInfo() << "timeline-row-move-test OK";
    return 0;
}

} // namespace pvf::tests
