// Regression test for the Wayland crash: dragging the docked camera monitor
// out of the stage must not detach it mid-gesture.
//
// Detaching created a fresh xdg_toplevel and the same event turn asked the
// unconfigured surface for a system move, which KWin rejects as a fatal
// xdg-shell protocol error.  The monitor now stays docked while dragging and
// only becomes its own window when the button is released outside the stage.

#include "TestApp.h"
#include "TestRegistry.h"

#include <QMouseEvent>

namespace pvf::tests {

namespace {

int cameraMonitorDragTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "camera-monitor-drag-test:" << message;
            ++failures;
        }
        return condition;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Monitor Drag");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(
        QDir(temp.path()).filePath(QStringLiteral("monitor-drag.pvproj")),
        settings, &error);
    if (!session)
    {
        qCritical() << "camera-monitor-drag-test: create failed" << error;
        return 1;
    }

    pvf::model::Entity camera;
    camera.uuid = pvf::model::newUuid();
    camera.name = QStringLiteral("主摄像机");
    camera.start = 0;
    camera.end = 60;
    camera.row = 0;
    camera.content.kind = pvf::ContentKind::Camera;
    session->mutableProject().entities.insert(camera.uuid, camera);
    if (!session->document()->commitEntity(camera.uuid,
                                           QStringLiteral("pvf: init camera"),
                                           &error))
    {
        qCritical() << "camera-monitor-drag-test: commit failed" << error;
        return 1;
    }
    session->flush();

    pvf::MainWindow window(session.release());
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* viewport = window.viewportForTest();
    auto* monitor = window.findChild<pvf::CameraMonitor*>();
    if (!check(viewport && monitor, QStringLiteral("monitor or viewport missing")))
        return 1;

    monitor->setVisibleFromMenu(true);
    monitor->setFrame(0);
    QApplication::processEvents();
    if (!check(!monitor->isDetached(),
               QStringLiteral("monitor should start docked")))
        return 1;
    if (!check(monitor->parentWidget() == viewport,
               QStringLiteral("docked monitor is not a viewport child")))
        return 1;

    // The title bar spans the whole widget top; its height is 28 px.
    const QPoint titleLocal(monitor->width() / 2, 10);
    const QPoint pressGlobal = monitor->mapToGlobal(titleLocal);
    const QRect stageGlobal(viewport->mapToGlobal(QPoint(0, 0)),
                            viewport->size());
    // Well outside the stage (and outside its 16 px tolerance).
    const QPoint outsideGlobal = stageGlobal.topLeft() + QPoint(-60, -60);

    const auto send = [monitor](QEvent::Type type, const QPoint& global,
                                Qt::MouseButton button, Qt::MouseButtons buttons) {
        const QPoint local = monitor->mapFromGlobal(global);
        QMouseEvent event(type, local, global, button, buttons,
                          Qt::NoModifier);
        QApplication::sendEvent(monitor, &event);
    };

    send(QEvent::MouseButtonPress, pressGlobal, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, outsideGlobal, Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();

    // Mid-drag the monitor must stay a docked child: the old code detached it
    // here and asked the fresh, still unconfigured top-level for a system move.
    if (!check(!monitor->isDetached(),
               QStringLiteral("monitor detached during the drag")))
        return 1;
    if (!check(monitor->parentWidget() == viewport,
               QStringLiteral("monitor left the viewport during the drag")))
        return 1;

    send(QEvent::MouseButtonRelease, outsideGlobal, Qt::LeftButton,
         Qt::NoButton);
    QApplication::processEvents();

    if (!check(monitor->isDetached(),
               QStringLiteral("release outside the stage did not detach")))
        return 1;
    if (!check(monitor->isWindow(),
               QStringLiteral("detached monitor is not a window")))
        return 1;

    // Dragging the detached title bar must keep it a window: the platform
    // drag is only requested for an exposed handle, and the fallback must not
    // re-dock it either.
    {
        const QPoint detachedPress = monitor->mapToGlobal(
            QPoint(monitor->width() / 2, 10));
        const QPoint detachedTarget = detachedPress + QPoint(40, 30);
        const auto sendTo = [monitor](QEvent::Type type, const QPoint& global,
                                     Qt::MouseButton button,
                                     Qt::MouseButtons buttons) {
            const QPoint local = monitor->mapFromGlobal(global);
            QMouseEvent event(type, local, global, button, buttons,
                              Qt::NoModifier);
            QApplication::sendEvent(monitor, &event);
        };
        sendTo(QEvent::MouseButtonPress, detachedPress, Qt::LeftButton,
               Qt::LeftButton);
        sendTo(QEvent::MouseMove, detachedTarget, Qt::NoButton, Qt::LeftButton);
        sendTo(QEvent::MouseButtonRelease, detachedTarget, Qt::LeftButton,
               Qt::NoButton);
        QApplication::processEvents();
        if (!check(monitor->isDetached(),
                   QStringLiteral("detached monitor re-docked while dragging")))
            return 1;
    }

    // Docking again restores the stage-bound child.
    monitor->setVisibleFromMenu(true);
    QApplication::processEvents();
    if (!check(!monitor->isDetached(),
               QStringLiteral("monitor did not dock again")))
        return 1;

    window.close();
    QApplication::processEvents();

    if (failures == 0)
        qInfo() << "camera-monitor-drag-test OK: docked while dragging, "
                   "detached on release";
    return failures == 0 ? 0 : 1;
}

} // namespace

void registerCameraMonitorDragTests(Registry& registry)
{
    registry.add(QStringLiteral("--camera-monitor-drag-test"),
                 [](QApplication& app) { return cameraMonitorDragTest(app); });
}

} // namespace pvf::tests
