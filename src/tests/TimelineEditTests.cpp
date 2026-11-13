#include "TimelineEditTests.h"

#include "TestApp.h"

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

    // Trim start: keys shift/crop and the child is clipped by the parent.
    const int childTrackSize = entity(child).tracks.value(QStringLiteral("opacity")).keys.size();
    if (!check(childTrackSize == 2, QStringLiteral("setup child keys wrong")))
        return 1;
    if (!check(actions->trimClip(child, 15, 40, &error),
               QStringLiteral("trim start failed: %1").arg(error)))
        return 1;
    {
        const auto& track = entity(child).tracks.value(QStringLiteral("opacity"));
        if (!check(entity(child).start == 15 && entity(child).end == 40
                       && track.keys.size() == 1 && track.keys.first().frame == 24,
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
        const auto& track = entity(child).tracks.value(QStringLiteral("opacity"));
        if (!check(entity(child).end == 30 && track.keys.size() == 1
                       && track.keys.first().frame == 0,
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
                   && entity(solverRoot).solvers.first().endFrame == 39,
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

    // Vertical drag between occupied rows; the row change is part of the same
    // single commit and swaps the two row slots.
    const int videoRow = rowForName(QStringLiteral("视频"));
    const int parentRow = rowForName(QStringLiteral("父级"));
    if (!check(videoRow >= 0 && parentRow >= 0,
               QStringLiteral("row reorder rows missing")))
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
    // Row slots are fixed: dropping on the video clip swaps the two slots, so
    // the dragged clip lands exactly on the row under the pointer.
    if (!check(changedSignals == 1,
               QStringLiteral("row drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("row drag did not create one undo step"))
        || !check(timeline->rowForEntityForTest(
                      timeline->entityIdForUuid(parent))
                      == videoRow,
                  QStringLiteral("row drag did not land on the dropped row (row %1, expected %2; model %3; rows %4/%5/%6)")
                      .arg(timeline->rowForEntityForTest(
                          timeline->entityIdForUuid(parent)))
                      .arg(videoRow)
                      .arg(entity(parent).row)
                      .arg(timeline->rowNameForTest(0))
                      .arg(timeline->rowNameForTest(1))
                      .arg(timeline->rowNameForTest(2)))
        || !check(timeline->rowForEntityForTest(
                      timeline->entityIdForUuid(video))
                      == parentRow,
                  QStringLiteral("row drag did not move the occupant to the vacated row")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("row drag undo failed: %1").arg(error)))
        return 1;
    if (!check(timeline->rowForEntityForTest(
                   timeline->entityIdForUuid(parent))
                   == parentRow,
               QStringLiteral("row drag undo did not restore rows")))
        return 1;

    // Right-edge drag trims the parent and clips the child in one commit.
    const int trimRow = rowForName(QStringLiteral("父级"));
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint trimFrom(timeline->frameXForTest(60),
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
    pvf::model::Track childTrack;
    childTrack.keys = {{0, 0.0, {}, {}}, {29, 1.0, {}, {}}};
    project.entities[child].tracks.insert(QStringLiteral("opacity"), childTrack);

    const pvf::Uuid audio = makeAsset(QStringLiteral("音频"), 20, 50, 1,
                                      pvf::AssetType::Audio, audioHash, 0);
    const pvf::Uuid video = makeAsset(QStringLiteral("视频"), 0, 30, 2,
                                      pvf::AssetType::Video, videoHash, 5);
    const pvf::Uuid lockedRoot = makeEntity(QStringLiteral("锁定父级"), 0, 60, 3);
    project.entities[lockedRoot].locked = true;
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

    const QVector<pvf::Uuid> all = {
        parent,      child,       audio,      video,      lockedRoot,
        lockedChild, target,      worldSource, localSource, solverRoot,
        trashRoot,   trashChild};
    if (!document->commitEntities(all, QStringLiteral("p2b1 setup"), &error))
    {
        qCritical() << "timeline-edit-test: setup commit failed" << error;
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

    const TimelineEditIds ids{parent,      child,       audio,
                              video,       lockedRoot,  lockedChild,
                              target,      worldSource, localSource,
                              solverRoot,  trashRoot,   trashChild};
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

} // namespace pvf::tests
