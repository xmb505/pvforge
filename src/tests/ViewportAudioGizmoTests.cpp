// Stage gizmo regression tests for audio entities.
//
// Audio assets are timeline/properties-panel objects with no visual shape on
// the stage.  Selecting one must leave the timeline and the right-hand
// properties panel working, but draw no selection box/handles, answer no gizmo
// hit test, start no gizmo drag and contribute no alignment-snap target.

#include "TestApp.h"
#include "TestRegistry.h"

namespace {

bool nearly(double value, double expected, double tolerance = 0.51)
{
    return std::abs(value - expected) <= tolerance;
}

int audioGizmoTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool ok, const char* label) {
        if (!ok)
        {
            qCritical() << "audio-gizmo-test:" << label;
            ++failures;
        }
        return ok;
    };

    // Entity edges on, canvas centre off: the only possible vertical snap
    // target in the drag test is the audio clip's box.
    {
        QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
        settings.setValue(QStringLiteral("snap/pixels"), 6);
        settings.setValue(QStringLiteral("snap/canvasCenter"), false);
        settings.setValue(QStringLiteral("snap/entityEdges"), true);
        settings.setValue(QStringLiteral("snap/edgeMidpoints"), false);
        settings.setValue(QStringLiteral("snap/angles"),
                          QStringLiteral("0,90,180,270"));
        settings.sync();
    }

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("audio-gizmo.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Audio Gizmo");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "audio-gizmo-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity vector;
    vector.uuid = pvf::model::newUuid();
    vector.name = QStringLiteral("视觉矢量");
    vector.start = 0;
    vector.end = 60;
    vector.row = 0;
    vector.content.kind = pvf::ContentKind::Vector;
    vector.content.vector.width = 200.0;
    vector.content.vector.height = 100.0;
    vector.content.vector.fillColor = QStringLiteral("#ff0000");
    const pvf::Uuid vectorUuid = vector.uuid;

    pvf::model::Entity audio;
    audio.uuid = pvf::model::newUuid();
    audio.name = QStringLiteral("音频");
    audio.start = 0;
    audio.end = 60;
    audio.row = 1;
    audio.content.kind = pvf::ContentKind::Asset;
    audio.content.asset.type = pvf::AssetType::Audio;
    audio.content.asset.hash = QStringLiteral("audiohash");
    audio.content.asset.volume = 0.75;
    // Clear of the vector so a click over the audio entity cannot fall through
    // to the visual box underneath.
    audio.transform.x = 220.0;
    audio.transform.y = 120.0;
    const pvf::Uuid audioUuid = audio.uuid;

    project.entities.insert(vectorUuid, vector);
    project.entities.insert(audioUuid, audio);
    if (!session->document()->commitEntities(
            {vectorUuid, audioUuid}, QStringLiteral("pvf: audio gizmo setup"),
            &error))
    {
        qCritical() << "audio-gizmo-test: setup commit failed" << error;
        return 1;
    }

    pvf::MainWindow window(session.release());
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window.editorSessionForTest();
    auto* viewport = window.viewportForTest();
    auto* document = live ? live->document() : nullptr;
    if (!live || !viewport || !document)
    {
        qCritical() << "audio-gizmo-test: no session viewport";
        return 1;
    }
    const auto flushSession = [live] {
        QString flushError;
        live->flush(&flushError);
        return flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };
    const auto select = [&](const pvf::Uuid& uuid) {
        document->setSelection(uuid);
        live->setPlayhead(0);
        QApplication::processEvents();
        viewport->repaint();
        QApplication::processEvents();
    };

    // 1) A visual entity still owns the full gizmo: centre, move axes and
    //    scale corners all hit, and the paint path emits pixels.
    select(vectorUuid);
    if (!check(document->selection() == vectorUuid,
               "visual entity selection missing"))
        return 1;
    const QPointF vectorPivot = viewport->sessionPivotForTest();
    check(viewport->gizmoHandleAtForTest(vectorPivot).has_value(),
          "visual centre handle missing");
    check(viewport->gizmoHandleAtForTest(
              viewport->gizmoHandleForTest(vectorUuid,
                                           pvf::gizmo::Handle::CenterScale))
              .has_value(),
          "visual centre scale handle missing");
    check(viewport->gizmoHandleAtForTest(
              viewport->gizmoHandleForTest(vectorUuid,
                                           pvf::gizmo::Handle::MoveX))
              .has_value(),
          "visual X axis handle missing");
    check(viewport->gizmoHandleAtForTest(
              viewport->gizmoHandleForTest(vectorUuid,
                                           pvf::gizmo::Handle::ScaleTopLeft))
              .has_value(),
          "visual scale corner missing");
    check(viewport->sessionSelectionGizmoPixelsForTest() > 0,
          "visual entity drew no gizmo pixels");

    // 2) Audio is skipped by the alignment-snap walk.  Drag the vector so its
    //    left edge sits 2 model units from the audio box's left edge (60).
    //    With a 6 px threshold the old code snapped x to 160 (left edge 60);
    //    audio now contributes no target, so x stays at the pixel-grid 158.
    {
        flushSession();
        const int depthBefore = undoDepth();
        const double scale = viewport->currentScale();
        const QPointF moveX =
            viewport->gizmoHandleForTest(vectorUuid, pvf::gizmo::Handle::MoveX);
        QMouseEvent press(QEvent::MouseButtonPress, moveX,
                          viewport->mapToGlobal(moveX.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        const QPointF target = moveX + QPointF(158.0 * scale, 0.0);
        QMouseEvent move(QEvent::MouseMove, target,
                         viewport->mapToGlobal(target.toPoint()), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, target,
                            viewport->mapToGlobal(target.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &release);
        QApplication::processEvents();
        flushSession();
        const double x = viewport->gizmoTransformForTest(vectorUuid).x;
        if (!check(nearly(x, 158.0), "audio box was used as a stage snap target")
            || !check(undoDepth() == depthBefore + 1,
                      "snap drag did not commit exactly once"))
        {
            qCritical() << "audio-gizmo-test: snapped x" << x
                        << "threshold-model"
                        << 6.0 / std::max(1e-6, scale);
        }
        document->undo();
        QApplication::processEvents();
        document->setSelection(vectorUuid);
        QApplication::processEvents();
    }

    // 3) Selecting the audio entity keeps the timeline/properties state intact
    //    but draws no gizmo at all.
    select(audioUuid);
    if (!check(document->selection() == audioUuid,
               "audio selection uuid lost"))
        return 1;

    auto* properties = window.propertiesForTest();
    check(properties && properties->isVisible(),
          "properties panel hidden for audio");
    auto* audioVolume = properties
                            ? properties->findChild<QDoubleSpinBox*>(
                                  QStringLiteral("audioVolume"))
                            : nullptr;
    check(audioVolume && audioVolume->isVisible(),
          "audio volume control missing");
    auto* audioFadeIn = properties
                            ? properties->findChild<QDoubleSpinBox*>(
                                  QStringLiteral("audioFadeIn"))
                            : nullptr;
    check(audioFadeIn && audioFadeIn->isVisible(),
          "audio fade-in control missing");

    auto* timeline = window.timelineForTest();
    check(timeline && timeline->entitySelectionCountForTest() == 1,
          "timeline lost the audio selection");

    check(viewport->sessionSelectionGizmoPixelsForTest() == 0,
          "audio selection drew gizmo pixels");

    // The transform math is still available; only the stage representation is
    // gone.  This guards against "gizmoTransformForTest broke" masking.
    const pvf::gizmo::Transform audioTransform =
        viewport->gizmoTransformForTest(audioUuid);
    check(nearly(audioTransform.x, 220.0, 1e-6)
              && nearly(audioTransform.y, 120.0, 1e-6),
          "audio transform no longer resolves");

    // Every handle the entity gizmo would offer must stay inert.
    const pvf::gizmo::Handle handles[] = {
        pvf::gizmo::Handle::MoveX,
        pvf::gizmo::Handle::MoveY,
        pvf::gizmo::Handle::CenterScale,
        pvf::gizmo::Handle::ScaleTopLeft,
        pvf::gizmo::Handle::ScaleTopRight,
        pvf::gizmo::Handle::ScaleBottomRight,
        pvf::gizmo::Handle::ScaleBottomLeft,
        pvf::gizmo::Handle::RotateTopLeft,
        pvf::gizmo::Handle::RotateTopRight,
        pvf::gizmo::Handle::RotateBottomRight,
        pvf::gizmo::Handle::RotateBottomLeft,
    };
    for (pvf::gizmo::Handle handle : handles)
    {
        const QPointF position = viewport->gizmoHandleForTest(audioUuid, handle);
        if (!check(!viewport->gizmoHandleAtForTest(position).has_value(),
                   "audio answered a gizmo hit test"))
            qCritical() << "audio-gizmo-test: handle" << int(handle)
                        << "at" << position;
    }

    // 4) A press/drag over the audio gizmo position must not start a stage
    //    gesture or commit anything.  Only the audio clip is there, so picking
    //    misses as well.
    {
        flushSession();
        const int depthBefore = undoDepth();
        const QPointF pivot =
            viewport->gizmoHandleForTest(audioUuid,
                                         pvf::gizmo::Handle::CenterScale);
        QMouseEvent press(QEvent::MouseButtonPress, pivot,
                          viewport->mapToGlobal(pivot.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        if (!check(!viewport->sessionDraggingForTest(),
                   "audio gizmo press started a drag"))
        {
            qCritical() << "audio-gizmo-test: pivot" << pivot;
        }
        const QPointF target = pivot + QPointF(80.0, 40.0);
        QMouseEvent move(QEvent::MouseMove, target,
                         viewport->mapToGlobal(target.toPoint()), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        check(!viewport->sessionDraggingForTest(),
              "audio gizmo move started a drag");
        QMouseEvent release(QEvent::MouseButtonRelease, target,
                            viewport->mapToGlobal(target.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &release);
        QApplication::processEvents();
        flushSession();
        check(undoDepth() == depthBefore,
              "audio gizmo press created an edit");
        check(nearly(viewport->gizmoTransformForTest(audioUuid).x, 220.0, 1e-6),
              "audio transform changed from an inert press");
    }

    if (failures == 0)
        qInfo() << "audio-gizmo-test: ok";
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerViewportAudioGizmoTests(Registry& registry)
{
    registry.add(QStringLiteral("--audio-gizmo-test"),
                 [](QApplication& app) { return audioGizmoTest(app); });
}

} // namespace pvf::tests
