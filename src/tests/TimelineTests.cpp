#include "LegacyTestSupport.h"
#include "TestRegistry.h"
#include "TimelineEditTests.h"

namespace {

int timelineTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir = QDir(temp.path()).filePath(
        QStringLiteral("p2a-timeline.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2a 时间轴");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid camera = makeEntity(QStringLiteral("摄像机"), 0, 60, 0);
    project.entities[camera].content.kind = pvf::ContentKind::Camera;

    const pvf::Uuid title = makeEntity(QStringLiteral("标题"), 0, 90, 1);
    project.entities[title].content.kind = pvf::ContentKind::Text;
    project.entities[title].content.text.text = QStringLiteral("PVForge");

    const pvf::Uuid vector = makeEntity(QStringLiteral("矢量"), 10, 50, 2);
    project.entities[vector].content.kind = pvf::ContentKind::Vector;

    const pvf::Uuid image = makeEntity(QStringLiteral("图片"), 20, 40, 3);
    project.entities[image].content.kind = pvf::ContentKind::Asset;
    project.entities[image].content.asset.type = pvf::AssetType::Image;
    project.entities[image].content.asset.hash = QStringLiteral("imagehash");

    const pvf::Uuid video = makeEntity(QStringLiteral("视频"), 0, 60, 4);
    project.entities[video].content.kind = pvf::ContentKind::Asset;
    project.entities[video].content.asset.type = pvf::AssetType::Video;
    project.entities[video].content.asset.hash = QStringLiteral("videohash");

    const pvf::Uuid audio = makeEntity(QStringLiteral("音频"), 5, 65, 5);
    project.entities[audio].content.kind = pvf::ContentKind::Asset;
    project.entities[audio].content.asset.type = pvf::AssetType::Audio;
    project.entities[audio].content.asset.hash = QStringLiteral("audiohash");

    const pvf::Uuid html = makeEntity(QStringLiteral("HTML"), 15, 75, 6);
    project.entities[html].content.kind = pvf::ContentKind::Asset;
    project.entities[html].content.asset.type = pvf::AssetType::Html;
    project.entities[html].content.asset.hash = QStringLiteral("htmlhash");

    // A child of "标题" exercises the private timeline and depth reporting.
    pvf::model::Entity child;
    child.uuid = pvf::model::newUuid();
    child.name = QStringLiteral("标题子层");
    child.parentUuid = title;
    child.start = 10;
    child.end = 40;
    child.row = 0;
    child.content.kind = pvf::ContentKind::Text;
    project.entities.insert(child.uuid, child);

    // Keyframes are stored relative to entity.start and must be shown at
    // absolute frames by the read-only adapter.
    pvf::model::Track track;
    track.keys = {{0, 0.0, {}, {}}, {30, 1.0, {}, {}}};
    project.entities[title].tracks.insert(QStringLiteral("opacity"), track);

    pvf::model::Marker markerA;
    markerA.id = QStringLiteral("m1");
    markerA.frame = 20;
    markerA.title = QStringLiteral("开场");
    markerA.color = QStringLiteral("#ffbe3c");
    pvf::model::Marker markerB;
    markerB.id = QStringLiteral("m2");
    markerB.frame = 55;
    markerB.title = QStringLiteral("结尾");
    markerB.color = QStringLiteral("#ff5a5f");
    project.markers = {markerA, markerB};

    QVector<pvf::Uuid> uuids = {camera, title, vector, image, video, audio,
                                html, child.uuid};
    if (!session->document()->commitEntities(uuids,
                                             QStringLiteral("timeline-test"), &error)
        || !session->commitProject(QStringLiteral("timeline-test markers"),
                                   &error))
    {
        qCritical() << "timeline-test: commit failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "timeline-test: no timeline panel";
        return 1;
    }
    auto* sessionPtr = window->editorSessionForTest();
    if (!sessionPtr || !sessionPtr->document())
    {
        qCritical() << "timeline-test: window has no session";
        return 1;
    }
    if (timeline->readOnly())
    {
        qCritical() << "timeline-test: session timeline should be editable";
        return 1;
    }
    if (timeline->entityRowCountForTest() != 7)
    {
        qCritical() << "timeline-test: expected 7 rows, got"
                    << timeline->entityRowCountForTest()
                    << "rowCount" << timeline->rowCountForTest();
        return 1;
    }

    const QStringList expectedNames = {
        QStringLiteral("摄像机"), QStringLiteral("标题"), QStringLiteral("矢量"),
        QStringLiteral("图片"),   QStringLiteral("视频"), QStringLiteral("音频"),
        QStringLiteral("HTML")};
    for (int row = 0; row < expectedNames.size(); ++row)
    {
        if (timeline->rowNameForTest(row) != expectedNames.at(row))
        {
            qCritical() << "timeline-test: row" << row << "expected"
                        << expectedNames.at(row) << "got"
                        << timeline->rowNameForTest(row);
            return 1;
        }
        if (timeline->rowDepthForTest(row) != 0)
        {
            qCritical() << "timeline-test: root row" << row << "has depth"
                        << timeline->rowDepthForTest(row);
            return 1;
        }
    }
    if (timeline->rowStartForTest(3) != 20 || timeline->rowEndForTest(3) != 40)
    {
        qCritical() << "timeline-test: image range"
                    << timeline->rowStartForTest(3)
                    << timeline->rowEndForTest(3);
        return 1;
    }
    if (timeline->rowStartForTest(1) != 0 || timeline->rowEndForTest(1) != 90)
    {
        qCritical() << "timeline-test: title range"
                    << timeline->rowStartForTest(1)
                    << timeline->rowEndForTest(1);
        return 1;
    }
    if (!timeline->rowIsAudioForTest(5)
        || timeline->rowAssetHashForTest(5) != QStringLiteral("audiohash"))
    {
        qCritical() << "timeline-test: audio row metadata missing"
                    << timeline->rowAssetHashForTest(5);
        return 1;
    }
    if (timeline->markerCountForTest() != 2
        || timeline->markerFrameForTest(0) != 20
        || timeline->markerFrameForTest(1) != 55
        || timeline->markerRectForTest(0).isEmpty())
    {
        qCritical() << "timeline-test: markers"
                    << timeline->markerCountForTest()
                    << timeline->markerFrameForTest(0)
                    << timeline->markerFrameForTest(1);
        return 1;
    }

    // Playhead follows ProjectDocument and is drawn in the panel.
    // The window owns the session; drive the playhead through the panel signal
    // path by simulating a ruler click, then verify via the exposed frame.
    const QPoint rulerPoint(timeline->frameXForTest(20), 10);
    QMouseEvent rulerPress(QEvent::MouseButtonPress, QPointF(rulerPoint),
                           QPointF(timeline->mapToGlobal(rulerPoint)),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rulerRelease(QEvent::MouseButtonRelease, QPointF(rulerPoint),
                             QPointF(timeline->mapToGlobal(rulerPoint)),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &rulerPress);
    QApplication::sendEvent(timeline, &rulerRelease);
    QApplication::processEvents();
    if (timeline->currentFrameForTest() != 20
        || sessionPtr->playhead() != 20)
    {
        qCritical() << "timeline-test: ruler click frame"
                    << timeline->currentFrameForTest()
                    << "playhead" << sessionPtr->playhead();
        return 1;
    }

    // Clicking a clip selects it in the Document (read-only, no edit).
    const QPoint vectorPoint(timeline->frameXForTest(30),
                             timeline->rowCenterYForTest(2));
    QMouseEvent clipPress(QEvent::MouseButtonPress, QPointF(vectorPoint),
                          QPointF(timeline->mapToGlobal(vectorPoint)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent clipRelease(QEvent::MouseButtonRelease, QPointF(vectorPoint),
                            QPointF(timeline->mapToGlobal(vectorPoint)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &clipPress);
    QApplication::sendEvent(timeline, &clipRelease);
    QApplication::processEvents();
    if (timeline->uuidForEntityId(timeline->rowEntityForTest(2)) != vector)
    {
        qCritical() << "timeline-test: row id does not map back to uuid";
        return 1;
    }
    const pvf::Uuid selectionUuid =
        timeline->uuidForEntityId(timeline->entityIdForUuid(vector));
    if (selectionUuid != vector)
    {
        qCritical() << "timeline-test: selection mapping broken";
        return 1;
    }
    if (sessionPtr->document()->selection() != vector)
    {
        qCritical() << "timeline-test: clip click did not select in Document"
                    << sessionPtr->document()->selection();
        return 1;
    }

    // Private timeline: only the children of the owner, with depth 1.
    auto* popup = timeline->openPopup(timeline->entityIdForUuid(title));
    QApplication::processEvents();
    if (!popup)
    {
        qCritical() << "timeline-test: private timeline did not open";
        return 1;
    }
    if (popup->entityRowCountForTest() != 1
        || popup->rowNameForTest(0) != QStringLiteral("标题子层")
        || popup->rowDepthForTest(0) != 1
        || popup->rowStartForTest(0) != 10 || popup->rowEndForTest(0) != 40)
    {
        qCritical() << "timeline-test: private timeline rows"
                    << popup->entityRowCountForTest()
                    << popup->rowNameForTest(0) << popup->rowDepthForTest(0)
                    << popup->rowStartForTest(0) << popup->rowEndForTest(0);
        return 1;
    }
    timeline->closeAllPopups();
    QApplication::processEvents();

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!window->grab().save(
            QStringLiteral("/tmp/opencode/pvforge-p2a-timeline-test.png")))
    {
        qCritical() << "timeline-test: screenshot failed";
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-test OK: 7 rows, ranges, depth, markers, playhead,"
               " private timeline";
    return 0;
}

int timelineBottomUpTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p3-bottomup.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P3 时间轴自下而上");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-bottomup-test: create failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !timeline)
    {
        qCritical() << "timeline-bottomup-test: session window/timeline missing";
        return 1;
    }

    int failures = 0;
    auto expect = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "timeline-bottomup-test:" << message;
            ++failures;
        }
        return condition;
    };
    int changedSignals = 0;
    QObject::connect(document, &pvf::next::Document::changed,
                     [&changedSignals] { ++changedSignals; });
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         qCritical() << "timeline-bottomup-test: edit failed"
                                     << message;
                     });

    const int rowHeight = 30;
    // Empty project: row 0 is the bottom row and nothing occupies it.
    expect(timeline->entityRowCountForTest() == 0,
           QStringLiteral("empty project should have no entity rows"));
    expect(timeline->rowTopForTest(0) + rowHeight
               == timeline->bodyBottomForTest() + 1,
           QStringLiteral("empty row 0 is not bottom-aligned"));
    expect(timeline->rowTopForTest(1) + rowHeight == timeline->rowTopForTest(0),
           QStringLiteral("row 1 is not stacked above row 0"));
    expect(timeline->dropRowForTest(timeline->rowCenterYForTest(0)) == 0,
           QStringLiteral("empty drop below did not resolve to row 0"));
    expect(timeline->dropRowForTest(timeline->rowCenterYForTest(6)) == 0,
           QStringLiteral("empty drop above did not clamp to row 0"));
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    window->grab().save(
        QStringLiteral("/tmp/opencode/pvforge-timeline-bottomup-empty.png"));

    auto makeEntity = [&live](const QString& name, int start, int end,
                              int row) {
        auto& project = live->mutableProject();
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Empty;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid bottom = makeEntity(QStringLiteral("底"), 0, 60, 0);
    const pvf::Uuid middle = makeEntity(QStringLiteral("中"), 10, 40, 1);
    const pvf::Uuid top = makeEntity(QStringLiteral("顶"), 20, 50, 2);
    // A non-empty, childless clip is a reorder target, not a parent target.
    live->mutableProject().entities[top].content.kind = pvf::ContentKind::Text;
    pvf::model::Track track;
    track.keys = {{0, 0.0, {}, {}}, {10, 100.0, {}, {}}};
    live->mutableProject().entities[middle].tracks.insert(QStringLiteral("x"),
                                                          track);
    if (!expect(document->commitEntities({bottom, middle, top},
                                         QStringLiteral("bottom-up setup"),
                                         &error),
                QStringLiteral("setup commit failed: %1").arg(error)))
        return 1;
    QString flushError;
    live->flush(&flushError);
    QApplication::processEvents();

    auto rowFor = [&](const pvf::Uuid& uuid) {
        return timeline->rowForEntityForTest(timeline->entityIdForUuid(uuid));
    };
    auto idFor = [&](const pvf::Uuid& uuid) {
        return timeline->entityIdForUuid(uuid);
    };
    auto sendDrag = [&](const QPoint& from, const QPoint& to) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          QPointF(timeline->mapToGlobal(from)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         QPointF(timeline->mapToGlobal(to)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &move);
        QApplication::processEvents();
    };
    auto sendRelease = [&](const QPoint& at) {
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at),
                            QPointF(timeline->mapToGlobal(at)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
    };
    auto undo = [&] {
        live->flush(&flushError);
        if (!document->undo(&error))
        {
            qCritical() << "timeline-bottomup-test: undo failed" << error;
            return false;
        }
        QApplication::processEvents();
        return true;
    };
    auto depthNow = [&] {
        // Human commits are debounced; flush before reading the undo stack.
        live->flush(&flushError);
        return document->store()->undoDepth();
    };

    // Three rows from the bottom up: row 0 is drawn below row 1, row 2 on top.
    expect(timeline->entityRowCountForTest() == 3,
           QStringLiteral("expected three entity rows"));
    expect(timeline->rowEntityForTest(0) == idFor(bottom)
               && timeline->rowEntityForTest(1) == idFor(middle)
               && timeline->rowEntityForTest(2) == idFor(top),
           QStringLiteral("rows did not map model row 0/1/2 to the bottom"));
    expect(timeline->rowCenterYForTest(0) > timeline->rowCenterYForTest(1)
               && timeline->rowCenterYForTest(1)
                      > timeline->rowCenterYForTest(2),
           QStringLiteral("rows are not painted bottom-up"));
    expect(timeline->clipRectFor(idFor(middle)).top()
                   + timeline->clipRectFor(idFor(middle)).height() / 2
               == timeline->rowCenterYForTest(1),
           QStringLiteral("clip rect does not follow the bottom-up row"));
    expect(timeline->dropRowForTest(timeline->rowCenterYForTest(3)) == 3,
           QStringLiteral("drop above the top clip did not resolve to highest+1"));
    expect(timeline->dropRowForTest(timeline->bodyBottomForTest() + 20) == 0,
           QStringLiteral("drop below the body did not resolve to row 0"));

    // 1. Middle clip -> topmost blank row.
    QPoint dragFrom(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(1));
    QPoint dragTo(timeline->frameXForTest(30),
                  timeline->rowCenterYForTest(3));
    int depthBefore = depthNow();
    changedSignals = 0;
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1,
           QStringLiteral("top blank drop emitted %1 changed signals")
               .arg(changedSignals));
    expect(depthNow() == depthBefore + 1,
           QStringLiteral("top blank drop did not create one undo step"));
    expect(rowFor(middle) == 3,
           QStringLiteral("middle clip did not land on the top blank row (%1)")
               .arg(rowFor(middle)));
    expect(rowFor(bottom) == 0 && rowFor(top) == 2,
           QStringLiteral("top blank drop moved an unrelated slot"));
    expect(undo() && rowFor(middle) == 1,
           QStringLiteral("undo did not restore the middle row"));

    // 2. Bottom blank row: vacate row 0 by dragging the bottom clip up, then
    // drop the middle clip onto the now-blank bottom row.
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(0));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    depthBefore = depthNow();
    changedSignals = 0;
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("partial drag / reorder steps do not match"));
    expect(rowFor(bottom) == 3 && rowFor(middle) == 1,
           QStringLiteral("bottom clip did not move to the blank top row"));
    depthBefore = depthNow();
    changedSignals = 0;
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(0));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("bottom blank drop did not commit once"));
    expect(rowFor(middle) == 0,
           QStringLiteral("middle clip did not land on the blank bottom row (%1)")
               .arg(rowFor(middle)));
    expect(rowFor(top) == 2 && rowFor(bottom) == 3,
           QStringLiteral("bottom blank drop compacted unrelated rows"));
    expect(undo() && rowFor(middle) == 1 && undo() && rowFor(bottom) == 0,
           QStringLiteral("undo did not restore the bottom blank drags"));

    // 3. Drag preview follows the pointer Y and Esc cancels without a commit.
    changedSignals = 0;
    const int depthAtDragStart = document->store()->undoDepth();
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    const QPoint previewBottom(timeline->frameXForTest(30),
                               timeline->rowCenterYForTest(0));
    sendDrag(dragFrom, previewBottom);
    expect(rowFor(middle) == 0,
           QStringLiteral("drag preview did not follow the pointer to row 0 (%1)")
               .arg(rowFor(middle)));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    QMouseEvent previewMove(QEvent::MouseMove, QPointF(dragTo),
                            QPointF(timeline->mapToGlobal(dragTo)), Qt::NoButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &previewMove);
    QApplication::processEvents();
    expect(rowFor(middle) == 3,
           QStringLiteral("drag preview did not follow the pointer to row 3 (%1)")
               .arg(rowFor(middle)));
    expect(changedSignals == 0
               && document->store()->undoDepth() == depthAtDragStart,
           QStringLiteral("preview wrote a commit before release"));
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    window->grab().save(
        QStringLiteral("/tmp/opencode/pvforge-timeline-bottomup-drag.png"));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(timeline, &escape);
    QApplication::processEvents();
    expect(rowFor(middle) == 1,
           QStringLiteral("Esc did not restore the preview row (%1)")
               .arg(rowFor(middle)));
    expect(changedSignals == 0 && depthNow() == depthAtDragStart,
           QStringLiteral("Esc created a commit"));

    // 4. Dropping on an occupied clip lands on that row and swaps slots.
    depthBefore = depthNow();
    changedSignals = 0;
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(2));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("occupied-slot drop did not commit once"));
    expect(rowFor(middle) == 2 && rowFor(top) == 1 && rowFor(bottom) == 0,
           QStringLiteral("occupied-slot drop did not swap rows (%1/%2)")
               .arg(rowFor(middle))
               .arg(rowFor(top)));
    expect(undo() && rowFor(middle) == 1 && rowFor(top) == 2,
           QStringLiteral("undo did not restore swapped rows"));

    // 4b. A blank gap row is a valid target and stays a gap for the others.
    depthBefore = depthNow();
    changedSignals = 0;
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(2));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(rowFor(top) == 3 && rowFor(bottom) == 0 && rowFor(middle) == 1,
           QStringLiteral("top drop above did not keep the other slots"));
    expect(depthNow() == depthBefore + 1,
           QStringLiteral("top gap drop did not commit once"));
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(2));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(rowFor(middle) == 2 && rowFor(top) == 3 && rowFor(bottom) == 0,
           QStringLiteral("gap drop into a middle blank row was compacted"));
    expect(depthNow() == depthBefore + 2,
           QStringLiteral("two gap drops did not create two undo steps"));
    const bool gapUndo1 = undo();
    const int gapMiddle1 = rowFor(middle);
    const int gapTop1 = rowFor(top);
    const bool gapUndo2 = undo();
    expect(gapUndo1 && gapUndo2 && rowFor(middle) == 1 && rowFor(top) == 2,
           QStringLiteral("undo did not restore the gap drops (%1/%2 -> %3/%4, %5/%6)")
               .arg(gapUndo1)
               .arg(gapUndo2)
               .arg(gapMiddle1)
               .arg(gapTop1)
               .arg(rowFor(middle))
               .arg(rowFor(top)));

    // 5. Wheel zoom/scroll keeps the bottom anchor and moves rows as a block.
    const int topBefore = timeline->rowTopForTest(0);
    QWheelEvent wheel(QPointF(timeline->frameXForTest(20),
                              timeline->rowCenterYForTest(1)),
                      QPointF(timeline->mapToGlobal(
                          QPoint(timeline->frameXForTest(20),
                                 timeline->rowCenterYForTest(1)))),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(timeline, &wheel);
    QApplication::processEvents();
    expect(timeline->verticalScrollForTest() > 0,
           QStringLiteral("wheel up did not scroll towards higher rows"));
    expect(timeline->rowTopForTest(0) > topBefore,
           QStringLiteral("scrolled rows did not move as a block"));
    QWheelEvent wheelBack(QPointF(timeline->frameXForTest(20),
                                  timeline->rowCenterYForTest(1)),
                          QPointF(timeline->mapToGlobal(
                              QPoint(timeline->frameXForTest(20),
                                     timeline->rowCenterYForTest(1)))),
                          QPoint(), QPoint(0, -120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(timeline, &wheelBack);
    QApplication::processEvents();
    expect(timeline->verticalScrollForTest() == 0,
           QStringLiteral("wheel down did not return to the bottom anchor"));

    // 6. Keyframe hit needs the diamond band, not only the X position.
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Keyframes);
    QApplication::processEvents();
    const QPoint keyPoint(timeline->frameXForTest(20),
                          timeline->rowCenterYForTest(1));
    QMouseEvent keyPress(QEvent::MouseButtonPress, QPointF(keyPoint),
                         QPointF(timeline->mapToGlobal(keyPoint)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &keyPress);
    QApplication::processEvents();
    expect(timeline->selectedKeyframeCount() == 1,
           QStringLiteral("keyframe diamond was not hit at its Y"));
    QMouseEvent keyRelease(QEvent::MouseButtonRelease, QPointF(keyPoint),
                           QPointF(timeline->mapToGlobal(keyPoint)),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &keyRelease);
    QApplication::processEvents();
    const QRect middleClip = timeline->clipRectFor(idFor(middle));
    expect(timeline->keyframeHitForTest(1, timeline->frameXForTest(20),
                                        timeline->rowCenterYForTest(1))
               == 20,
           QStringLiteral("keyframe lookup missed the diamond centre"));
    const QPoint offBand(keyPoint.x(), middleClip.top() + 1);
    expect(timeline->keyframeHitForTest(1, offBand.x(), offBand.y()) == -1,
           QStringLiteral("same X outside the diamond band still hit a keyframe"));
    QMouseEvent offPress(QEvent::MouseButtonPress, QPointF(offBand),
                         QPointF(timeline->mapToGlobal(offBand)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &offPress);
    QApplication::processEvents();
    QMouseEvent offRelease(QEvent::MouseButtonRelease, QPointF(offBand),
                           QPointF(timeline->mapToGlobal(offBand)),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &offRelease);
    QApplication::processEvents();
    expect(timeline->selectedKeyframeCount() == 0,
           QStringLiteral("clip body click selected a keyframe"));
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Clips);
    QApplication::processEvents();

    // 7. Cross-parent drop: a folder row asks for reparent and lands on the
    // new parent's next free row.
    const pvf::Uuid folder = makeEntity(QStringLiteral("文件夹"), 0, 60, 3);
    if (!expect(document->commitEntities({folder},
                                         QStringLiteral("folder setup"), &error),
                QStringLiteral("folder commit failed: %1").arg(error)))
        return 1;
    live->flush(&flushError);
    QApplication::processEvents();
    expect(rowFor(folder) == 3, QStringLiteral("folder row missing"));
    depthBefore = depthNow();
    changedSignals = 0;
    QTimer::singleShot(50, [] {
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
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("cross-parent drop did not commit once"));
    expect(live->project().entities.value(middle).parentUuid == folder,
           QStringLiteral("cross-parent drop did not reparent"));
    expect(rowFor(middle) == 0 && rowFor(folder) == 3,
           QStringLiteral("cross-parent drop rows wrong (%1/%2)")
               .arg(rowFor(middle))
               .arg(rowFor(folder)));
    {
        auto* popup = timeline->openPopup(idFor(folder));
        QApplication::processEvents();
        expect(popup && popup->entityRowCountForTest() == 1,
               QStringLiteral("private timeline did not open the new child"));
        if (popup)
        {
            expect(popup->rowCenterYForTest(0) > popup->rowCenterYForTest(1),
                   QStringLiteral("private timeline is not bottom-up"));
            expect(popup->rowEntityForTest(0) == idFor(middle),
                   QStringLiteral("private timeline row 0 is not the child"));
            // Vertical drag inside the private timeline also lands on a blank
            // row and commits once.
            const int popupDepth = depthNow();
            const QPoint popupFrom(popup->frameXForTest(30),
                                   popup->rowCenterYForTest(0));
            const QPoint popupTo(popup->frameXForTest(30),
                                 popup->rowCenterYForTest(2));
            QMouseEvent popupPress(QEvent::MouseButtonPress, QPointF(popupFrom),
                                   QPointF(popup->mapToGlobal(popupFrom)),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent popupMove(QEvent::MouseMove, QPointF(popupTo),
                                  QPointF(popup->mapToGlobal(popupTo)),
                                  Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent popupRelease(
                QEvent::MouseButtonRelease, QPointF(popupTo),
                QPointF(popup->mapToGlobal(popupTo)), Qt::LeftButton,
                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(popup, &popupPress);
            QApplication::sendEvent(popup, &popupMove);
            QApplication::sendEvent(popup, &popupRelease);
            QApplication::processEvents();
            expect(rowFor(middle) == 1,
                   QStringLiteral("private timeline blank-row drag did not land (%1)")
                       .arg(rowFor(middle)));
            expect(depthNow() == popupDepth + 1,
                   QStringLiteral("private timeline drag did not commit once"));
            expect(undo() && rowFor(middle) == 0,
                   QStringLiteral("private timeline drag undo did not restore row 0"));
        }
        timeline->closeAllPopups();
        QApplication::processEvents();
    }
    expect(undo() && rowFor(middle) == 1,
           QStringLiteral("cross-parent undo did not restore the root row"));

    // 8. Deleting a clip leaves the other row slots untouched.
    const int rowsBefore = timeline->entityRowCountForTest();
    if (!expect(live->editActions()->deleteEntities({top}, false, nullptr,
                                                    &error),
                QStringLiteral("delete failed: %1").arg(error)))
        return 1;
    live->flush(&flushError);
    QApplication::processEvents();
    expect(timeline->entityRowCountForTest() == rowsBefore - 1,
           QStringLiteral("delete did not remove one row"));
    expect(rowFor(bottom) == 0 && rowFor(middle) == 1,
           QStringLiteral("delete compacted the remaining rows"));
    expect(undo() && rowFor(top) == 2,
           QStringLiteral("delete undo did not restore the top row"));

    window->grab().save(
        QStringLiteral("/tmp/opencode/pvforge-timeline-bottomup.png"));
    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-bottomup-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int timelineUiTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p3-timeline-ui.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P3 时间轴回归");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-ui-test: create failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !timeline)
    {
        qCritical() << "timeline-ui-test: session window/timeline missing";
        return 1;
    }

    int failures = 0;
    auto expect = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "timeline-ui-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        QApplication::processEvents();
        return document->store()->undoDepth();
    };

    auto makeEntity = [&live](const QString& name, int start, int end, int row) {
        auto& project = live->mutableProject();
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Text;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid hero = makeEntity(QStringLiteral("角色A"), 0, 120, 0);
    const pvf::Uuid background = makeEntity(QStringLiteral("背景B"), 20, 90, 1);
    const pvf::Uuid prop = makeEntity(QStringLiteral("道具C"), 40, 80, 2);
    pvf::model::Track xTrack;
    xTrack.keys = {{0, 0.0, {}, {}}, {30, 100.0, {}, {}},
                   {60, 200.0, {}, {}}};
    live->mutableProject().entities[hero].tracks.insert(QStringLiteral("x"),
                                                        xTrack);
    if (!document->commitEntities({hero, background, prop},
                                  QStringLiteral("timeline ui setup"), &error))
    {
        qCritical() << "timeline-ui-test: setup commit failed" << error;
        return 1;
    }
    flush();

    auto rowFor = [&](const pvf::Uuid& uuid) {
        return timeline->rowForEntityForTest(timeline->entityIdForUuid(uuid));
    };
    auto idFor = [&](const pvf::Uuid& uuid) {
        return timeline->entityIdForUuid(uuid);
    };
    auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                Qt::MouseButtons buttons,
                                Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(type, QPointF(pos),
                          QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                          buttons, modifiers);
        QApplication::sendEvent(timeline, &event);
    };
    auto click = [&](const QPoint& pos) {
        sendMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, pos, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
    };

    // 1. Clip-first: no left name column and no timeline-side entity search.
    expect(timeline->nameColumnWidthForTest() == 0,
           QStringLiteral("timeline still reserves a name column"));
    expect(timeline->findChild<QLineEdit*>(
               QStringLiteral("timelineSearchEdit"))
               == nullptr,
           QStringLiteral("timeline-side entity search still exists"));
    expect(timeline->entityRowCountForTest() == 3,
           QStringLiteral("entity row count is wrong"));
    expect(timeline->rowNameForTest(rowFor(background))
               == QStringLiteral("背景B"),
           QStringLiteral("clip name is not the entity name"));
    expect(!timeline->clipNameTextForTest(idFor(background)).isEmpty(),
           QStringLiteral("clip does not draw the entity name"));

    // 2. Ruler density table and minor ticks.
    auto checkSteps = [&](double pixels, int tick, int label) {
        timeline->setPixelsPerFrameForTest(pixels);
        expect(timeline->rulerTickStepForTest() == tick
                   && timeline->rulerLabelStepForTest() == label,
               QStringLiteral("ruler steps at %1 px are %2/%3, expected %4/%5")
                   .arg(pixels)
                   .arg(timeline->rulerTickStepForTest())
                   .arg(timeline->rulerLabelStepForTest())
                   .arg(tick)
                   .arg(label));
    };
    checkSteps(8.0, 1, 5);
    checkSteps(4.0, 5, 10);
    checkSteps(2.0, 10, 50);
    checkSteps(0.8, 20, 100);
    checkSteps(0.4, 50, 200);
    timeline->setPixelsPerFrameForTest(6.0);
    QApplication::processEvents();

    // 3. Clip appearance is square and flat, not rounded/gradient.
    expect(timeline->clipCornerRadiusForTest() == 0,
           QStringLiteral("clip corner radius is not zero"));
    const QRect propClip = timeline->clipRectFor(idFor(prop));
    expect(!propClip.isEmpty(), QStringLiteral("clip rect is missing"));
    if (!propClip.isEmpty())
    {
        const QImage shot = timeline->grab().toImage();
        const QColor corner =
            shot.pixelColor(propClip.left() + 2, propClip.top() + 2);
        const QColor center = shot.pixelColor(propClip.center());
        const int distance = std::abs(corner.red() - center.red())
                             + std::abs(corner.green() - center.green())
                             + std::abs(corner.blue() - center.blue());
        expect(distance < 24,
               QStringLiteral("clip corner is not flat (corner %1, center %2)")
                   .arg(corner.name(), center.name()));
    }

    // 4. Default clip mode: clicking a diamond selects it, seeks, and drags.
    expect(timeline->editMode() == pvf::TimelinePanel::EditMode::Clips,
           QStringLiteral("timeline does not start in clip mode"));
    const int heroRow = rowFor(hero);
    const QPoint key30(timeline->frameXForTest(30),
                       timeline->rowCenterYForTest(heroRow));
    click(key30);
    expect(timeline->selectedKeyframeCount() == 1,
           QStringLiteral("clicking a diamond in clip mode did not select it"));
    expect(window->currentFrameForTest() == 30,
           QStringLiteral("clicking a diamond did not seek (%1)")
               .arg(window->currentFrameForTest()));

    const int depthBeforeKeyDrag = flush();
    const QPoint key45(timeline->frameXForTest(45),
                       timeline->rowCenterYForTest(heroRow));
    sendMouse(QEvent::MouseButtonPress, key30, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, key45, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, key45, Qt::NoButton, Qt::NoModifier);
    QApplication::processEvents();
    flush();
    const pvf::model::Entity& heroAfter = live->project().entities.value(hero);
    bool has30 = false;
    bool has45 = false;
    for (const pvf::model::Keyframe& key : heroAfter.tracks.value(QStringLiteral("x")).keys)
    {
        if (key.frame == 30)
            has30 = true;
        if (key.frame == 45)
            has45 = true;
    }
    expect(has45 && !has30,
           QStringLiteral("dragging the diamond did not move the key"));
    expect(window->currentFrameForTest() == 45,
           QStringLiteral("playhead did not follow the dragged key (%1)")
               .arg(window->currentFrameForTest()));
    expect(flush() == depthBeforeKeyDrag + 1,
           QStringLiteral("key drag did not create exactly one commit"));

    // 5. Eye / lock buttons commit through EditActions exactly once.
    auto entityFlag = [&](const pvf::Uuid& uuid, bool hidden) {
        const auto it = live->project().entities.constFind(uuid);
        if (it == live->project().entities.constEnd())
            return false;
        return hidden ? it->hidden : it->locked;
    };
    auto clickRowButton = [&](const pvf::Uuid& uuid, int index) {
        const int row = rowFor(uuid);
        click(timeline->rowButtonRect(row, index).center());
        QApplication::processEvents();
        flush();
    };
    int depthBeforeFlag = flush();
    clickRowButton(background, 0);
    expect(entityFlag(background, true),
           QStringLiteral("eye button did not hide the entity"));
    expect(flush() == depthBeforeFlag + 1,
           QStringLiteral("eye button did not commit once"));
    depthBeforeFlag = flush();
    clickRowButton(hero, 1);
    expect(entityFlag(hero, false),
           QStringLiteral("lock button did not lock the entity"));
    expect(flush() == depthBeforeFlag + 1,
           QStringLiteral("lock button did not commit once"));
    // Unlock so the screenshot shows an editable clip again.
    clickRowButton(hero, 1);
    expect(!entityFlag(hero, false), QStringLiteral("lock button did not unlock"));

    // 6. Screenshot in frame mode so the 1/5/10 tick density is visible.
    timeline->setPixelsPerFrameForTest(6.0);
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Clips);
    timeline->setDisplayFrames(true);
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    expect(window->grab().save(
               QStringLiteral("/tmp/opencode/pvforge-timeline-ui.png")),
           QStringLiteral("screenshot failed"));
    timeline->setDisplayFrames(false);

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-ui-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int timelineAssetsTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("timeline-assets.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("时间轴素材栏");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-assets-test: create failed" << error;
        return 1;
    }

    const QString imagePath =
        QDir(temp.path()).filePath(QStringLiteral("短素材.png"));
    QImage image(96, 64, QImage::Format_ARGB32);
    image.fill(QColor(60, 150, 220));
    {
        QPainter painter(&image);
        painter.setPen(Qt::white);
        painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("ASSET"));
    }
    if (!image.save(imagePath))
    {
        qCritical() << "timeline-assets-test: cannot write test image";
        return 1;
    }
    const auto asset = session->assets().importFile(imagePath, &error);
    if (!asset)
    {
        qCritical() << "timeline-assets-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    auto* timeline = window->timelineForTest();
    auto* split =
        window->findChild<QSplitter*>(QStringLiteral("timelineAssetSplit"));
    if (!live || !panel || !timeline || !split)
    {
        qCritical() << "timeline-assets-test: session window widgets missing";
        return 1;
    }

    int failures = 0;
    auto expect = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "timeline-assets-test:" << message;
            ++failures;
        }
        return condition;
    };

    // 1. Clip-first geometry: no left name column and no timeline search.
    expect(timeline->nameColumnWidthForTest() == 0,
           QStringLiteral("timeline still has a name column"));
    expect(timeline->findChild<QLineEdit*>(
               QStringLiteral("timelineSearchEdit"))
               == nullptr,
           QStringLiteral("timeline-side entity search still exists"));

    // 2. Asset bin sits left of the timeline, is compact, and shows data.
    auto* panelWidget =
        window->findChild<QWidget*>(QStringLiteral("timelineAssetPanel"));
    auto* assetList =
        window->findChild<QListWidget*>(QStringLiteral("assetList"));
    auto* importButton =
        window->findChild<QToolButton*>(QStringLiteral("assetImport"));
    auto* tagFilter =
        window->findChild<QComboBox*>(QStringLiteral("assetTagFilter"));
    expect(panelWidget && split->isAncestorOf(panelWidget),
           QStringLiteral("timeline asset bar is missing"));
    expect(assetList && importButton && tagFilter,
           QStringLiteral("asset bar list/import/tag widgets missing"));
    if (panelWidget && assetList)
    {
        const QPoint panelPos = panelWidget->mapTo(window, QPoint(0, 0));
        const QPoint timelinePos = timeline->mapTo(window, QPoint(0, 0));
        expect(panelWidget->width() >= 140 && panelWidget->width() <= 260,
               QStringLiteral("asset bar width is not 160-200 px (%1)")
                   .arg(panelWidget->width()));
        expect(panelPos.x() < timelinePos.x(),
               QStringLiteral("asset bar is not left of the timeline"));
        expect(assetList->count() >= 1,
               QStringLiteral("asset bar list is empty"));
        expect(importButton->isVisible(),
               QStringLiteral("asset import button is hidden"));
        expect(tagFilter->isVisible(),
               QStringLiteral("asset tag filter is hidden"));
    }
    expect(panel->assetCount() >= 1, QStringLiteral("asset entry missing"));
    QString tagError;
    expect(panel->renameAsset(asset->hash, QStringLiteral("短素材"), &tagError),
           QStringLiteral("renameAsset failed: %1").arg(tagError));
    panel->refresh();
    expect(panel->assetNameAt(0) == QStringLiteral("短素材"),
           QStringLiteral("asset name is wrong: %1").arg(panel->assetNameAt(0)));
    expect(!panel->assetThumbnailAt(0).isNull(),
           QStringLiteral("asset thumbnail is null"));
    expect(!panel->assetMissingAt(0),
           QStringLiteral("freshly imported asset is marked missing"));

    // Search belongs to the asset bin, not the timeline.
    panel->setSearchText(QStringLiteral("短素材"));
    expect(panel->assetCount() == 1,
           QStringLiteral("asset search did not filter: %1").arg(panel->assetCount()));
    panel->setSearchText(QString());
    expect(panel->assetCount() >= 1,
           QStringLiteral("clearing asset search did not restore"));

    // Tags stay available in the compact bar.
    expect(panel->setAssetTags(asset->hash, {QStringLiteral("intro")}, &tagError),
           QStringLiteral("setAssetTags failed: %1").arg(tagError));
    expect(panel->tagFilters().contains(QStringLiteral("intro")),
           QStringLiteral("tag filter missing after tagging"));

    // 3. Drag an asset onto the timeline through the real MIME payload.
    const qsizetype beforeDrop = live->project().entities.size();
    QMimeData* mime = panel->assetMimeData(asset->hash);
    const pvf::EntityId dropped =
        timeline->dropAssetForTest(*mime, 20, 0);
    delete mime;
    QApplication::processEvents();
    expect(dropped != pvf::kInvalidEntity
               && live->project().entities.size() == beforeDrop + 1,
           QStringLiteral("asset drag did not create an entity"));
    const int row = timeline->rowForEntityForTest(dropped);
    expect(row >= 0, QStringLiteral("dropped clip has no row"));
    expect(timeline->rowNameForTest(row) == QStringLiteral("短素材"),
           QStringLiteral("clip name is not the entity name: %1")
               .arg(timeline->rowNameForTest(row)));
    expect(timeline->clipNameTextForTest(dropped) == QStringLiteral("短素材"),
           QStringLiteral("wide clip does not draw the full entity name: %1")
               .arg(timeline->clipNameTextForTest(dropped)));

    // 4. Bottom-up rows and free vertical drop rows still work.
    const int bottom = timeline->bodyBottomForTest();
    expect(timeline->rowTopForTest(0) >= bottom - 31
               && timeline->rowTopForTest(0) <= bottom,
           QStringLiteral("row 0 is not at the bottom"));
    expect(timeline->dropRowForTest(bottom - 5) == 0,
           QStringLiteral("drop row at the bottom is not row 0"));

    // 5. Eye / lock stay reachable inside the clip and commit through edits.
    auto click = [timeline](const QPoint& pos) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos),
                          QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos),
                            QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
    };
    const pvf::Uuid droppedUuid = timeline->uuidForEntityId(dropped);
    click(timeline->rowButtonRect(row, 0).center());
    expect(live->project().entities.value(droppedUuid).hidden,
           QStringLiteral("eye button did not hide the clip"));
    click(timeline->rowButtonRect(row, 0).center());
    expect(!live->project().entities.value(droppedUuid).hidden,
           QStringLiteral("eye button did not show the clip again"));
    click(timeline->rowButtonRect(row, 1).center());
    expect(live->project().entities.value(droppedUuid).locked,
           QStringLiteral("lock button did not lock the clip"));
    click(timeline->rowButtonRect(row, 1).center());
    expect(!live->project().entities.value(droppedUuid).locked,
           QStringLiteral("lock button did not unlock the clip"));

    // 6. Short clip: name elides and the tooltip carries the full identity.
    auto& entity = live->mutableProject().entities[droppedUuid];
    entity.end = entity.start + 5;
    expect(live->document()->commitEntities({droppedUuid},
                                            QStringLiteral("short clip"), &error),
           QStringLiteral("short clip commit failed: %1").arg(error));
    QApplication::processEvents();
    expect(timeline->clipNameElidedForTest(dropped),
           QStringLiteral("short clip name is not elided: %1")
               .arg(timeline->clipNameTextForTest(dropped)));
    const QPoint shortPoint = timeline->clipRectFor(dropped).center();
    QMouseEvent move(QEvent::MouseMove, QPointF(shortPoint),
                     QPointF(timeline->mapToGlobal(shortPoint)), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &move);
    QApplication::processEvents();
    const QString tip = timeline->toolTip();
    expect(tip.contains(QStringLiteral("短素材")),
           QStringLiteral("short clip tooltip lost the entity name: %1").arg(tip));
    expect(tip.contains(QStringLiteral("时长")),
           QStringLiteral("short clip tooltip lost the duration: %1").arg(tip));

    // 7. Missing asset state remains visible in the same compact bar.
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    expect(window->grab().save(
               QStringLiteral("/tmp/opencode/pvforge-timeline-assets.png")),
           QStringLiteral("screenshot failed"));

    expect(panel->deleteAsset(asset->hash, &tagError),
           QStringLiteral("deleteAsset failed: %1").arg(tagError));
    panel->refresh();
    int missingRow = -1;
    for (int i = 0; i < panel->assetCount(); ++i)
        if (panel->assetHashAt(i) == asset->hash)
            missingRow = i;
    expect(missingRow >= 0 && panel->assetMissingAt(missingRow),
           QStringLiteral("missing asset is not marked in the bar"));
    expect(!panel->assetMissingReasonAt(missingRow).isEmpty(),
           QStringLiteral("missing asset has no reason"));

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-assets-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int timelineKeysTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b2-keys.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b2 关键帧");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-keys-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto* actions = session->editActions();
    if (!actions)
    {
        qCritical() << "timeline-keys-test: no EditActions";
        return 1;
    }
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-keys-test:" << message;
        return condition;
    };

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Text;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid clip = makeEntity(QStringLiteral("关键帧片段"), 0, 60, 0);
    const pvf::Uuid other = makeEntity(QStringLiteral("其他片段"), 100, 160, 1);
    const pvf::Uuid solver = makeEntity(QStringLiteral("求解片段"), 0, 60, 2);
    pvf::model::SolverSegment segment;
    segment.id = pvf::model::newUuid();
    segment.effect = pvf::SolverEffect::Gravity;
    segment.startFrame = 10;
    segment.endFrame = 40;
    segment.params.insert(QStringLiteral("targetUuid"), other);
    project.entities[solver].solvers.append(segment);
    pvf::model::Track solverX;
    solverX.keys = {{10, 0.0, {}, {}}};
    project.entities[solver].tracks.insert(QStringLiteral("x"), solverX);

    if (!document->commitEntities({clip, other, solver},
                                  QStringLiteral("p2b2 keys setup"), &error))
    {
        qCritical() << "timeline-keys-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    pvf::EditorSession* sessionPtr = session.get();
    auto flush = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-keys-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto undo = [document, &flush] {
        flush();
        QString undoError;
        if (!document->undo(&undoError))
        {
            qCritical() << "timeline-keys-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto keyAt = [sessionPtr](const pvf::Uuid& uuid, const QString& track,
                              int frame) {
        const auto entity = sessionPtr->project().entities.constFind(uuid);
        if (entity == sessionPtr->project().entities.constEnd())
            return false;
        const auto keys = entity.value().tracks.constFind(track);
        if (keys == entity.value().tracks.constEnd())
            return false;
        for (const auto& key : keys.value().keys)
            if (key.frame == frame)
                return true;
        return false;
    };
    auto keyCount = [sessionPtr](const pvf::Uuid& uuid, const QString& track) {
        const auto entity = sessionPtr->project().entities.constFind(uuid);
        if (entity == sessionPtr->project().entities.constEnd())
            return 0;
        const auto keys = entity.value().tracks.constFind(track);
        return keys == entity.value().tracks.constEnd()
                   ? 0
                   : int(keys.value().keys.size());
    };

    // EditActions: insert, one commit, undo.
    int beforeDepth = depth();
    if (!check(actions->addKeyframe(clip, QStringLiteral("x"), 15, &error),
               QStringLiteral("addKeyframe failed: %1").arg(error)))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 15),
               QStringLiteral("addKeyframe did not write the key")))
        return 1;
    flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("addKeyframe did not commit once")))
        return 1;
    if (!undo())
        return 1;

    // Panel: double-click inserts on the selected track.
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline,
               QStringLiteral("session window/timeline missing")))
        return 1;
    auto rowForName = [&](const QString& name) {
        for (int row = 0; row < timeline->entityRowCountForTest(); ++row)
            if (timeline->rowNameForTest(row) == name)
                return row;
        return -1;
    };
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         qCritical() << "timeline-keys-test: panel edit failed"
                                     << message;
                     });

    const int clipRow = rowForName(QStringLiteral("关键帧片段"));
    if (!check(clipRow >= 0, QStringLiteral("clip row missing")))
        return 1;
    timeline->setSelectedTrack(QStringLiteral("x"));
    beforeDepth = depth();
    const QPoint dblPoint(timeline->frameXForTest(20),
                          timeline->rowCenterYForTest(clipRow));
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(dblPoint),
                            QPointF(timeline->mapToGlobal(dblPoint)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &doubleClick);
    QApplication::processEvents();
    if (!check(keyAt(clip, QStringLiteral("x"), 20),
               QStringLiteral("double-click did not insert a key")))
        return 1;
    live->flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("double-click did not commit once")))
        return 1;
    if (!undo())
        return 1;

    // Two tracks at one frame paint one merged diamond; dragging moves both.
    if (!check(actions->addKeyframe(clip, QStringLiteral("x"), 10, &error)
                   && actions->addKeyframe(clip, QStringLiteral("y"), 10, &error),
               QStringLiteral("merged key setup failed: %1").arg(error)))
        return 1;
    live->flush();
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Keyframes);
    timeline->setSelectedTrack(QStringLiteral("x"));
    beforeDepth = depth();
    const QPoint mergeFrom(timeline->frameXForTest(10),
                           timeline->rowCenterYForTest(clipRow));
    const QPoint mergeTo(timeline->frameXForTest(15),
                         timeline->rowCenterYForTest(clipRow));
    QMouseEvent mergePress(QEvent::MouseButtonPress, QPointF(mergeFrom),
                           QPointF(timeline->mapToGlobal(mergeFrom)),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent mergeMove(QEvent::MouseMove, QPointF(mergeTo),
                          QPointF(timeline->mapToGlobal(mergeTo)), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
    QMouseEvent mergeRelease(QEvent::MouseButtonRelease, QPointF(mergeTo),
                             QPointF(timeline->mapToGlobal(mergeTo)),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &mergePress);
    QApplication::sendEvent(timeline, &mergeMove);
    QApplication::sendEvent(timeline, &mergeRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("merged drag did not commit once")))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 15)
                   && keyAt(clip, QStringLiteral("y"), 15)
                   && !keyAt(clip, QStringLiteral("x"), 10)
                   && !keyAt(clip, QStringLiteral("y"), 10),
               QStringLiteral("merged drag did not move both tracks")))
        return 1;
    if (!undo())
        return 1;

    // Esc cancels a key drag without committing.
    beforeDepth = depth();
    const QPoint cancelFrom(timeline->frameXForTest(10),
                            timeline->rowCenterYForTest(clipRow));
    const QPoint cancelTo(timeline->frameXForTest(20),
                          timeline->rowCenterYForTest(clipRow));
    QMouseEvent cancelPress(QEvent::MouseButtonPress, QPointF(cancelFrom),
                            QPointF(timeline->mapToGlobal(cancelFrom)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent cancelMove(QEvent::MouseMove, QPointF(cancelTo),
                           QPointF(timeline->mapToGlobal(cancelTo)), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &cancelPress);
    QApplication::sendEvent(timeline, &cancelMove);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(timeline, &escape);
    QApplication::processEvents();
    live->flush();
    if (!check(depth() == beforeDepth,
               QStringLiteral("Esc created an undo step")))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 10)
                   && keyAt(clip, QStringLiteral("y"), 10)
                   && !keyAt(clip, QStringLiteral("x"), 20),
               QStringLiteral("Esc did not restore the key preview")))
        return 1;

    // Alt-drag moves only the selected track.
    timeline->setSelectedTrack(QStringLiteral("x"));
    beforeDepth = depth();
    const QPoint singleFrom(timeline->frameXForTest(10),
                            timeline->rowCenterYForTest(clipRow));
    const QPoint singleTo(timeline->frameXForTest(12),
                          timeline->rowCenterYForTest(clipRow));
    QMouseEvent singlePress(QEvent::MouseButtonPress, QPointF(singleFrom),
                            QPointF(timeline->mapToGlobal(singleFrom)),
                            Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    QMouseEvent singleMove(QEvent::MouseMove, QPointF(singleTo),
                           QPointF(timeline->mapToGlobal(singleTo)), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QMouseEvent singleRelease(QEvent::MouseButtonRelease, QPointF(singleTo),
                              QPointF(timeline->mapToGlobal(singleTo)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &singlePress);
    QApplication::sendEvent(timeline, &singleMove);
    QApplication::sendEvent(timeline, &singleRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("single-track drag did not commit once")))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 12)
                   && keyAt(clip, QStringLiteral("y"), 10)
                   && !keyAt(clip, QStringLiteral("x"), 10),
               QStringLiteral("single-track drag moved the wrong tracks")))
        return 1;
    if (!undo())
        return 1;

    // Click selects the merged diamond, Delete removes every track at the frame.
    const QPoint selectPoint(timeline->frameXForTest(10),
                             timeline->rowCenterYForTest(clipRow));
    QMouseEvent selectPress(QEvent::MouseButtonPress, QPointF(selectPoint),
                            QPointF(timeline->mapToGlobal(selectPoint)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent selectRelease(QEvent::MouseButtonRelease, QPointF(selectPoint),
                              QPointF(timeline->mapToGlobal(selectPoint)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &selectPress);
    QApplication::sendEvent(timeline, &selectRelease);
    QApplication::processEvents();
    if (!check(timeline->selectedKeyframeCount() == 1,
               QStringLiteral("merged diamond was not selected")))
        return 1;
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(timeline, &del);
    QApplication::processEvents();
    live->flush();
    if (!check(keyCount(clip, QStringLiteral("x")) == 0
                   && keyCount(clip, QStringLiteral("y")) == 0,
               QStringLiteral("Delete did not remove the merged keys")))
        return 1;
    if (!undo())
        return 1;

    // Ctrl+C / Ctrl+V pastes at the playhead.
    QApplication::sendEvent(timeline, &selectPress);
    QApplication::sendEvent(timeline, &selectRelease);
    QApplication::processEvents();
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(timeline, &copy);
    live->setPlayhead(40);
    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(timeline, &paste);
    QApplication::processEvents();
    live->flush();
    if (!check(keyAt(clip, QStringLiteral("x"), 40)
                   && keyAt(clip, QStringLiteral("y"), 40),
               QStringLiteral("paste did not land on the playhead")))
        return 1;
    if (!undo())
        return 1;

    // Easing write path; the timeline opens EasingChooser for a key.
    pvf::model::EasingSpec easing;
    easing.type = QStringLiteral("easeinoutcubic");
    error.clear();
    if (!check(actions->setKeyframeEasing(
                   QVector<pvf::KeyRef>{{clip, QStringLiteral("x"), 10}}, easing,
                   &error),
               QStringLiteral("setKeyframeEasing failed: %1").arg(error)))
        return 1;
    {
        bool eased = false;
        const auto track = sessionPtr->project()
                               .entities.value(clip)
                               .tracks.value(QStringLiteral("x"));
        for (const auto& key : track.keys)
            if (key.frame == 10
                && key.easing.type == QStringLiteral("easeinoutcubic"))
                eased = true;
        if (!check(eased, QStringLiteral("key easing was not written")))
            return 1;
    }
    if (!undo())
        return 1;

    // Solver protection: the segment start key cannot be deleted or moved,
    // and normal keys cannot be written inside the segment.
    error.clear();
    if (!check(!actions->deleteKeyframes(
                   QVector<pvf::KeyRef>{{solver, QStringLiteral("x"), 10}},
                   &error),
               QStringLiteral("solver start key delete should fail")))
        return 1;
    error.clear();
    if (!check(!actions->moveKeyframes(
                   QVector<pvf::KeyRef>{{solver, QStringLiteral("x"), 10}}, 1,
                   &error),
               QStringLiteral("solver start key move should fail")))
        return 1;
    error.clear();
    if (!check(!actions->addKeyframe(solver, QStringLiteral("x"), 20, &error),
               QStringLiteral("key inside a solver segment should fail")))
        return 1;
    if (!check(keyAt(solver, QStringLiteral("x"), 10)
                   && keyCount(solver, QStringLiteral("x")) == 1,
               QStringLiteral("failed solver edits changed the keys")))
        return 1;

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!check(window->grab().save(
                   QStringLiteral("/tmp/opencode/pvforge-p2b2-keys.png")),
               QStringLiteral("screenshot failed")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-keys-test OK: double-click add, merged/single-track drag,"
               " Delete, copy/paste, solver start-key protection";
    return 0;
}

int timelineSplitTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b2-split.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b2 切分");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-split-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto* actions = session->editActions();
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-split-test:" << message;
        return condition;
    };

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
    const pvf::Uuid parent = makeEntity(QStringLiteral("父级"), 0, 60, 0);
    const pvf::Uuid child = makeEntity(QStringLiteral("子级"), 10, 50, 0, parent);
    pvf::model::Track parentX;
    parentX.keys = {{0, 0.0, {}, {}}, {10, 1.0, {}, {}},
                    {30, 2.0, {}, {}}, {50, 3.0, {}, {}}};
    project.entities[parent].tracks.insert(QStringLiteral("x"), parentX);

    pvf::model::Entity video;
    video.uuid = pvf::model::newUuid();
    video.name = QStringLiteral("视频");
    video.start = 0;
    video.end = 40;
    video.row = 1;
    video.content.kind = pvf::ContentKind::Asset;
    video.content.asset.type = pvf::AssetType::Video;
    video.content.asset.hash = QString(64, QLatin1Char('c'));
    video.content.asset.sourceIn = 5;
    project.entities.insert(video.uuid, video);

    const pvf::Uuid locked = makeEntity(QStringLiteral("锁定"), 0, 60, 2);
    project.entities[locked].locked = true;

    if (!document->commitEntities({parent, child, video.uuid, locked},
                                  QStringLiteral("p2b2 split setup"), &error))
    {
        qCritical() << "timeline-split-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    pvf::EditorSession* sessionPtr = session.get();
    auto flush = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-split-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto undo = [document, &flush] {
        flush();
        QString undoError;
        if (!document->undo(&undoError))
        {
            qCritical() << "timeline-split-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto findByName = [](const pvf::model::Project& p, const QString& name,
                         int start, const pvf::Uuid& exclude) {
        for (auto it = p.entities.constBegin(); it != p.entities.constEnd(); ++it)
            if (it.key() != exclude && it.value().name == name
                && it.value().start == start)
                return it.key();
        return pvf::Uuid();
    };
    auto keyFrames = [](const pvf::model::Project& p, const pvf::Uuid& uuid,
                        const QString& track) {
        QVector<int> frames;
        const auto entity = p.entities.constFind(uuid);
        if (entity == p.entities.constEnd())
            return frames;
        const auto keys = entity.value().tracks.constFind(track);
        if (keys == entity.value().tracks.constEnd())
            return frames;
        for (const auto& key : keys.value().keys)
            frames.append(key.frame);
        std::sort(frames.begin(), frames.end());
        return frames;
    };

    // Parent + child split: the right halves get fresh UUIDs and shifted keys.
    int beforeDepth = depth();
    pvf::EditPlan plan;
    if (!check(actions->planSplit(parent, 30, &plan, &error),
               QStringLiteral("planSplit failed: %1").arg(error)))
        return 1;
    const pvf::Uuid rightParent =
        findByName(plan.project, QStringLiteral("父级"), 30, parent);
    if (!check(!rightParent.isEmpty(),
               QStringLiteral("right parent missing from the plan")))
        return 1;
    const pvf::Uuid rightChild =
        findByName(plan.project, QStringLiteral("子级"), 30, child);
    if (!check(!rightChild.isEmpty()
                   && plan.project.entities.value(rightChild).parentUuid
                          == rightParent,
               QStringLiteral("child was not split under the right parent")))
        return 1;
    if (!check(plan.project.entities.value(parent).end == 30
                   && plan.project.entities.value(rightParent).end == 60
                   && plan.project.entities.value(child).end == 30
                   && plan.project.entities.value(rightChild).end == 50,
               QStringLiteral("split ranges are wrong")))
        return 1;
    if (!check(keyFrames(plan.project, parent, QStringLiteral("x"))
                       == QVector<int>({0, 10})
                   && keyFrames(plan.project, rightParent, QStringLiteral("x"))
                          == QVector<int>({0, 20}),
               QStringLiteral("split keys were not cropped/shifted")))
        return 1;
    if (!check(actions->apply(plan, &error),
               QStringLiteral("split apply failed: %1").arg(error)))
        return 1;
    flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("split did not commit once")))
        return 1;
    if (!undo())
        return 1;

    // Video sourceIn shifts on the right half.
    beforeDepth = depth();
    if (!check(actions->splitClip(video.uuid, 20, &error),
               QStringLiteral("video split failed: %1").arg(error)))
        return 1;
    const pvf::Uuid rightVideo = [&] {
        for (auto it = session->project().entities.constBegin();
             it != session->project().entities.constEnd(); ++it)
            if (it.key() != video.uuid && it.value().name == QStringLiteral("视频"))
                return it.key();
        return pvf::Uuid();
    }();
    if (!check(!rightVideo.isEmpty()
                   && session->project().entities.value(video.uuid).content.asset.sourceIn
                          == 5
                   && session->project().entities.value(rightVideo).content.asset.sourceIn
                          == 25,
               QStringLiteral("video sourceIn was not shifted")))
        return 1;
    if (!undo())
        return 1;

    // Boundaries and locks reject without changing anything.
    const int entityCount = session->project().entities.size();
    if (!check(!actions->splitClip(video.uuid, 0, &error),
               QStringLiteral("split at the start should fail"))
        || !check(!actions->splitClip(video.uuid, 40, &error),
                  QStringLiteral("split at the end should fail"))
        || !check(!actions->splitClip(locked, 20, &error),
                  QStringLiteral("split of a locked entity should fail"))
        || !check(session->project().entities.size() == entityCount,
                  QStringLiteral("failed splits changed the project")))
        return 1;

    // S in the panel splits the selected clip at the playhead.
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline,
               QStringLiteral("session window/timeline missing")))
        return 1;
    const pvf::EntityId parentId = timeline->entityIdForUuid(parent);
    timeline->setSelectedEntity(parentId);
    live->setPlayhead(15);
    beforeDepth = depth();
    QKeyEvent splitKey(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
    QApplication::sendEvent(timeline, &splitKey);
    QApplication::processEvents();
    live->flush();
    const pvf::Uuid rightByS = [&] {
        for (auto it = live->project().entities.constBegin();
             it != live->project().entities.constEnd(); ++it)
            if (it.key() != parent && it.value().name == QStringLiteral("父级")
                && it.value().start == 15)
                return it.key();
        return pvf::Uuid();
    }();
    if (!check(!rightByS.isEmpty()
                   && live->project().entities.value(parent).end == 15,
               QStringLiteral("S did not split at the playhead")))
        return 1;
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("S split did not commit once")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("S split undo failed: %1").arg(error)))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-split-test OK: parent/child, video sourceIn,"
               " boundary and lock rejection, S shortcut";
    return 0;
}

int timelineMarkerTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b2-marker.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b2 标记");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-marker-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto* actions = session->editActions();
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-marker-test:" << message;
        return condition;
    };

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("片段");
    entity.start = 0;
    entity.end = 120;
    entity.content.kind = pvf::ContentKind::Empty;
    session->mutableProject().entities.insert(entity.uuid, entity);
    if (!document->commitEntities({entity.uuid}, QStringLiteral("p2b2 marker setup"),
                                  &error))
    {
        qCritical() << "timeline-marker-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    pvf::EditorSession* sessionPtr = session.get();
    auto flush = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-marker-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline,
               QStringLiteral("session window/timeline missing")))
        return 1;

    // M adds a marker at the playhead.
    live->setPlayhead(30);
    int beforeDepth = depth();
    QKeyEvent markerKey(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier);
    QApplication::sendEvent(timeline, &markerKey);
    QApplication::processEvents();
    live->flush();
    if (!check(live->project().markers.size() == 1
                   && live->project().markers.first().frame == 30,
               QStringLiteral("M did not add a marker at the playhead")))
        return 1;
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("marker add did not commit once")))
        return 1;

    // Dragging the marker changes its frame in one commit.
    const QRect markerRect = timeline->markerRectForTest(0);
    if (!check(!markerRect.isEmpty(), QStringLiteral("marker rect missing")))
        return 1;
    const QPoint markerFrom = markerRect.center();
    const QPoint markerTo(timeline->frameXForTest(45), markerRect.center().y());
    QMouseEvent markerPress(QEvent::MouseButtonPress, QPointF(markerFrom),
                            QPointF(timeline->mapToGlobal(markerFrom)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent markerMove(QEvent::MouseMove, QPointF(markerTo),
                           QPointF(timeline->mapToGlobal(markerTo)), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QMouseEvent markerRelease(QEvent::MouseButtonRelease, QPointF(markerTo),
                              QPointF(timeline->mapToGlobal(markerTo)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &markerPress);
    QApplication::sendEvent(timeline, &markerMove);
    QApplication::sendEvent(timeline, &markerRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(live->project().markers.first().frame == 45,
               QStringLiteral("marker drag did not change the frame")))
        return 1;
    if (!check(depth() == beforeDepth + 2,
               QStringLiteral("marker drag did not commit once")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("marker drag undo failed: %1").arg(error)))
        return 1;
    if (!check(live->project().markers.first().frame == 30,
               QStringLiteral("marker drag undo did not restore the frame")))
        return 1;

    // Edit title/color/note/tags; empty titles are rejected.
    pvf::model::Marker marker = live->project().markers.first();
    marker.title = QStringLiteral("高潮");
    marker.color = QStringLiteral("#ff3355");
    marker.note = QStringLiteral("副歌开始");
    marker.tags = {QStringLiteral("音乐"), QStringLiteral("段落")};
    if (!check(actions->updateMarker(marker, &error),
               QStringLiteral("updateMarker failed: %1").arg(error)))
        return 1;
    const auto updated = live->project().markers.first();
    if (!check(updated.title == QStringLiteral("高潮")
                   && updated.color == QStringLiteral("#ff3355")
                   && updated.note == QStringLiteral("副歌开始")
                   && updated.tags.size() == 2,
               QStringLiteral("marker fields were not saved")))
        return 1;
    live->flush();
    pvf::model::Marker invalid = updated;
    invalid.title.clear();
    error.clear();
    if (!check(!actions->updateMarker(invalid, &error),
               QStringLiteral("empty marker title should fail")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("marker edit undo failed: %1").arg(error)))
        return 1;

    // Delete and undo.
    const QString markerId = live->project().markers.first().id;
    if (!check(actions->deleteMarker(markerId, &error),
               QStringLiteral("deleteMarker failed: %1").arg(error)))
        return 1;
    if (!check(live->project().markers.isEmpty(),
               QStringLiteral("marker was not deleted")))
        return 1;
    live->flush();
    if (!check(live->document()->undo(&error),
               QStringLiteral("marker delete undo failed: %1").arg(error)))
        return 1;
    if (!check(live->project().markers.size() == 1,
               QStringLiteral("marker delete undo did not restore it")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-marker-test OK: M add, drag frame, edit fields,"
               " validation, delete and undo";
    return 0;
}

int timelineEditE2eTest(QApplication& app, const QString& projectDir)
{
    QString error;
    auto session = pvf::EditorSession::openProject(projectDir, &error);
    if (!session)
    {
        qCritical() << "timeline-edit-e2e: open failed" << error;
        return 1;
    }
    auto* document = session->document();
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

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!window->grab().save(
            QStringLiteral("/tmp/opencode/pvforge-p2b1-edit.png")))
    {
        qCritical() << "timeline-edit-e2e: screenshot failed";
        return 1;
    }
    qInfo() << "timeline-edit-e2e OK: audio"
            << audioStartBefore << "->" << audioStart << " image"
            << imageStartBefore << "->" << imageStart << projectDir;
    window->close();
    QApplication::processEvents();
    Q_UNUSED(document);
    Q_UNUSED(app);
    return 0;
}

int timelineRowButtonsTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::EntityId firstId = pvf::kInvalidEntity;
    {
        pvf::Entity* first = project->addVector(0, 120, 80, Qt::red, Qt::white, 0);
        firstId = first->id;
        project->setValue(firstId, QStringLiteral("x"), 0, 0, true);
        project->setValue(firstId, QStringLiteral("x"), 60, 300, true);
    }
    {
        pvf::Entity* second = project->addVector(1, 120, 80, Qt::green, Qt::white, 0);
        project->setValue(second->id, QStringLiteral("x"), 30, 100, true);
    }
    qInfo() << "entities" << project->entities().size();

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
        return 1;

    QTimer::singleShot(400, &window, [&app, &window, timeline, project, firstId] {
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(timeline->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };
        auto click = [&](const QPoint& pos) {
            send(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
        };

        int failures = 0;

        // Hover the first row so the inline buttons show.
        send(QEvent::MouseMove, QPoint(400, timeline->rowCenterYForTest(0)),
             Qt::NoButton, Qt::NoButton);
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_row_buttons.png"));

        // Rubber-band from the empty row above down across both entity rows,
        // picking up every keyframe in range.
        const QPoint boxStart(timeline->frameXForTest(5),
                              timeline->rowCenterYForTest(3));
        const QPoint boxEnd(timeline->frameXForTest(65),
                            timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, boxStart, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, boxEnd, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, boxEnd, Qt::LeftButton, Qt::NoButton);
        const int boxed = timeline->selectedKeyframeCount();
        qInfo() << "boxed" << boxed << "entities" << project->entities().size();
        if (boxed != 2)
        {
            qCritical() << "timeline-rowbuttons-test: boxed keyframes" << boxed;
            ++failures;
        }

        // Inline buttons: select all keyframes, hide, lock.
        click(timeline->rowButtonRect(0, 2).center());
        const int all = timeline->selectedKeyframeCount();
        if (all != 2)
        {
            qCritical() << "timeline-rowbuttons-test: select all keyframes" << all;
            ++failures;
        }

        click(timeline->rowButtonRect(0, 0).center());
        if (!project->entity(firstId)->hidden)
        {
            qCritical() << "timeline-rowbuttons-test: hide failed";
            ++failures;
        }

        click(timeline->rowButtonRect(0, 1).center());
        if (!project->entity(firstId)->locked)
        {
            qCritical() << "timeline-rowbuttons-test: lock failed";
            ++failures;
        }

        qInfo() << "timeline-rowbuttons-test: boxed" << boxed << "all" << all << "failures"
                << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int timelineInteractionTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::Entity* first = project->addVector(0, 120, 80, Qt::red, Qt::white, 0);
    project->setValue(first->id, QStringLiteral("x"), 0, 0, true);
    project->setValue(first->id, QStringLiteral("x"), 60, 300, true);
    project->addVector(1, 120, 80, Qt::green, Qt::white, 0);
    project->addVector(2, 120, 80, Qt::blue, Qt::white, 0);

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "timeline-interaction-test: timeline not found";
        return 1;
    }

    QTimer::singleShot(400, &window, [&app, &window, timeline] {
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(timeline->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        int failures = 0;

        // Ruler press moves the playhead.
        const QPoint rulerPoint(timeline->frameXForTest(53), 10);
        send(QEvent::MouseButtonPress, rulerPoint, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, rulerPoint, Qt::LeftButton, Qt::NoButton);
        const int rulerFrame = window.currentFrame();
        if (std::abs(rulerFrame - 53) > 2)
        {
            qCritical() << "timeline-interaction-test: ruler frame" << rulerFrame;
            ++failures;
        }

        // Empty row click jumps on release without scrolling the keyframes
        // out of view.  Row 3 is the padded blank row above the clips.
        const QPoint emptyRow(timeline->frameXForTest(20),
                              timeline->rowCenterYForTest(3));
        send(QEvent::MouseButtonPress, emptyRow, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, emptyRow, Qt::LeftButton, Qt::NoButton);
        const int emptyFrame = window.currentFrame();
        if (std::abs(emptyFrame - 20) > 2)
        {
            qCritical() << "timeline-interaction-test: empty frame" << emptyFrame;
            ++failures;
        }

        // Keyframe click selects it and moves the playhead to the keyframe,
        // so the property panel shows the values stored in that keyframe.
        const int beforeKey = window.currentFrame();
        const QPoint keyPoint(timeline->frameXForTest(60),
                              timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, keyPoint, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, keyPoint, Qt::LeftButton, Qt::NoButton);
        const int afterKey = window.currentFrame();
        if (afterKey != 60)
        {
            qCritical() << "timeline-interaction-test: keyframe did not focus"
                        << beforeKey << afterKey;
            ++failures;
        }
        if (timeline->selectedKeyframeCount() != 1)
        {
            qCritical() << "timeline-interaction-test: keyframe not selected";
            ++failures;
        }

        // Clicking empty space clears the keyframe selection.
        send(QEvent::MouseButtonPress, emptyRow, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, emptyRow, Qt::LeftButton, Qt::NoButton);
        if (timeline->selectedKeyframeCount() != 0)
        {
            qCritical() << "timeline-interaction-test: selection not cleared";
            ++failures;
        }

        // Dragging rubber-bands instead of scrubbing.
        const QPoint dragStart(timeline->frameXForTest(300),
                               timeline->rowCenterYForTest(3));
        const QPoint dragEnd(timeline->frameXForTest(200),
                             timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, dragStart, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, dragEnd, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, dragEnd, Qt::LeftButton, Qt::NoButton);
        if (window.currentFrame() != emptyFrame)
        {
            qCritical() << "timeline-interaction-test: drag moved playhead";
            ++failures;
        }

        qInfo() << "timeline-interaction-test: ruler" << rulerFrame << "empty" << emptyFrame
                << "key" << beforeKey << "->" << afterKey << "failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int timelineFrameTest(QApplication& app)
{
    pvf::Settings::setTimelineDisplayFrames(false);

    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    window.activateWindow();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        if (!timeline)
        {
            qCritical() << "timeline-frame-test: timeline missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        project->addKeyframe(entity->id, QStringLiteral("x"), 100.0, 300.0);

        // Mine-imator's frame-tick density table (frame label mode).
        auto checkSteps = [&](double pixels, int tick, int label) {
            timeline->setPixelsPerFrameForTest(pixels);
            if (timeline->rulerTickStepForTest() != tick
                || timeline->rulerLabelStepForTest() != label)
            {
                qCritical() << "timeline-frame-test: wrong frame steps at" << pixels
                            << timeline->rulerTickStepForTest()
                            << timeline->rulerLabelStepForTest();
                ++failures;
            }
        };
        checkSteps(8.0, 1, 5);
        checkSteps(6.0, 1, 5);
        checkSteps(4.0, 5, 10);
        checkSteps(2.0, 10, 50);
        checkSteps(0.8, 20, 100);
        checkSteps(0.4, 50, 200);

        // Seconds mode must subdivide one second evenly for the project fps.
        auto checkSeconds = [&](int fps, double pixels, int cell, int label) {
            project->fps = fps;
            timeline->setPixelsPerFrameForTest(pixels);
            const int actualCell = timeline->secondsCellStepForTest();
            const int actualLabel = timeline->secondsLabelStepForTest();
            if (actualCell != cell || actualLabel != label)
            {
                qCritical() << "timeline-frame-test: wrong second grid at" << fps
                            << "fps" << pixels << "px: cells" << actualCell
                            << "label" << actualLabel;
                ++failures;
            }
            if (actualLabel % actualCell != 0
                || (fps % actualCell != 0 && actualCell % fps != 0))
            {
                qCritical() << "timeline-frame-test: grid does not divide seconds"
                            << fps << actualCell << actualLabel;
                ++failures;
            }
        };
        // 24 fps: 24, 12, 8, 6, 4, 2 cells per second as the zoom shrinks.
        checkSeconds(24, 6.0, 1, 24);
        checkSeconds(24, 4.0, 2, 24);
        checkSeconds(24, 2.0, 3, 24);
        checkSeconds(24, 1.0, 6, 48);
        checkSeconds(24, 0.5, 12, 96);
        checkSeconds(24, 0.25, 24, 192);
        checkSeconds(24, 32.0, 1, 6);   // 4 cells per quarter second
        // Other rates use their own divisors, not a hard-coded 5 frames.
        checkSeconds(30, 6.0, 1, 30);
        checkSeconds(30, 1.0, 5, 60);
        checkSeconds(60, 6.0, 1, 30);
        checkSeconds(25, 6.0, 1, 25);
        project->fps = 24;   // keep the screenshots at the regression case

        // Clicking the ruler focuses the timeline so arrow keys work.
        const QPoint rulerPoint(230, 10);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(rulerPoint),
                          QPointF(timeline->mapToGlobal(rulerPoint)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(rulerPoint),
                            QPointF(timeline->mapToGlobal(rulerPoint)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &release);
        if (!timeline->hasFocus())
        {
            qCritical() << "timeline-frame-test: ruler click did not focus the timeline";
            ++failures;
        }

        auto sendKey = [&](QEvent::Type type, int key, bool autoRepeat) {
            QKeyEvent event(type, key, Qt::NoModifier, QString(), autoRepeat);
            QApplication::sendEvent(timeline, &event);
        };
        auto wait = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };

        // A normal tap (held for a moment) moves exactly one frame. The
        // continuous repeat only starts after the hold delay.
        window.goToFrame(10);
        sendKey(QEvent::KeyPress, Qt::Key_Right, false);
        wait(120);
        sendKey(QEvent::KeyRelease, Qt::Key_Right, false);
        wait(100);
        if (window.currentFrame() != 11)
        {
            qCritical() << "timeline-frame-test: tap did not step exactly one frame"
                        << window.currentFrame();
            ++failures;
        }

        // Holding past the delay keeps stepping at the project frame rate.
        const int holdStart = window.currentFrame();
        sendKey(QEvent::KeyPress, Qt::Key_Right, false);
        wait(650);
        const int heldTo = window.currentFrame();
        sendKey(QEvent::KeyRelease, Qt::Key_Right, false);
        wait(120);
        if (heldTo <= holdStart)
        {
            qCritical() << "timeline-frame-test: hold did not advance" << heldTo;
            ++failures;
        }
        if (window.currentFrame() != heldTo)
        {
            qCritical() << "timeline-frame-test: release did not stop"
                        << heldTo << window.currentFrame();
            ++failures;
        }

        // Left arrow: one tap is one frame, holding runs backwards.
        window.goToFrame(40);
        sendKey(QEvent::KeyPress, Qt::Key_Left, false);
        wait(120);
        sendKey(QEvent::KeyRelease, Qt::Key_Left, false);
        wait(80);
        if (window.currentFrame() != 39)
        {
            qCritical() << "timeline-frame-test: left tap did not step one frame"
                        << window.currentFrame();
            ++failures;
        }

        sendKey(QEvent::KeyPress, Qt::Key_Left, false);
        wait(650);
        const int reverseTo = window.currentFrame();
        sendKey(QEvent::KeyRelease, Qt::Key_Left, false);
        if (reverseTo >= 39)
        {
            qCritical() << "timeline-frame-test: reverse hold did not move" << reverseTo;
            ++failures;
        }

        // Reverse stops at the start, forward loops at the end.
        window.goToFrame(0);
        sendKey(QEvent::KeyPress, Qt::Key_Left, false);
        wait(80);
        sendKey(QEvent::KeyRelease, Qt::Key_Left, false);
        if (window.currentFrame() != 0)
        {
            qCritical() << "timeline-frame-test: reverse passed zero"
                        << window.currentFrame();
            ++failures;
        }
        window.goToFrame(project->timelineLength() - 1);
        sendKey(QEvent::KeyPress, Qt::Key_Right, false);
        sendKey(QEvent::KeyRelease, Qt::Key_Right, false);
        if (window.currentFrame() != 0)
        {
            qCritical() << "timeline-frame-test: forward hold did not loop"
                        << window.currentFrame();
            ++failures;
        }

        // Both ruler label modes render, and the status bar follows the unit
        // instead of always mixing frames and seconds.
        auto* status = window.findChild<QLabel*>(QStringLiteral("statusInfo"));
        auto* framesAction = window.findChild<QAction*>(QStringLiteral("timelineFramesAction"));
        auto* secondsAction =
            window.findChild<QAction*>(QStringLiteral("timelineSecondsAction"));
        if (!status || !framesAction || !secondsAction)
        {
            qCritical() << "timeline-frame-test: status label or unit actions missing";
            ++failures;
        }
        else
        {
            // The visible unit button in the timeline header toggles both the
            // ruler and the status bar, and keeps the menu in sync.
            auto* unitButton =
                window.findChild<QToolButton*>(QStringLiteral("timelineUnitButton"));
            if (!unitButton)
            {
                qCritical() << "timeline-frame-test: unit button missing";
                ++failures;
            }
            else
            {
                const bool before = timeline->displayFrames();
                unitButton->click();
                if (timeline->displayFrames() == before
                    || framesAction->isChecked() != timeline->displayFrames())
                {
                    qCritical() << "timeline-frame-test: unit button did not sync"
                                << timeline->displayFrames()
                                << framesAction->isChecked();
                    ++failures;
                }
                unitButton->click();   // back to seconds
            }

            window.goToFrame(20);
            if (!status->text().contains(QStringLiteral("s /"))
                || status->text().contains(QStringLiteral("帧")))
            {
                qCritical() << "timeline-frame-test: seconds status is wrong"
                            << status->text();
                ++failures;
            }

            framesAction->trigger();
            if (!timeline->displayFrames()
                || !status->text().contains(QStringLiteral("帧"))
                || status->text().contains(QStringLiteral("s /")))
            {
                qCritical() << "timeline-frame-test: frame mode status is wrong"
                            << status->text();
                ++failures;
            }
            timeline->setPixelsPerFrameForTest(6.0);
            QApplication::processEvents();
            timeline->grab().save(QStringLiteral("/tmp/pvforge_timeline_frames.png"));

            secondsAction->trigger();
            if (timeline->displayFrames()
                || !status->text().contains(QStringLiteral("s /")))
            {
                qCritical() << "timeline-frame-test: seconds mode not restored"
                            << status->text();
                ++failures;
            }
            QApplication::processEvents();
            timeline->grab().save(QStringLiteral("/tmp/pvforge_timeline_seconds.png"));
        }

        pvf::Settings::setTimelineDisplayFrames(false);
        qInfo() << "timeline-frame-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int timelineZoomTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "timeline-zoom-test: timeline not found";
        return 1;
    }

    for (int i = 0; i < 30; ++i)
        project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    // Give the time axis some content so it can actually scroll horizontally.
    if (const pvf::Entity* first = project->entities().isEmpty()
                                        ? nullptr
                                        : &project->entities().first())
        project->setValue(first->id, QStringLiteral("x"), 400, 100.0, true);

    QTimer::singleShot(400, &window, [&app, &window, timeline, project] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_before.png"));
        const double before = timeline->pixelsPerFrame();

        const QPointF pos(500, 120);
        for (int i = 0; i < 3; ++i)
        {
            QWheelEvent event(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(timeline, &event);
        }

        int failures = 0;
        const double after = timeline->pixelsPerFrame();
        if (!(after > before * 4.0))
        {
            qCritical() << "timeline-zoom-test: ctrl wheel did not zoom" << before
                        << after;
            ++failures;
        }
        if (!timeline->verticalScrollVisibleForTest())
        {
            qCritical() << "timeline-zoom-test: vertical scrollbar not visible";
            ++failures;
        }
        const int verticalBefore = timeline->verticalScrollForTest();
        // Bottom-up rows: wheel up grows the upward (bottom-anchored) offset.
        QWheelEvent plain(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(timeline, &plain);
        if (timeline->verticalScrollForTest() <= verticalBefore)
        {
            qCritical() << "timeline-zoom-test: wheel did not scroll rows"
                        << verticalBefore << timeline->verticalScrollForTest();
            ++failures;
        }
        const int horizontalBefore = timeline->horizontalScrollForTest();
        QWheelEvent shifted(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                            Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(timeline, &shifted);
        if (timeline->horizontalScrollForTest() == horizontalBefore)
        {
            qCritical() << "timeline-zoom-test: shift wheel did not scroll time"
                        << horizontalBefore << timeline->horizontalScrollForTest();
            ++failures;
        }

        QTimer::singleShot(200, &window, [&app, &window, before, after, failures] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_after.png"));
            qInfo() << "timeline-zoom-test:" << before << "->" << after
                    << "failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    });
    return app.exec();
}

int clipTimelineTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::Entity* group = project->addFolder(QStringLiteral("场景组"));
    const pvf::EntityId groupId = group->id;
    pvf::Entity* rect =
        project->addVector(0, 120, 80, QColor(235, 87, 87), Qt::white, 0);
    rect->name = QStringLiteral("红色矩形");
    const pvf::EntityId rectId = rect->id;
    pvf::Entity* text = project->addText(QStringLiteral("标题文字"),
                                         QStringLiteral("Sans"), 64, Qt::white,
                                         Qt::AlignCenter);
    text->name = QStringLiteral("标题文字");
    const pvf::EntityId textId = text->id;
    project->setEntityParent(rectId, groupId);
    project->setEntityParent(textId, groupId);
    project->entity(groupId)->data[QStringLiteral("clipStart")] = 0;
    project->entity(groupId)->data[QStringLiteral("clipEnd")] = 150;
    project->entity(rectId)->data[QStringLiteral("clipStart")] = 0;
    project->entity(rectId)->data[QStringLiteral("clipEnd")] = 120;
    project->entity(textId)->data[QStringLiteral("clipStart")] = 30;
    project->entity(textId)->data[QStringLiteral("clipEnd")] = 150;

    auto audio = std::make_shared<pvf::AudioClip>();
    audio->duration = 4.0;
    audio->peaks.resize(4000);
    for (int i = 0; i < audio->peaks.size(); ++i)
        audio->peaks[i] =
            0.12f + 0.88f * float(std::abs(std::sin(i * 0.006)));
    pvf::Entity* bgm =
        project->addAudio(QStringLiteral("bgm.wav"), audio, QStringLiteral("BGM"));
    const pvf::EntityId bgmId = bgm->id;
    project->setEntityParent(bgmId, groupId);

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "clip-timeline-test: no timeline";
        return 1;
    }
    QTimer::singleShot(300, &window,
                       [&app, &window, timeline, groupId, rectId, textId, bgmId] {
        timeline->setPixelsPerFrameForTest(8.0);
        timeline->setCurrentFrame(45);
        timeline->openPopup(groupId);
        QTimer::singleShot(400, &window,
                           [&app, &window, timeline, groupId, rectId, textId,
                            bgmId] {
            int failures = 0;
            pvf::TimelinePanel* popup = nullptr;
            for (pvf::TimelinePanel* panel :
                 window.findChildren<pvf::TimelinePanel*>())
            {
                if (panel->owner() == groupId)
                {
                    popup = panel;
                    break;
                }
            }
            if (!popup)
            {
                qCritical() << "clip-timeline-test: popup missing";
                app.exit(1);
                return;
            }
            QToolButton* timeMode = nullptr;
            for (QToolButton* button : window.findChildren<QToolButton*>())
            {
                if (button->text() == QStringLiteral("相对"))
                {
                    timeMode = button;
                    break;
                }
            }
            bool relativeSwitched = false;
            if (timeMode)
            {
                timeMode->click();
                relativeSwitched = timeMode->text() == QStringLiteral("绝对");
                timeMode->click();
            }
            if (!relativeSwitched)
                ++failures;
            pvf::Project* project = window.project();
            if (!project)
            {
                qCritical() << "clip-timeline-test: project missing";
                app.exit(1);
                return;
            }
            project->addKeyframe(rectId, QStringLiteral("x"), 110, 0.0);
            const QRect before = popup->clipRectFor(rectId);
            const int initialEnd = project->clipEndFrame(rectId);
            const int row = 0;
            auto send = [popup](QEvent::Type type, const QPoint& pos,
                                Qt::MouseButtons buttons) {
                QMouseEvent event(type, QPointF(pos),
                                  QPointF(popup->mapToGlobal(pos)),
                                  Qt::LeftButton, buttons, Qt::NoModifier);
                QApplication::sendEvent(popup, &event);
            };
            const QPoint from(before.right() - 3,
                              popup->rowCenterYForTest(row));
            const QPoint to(from.x() - 30 * 8, from.y());
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
            QApplication::processEvents();
            const int clippedEnd = project->clipEndFrame(rectId);
            const bool keyGone = project->keyframe(rectId, QStringLiteral("x"),
                                                   110.0)
                                 == nullptr;
            const QRect clipped = popup->clipRectFor(rectId);
            const QPoint expandFrom(clipped.right() - 3,
                                    popup->rowCenterYForTest(row));
            const QPoint expandTo(expandFrom.x() + 30 * 8, expandFrom.y());
            send(QEvent::MouseButtonPress, expandFrom, Qt::LeftButton);
            send(QEvent::MouseMove, expandTo, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, expandTo, Qt::NoButton);
            QApplication::processEvents();
            const int expandedEnd = project->clipEndFrame(rectId);
            const bool keyStayedGone =
                project->keyframe(rectId, QStringLiteral("x"), 110.0) == nullptr;
            int undoEnd1 = -1;
            int undoEnd2 = -1;
            bool keyRestored = false;
            for (QAction* action : window.findChildren<QAction*>())
            {
                if (!action->text().contains(QStringLiteral("撤销")))
                    continue;
                action->trigger();
                undoEnd1 = project->clipEndFrame(rectId);
                action->trigger();
                undoEnd2 = project->clipEndFrame(rectId);
                keyRestored =
                    project->keyframe(rectId, QStringLiteral("x"), 110.0) != nullptr;
                break;
            }
            if (clippedEnd >= 110 || !keyGone || expandedEnd <= clippedEnd
                || !keyStayedGone || undoEnd1 != clippedEnd
                || undoEnd2 != initialEnd || !keyRestored)
            {
                qCritical() << "clip-timeline-test: trim" << before << clippedEnd
                            << expandedEnd << keyGone << keyStayedGone << "undo"
                            << undoEnd1 << undoEnd2 << keyRestored;
                ++failures;
            }

            // Clicking a keyframe must feed the property panel's existing
            // easing button (the previous milestone's keyframe page).
            auto* modeButton =
                timeline->findChild<QToolButton*>(QStringLiteral("timelineModeButton"));
            bool modeSwitched = false;
            if (modeButton)
            {
                modeButton->click();
                modeSwitched =
                    modeButton->text() == QStringLiteral("关键帧")
                    && popup->editMode()
                           == pvf::TimelinePanel::EditMode::Keyframes;
            }
            if (!modeSwitched)
            {
                qCritical() << "clip-timeline-test: mode switch failed";
                ++failures;
            }
            project->addKeyframe(rectId, QStringLiteral("x"), 0, 0.0);
            project->addKeyframe(rectId, QStringLiteral("x"), 60, 0.0);
            QApplication::processEvents();
            const QPoint keyPoint(popup->frameXForTest(60),
                                  popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonPress, keyPoint, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, keyPoint, Qt::NoButton);
            QApplication::processEvents();
            auto* easeButton =
                window.findChild<QPushButton*>(QStringLiteral("keyframeEasingButton"));
            const bool easingReady =
                easeButton && easeButton->isEnabled()
                && easeButton->text().contains(QStringLiteral("缓动"));
            if (window.currentFrame() != 60 || !easingReady)
            {
                qCritical() << "clip-timeline-test: easing" << window.currentFrame()
                            << (easeButton ? easeButton->text() : QStringLiteral("none"))
                            << (easeButton && easeButton->isEnabled());
                ++failures;
            }

            // Drag a selected keyframe to a new frame and verify the track
            // data, not just the painted diamond.
            const QPoint keyFrom(popup->frameXForTest(60),
                                 popup->rowCenterYForTest(0));
            const QPoint keyTo(popup->frameXForTest(75),
                               popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonPress, keyFrom, Qt::LeftButton);
            send(QEvent::MouseMove, keyTo, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, keyTo, Qt::NoButton);
            QApplication::processEvents();
            const bool keyMoved = project->keyframe(rectId, QStringLiteral("x"),
                                                    75.0)
                                  != nullptr
                              && project->keyframe(rectId, QStringLiteral("x"),
                                                   60.0)
                                     == nullptr;
            if (!keyMoved)
            {
                qCritical() << "clip-timeline-test: keyframe drag failed";
                ++failures;
            }

            // Double-click inside a clip must create keys at that frame.
            const QPoint dblPoint(popup->frameXForTest(90),
                                  popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonDblClick, dblPoint, Qt::LeftButton);
            QApplication::processEvents();
            const bool keyAdded =
                project->keyframe(rectId, QStringLiteral("x"), 90.0) != nullptr;
            if (!keyAdded)
            {
                qCritical() << "clip-timeline-test: double-click key add failed";
                ++failures;
            }

            // Rubber-band select across rows and frames.
            const QPoint boxStart(popup->frameXForTest(130),
                                  popup->rowCenterYForTest(0));
            const QPoint boxEnd(popup->frameXForTest(0),
                                popup->rowCenterYForTest(2));
            send(QEvent::MouseButtonPress, boxStart, Qt::LeftButton);
            send(QEvent::MouseMove, boxEnd, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, boxEnd, Qt::NoButton);
            QApplication::processEvents();
            const int boxed = popup->selectedKeyframeCount();
            if (boxed < 3)
            {
                qCritical() << "clip-timeline-test: box select" << boxed;
                ++failures;
            }

            // Delete row 0 and verify rows 1/2 keep their positions.
            const QPoint redBody(popup->frameXForTest(30),
                                 popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonPress, redBody, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, redBody, Qt::NoButton);
            QKeyEvent rowDelete(QEvent::KeyPress, Qt::Key_Delete,
                                Qt::NoModifier);
            QApplication::sendEvent(popup, &rowDelete);
            QApplication::processEvents();
            if (project->entity(rectId)
                || popup->rowEntityForTest(0) != pvf::kInvalidEntity
                || popup->rowEntityForTest(1) != textId
                || popup->rowEntityForTest(2) != bgmId)
            {
                qCritical() << "clip-timeline-test: stable rows"
                            << popup->rowEntityForTest(0)
                            << popup->rowEntityForTest(1)
                            << popup->rowEntityForTest(2) << textId << bgmId;
                ++failures;
            }

            // Dragging the playhead past the right edge must keep scrolling
            // and keep the line inside the visible timeline.
            auto sendMain = [timeline](QEvent::Type type, const QPoint& pos,
                                       Qt::MouseButtons buttons) {
                QMouseEvent event(type, QPointF(pos),
                                  QPointF(timeline->mapToGlobal(pos)),
                                  Qt::LeftButton, buttons, Qt::NoModifier);
                QApplication::sendEvent(timeline, &event);
            };
            const int beforeScroll = timeline->horizontalScrollForTest();
            const int beforeFrame = window.currentFrame();
            const QPoint scrubStart(20, 10);
            const QPoint scrubEnd(timeline->width() + 80, 10);
            sendMain(QEvent::MouseButtonPress, scrubStart, Qt::LeftButton);
            sendMain(QEvent::MouseMove, scrubEnd, Qt::LeftButton);
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 400)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            sendMain(QEvent::MouseButtonRelease, scrubEnd, Qt::NoButton);
            QApplication::processEvents();
            const int playheadX = timeline->frameXForTest(window.currentFrame());
            if (window.currentFrame() <= beforeFrame || playheadX < 0
                || playheadX >= timeline->width())
            {
                qCritical() << "clip-timeline-test: scrub" << beforeScroll
                            << timeline->horizontalScrollForTest() << beforeFrame
                            << window.currentFrame() << playheadX
                            << timeline->width();
                ++failures;
            }

            // The private timeline must hide the playhead when the global
            // frame is outside the parent clip range.
            timeline->setCurrentFrame(200);
            QApplication::processEvents();
            const bool popupHidden = !popup->playheadVisibleForTest();
            timeline->setCurrentFrame(60);
            QApplication::processEvents();
            const bool popupShown = popup->playheadVisibleForTest();
            if (!popupHidden || !popupShown)
            {
                qCritical() << "clip-timeline-test: popup playhead"
                            << popupHidden << popupShown;
                ++failures;
            }

            // With the parent clip start scrolled off the left edge, the
            // popup must scroll to the same global frame as the main axis.
            timeline->setHorizontalScrollForTest(320);
            QApplication::processEvents();
            const int popupAxisX =
                popup->mapToGlobal(QPoint(popup->frameXForTest(75), 0)).x();
            const int mainAxisX =
                timeline->mapToGlobal(QPoint(timeline->frameXForTest(75), 0)).x();
            if (std::abs(popupAxisX - mainAxisX) > 2)
            {
                qCritical() << "clip-timeline-test: popup axis"
                            << popupAxisX << mainAxisX;
                ++failures;
            }
            auto* assetList =
                window.findChild<QListWidget*>(QStringLiteral("assetList"));
            auto* viewToggle =
                window.findChild<QToolButton*>(QStringLiteral("assetViewToggle"));
            if (!assetList || !viewToggle)
            {
                qCritical() << "clip-timeline-test: asset widgets missing";
                ++failures;
            }
            else
            {
                viewToggle->click();
                const bool iconMode =
                    assetList->viewMode() == QListView::IconMode;
                viewToggle->click();
                const bool listMode =
                    assetList->viewMode() == QListView::ListMode;
                QApplication::processEvents();
                assetList->setCurrentRow(0);
                const QRect itemRect = assetList->visualItemRect(assetList->item(0));
                const QPoint hoverPos = itemRect.center();
                QMouseEvent hover(
                    QEvent::MouseMove, QPointF(hoverPos),
                    QPointF(assetList->viewport()->mapToGlobal(hoverPos)),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(assetList->viewport(), &hover);
                QApplication::processEvents();
                auto* hoverPreview = window.findChild<QWidget*>(
                    QStringLiteral("assetHoverPreview"));
                const bool hoverVisible = hoverPreview && hoverPreview->isVisible();
                if (!iconMode || !listMode || !hoverVisible)
                {
                    qCritical() << "clip-timeline-test: asset view"
                                << iconMode << listMode << hoverVisible;
                    ++failures;
                }
            }

            auto* unitButton =
                timeline->findChild<QToolButton*>(QStringLiteral("timelineUnitButton"));
            if (unitButton)
            {
                unitButton->click();
                const bool framesOn = timeline->displayFrames();
                unitButton->click();
                const bool framesOff = !timeline->displayFrames();
                if (!framesOn || !framesOff)
                {
                    qCritical() << "clip-timeline-test: unit toggle" << framesOn
                                << framesOff;
                    ++failures;
                }
            }

            // Drag a library template onto the timeline and delete the new
            // clip with Del (right-click uses the same delete signal).
            const int beforeCount = int(project->entities().size());
            QMimeData dropMime;
            dropMime.setData(QStringLiteral("application/x-pvforge-asset-template"),
                             QByteArray::number(0));
            const pvf::EntityId droppedId =
                timeline->dropAssetForTest(dropMime, 30, 3);
            QApplication::processEvents();
            const bool dropped =
                int(project->entities().size()) == beforeCount + 1
                && droppedId != pvf::kInvalidEntity
                && project->clipStartFrame(droppedId) == 30
                && timeline->rowForEntityForTest(droppedId) == 3
                && timeline->rowEntityForTest(0) == groupId;
            if (!dropped)
            {
                qCritical() << "clip-timeline-test: asset drop"
                            << beforeCount << project->entities().size()
                            << timeline->rowForEntityForTest(droppedId)
                            << timeline->rowEntityForTest(0) << groupId;
                ++failures;
            }
            else
            {
                const QPoint clipPoint(
                    timeline->frameXForTest(45),
                    timeline->rowCenterYForTest(3));
                sendMain(QEvent::MouseButtonPress, clipPoint, Qt::LeftButton);
                sendMain(QEvent::MouseButtonRelease, clipPoint, Qt::NoButton);
                QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete,
                                    Qt::NoModifier);
                QApplication::sendEvent(timeline, &deleteKey);
                QApplication::processEvents();
                if (project->entity(droppedId)
                    || timeline->rowEntityForTest(0) != groupId)
                {
                    qCritical() << "clip-timeline-test: delete clip"
                                << timeline->rowEntityForTest(0) << groupId;
                    ++failures;
                }
            }

            window.grab().toImage().save(
                QStringLiteral("/tmp/pvforge_clip_timeline.png"));
            qInfo() << "clip-timeline-test: popup panels"
                    << window.findChildren<pvf::TimelinePanel*>().size()
                    << "trim" << clippedEnd << "expand" << expandedEnd
                    << "key gone" << keyGone << "failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    });
    return app.exec();
}

int openTimelineTest(QApplication& app){
    auto* project = new pvf::Project;
    project->fps = 60;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    window.goToFrame(0);
    window.togglePlayback();

    QTimer::singleShot(2500, &window, [&app, &window] {
        const int frame = window.currentFrame();
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_open_timeline.png"));
        qInfo() << "open-timeline-test: frame after 2.5s =" << frame;
        app.exit(frame > 130 ? 0 : 1);
    });
    return app.exec();
}


} // namespace

namespace pvf::tests {

void registerTimelineTests(Registry& registry)
{
    registry.add(QStringLiteral("--timeline-test"),
                 [](QApplication& app) { return timelineTest(app); });
    registry.add(QStringLiteral("--timeline-bottomup-test"),
                 [](QApplication& app) { return timelineBottomUpTest(app); });
    registry.add(QStringLiteral("--timeline-ui-test"),
                 [](QApplication& app) { return timelineUiTest(app); });
    registry.add(QStringLiteral("--timeline-assets-test"),
                 [](QApplication& app) { return timelineAssetsTest(app); });
    registry.add(QStringLiteral("--timeline-edit-test"),
                 [](QApplication& app) { return runTimelineEditTest(app); });
    registry.add(QStringLiteral("--timeline-keys-test"),
                 [](QApplication& app) { return timelineKeysTest(app); });
    registry.add(QStringLiteral("--timeline-split-test"),
                 [](QApplication& app) { return timelineSplitTest(app); });
    registry.add(QStringLiteral("--timeline-marker-test"),
                 [](QApplication& app) { return timelineMarkerTest(app); });
    registry.add(QStringLiteral("--timeline-edit-e2e"),
                 [](QApplication& app) {
                     const QStringList arguments = app.arguments();
                     const int index = arguments.indexOf(
                         QStringLiteral("--timeline-edit-e2e"));
                     QString projectDir = QStringLiteral("/tmp/pvfe2e/proj");
                     if (index >= 0 && index + 1 < arguments.size()
                         && !arguments.at(index + 1)
                                 .startsWith(QLatin1Char('-')))
                         projectDir = arguments.at(index + 1);
                     return timelineEditE2eTest(app, projectDir);
                 });
    registry.add(QStringLiteral("--timeline-rowbuttons-test"),
                 [](QApplication& app) { return timelineRowButtonsTest(app); });
    registry.add(QStringLiteral("--timeline-interaction-test"),
                 [](QApplication& app) { return timelineInteractionTest(app); });
    registry.add(QStringLiteral("--timeline-frame-test"),
                 [](QApplication& app) { return timelineFrameTest(app); });
    registry.add(QStringLiteral("--timeline-zoom-test"),
                 [](QApplication& app) { return timelineZoomTest(app); });
    registry.add(QStringLiteral("--clip-timeline-test"),
                 [](QApplication& app) { return clipTimelineTest(app); });
    registry.add(QStringLiteral("--open-timeline-test"),
                 [](QApplication& app) { return openTimelineTest(app); });
}

} // namespace pvf::tests
