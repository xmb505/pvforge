// Session viewport tests: gizmo math/snapping, Inkscape-style rotate handles
// and the camera monitor. The Project-based editor gestures they used to be
// bundled with went away with the retired shell.

#include "TestApp.h"
#include "TestRegistry.h"

namespace {

QPointF rotatePointForTest(const QPointF& point, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    return QPointF(point.x() * std::cos(radians) - point.y() * std::sin(radians),
                   point.x() * std::sin(radians) + point.y() * std::cos(radians));
}

struct MouseDriver
{
    QWidget* widget = nullptr;

    void send(QEvent::Type type, const QPointF& pos, Qt::MouseButton button,
              Qt::MouseButtons buttons,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(type, pos, widget->mapToGlobal(pos.toPoint()), button,
                          buttons, modifiers);
        QApplication::sendEvent(widget, &event);
    }

    void drag(const QPointF& from, const QPointF& to,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton,
             modifiers);
        send(QEvent::MouseMove, (from + to) / 2.0, Qt::NoButton, Qt::LeftButton,
             modifiers);
        send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
        send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton,
             modifiers);
    }
};

int sessionGizmoTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    // P3b snap settings: the 0/30/45/90 set and a 6 px threshold.  Use the
    // same explicit organisation/application QSettings as SettingsStore.
    {
        QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
        settings.setValue(QStringLiteral("snap/pixels"), 6);
        settings.setValue(QStringLiteral("snap/canvasCenter"), true);
        settings.setValue(QStringLiteral("snap/entityEdges"), true);
        settings.setValue(QStringLiteral("snap/edgeMidpoints"), true);
        settings.setValue(QStringLiteral("snap/angles"),
                          QStringLiteral("0,30,45,90"));
        settings.sync();
    }

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p3b-gizmo.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P3b Gizmo");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "gizmo-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("方块");
    entity.start = 0;
    entity.end = 60;
    entity.row = 0;
    entity.content.kind = pvf::ContentKind::Vector;
    entity.content.vector.width = 200.0;
    entity.content.vector.height = 100.0;
    entity.content.vector.fillColor = QStringLiteral("#ffffff");
    // Model space is centred on the canvas origin with +Y up, so (0,0) is the
    // middle of the 640x360 canvas.
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    const pvf::Uuid uuid = entity.uuid;
    project.entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("pvf: init"),
                                          &error))
    {
        qCritical() << "gizmo-test: init commit failed" << error;
        return 1;
    }

    pvf::MainWindow window(session.release());
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* sessionPtr = window.editorSessionForTest();
    auto* viewport = window.viewportForTest();
    if (!sessionPtr || !viewport)
    {
        qCritical() << "gizmo-test: no session viewport";
        return 1;
    }
    auto* document = sessionPtr->document();
    const auto flushSession = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        return flushError;
    };
    document->setSelection(uuid);
    sessionPtr->setPlayhead(0);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();

    MouseDriver mouse{viewport};
    const auto undoDepth = [document] { return document->store()->undoDepth(); };

    // 1) The gizmo axes follow the entity: +X right, +Y up.  Model space is
    //    Y-up (the viewport applies the single model -> screen Y flip) and a
    //    positive rotation is counter-clockwise on screen.
    const QPointF pivot = viewport->sessionPivotForTest();
    const QPointF modelOrigin =
        viewport->projectToWidgetForTest(QPointF(0.0, 0.0));
    if (std::hypot(pivot.x() - modelOrigin.x(),
                   pivot.y() - modelOrigin.y()) > 0.5)
    {
        qCritical() << "gizmo-test: model origin is not the canvas centre"
                    << modelOrigin << pivot;
        ++failures;
    }
    const QPointF xHandle =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    const QPointF yHandle =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY);
    if (!(xHandle.x() > pivot.x() + 10.0
          && std::abs(xHandle.y() - pivot.y()) < 1.0))
    {
        qCritical() << "gizmo-test: X axis not to the right" << pivot << xHandle;
        ++failures;
    }
    if (!(yHandle.y() < pivot.y() - 10.0
          && std::abs(yHandle.x() - pivot.x()) < 1.0))
    {
        qCritical() << "gizmo-test: Y axis not up" << pivot << yHandle;
        ++failures;
    }

    // 2) Rotate 90 degrees counter-clockwise on screen with the corner handle.
    //    Widget space is Y-down, so the screen-space target is -90 while the
    //    model rotation must become +90.  One gesture = one commit.
    flushSession();
    const int depthBeforeRotate = undoDepth();
    const QPointF rotateStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF rotateTarget =
        pivot + rotatePointForTest(rotateStart - pivot, -90.0);
    mouse.drag(rotateStart, rotateTarget);
    QApplication::processEvents();
    const QString rotateFlushError = flushSession();
    if (!rotateFlushError.isEmpty())
    {
        qCritical() << "gizmo-test: rotate flush failed" << rotateFlushError;
        ++failures;
    }
    pvf::gizmo::Transform rotated = viewport->gizmoTransformForTest(uuid);
    if (std::abs(std::remainder(rotated.rotation - 90.0, 360.0)) > 0.01)
    {
        qCritical() << "gizmo-test: rotation snap 90 failed" << rotated.rotation;
        ++failures;
    }
    if (undoDepth() != depthBeforeRotate + 1)
    {
        qCritical() << "gizmo-test: rotation was not exactly one commit"
                    << depthBeforeRotate << undoDepth();
        ++failures;
    }

    // Auto-key writes the current frame's key (relative frame 0 here).
    {
        const auto& rotatedEntity = sessionPtr->project().entities[uuid];
        const auto track = rotatedEntity.tracks.constFind(
            QStringLiteral("rotation"));
        bool keyed = false;
        if (track != rotatedEntity.tracks.constEnd())
            for (const pvf::model::Keyframe& key : track.value().keys)
                if (key.frame == 0
                    && std::abs(std::remainder(key.value - 90.0, 360.0)) < 0.01)
                    keyed = true;
        if (!keyed)
        {
            qCritical() << "gizmo-test: auto-key did not write the rotation key";
            ++failures;
        }
    }

    // After rotating +90 (counter-clockwise) the local axes must have turned
    // with the entity: +X points up on screen, +Y points left.
    const QPointF pivot90 = viewport->sessionPivotForTest();
    const QPointF xHandle90 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    const QPointF yHandle90 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY);
    if (!(xHandle90.y() < pivot90.y() - 10.0
          && std::abs(xHandle90.x() - pivot90.x()) < 1.0))
    {
        qCritical() << "gizmo-test: rotated X axis did not follow"
                    << pivot90 << xHandle90;
        ++failures;
    }
    if (!(yHandle90.x() < pivot90.x() - 10.0
          && std::abs(yHandle90.y() - pivot90.y()) < 1.0))
    {
        qCritical() << "gizmo-test: rotated Y axis did not follow"
                    << pivot90 << yHandle90;
        ++failures;
    }
    document->undo();
    QApplication::processEvents();
    if (std::abs(viewport->gizmoTransformForTest(uuid).rotation) > 0.01)
    {
        qCritical() << "gizmo-test: undo did not restore rotation";
        ++failures;
    }

    // 3) Drag projection: with rotation 30 the local X axis is 30 degrees
    // away from screen X, so a horizontal 120 px drag only advances x by the
    // projected amount.  Ctrl disables snapping.
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 30.0;
    document->commitEntity(uuid, QStringLiteral("pvf: set rotation"), &error);
    flushSession();
    QApplication::processEvents();
    // A +30 model rotation is counter-clockwise: the 48 px +X handle moves
    // right and up on screen, and +Y moves left and up.
    const QPointF pivot30 = viewport->sessionPivotForTest();
    const QPointF x30 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX) - pivot30;
    const QPointF y30 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY) - pivot30;
    const double axisLength = 48.0;
    const double cos30 = std::cos(qDegreesToRadians(30.0));
    const double sin30 = std::sin(qDegreesToRadians(30.0));
    if (std::abs(x30.x() - axisLength * cos30) > 0.5
        || std::abs(x30.y() + axisLength * sin30) > 0.5
        || std::abs(y30.x() + axisLength * sin30) > 0.5
        || std::abs(y30.y() + axisLength * cos30) > 0.5)
    {
        qCritical() << "gizmo-test: rotation 30 axes are not Y-up/CCW" << x30
                    << y30;
        ++failures;
    }
    const pvf::gizmo::Transform beforeProjection =
        viewport->gizmoTransformForTest(uuid);
    const QPointF axisStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    const double scale = viewport->currentScale();
    mouse.drag(axisStart, axisStart + QPointF(120.0, 0.0),
               Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform projected = viewport->gizmoTransformForTest(uuid);
    const double expected = (120.0 / scale) * std::cos(qDegreesToRadians(30.0));
    const double deltaX = projected.x - beforeProjection.x;
    const double deltaY = projected.y - beforeProjection.y;
    if (std::abs(deltaX - expected) > 1.5 || std::abs(deltaY) > 1.5)
    {
        qCritical() << "gizmo-test: X axis projection wrong" << deltaX << deltaY
                    << expected;
        ++failures;
    }
    // The projection gesture wrote auto-key tracks at frame 0; undo it so the
    // next checks start from the plain rotation-30 state.
    document->undo();
    QApplication::processEvents();

    // The Y axis is perpendicular: the same horizontal drag advances y by the
    // projected amount instead.
    const QPointF yAxisStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY);
    const pvf::gizmo::Transform beforeY =
        viewport->gizmoTransformForTest(uuid);
    mouse.drag(yAxisStart, yAxisStart + QPointF(120.0, 0.0),
               Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform movedY = viewport->gizmoTransformForTest(uuid);
    const double expectedY =
        -(120.0 / scale) * std::sin(qDegreesToRadians(30.0));
    if (std::abs(movedY.y - beforeY.y - expectedY) > 1.5
        || std::abs(movedY.x - beforeY.x) > 1.5)
    {
        qCritical() << "gizmo-test: Y axis projection wrong"
                    << movedY.x - beforeY.x << movedY.y - beforeY.y << expectedY;
        ++failures;
    }
    document->undo();
    QApplication::processEvents();

    // 4) Angle snapping from AppSettings (0/30/45/90) and Ctrl bypass.
    if (std::abs(viewport->snapAngleForTest(28.5, false) - 30.0) > 0.01
        || std::abs(viewport->snapAngleForTest(47.0, true) - 45.0) > 0.01
        || std::abs(viewport->snapAngleForTest(52.0, false) - 52.0) > 0.01)
    {
        qCritical() << "gizmo-test: angle snap settings wrong"
                    << viewport->snapAngleForTest(28.5, false)
                    << viewport->snapAngleForTest(47.0, true)
                    << viewport->snapAngleForTest(52.0, false);
        ++failures;
    }
    const double rotationBeforeCtrl = viewport->gizmoTransformForTest(uuid).rotation;
    const QPointF ctrlRotateStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF ctrlPivot = viewport->sessionPivotForTest();
    // 28.5 degrees counter-clockwise on screen: widget Y is down, so the
    // screen-space rotation is negative while the model delta stays positive.
    const QPointF ctrlRotateTarget =
        ctrlPivot + rotatePointForTest(ctrlRotateStart - ctrlPivot, -28.5);
    mouse.drag(ctrlRotateStart, ctrlRotateTarget, Qt::ControlModifier);
    QApplication::processEvents();
    const double ctrlRotation = viewport->gizmoTransformForTest(uuid).rotation;
    const double ctrlDelta =
        std::remainder(ctrlRotation - rotationBeforeCtrl, 360.0);
    if (std::abs(ctrlDelta - 28.5) > 0.6)
    {
        qCritical() << "gizmo-test: Ctrl did not bypass angle snap" << ctrlDelta;
        ++failures;
    }
    flushSession();
    document->undo();
    QApplication::processEvents();

    // 5) Move snapping: near the canvas centre (the model origin) the drag
    // snaps to it; Ctrl keeps the unsnapped position.
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 0.0;
    sessionPtr->mutableProject().entities[uuid].transform.x = 3.0;
    sessionPtr->mutableProject().entities[uuid].transform.y = 0.0;
    document->commitEntity(uuid, QStringLiteral("pvf: offset"), &error);
    flushSession();
    QApplication::processEvents();
    const QPointF snapStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    mouse.drag(snapStart, snapStart + QPointF(1.0, 0.0));
    QApplication::processEvents();
    flushSession();
    const double snappedX = viewport->gizmoTransformForTest(uuid).x;
    if (std::abs(snappedX - 0.0) > 0.01)
    {
        qCritical() << "gizmo-test: centre-line snap failed" << snappedX;
        ++failures;
    }
    document->undo();
    QApplication::processEvents();
    const QPointF ctrlStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    mouse.drag(ctrlStart, ctrlStart + QPointF(1.0, 0.0), Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const double ctrlX = viewport->gizmoTransformForTest(uuid).x;
    const double expectedCtrlX = 3.0 + 1.0 / scale;
    if (std::abs(ctrlX - expectedCtrlX) > 0.2)
    {
        qCritical() << "gizmo-test: Ctrl did not bypass move snap" << ctrlX
                    << expectedCtrlX;
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    // 6) Esc cancels the live gesture without a commit.
    flushSession();
    const int depthBeforeCancel = undoDepth();
    const QPointF cancelStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    mouse.send(QEvent::MouseButtonPress, cancelStart, Qt::LeftButton,
               Qt::LeftButton);
    mouse.send(QEvent::MouseMove, cancelStart + QPointF(80.0, 0.0), Qt::NoButton,
               Qt::LeftButton);
    if (!viewport->sessionDraggingForTest())
    {
        qCritical() << "gizmo-test: drag did not start";
        ++failures;
    }
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(viewport, &escape);
    mouse.send(QEvent::MouseButtonRelease, cancelStart + QPointF(80.0, 0.0),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    if (undoDepth() != depthBeforeCancel
        || std::abs(viewport->gizmoTransformForTest(uuid).x - 3.0) > 0.01)
    {
        qCritical() << "gizmo-test: Esc did not cancel" << undoDepth()
                    << viewport->gizmoTransformForTest(uuid).x;
        ++failures;
    }

    // 7) Workbench cache is bypassed while dragging and refreshed on release.
    auto* workbench = sessionPtr->workbench();
    if (!workbench)
    {
        qCritical() << "gizmo-test: workbench cache missing";
        ++failures;
    }
    else
    {
        viewport->repaint();
        QApplication::processEvents();
        bool hit = false;
        const QImage cachedBefore = workbench->lookup(0, pvf::Uuid(), &hit);
        if (!hit || cachedBefore.isNull())
        {
            qCritical() << "gizmo-test: cache was not seeded";
            ++failures;
        }

        const int hitsBefore = workbench->stats().hits;
        const QPointF cacheStart =
            viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
        mouse.send(QEvent::MouseButtonPress, cacheStart, Qt::LeftButton,
                   Qt::LeftButton);
        mouse.send(QEvent::MouseMove, cacheStart + QPointF(60.0, 0.0),
                   Qt::NoButton, Qt::LeftButton);
        viewport->repaint();
        QApplication::processEvents();
        if (workbench->stats().hits != hitsBefore)
        {
            qCritical() << "gizmo-test: cache was used during a drag"
                        << hitsBefore << workbench->stats().hits;
            ++failures;
        }
        mouse.send(QEvent::MouseButtonRelease, cacheStart + QPointF(60.0, 0.0),
                   Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();

        bool updated = false;
        QImage cachedAfter;
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < 3000)
        {
            bool afterHit = false;
            cachedAfter = workbench->lookup(0, pvf::Uuid(), &afterHit);
            if (afterHit && !cachedAfter.isNull()
                && cachedAfter != cachedBefore)
            {
                updated = true;
                break;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
        }
        if (!updated)
        {
            qCritical() << "gizmo-test: cache was not refreshed after release";
            ++failures;
        }
    }

    // Screenshot with a visible gizmo.  The floating monitor would cover the
    // handles, so hide it for this shot only.
    if (auto* monitor = window.findChild<pvf::CameraMonitor*>())
        monitor->hide();
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 30.0;
    document->commitEntity(uuid, QStringLiteral("pvf: shot rotation"), &error);
    flushSession();
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-p3b-gizmo.png");
    if (!window.grab().save(screenshot))
    {
        qCritical() << "gizmo-test: screenshot failed" << screenshot;
        ++failures;
    }
    else
    {
        qInfo() << "gizmo-test: screenshot" << screenshot;
    }

    if (failures == 0)
        qInfo() << "gizmo-test: ok";
    return failures == 0 ? 0 : 1;
}

// Inkscape-style corner rotate handles: drawing geometry, square hit boxes,
// constant screen size, Shift-opposite rotation, Ctrl snap bypass, one commit
// per gesture, Esc cancel and negative-scale corner identity.
int inkscapeHandlesTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    {
        QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
        settings.setValue(QStringLiteral("snap/pixels"), 6);
        settings.setValue(QStringLiteral("snap/angles"), QStringLiteral("0,30,45,90"));
        settings.sync();
    }

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("inkscape-handles.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Inkscape Handles");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "inkscape-handles-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("弯箭头");
    entity.start = 0;
    entity.end = 60;
    entity.row = 0;
    entity.content.kind = pvf::ContentKind::Vector;
    entity.content.vector.width = 200.0;
    entity.content.vector.height = 100.0;
    entity.content.vector.fillColor = QStringLiteral("#ffffff");
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    const pvf::Uuid uuid = entity.uuid;
    project.entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("pvf: init"), &error))
    {
        qCritical() << "inkscape-handles-test: init commit failed" << error;
        return 1;
    }

    pvf::MainWindow window(session.release());
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* sessionPtr = window.editorSessionForTest();
    auto* viewport = window.viewportForTest();
    if (!sessionPtr || !viewport)
    {
        qCritical() << "inkscape-handles-test: no session viewport";
        return 1;
    }
    auto* document = sessionPtr->document();
    const auto flushSession = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        return flushError;
    };
    document->setSelection(uuid);
    sessionPtr->setPlayhead(0);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();

    MouseDriver mouse{viewport};
    const auto undoDepth = [document] { return document->store()->undoDepth(); };
    const auto nearAngle = [](double value, double expected) {
        return std::abs(std::remainder(value - expected, 360.0)) < 0.01;
    };
    const auto rotateHandle = [&](int visualCorner) {
        return pvf::gizmo::rotateHandleForVisualCorner(visualCorner);
    };
    const auto expectedAngles = [&](const std::array<double, 4>& angles,
                                    double rotation, const char* label) {
        for (int visual = 0; visual < 4; ++visual)
        {
            const double actual =
                viewport->gizmoRotateGlyphAngleForTest(uuid, visual);
            // A positive entity rotation is counter-clockwise in the Y-up
            // model; QPainter::rotate() is clockwise on screen, so the glyph
            // angle must decrease by the same amount.
            if (!nearAngle(actual, angles[visual] - rotation))
            {
                qCritical() << "inkscape-handles-test:" << label << "glyph"
                            << visual << "angle" << actual << "expected"
                            << angles[visual] - rotation;
                ++failures;
            }
        }
    };

    // 1) Unzoomed geometry: each corner glyph rides its visual corner at a
    //    fixed 3 px inset, and each hit box is an 18 px axis-aligned square.
    const std::array<double, 4> baseAngles{270.0, 0.0, 90.0, 180.0};
    if (std::abs(pvf::gizmo::kRotateHandleSize - 18.0) > 1e-9
        || std::abs(pvf::gizmo::kRotateHitSize - 18.0) > 1e-9)
    {
        qCritical() << "inkscape-handles-test: unexpected handle size";
        ++failures;
    }
    for (int visual = 0; visual < 4; ++visual)
    {
        const QPointF corner = viewport->gizmoCornerForTest(uuid, visual);
        const pvf::gizmo::Handle handle = rotateHandle(visual);
        const QPointF centre = viewport->gizmoHandleForTest(uuid, handle);
        const double inset = std::hypot(centre.x() - corner.x(),
                                        centre.y() - corner.y());
        if (std::abs(inset - pvf::gizmo::kRotateHandleInset) > 0.01)
        {
            qCritical() << "inkscape-handles-test: inset" << visual << inset;
            ++failures;
        }
        const QRectF hit = viewport->gizmoRotateHitRectForTest(uuid, handle);
        if (std::abs(hit.width() - pvf::gizmo::kRotateHitSize) > 0.01
            || std::abs(hit.height() - pvf::gizmo::kRotateHitSize) > 0.01
            || std::abs(hit.center().x() - centre.x()) > 0.01
            || std::abs(hit.center().y() - centre.y()) > 0.01)
        {
            qCritical() << "inkscape-handles-test: hit rect" << visual << hit;
            ++failures;
        }
        if (viewport->gizmoHandleAtForTest(centre) != handle)
        {
            qCritical() << "inkscape-handles-test: centre hit" << visual;
            ++failures;
        }
        // Square, not circle: a diagonal offset closer than the half-size is
        // inside, a point past the side is outside.  Probe the outward
        // diagonal so the nearby scale square cannot win the nearest-centre
        // test.
        const QPointF outward = corner - centre;
        const QPointF inside = centre
                               + QPointF(outward.x() < 0.0 ? -8.9 : 8.9,
                                         outward.y() < 0.0 ? -8.9 : 8.9);
        const pvf::gizmo::Handle hitInside = viewport->gizmoHandleAtForTest(inside)
                                                 .value_or(pvf::gizmo::Handle::MoveX);
        if (!pvf::gizmo::isRotateHandle(hitInside))
        {
            qCritical() << "inkscape-handles-test: square corner miss" << visual;
            ++failures;
        }
        const QPointF outside = centre + QPointF(9.2, 0.0);
        const auto outsideHit = viewport->gizmoHandleAtForTest(outside);
        if (outsideHit && pvf::gizmo::isRotateHandle(*outsideHit))
        {
            qCritical() << "inkscape-handles-test: square side hit" << visual;
            ++failures;
        }
    }
    expectedAngles(baseAngles, 0.0, "rotation 0");

    // 2) Zoom must not change the screen-pixel inset, hit size or icon angle.
    const QPointF zoomCentre = viewport->rect().center();
    for (int step = 0; step < 4; ++step)
    {
        QWheelEvent zoomEvent(zoomCentre,
                              viewport->mapToGlobal(zoomCentre.toPoint()),
                              QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewport, &zoomEvent);
    }
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    for (int visual = 0; visual < 4; ++visual)
    {
        const QPointF corner = viewport->gizmoCornerForTest(uuid, visual);
        const pvf::gizmo::Handle handle = rotateHandle(visual);
        const QPointF centre = viewport->gizmoHandleForTest(uuid, handle);
        const double inset = std::hypot(centre.x() - corner.x(),
                                        centre.y() - corner.y());
        const QRectF hit = viewport->gizmoRotateHitRectForTest(uuid, handle);
        if (std::abs(inset - pvf::gizmo::kRotateHandleInset) > 0.01
            || std::abs(hit.width() - pvf::gizmo::kRotateHitSize) > 0.01
            || std::abs(hit.height() - pvf::gizmo::kRotateHitSize) > 0.01)
        {
            qCritical() << "inkscape-handles-test: zoom changed geometry"
                        << visual << inset << hit.size();
            ++failures;
        }
    }
    expectedAngles(baseAngles, 0.0, "zoomed");
    viewport->resetView();
    QApplication::processEvents();

    // 3) Icon direction turns with the entity in 90 degree steps (the glyph
    //    angle decreases as the Y-up rotation grows counter-clockwise).
    const auto setRotation = [&](double rotation) {
        auto it = sessionPtr->mutableProject().entities.find(uuid);
        if (it == sessionPtr->mutableProject().entities.end())
            return;
        it->transform.rotation = rotation;
        document->commitEntity(uuid, QStringLiteral("pvf: set rotation"), &error);
        flushSession();
        QApplication::processEvents();
        viewport->repaint();
        QApplication::processEvents();
    };
    const auto setScale = [&](double scaleX, double scaleY) {
        auto it = sessionPtr->mutableProject().entities.find(uuid);
        if (it == sessionPtr->mutableProject().entities.end())
            return;
        it->transform.scaleX = scaleX;
        it->transform.scaleY = scaleY;
        document->commitEntity(uuid, QStringLiteral("pvf: set scale"), &error);
        flushSession();
        QApplication::processEvents();
        viewport->repaint();
        QApplication::processEvents();
    };

    setRotation(90.0);
    expectedAngles(baseAngles, 90.0, "rotation 90");
    setRotation(180.0);
    expectedAngles(baseAngles, 180.0, "rotation 180");
    setRotation(0.0);
    document->setSelection(uuid);
    QApplication::processEvents();

    // 4) Shift: rotate around the opposite visual corner.  A screen-space -90
    //    degree drag is +90 in the Y-up model.
    flushSession();
    const int depthBeforeShift = undoDepth();
    const QPointF shiftCorner = viewport->gizmoCornerForTest(uuid, 3);
    const QPointF shiftStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF shiftTarget =
        shiftCorner + rotatePointForTest(shiftStart - shiftCorner, -90.0);
    mouse.send(QEvent::MouseButtonPress, shiftStart, Qt::LeftButton,
               Qt::LeftButton, Qt::ShiftModifier);
    mouse.send(QEvent::MouseMove, shiftTarget, Qt::NoButton, Qt::LeftButton,
               Qt::ShiftModifier);
    if (!viewport->sessionDraggingForTest()
        || !viewport->gizmoDraggingAroundCornerForTest()
        || viewport->gizmoCornerHandlesVisibleForTest())
    {
        qCritical() << "inkscape-handles-test: Shift drag state"
                    << viewport->sessionDraggingForTest()
                    << viewport->gizmoDraggingAroundCornerForTest()
                    << viewport->gizmoCornerHandlesVisibleForTest();
        ++failures;
    }
    if (std::hypot(viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).x()
                       - shiftCorner.x(),
                   viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).y()
                       - shiftCorner.y())
        > 0.6)
    {
        qCritical() << "inkscape-handles-test: Shift origin"
                    << viewport->projectToWidgetForTest(
                           viewport->gizmoDragOriginForTest())
                    << shiftCorner;
        ++failures;
    }
    mouse.send(QEvent::MouseButtonRelease, shiftTarget, Qt::LeftButton,
               Qt::NoButton, Qt::ShiftModifier);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform shiftResult = viewport->gizmoTransformForTest(uuid);
    const QPointF shiftCornerAfter = viewport->gizmoCornerForTest(uuid, 3);
    if (!nearAngle(shiftResult.rotation, 90.0))
    {
        qCritical() << "inkscape-handles-test: Shift rotation"
                    << shiftResult.rotation;
        ++failures;
    }
    if (std::hypot(shiftCornerAfter.x() - shiftCorner.x(),
                   shiftCornerAfter.y() - shiftCorner.y()) > 0.6)
    {
        qCritical() << "inkscape-handles-test: opposite corner moved"
                    << shiftCorner << shiftCornerAfter;
        ++failures;
    }
    if (undoDepth() != depthBeforeShift + 1)
    {
        qCritical() << "inkscape-handles-test: Shift commits"
                    << depthBeforeShift << undoDepth();
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    // 5) Snap settings 0/30/45/90 with a 3 degree threshold; Ctrl bypasses it.
    flushSession();
    const int depthBeforeSnap = undoDepth();
    const QPointF snapPivot = viewport->sessionPivotForTest();
    const QPointF snapStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF snapTarget =
        snapPivot + rotatePointForTest(snapStart - snapPivot, -28.5);
    mouse.drag(snapStart, snapTarget);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform snapped = viewport->gizmoTransformForTest(uuid);
    if (!nearAngle(snapped.rotation, 30.0))
    {
        qCritical() << "inkscape-handles-test: snap 30" << snapped.rotation;
        ++failures;
    }
    if (undoDepth() != depthBeforeSnap + 1)
    {
        qCritical() << "inkscape-handles-test: snap commits" << depthBeforeSnap
                    << undoDepth();
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    const pvf::gizmo::Transform beforeCtrl = viewport->gizmoTransformForTest(uuid);
    mouse.drag(snapStart, snapTarget, Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const double ctrlDelta =
        std::remainder(viewport->gizmoTransformForTest(uuid).rotation
                           - beforeCtrl.rotation,
                       360.0);
    if (std::abs(ctrlDelta - 28.5) > 0.6)
    {
        qCritical() << "inkscape-handles-test: Ctrl delta" << ctrlDelta;
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    // 6) Esc cancels without a commit, and the drag hides the corner handles.
    flushSession();
    const int depthBeforeCancel = undoDepth();
    const QPointF cancelStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    mouse.send(QEvent::MouseButtonPress, cancelStart, Qt::LeftButton,
               Qt::LeftButton);
    mouse.send(QEvent::MouseMove, cancelStart + QPointF(40.0, 10.0),
               Qt::NoButton, Qt::LeftButton);
    if (viewport->gizmoCornerHandlesVisibleForTest())
    {
        qCritical() << "inkscape-handles-test: corners visible while dragging";
        ++failures;
    }
    const QPointF pivotDuringDrag = viewport->sessionPivotForTest();
    if (std::hypot(viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).x()
                       - pivotDuringDrag.x(),
                   viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).y()
                       - pivotDuringDrag.y())
        > 0.6
        || viewport->gizmoDraggingAroundCornerForTest())
    {
        qCritical() << "inkscape-handles-test: default origin is not the pivot";
        ++failures;
    }
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(viewport, &escape);
    mouse.send(QEvent::MouseButtonRelease, cancelStart + QPointF(40.0, 10.0),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    if (undoDepth() != depthBeforeCancel
        || !nearAngle(viewport->gizmoTransformForTest(uuid).rotation, 0.0))
    {
        qCritical() << "inkscape-handles-test: Esc cancel" << undoDepth()
                    << viewport->gizmoTransformForTest(uuid).rotation;
        ++failures;
    }

    // 7) Negative scale: RotateTopLeft stays the visual top-left handle (the
    //    local corner under it swaps) and the icon is not mirrored.
    const QPointF baseTopLeft =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopLeft);
    const QPointF baseTopRight =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF baseBottomRight =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateBottomRight);
    const double baseAngleTopLeft = viewport->gizmoRotateGlyphAngleForTest(uuid, 0);
    if (std::hypot(viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).x()
                       - viewport->gizmoCornerForTest(uuid, 2).x(),
                   viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).y()
                       - viewport->gizmoCornerForTest(uuid, 2).y())
        > 0.01)
    {
        qCritical() << "inkscape-handles-test: opposite corner (base)";
        ++failures;
    }

    setScale(-1.0, 1.0);
    const QPointF flippedTopLeft =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopLeft);
    const QPointF flippedTopRight =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const double flippedAngleTopLeft =
        viewport->gizmoRotateGlyphAngleForTest(uuid, 0);
    // The mirrored box occupies the same screen rectangle, so the visual
    // top-left handle must not move.  Moving to baseTopRight is exactly the
    // old local-corner bug.
    if (std::hypot(flippedTopLeft.x() - baseTopLeft.x(),
                   flippedTopLeft.y() - baseTopLeft.y()) > 0.01
        || std::hypot(flippedTopRight.x() - baseTopRight.x(),
                      flippedTopRight.y() - baseTopRight.y()) > 0.01
        || std::hypot(flippedTopLeft.x() - baseTopRight.x(),
                      flippedTopLeft.y() - baseTopRight.y()) < 1.0)
    {
        qCritical() << "inkscape-handles-test: flip X corner identity"
                    << flippedTopLeft << baseTopLeft << flippedTopRight
                    << baseTopRight;
        ++failures;
    }
    if (std::hypot(viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).x()
                       - viewport->gizmoCornerForTest(uuid, 2).x(),
                   viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).y()
                       - viewport->gizmoCornerForTest(uuid, 2).y())
        > 0.01)
    {
        qCritical() << "inkscape-handles-test: opposite corner (flip X)";
        ++failures;
    }
    if (!nearAngle(flippedAngleTopLeft, baseAngleTopLeft))
    {
        qCritical() << "inkscape-handles-test: icon mirrored on flip"
                    << flippedAngleTopLeft << baseAngleTopLeft;
        ++failures;
    }
    if (viewport->gizmoHandleAtForTest(flippedTopLeft)
        != pvf::gizmo::Handle::RotateTopLeft)
    {
        qCritical() << "inkscape-handles-test: visual top-left hit";
        ++failures;
    }
    setScale(-1.0, -1.0);
    const QPointF bothTopLeft =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopLeft);
    if (std::hypot(bothTopLeft.x() - baseTopLeft.x(),
                   bothTopLeft.y() - baseTopLeft.y()) > 0.01
        || std::hypot(bothTopLeft.x() - baseBottomRight.x(),
                      bothTopLeft.y() - baseBottomRight.y()) < 1.0)
    {
        qCritical() << "inkscape-handles-test: flip XY corner identity"
                    << bothTopLeft << baseTopLeft << baseBottomRight;
        ++failures;
    }
    setScale(1.0, 1.0);
    document->setSelection(uuid);
    QApplication::processEvents();

    // 8) Screenshot with a selected entity and all four carrow handles.
    if (auto* monitor = window.findChild<pvf::CameraMonitor*>())
        monitor->hide();
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 30.0;
    document->commitEntity(uuid, QStringLiteral("pvf: shot rotation"), &error);
    flushSession();
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-inkscape-handles.png");
    if (!window.grab().save(screenshot))
    {
        qCritical() << "inkscape-handles-test: screenshot failed" << screenshot;
        ++failures;
    }
    else
    {
        qInfo() << "inkscape-handles-test: screenshot" << screenshot;
    }

    if (failures == 0)
        qInfo() << "inkscape-handles-test: ok";
    return failures == 0 ? 0 : 1;
}

int cameraMonitorTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    // Generate a blue reference video for the trace overlay.
    const QString tracePath = QDir(temp.path()).filePath(QStringLiteral("trace.mp4"));
    bool traceVideoReady = false;
    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
    {
        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"),
                     {QStringLiteral("-y"), QStringLiteral("-v"),
                      QStringLiteral("error"), QStringLiteral("-f"),
                      QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("color=c=blue:s=320x240:d=1:r=30"),
                      QStringLiteral("-c:v"), QStringLiteral("libx264"),
                      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                      QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                      tracePath});
        traceVideoReady = ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0;
        if (!traceVideoReady)
            qInfo() << "camera-monitor-test: ffmpeg trace generation skipped";
    }

    const auto buildProject = [&temp](const QString& name, bool withCamera) {
        const QString dir = QDir(temp.path()).filePath(name);
        pvf::model::ProjectSettings settings;
        settings.name = name;
        settings.width = 640;
        settings.height = 360;
        settings.fps = 30;
        QString error;
        auto session =
            pvf::EditorSession::createProject(dir, settings, &error);
        if (!session)
            return std::unique_ptr<pvf::EditorSession>();
        auto& project = session->mutableProject();

        pvf::model::Entity shape;
        shape.uuid = pvf::model::newUuid();
        shape.name = QStringLiteral("红色方块");
        shape.start = 0;
        shape.end = 60;
        shape.content.kind = pvf::ContentKind::Vector;
        shape.content.vector.width = 260.0;
        shape.content.vector.height = 160.0;
        shape.content.vector.fillColor = QStringLiteral("#ff0000");
        shape.transform.x = 0.0;
        shape.transform.y = 0.0;
        project.entities.insert(shape.uuid, shape);

        pvf::Uuid cameraUuid;
        if (withCamera)
        {
            pvf::model::Entity camera;
            camera.uuid = pvf::model::newUuid();
            camera.name = QStringLiteral("主摄像机");
            camera.start = 0;
            camera.end = 60;
            camera.content.kind = pvf::ContentKind::Camera;
            camera.transform.x = 0.0;
            camera.transform.y = 0.0;
            cameraUuid = camera.uuid;
            project.entities.insert(cameraUuid, camera);
        }
        if (!session->document()->commitEntity(shape.uuid,
                                               QStringLiteral("pvf: init shape"),
                                               &error)
            || (withCamera
                && !session->document()->commitEntity(
                    cameraUuid, QStringLiteral("pvf: init camera"), &error))
            || !session->document()->commitProject(QStringLiteral("pvf: init"),
                                                   &error))
        {
            qCritical() << "camera-monitor-test: commit failed" << error;
            return std::unique_ptr<pvf::EditorSession>();
        }
        session->flush();
        session->document()->setSelection(cameraUuid);
        return session;
    };

    // A camera project: the monitor must show the camera view.
    auto session = buildProject(QStringLiteral("with-camera.pvproj"), true);
    if (!session)
        return 1;
    const pvf::Uuid cameraUuid = session->activeCameraUuid();
    if (cameraUuid.isEmpty())
    {
        qCritical() << "camera-monitor-test: no active camera";
        return 1;
    }

    pvf::MainWindow window(session.release());
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* sessionPtr = window.editorSessionForTest();
    auto* monitor = window.findChild<pvf::CameraMonitor*>();
    if (!sessionPtr || !monitor)
    {
        qCritical() << "camera-monitor-test: monitor missing";
        return 1;
    }
    monitor->setVisibleFromMenu(true);
    monitor->setFrame(0);
    monitor->repaint();
    QApplication::processEvents();

    if (monitor->findChild<QToolButton*>(QStringLiteral("monitorStreamButton")))
    {
        qCritical() << "camera-monitor-test: session monitor has a stream button";
        ++failures;
    }

    const QImage preview = monitor->previewImageForTest();
    if (preview.isNull())
    {
        qCritical() << "camera-monitor-test: camera preview is empty";
        ++failures;
    }
    else
    {
        const QColor center = preview.pixelColor(preview.width() / 2,
                                                 preview.height() / 2);
        if (center.red() < 180 || center.green() > 100 || center.blue() > 100)
        {
            qCritical() << "camera-monitor-test: camera view centre is not red"
                        << center << preview.size();
            ++failures;
        }
    }
    if (monitor->monitorCameraForTest() != cameraUuid)
    {
        qCritical() << "camera-monitor-test: monitor camera not synced";
        ++failures;
    }

    // The viewport draws the selected camera's viewfinder and can drag it.
    auto* viewport = window.viewportForTest();
    if (!viewport || viewport->cameraFrameForTest().isEmpty())
    {
        qCritical() << "camera-monitor-test: viewfinder frame missing";
        ++failures;
    }
    else
    {
        const auto flushSession = [sessionPtr] {
            QString flushError;
            sessionPtr->flush(&flushError);
            return flushError;
        };
        auto* document = sessionPtr->document();
        MouseDriver mouse{viewport};
        const QPolygonF frame = viewport->cameraFrameForTest();
        const QPointF edge = frame[0] * 0.75 + frame[1] * 0.25;
        flushSession();
        const int depthBefore = document->store()->undoDepth();
        const double xBefore =
            viewport->gizmoTransformForTest(cameraUuid).x;
        mouse.drag(edge, edge + QPointF(60.0, 0.0));
        QApplication::processEvents();
        flushSession();
        const double xAfter = viewport->gizmoTransformForTest(cameraUuid).x;
        const double expectedDelta = 60.0 / viewport->currentScale();
        if (std::abs(xAfter - xBefore - expectedDelta) > 2.0
            || document->store()->undoDepth() != depthBefore + 1)
        {
            qCritical() << "camera-monitor-test: camera move drag failed"
                        << xBefore << xAfter << expectedDelta
                        << document->store()->undoDepth();
            ++failures;
        }
        document->undo();
        QApplication::processEvents();
        document->setSelection(cameraUuid);
        QApplication::processEvents();

        // Rotation handle: 90 degrees around the frame centre.
        const QPolygonF rotatedFrame = viewport->cameraFrameForTest();
        if (rotatedFrame.size() < 4)
        {
            qCritical() << "camera-monitor-test: viewfinder missing after undo";
            ++failures;
        }
        else
        {
            const QPointF topCenter =
                (rotatedFrame[0] + rotatedFrame[1]) / 2.0;
            const QPointF frameCenter =
                (rotatedFrame[0] + rotatedFrame[2]) / 2.0;
            QPointF up = topCenter - frameCenter;
            up /= std::max(1e-6, std::hypot(up.x(), up.y()));
            const QPointF handle = topCenter + up * 28.0;
            // 90 degrees counter-clockwise on screen.  Widget space is Y-down,
            // so the screen-space target is -90 and the model rotation +90.
            const QPointF target =
                frameCenter + rotatePointForTest(handle - frameCenter, -90.0);
            mouse.drag(handle, target);
            QApplication::processEvents();
            flushSession();
            const double rotation =
                viewport->gizmoTransformForTest(cameraUuid).rotation;
            if (std::abs(std::remainder(rotation - 90.0, 360.0)) > 0.5)
            {
                qCritical()
                    << "camera-monitor-test: camera rotation drag failed"
                    << rotation;
                ++failures;
            }
            document->undo();
            QApplication::processEvents();
        }
    }

    // Switching the combo to the whole canvas must stick.
    monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"))
        ->setCurrentIndex(0);
    QApplication::processEvents();
    if (!sessionPtr->activeCameraUuid().isEmpty())
    {
        qCritical() << "camera-monitor-test: whole-canvas switch did not stick";
        ++failures;
    }
    monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"))
        ->setCurrentIndex(1);
    QApplication::processEvents();
    if (sessionPtr->activeCameraUuid() != cameraUuid)
    {
        qCritical() << "camera-monitor-test: camera switch did not restore";
        ++failures;
    }

    // Trace reference video: decoded through video2, alpha blended on top.
    monitor->setTraceOpacity(0.5);
    if (traceVideoReady)
    {
        if (!monitor->loadTraceVideo(tracePath))
        {
            qCritical() << "camera-monitor-test: trace import failed"
                        << monitor->traceErrorForTest();
            ++failures;
        }
        if (monitor->traceImageForTest().isNull())
        {
            qCritical() << "camera-monitor-test: trace image empty";
            ++failures;
        }
        if (std::abs(monitor->traceOpacityForTest() - 0.5) > 0.001)
        {
            qCritical() << "camera-monitor-test: trace opacity wrong";
            ++failures;
        }
        monitor->repaint();
        QApplication::processEvents();
        const QImage shot = monitor->grab().toImage();
        const QColor blended = shot.pixelColor(
            shot.width() / 2, 28 + (shot.height() - 28) / 2);
        if (blended.blue() < 60 || blended.red() < 40)
        {
            qCritical() << "camera-monitor-test: trace overlay not blended"
                        << blended;
            ++failures;
        }
    }
    if (monitor->loadTraceVideo(
            QDir(temp.path()).filePath(QStringLiteral("missing.mp4"))))
    {
        qCritical() << "camera-monitor-test: missing trace did not fail";
        ++failures;
    }
    if (monitor->traceErrorForTest().isEmpty())
    {
        qCritical() << "camera-monitor-test: missing trace gave no hint";
        ++failures;
    }

    // A project without cameras must not crash and must show the canvas.
    auto noCamera = buildProject(QStringLiteral("no-camera.pvproj"), false);
    if (!noCamera)
        return 1;
    pvf::MainWindow noCameraWindow(noCamera.release());
    noCameraWindow.resize(1000, 700);
    noCameraWindow.show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* noCameraMonitor = noCameraWindow.findChild<pvf::CameraMonitor*>();
    if (!noCameraMonitor)
    {
        qCritical() << "camera-monitor-test: no-camera monitor missing";
        ++failures;
    }
    else
    {
        noCameraMonitor->setVisibleFromMenu(true);
        noCameraMonitor->setFrame(0);
        noCameraMonitor->repaint();
        QApplication::processEvents();
        if (noCameraMonitor->previewImageForTest().isNull())
        {
            qCritical() << "camera-monitor-test: no-camera preview empty";
            ++failures;
        }
    }

    if (failures == 0)
        qInfo() << "camera-monitor-test: ok";
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerViewportCameraTests(Registry& registry)
{
    registry.add(QStringLiteral("--gizmo-test"),
                 [](QApplication& app) { return sessionGizmoTest(app); });
    registry.add(QStringLiteral("--inkscape-handles-test"),
                 [](QApplication& app) { return inkscapeHandlesTest(app); });
    registry.add(QStringLiteral("--camera-monitor-test"),
                 [](QApplication& app) { return cameraMonitorTest(app); });
}

} // namespace pvf::tests
