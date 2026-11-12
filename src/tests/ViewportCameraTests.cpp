#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

int viewportTest(QApplication& app, const QString& projectDir)
{
    Q_UNUSED(app);
    QString error;
    auto session = pvf::EditorSession::openProject(projectDir, &error);
    if (!session)
    {
        qCritical() << "viewport-test: open failed" << projectDir << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* sessionPtr = window->editorSessionForTest();
    auto* viewport = window->viewportForTest();
    if (!sessionPtr || !sessionPtr->document() || !viewport)
    {
        qCritical() << "viewport-test: window has no session viewport";
        return 1;
    }
    if (sessionPtr->playhead() != 0)
    {
        qCritical() << "viewport-test: open did not start at frame 0"
                    << sessionPtr->playhead();
        return 1;
    }
    if (viewport->grabProjectFrame().isNull())
    {
        qCritical() << "viewport-test: frame 0 render is empty";
        return 1;
    }

    auto waitFor = [](const std::function<bool()>& predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs)
        {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
        }
        return predicate();
    };

    sessionPtr->setPlayhead(30);
    QApplication::processEvents();

    auto* media = sessionPtr->media();
    if (media)
    {
        media->prepareHtmlPreview(3000);
        waitFor([media] { return !media->htmlPending(); }, 5000);
    }
    viewport->refreshSession();
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();

    const QImage grab = viewport->grab().toImage();
    if (grab.isNull() || grab.width() < 100 || grab.height() < 100)
    {
        qCritical() << "viewport-test: viewport grab is empty" << grab.size();
        return 1;
    }

    // The HTML layer is a solid #ff3b30 rectangle at project (0,0)-(320,150)
    // (the clip starts above the canvas).  A camera view maps it into the
    // upper-left quadrant of the viewport.
    int redPixels = 0;
    for (int y = 0; y < grab.height(); y += 2)
    {
        for (int x = 0; x < grab.width(); x += 2)
        {
            const QColor color = grab.pixelColor(x, y);
            if (color.red() > 180 && color.green() < 100 && color.blue() < 100)
                ++redPixels;
        }
    }
    if (redPixels < 200)
    {
        qCritical() << "viewport-test: HTML red area missing; red pixels"
                    << redPixels
                    << "htmlStartFailed" << (media && media->htmlStartFailed())
                    << (media ? media->htmlError() : QString());
        return 1;
    }

    // Sample a point that only the HTML layer covers: project (40,40) is inside
    // the 320x180 HTML frame, left of the video and image entities.
    const QPoint htmlPoint =
        viewport->projectToWidgetForTest(QPointF(40.0, 40.0)).toPoint();
    if (!grab.rect().contains(htmlPoint))
    {
        qCritical() << "viewport-test: HTML sample point outside viewport"
                    << htmlPoint;
        return 1;
    }
    const QColor htmlColor = grab.pixelColor(htmlPoint);
    if (htmlColor.red() < 180 || htmlColor.green() > 100
        || htmlColor.blue() > 100)
    {
        qCritical() << "viewport-test: HTML pixel is not red" << htmlPoint
                    << htmlColor;
        return 1;
    }

    // The title text sits on the canvas centre at frame 30.
    bool centreLit = false;
    const QPoint centre(grab.width() / 2, grab.height() / 2);
    for (int dy = -8; dy <= 8 && !centreLit; ++dy)
    {
        for (int dx = -8; dx <= 8; ++dx)
        {
            const QColor color = grab.pixelColor(centre.x() + dx, centre.y() + dy);
            if (color.lightness() > 150)
            {
                centreLit = true;
                break;
            }
        }
    }
    if (!centreLit)
    {
        qCritical() << "viewport-test: canvas centre has no rendered pixels";
        return 1;
    }

    auto* workbench = sessionPtr->workbench();
    const QString cacheDir = workbench ? workbench->cacheDirectory() : QString();
    if (cacheDir.isEmpty() || !QFileInfo(cacheDir).isDir())
    {
        qCritical() << "viewport-test: workbench cache directory missing" << cacheDir;
        return 1;
    }
    const pvf::workbench::CacheStats cacheStats =
        workbench ? workbench->stats() : pvf::workbench::CacheStats();
    if (cacheStats.entries <= 0)
    {
        qCritical() << "viewport-test: workbench cache has no entries" << cacheDir;
        return 1;
    }

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-p3a-viewport.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "viewport-test: screenshot failed" << screenshot;
        return 1;
    }

    // Compare the same frame with preview.render_frame (same render2 call and
    // the same MediaServices providers).
    pvf::mcp2::Service service(sessionPtr->document()->store(),
                               &sessionPtr->assets());
    if (media)
    {
        service.setVideoFrameProvider([media](const pvf::Uuid& uuid, double frame) {
            return media->videoFrame(uuid, frame);
        });
        service.setHtmlFrameProvider([media](const pvf::Uuid& uuid, double frame) {
            return media->htmlFrame(uuid, frame);
        });
    }
    QJsonObject args;
    args.insert(QStringLiteral("frame"), 30);
    const QJsonObject previewResult =
        service.execute(QStringLiteral("preview.render_frame"), args);
    if (!previewResult.value(QStringLiteral("ok")).toBool())
    {
        qCritical() << "viewport-test: preview.render_frame failed"
                    << previewResult;
        return 1;
    }
    const QString previewPath =
        previewResult.value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("path"))
            .toString();
    const QImage preview(previewPath);
    const QImage viewportFrame = viewport->grabProjectFrame();
    if (preview.isNull() || viewportFrame.isNull()
        || preview.size() != viewportFrame.size())
    {
        qCritical() << "viewport-test: preview/viewport size mismatch"
                    << preview.size() << viewportFrame.size() << previewPath;
        return 1;
    }
    QFile::remove(QStringLiteral("/tmp/opencode/pvforge-p3a-preview.png"));
    QFile::copy(previewPath,
                QStringLiteral("/tmp/opencode/pvforge-p3a-preview.png"));

    double colourDiff = 0.0;
    int samples = 0;
    for (int y = 0; y < preview.height(); y += 4)
    {
        for (int x = 0; x < preview.width(); x += 4)
        {
            const QColor a = preview.pixelColor(x, y);
            const QColor b = viewportFrame.pixelColor(x, y);
            colourDiff += std::abs(a.red() - b.red())
                          + std::abs(a.green() - b.green())
                          + std::abs(a.blue() - b.blue());
            ++samples;
        }
    }
    colourDiff /= std::max(1, samples);
    if (colourDiff > 12.0)
    {
        qCritical() << "viewport-test: preview colour mismatch" << colourDiff
                    << previewPath;
        return 1;
    }

    // Playback: the frame clock must advance the playhead by ~30 frames.
    auto* playback = sessionPtr->playback();
    if (!playback)
    {
        qCritical() << "viewport-test: no playback controller";
        return 1;
    }
    sessionPtr->setPlayhead(30);
    const int playStart = sessionPtr->playhead();
    playback->play();
    const bool advanced =
        waitFor([sessionPtr, playStart] {
            return sessionPtr->playhead() >= playStart + 30;
        }, 6000);
    playback->pause();
    const int playEnd = sessionPtr->playhead();
    if (!advanced || playEnd <= playStart)
    {
        qCritical() << "viewport-test: playback did not advance" << playStart
                    << playEnd;
        return 1;
    }

    // The old camera selection rule: selecting a camera entity switches
    // render2's cameraUuid to it; clearing the selection returns to canvas.
    pvf::Uuid cameraUuid;
    for (auto it = sessionPtr->project().entities.constBegin();
         it != sessionPtr->project().entities.constEnd(); ++it)
    {
        if (it->content.kind == pvf::ContentKind::Camera)
        {
            cameraUuid = it->uuid;
            break;
        }
    }
    if (!cameraUuid.isEmpty())
    {
        sessionPtr->document()->setSelection(cameraUuid);
        QApplication::processEvents();
        if (sessionPtr->activeCameraUuid() != cameraUuid
            || viewport->grabProjectFrame(cameraUuid).isNull())
        {
            qCritical() << "viewport-test: camera selection did not reach render2";
            return 1;
        }
        sessionPtr->document()->setSelection(pvf::Uuid());
        QApplication::processEvents();
        if (!sessionPtr->activeCameraUuid().isEmpty())
        {
            qCritical() << "viewport-test: canvas selection did not clear camera";
            return 1;
        }
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "viewport-test OK: frame 30 red pixels" << redPixels
            << "cache entries" << cacheStats.entries << "preview diff"
            << colourDiff << "playback" << playStart << "->" << playEnd
            << "screenshot" << screenshot;
    return 0;
}

int snapTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "snap-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        const double canvasHalfW = project->width / 2.0;
        const double canvasHalfH = project->height / 2.0;

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, mods);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mods) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
        };

        // X axis snaps to the centre line.
        drag(center + QPoint(40, 0), center + QPoint(42, 0), Qt::NoModifier);
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "snap-test: centre snap"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        // X axis snaps the entity edge flush with the canvas edge.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        const double targetX = canvasHalfW - 150.0 - 5.0;   // right edge 5px short
        const int edgeDx = int(std::lround(targetX * s));
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0), Qt::NoModifier);
        const double expectedEdgeX = canvasHalfW - 150.0;   // entity half width
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - expectedEdgeX) > 1e-6)
        {
            qCritical() << "snap-test: edge snap"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        // Ctrl disables snapping.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        drag(center + QPoint(40, 0), center + QPoint(42, 0), Qt::ControlModifier);
        const double unsnapped = project->value(id, QStringLiteral("x"), 0);
        if (std::abs(unsnapped) < 1.0)
        {
            qCritical() << "snap-test: ctrl still snapped";
            ++failures;
        }

        // Free drag snaps to the bottom-right canvas corner.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        const int cornerDx = int(std::lround((canvasHalfW - 150.0 - 5.0) * s));
        const int cornerDy = int(std::lround((canvasHalfH - 100.0 - 5.0) * s));
        drag(center, center + QPoint(cornerDx, cornerDy), Qt::NoModifier);
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - (canvasHalfW - 150.0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("y"), 0) - (canvasHalfH - 100.0))
                   > 1e-6)
        {
            qCritical() << "snap-test: corner snap"
                        << project->value(id, QStringLiteral("x"), 0)
                        << project->value(id, QStringLiteral("y"), 0);
            ++failures;
        }

        // Snapping still works with a custom pivot: the visible body snaps.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        window.applyPivotForTest(id, 100.0, 0.0);   // x becomes 100, body stays
        const QPoint pivotPoint = viewport->entityPivotForTest(id).toPoint();
        const int moveDx = int(std::lround((canvasHalfW - 150.0 - 5.0) * s));
        drag(pivotPoint + QPoint(30, 0), pivotPoint + QPoint(30 + moveDx, 0),
             Qt::NoModifier);
        const double expectedPivotSnap = canvasHalfW - 150.0 + 100.0;
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - expectedPivotSnap)
            > 2.0)
        {
            qCritical() << "snap-test: pivot offset snap"
                        << project->value(id, QStringLiteral("x"), 0) << expectedPivotSnap;
            ++failures;
        }
        window.applyPivotForTest(id, 0.0, 0.0);

        // Rotated entities snap using their rotated bounds (90° swaps w/h).
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("rotation"), 0, 90.0, false);
        const int rotatedDx =
            int(std::lround((canvasHalfW - 100.0 - 5.0) * s));   // half height now
        drag(center + QPoint(40, 0), center + QPoint(40 + rotatedDx, 0), Qt::NoModifier);
        const double rotatedExpected = canvasHalfW - 100.0;
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - rotatedExpected) > 1.5)
        {
            qCritical() << "snap-test: rotated edge snap"
                        << project->value(id, QStringLiteral("x"), 0) << rotatedExpected;
            ++failures;
        }
        project->setValue(id, QStringLiteral("rotation"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);

        // Scaling the bottom-right corner snaps the edge to the canvas edge.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);
        const QPoint corner(center.x() + int(std::lround(150.0 * s)),
                            center.y() + int(std::lround(100.0 * s)));
        const QPoint edge(center.x() + int(std::lround((canvasHalfW - 5.0) * s)),
                          center.y() + int(std::lround(100.0 * s)));
        drag(corner, edge, Qt::NoModifier);
        const double scaleX = project->value(id, QStringLiteral("scaleX"), 0);
        const double expected = canvasHalfW / 150.0;
        if (std::abs(scaleX - expected) > 0.05)
        {
            qCritical() << "snap-test: scale snap" << scaleX << expected;
            ++failures;
        }

        qInfo() << "snap-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int scaleHandleTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "scale-handle-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        const int halfW = int(std::lround(150.0 * s));
        const int halfH = int(std::lround(100.0 * s));
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_handles.png"));

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, modifiers);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mods) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
        };

        auto cornerNow = [&] {
            return viewport->entityPivotForTest(id).toPoint()
                   + QPoint(int(std::lround(150.0 * s)), int(std::lround(100.0 * s)));
        };

        // Corner handle scales both axes independently.
        drag(cornerNow(), center + QPoint(2 * halfW, 3 * halfH / 2), Qt::NoModifier);
        const double sx1 = project->value(id, QStringLiteral("scaleX"), 0);
        const double sy1 = project->value(id, QStringLiteral("scaleY"), 0);
        if (std::abs(sx1 - 2.0) > 0.1 || std::abs(sy1 - 1.5) > 0.1)
        {
            qCritical() << "scale-handle-test: corner handle" << sx1 << sy1;
            ++failures;
        }

        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);

        // Shift keeps the aspect ratio. Use the current corner as the target
        // base so the drag distance matches the new centre.
        const QPoint shiftCorner = cornerNow();
        drag(shiftCorner, shiftCorner + QPoint(int(std::lround(150.0 * s)),
                                               int(std::lround(50.0 * s))),
             Qt::ShiftModifier);
        const double sx2 = project->value(id, QStringLiteral("scaleX"), 0);
        const double sy2 = project->value(id, QStringLiteral("scaleY"), 0);
        if (std::abs(sx2 - 2.0) > 0.1 || std::abs(sy2 - 2.0) > 0.1)
        {
            qCritical() << "scale-handle-test: shift uniform" << sx2 << sy2;
            ++failures;
        }

        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);

        // Dragging past the centre mirrors the entity.
        drag(cornerNow(), center - QPoint(halfW, halfH), Qt::NoModifier);
        const double sx3 = project->value(id, QStringLiteral("scaleX"), 0);
        if (sx3 >= -0.5)
        {
            qCritical() << "scale-handle-test: mirror" << sx3;
            ++failures;
        }

        // A 16:9 entity in a 16:9 project: Shift + corner must snap the
        // visual bounds to the canvas edges and fill it from the centre.
        pvf::Entity* videoLike =
            project->addVector(0, 320, 180, Qt::blue, Qt::white, 0);
        const pvf::EntityId videoId = videoLike->id;
        window.setSelectedEntityForTest(videoId);
        const QPoint videoCenter = viewport->entityPivotForTest(videoId).toPoint();
        const QPoint videoCorner(
            videoCenter.x() + int(std::lround(160.0 * s)),
            videoCenter.y() + int(std::lround(90.0 * s)));
        const QPoint canvasCorner(
            videoCenter.x() + int(std::lround(960.0 * s)) - 3,
            videoCenter.y() + int(std::lround(540.0 * s)) - 3);
        drag(videoCorner, canvasCorner, Qt::ShiftModifier);
        double fillX = project->value(videoId, QStringLiteral("scaleX"), 0);
        double fillY = project->value(videoId, QStringLiteral("scaleY"), 0);
        if (std::abs(fillX - 6.0) > 0.05 || std::abs(fillY - 6.0) > 0.05)
        {
            qCritical() << "scale-handle-test: fullscreen snap" << fillX << fillY;
            ++failures;
        }

        // The same visual-AABB snap has to work at special rotation angles.
        project->setValue(videoId, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(videoId, QStringLiteral("scaleY"), 0, 1.0, false);
        project->setValue(videoId, QStringLiteral("rotation"), 0, 180.0, false);
        const QPoint rotatedCorner(
            videoCenter.x() - int(std::lround(160.0 * s)),
            videoCenter.y() - int(std::lround(90.0 * s)));
        const QPoint rotatedTarget(
            videoCenter.x() - int(std::lround(960.0 * s)) + 3,
            videoCenter.y() - int(std::lround(540.0 * s)) + 3);
        drag(rotatedCorner, rotatedTarget, Qt::ShiftModifier);
        fillX = project->value(videoId, QStringLiteral("scaleX"), 0);
        fillY = project->value(videoId, QStringLiteral("scaleY"), 0);
        if (std::abs(fillX - 6.0) > 0.05 || std::abs(fillY - 6.0) > 0.05)
        {
            qCritical() << "scale-handle-test: rotated fullscreen snap" << fillX
                        << fillY;
            ++failures;
        }
        project->setValue(videoId, QStringLiteral("rotation"), 0, 0.0, false);

        qInfo() << "scale-handle-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int scaleUndoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "scale-undo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPoint center = viewport->entityPivotForTest(id).toPoint();
        const QPoint corner(center.x() + int(std::lround(150.0 * s)),
                            center.y() + int(std::lround(100.0 * s)));

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };

        send(QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i)
            send(QEvent::MouseMove,
                 corner + QPoint(int(std::lround(15.0 * s * i)),
                                 int(std::lround(10.0 * s * i))),
                 Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease,
             corner + QPoint(int(std::lround(120.0 * s)), int(std::lround(80.0 * s))),
             Qt::LeftButton, Qt::NoButton);

        const double endSx = project->value(id, QStringLiteral("scaleX"), 0);
        const double endSy = project->value(id, QStringLiteral("scaleY"), 0);
        if (endSx <= 1.5 || endSy <= 1.3)
        {
            qCritical() << "scale-undo-test: scaling did nothing" << endSx << endSy;
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(id, QStringLiteral("scaleX"), 0) - 1.0) > 1e-6
            || std::abs(project->value(id, QStringLiteral("scaleY"), 0) - 1.0) > 1e-6)
        {
            qCritical() << "scale-undo-test: one undo did not restore scale"
                        << project->value(id, QStringLiteral("scaleX"), 0)
                        << project->value(id, QStringLiteral("scaleY"), 0);
            ++failures;
        }

        // Pivot drags merge into one undo step too.
        window.applyPivotForTest(id, 50.0, 0.0);
        window.applyPivotForTest(id, 100.0, 0.0);
        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(id, QStringLiteral("rotPointX"), 0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "scale-undo-test: pivot undo failed"
                        << project->value(id, QStringLiteral("rotPointX"), 0)
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        qInfo() << "scale-undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int easingTest(QApplication& app)
{
    int failures = 0;
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };

    pvf::EasingSpec linear;
    if (!near(pvf::applyEasing(linear, 0.25), 0.25))
    {
        qCritical() << "easing-test: linear";
        ++failures;
    }

    pvf::EasingSpec instant;
    instant.easing = pvf::Easing::Instant;
    if (!near(pvf::applyEasing(instant, 0.5), 0.0)
        || !near(pvf::applyEasing(instant, 0.999), 0.0)
        || !near(pvf::applyEasing(instant, 1.0), 1.0))
    {
        qCritical() << "easing-test: instant";
        ++failures;
    }

    pvf::EasingSpec inQuad;
    inQuad.easing = pvf::Easing::EaseInQuad;
    pvf::EasingSpec outQuad;
    outQuad.easing = pvf::Easing::EaseOutQuad;
    if (!near(pvf::applyEasing(inQuad, 0.5), 0.25)
        || !near(pvf::applyEasing(outQuad, 0.5), 0.75))
    {
        qCritical() << "easing-test: quad";
        ++failures;
    }

    pvf::EasingSpec bezier;
    bezier.easing = pvf::Easing::Bezier;
    bezier.inX = 0.0;
    bezier.inY = 0.0;
    bezier.outX = 1.0;
    bezier.outY = 1.0;
    if (!near(pvf::applyEasing(bezier, 0.5), 0.5))
    {
        qCritical() << "easing-test: bezier linear handles";
        ++failures;
    }

    pvf::EasingSpec back;
    back.easing = pvf::Easing::EaseOutBack;
    pvf::EasingSpec backIn;
    backIn.easing = pvf::Easing::EaseInBack;
    if (pvf::applyEasing(back, 0.8) <= 1.0 || pvf::applyEasing(backIn, 0.2) >= 0.0)
    {
        qCritical() << "easing-test: overshoot functions are clamped";
        ++failures;
    }

    auto* project = new pvf::Project;
    pvf::Entity* entity = project->addVector(0, 200, 200, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    if (!near(project->value(id, QStringLiteral("x"), 5), 50.0))
    {
        qCritical() << "easing-test: linear track";
        ++failures;
    }
    if (!near(project->previousKeyframeFrame(id, 10), 0.0)
        || project->previousKeyframeFrame(id, 0) >= 0.0)
    {
        qCritical() << "easing-test: previous keyframe lookup";
        ++failures;
    }

    // The first keyframe has no incoming segment, so its ease is ignored.
    project->setKeyframeEasing(id, QStringLiteral("x"), 0, back);
    if (!near(project->value(id, QStringLiteral("x"), 5), 50.0))
    {
        qCritical() << "easing-test: first key easing leaked into the segment";
        ++failures;
    }

    project->setKeyframeEasing(id, QStringLiteral("x"), 10, inQuad);
    if (!near(project->value(id, QStringLiteral("x"), 5), 25.0))
    {
        qCritical() << "easing-test: eased track"
                    << project->value(id, QStringLiteral("x"), 5);
        ++failures;
    }

    project->setKeyframeEasing(id, QStringLiteral("x"), 10, instant);
    if (!near(project->value(id, QStringLiteral("x"), 5), 0.0)
        || !near(project->value(id, QStringLiteral("x"), 10), 100.0))
    {
        qCritical() << "easing-test: instant track";
        ++failures;
    }

    project->setKeyframeEasing(id, QStringLiteral("x"), 10, back);
    if (project->value(id, QStringLiteral("x"), 8) <= 100.0)
    {
        qCritical() << "easing-test: track clamped overshoot"
                    << project->value(id, QStringLiteral("x"), 8);
        ++failures;
    }

    pvf::EasingSpec custom;
    custom.easing = pvf::Easing::Bezier;
    custom.inX = 0.25;
    custom.inY = 0.0;
    custom.outX = 0.75;
    custom.outY = 1.0;
    project->setKeyframeEasing(id, QStringLiteral("x"), 10, custom);

    // setKey updates the value without dropping the easing.
    project->setValue(id, QStringLiteral("x"), 10, 200.0, true);
    const pvf::Keyframe* key = project->keyframe(id, QStringLiteral("x"), 10);
    if (!key || key->ease != custom)
    {
        qCritical() << "easing-test: setKey dropped easing";
        ++failures;
    }

    const QString path = QStringLiteral("/tmp/pvforge_easing.pvproj");
    QString error;
    if (!project->save(path, &error))
    {
        qCritical() << "easing-test: save failed" << error;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(path, &error))
        {
            qCritical() << "easing-test: load failed" << error;
            ++failures;
        }
        else
        {
            const pvf::Keyframe* loadedKey = loaded.keyframe(id, QStringLiteral("x"), 10);
            if (!loadedKey || loadedKey->ease != custom)
            {
                qCritical() << "easing-test: easing not serialized";
                ++failures;
            }
        }
    }

    // The custom bezier dialog must build and paint without a project.
    {
        pvf::BezierEditorDialog dialog(custom);
        dialog.show();
        QApplication::processEvents();
        const QImage dialogShot = dialog.grab().toImage();
        dialogShot.save(QStringLiteral("/tmp/pvforge_bezier.png"));
        if (dialogShot.isNull() || dialogShot.width() < 400)
        {
            qCritical() << "easing-test: bezier dialog failed";
            ++failures;
        }
        dialog.close();
    }

    // The visual template picker must build and paint.
    {
        pvf::EasingPicker picker(custom);
        picker.show();
        QApplication::processEvents();
        const QImage pickerShot = picker.grab().toImage();
        pickerShot.save(QStringLiteral("/tmp/pvforge_easing_picker.png"));
        if (pickerShot.isNull() || pickerShot.width() < 200)
        {
            qCritical() << "easing-test: easing picker failed";
            ++failures;
        }
        picker.close();
    }

    // Embedding the picker in a menu (the timeline popup) must keep clicks.
    {
        QMenu menu;
        auto* picker = new pvf::EasingPicker(custom);
        auto* action = new QWidgetAction(&menu);
        action->setDefaultWidget(picker);
        menu.addAction(action);
        menu.popup(QPoint(100, 100));
        QApplication::processEvents();
        menu.grab().toImage().save(QStringLiteral("/tmp/pvforge_easing_menu.png"));

        bool picked = false;
        pvf::EasingSpec chosen;
        QObject::connect(picker, &pvf::EasingPicker::picked, &menu,
                         [&picked, &chosen](const pvf::EasingSpec& spec) {
                             picked = true;
                             chosen = spec;
                         });
        const QPoint tile(28, 46);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(tile),
                          QPointF(picker->mapToGlobal(tile)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(tile),
                            QPointF(picker->mapToGlobal(tile)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(picker, &press);
        QApplication::sendEvent(picker, &release);
        if (!picked || chosen.easing != pvf::Easing::Linear)
        {
            qCritical() << "easing-test: menu picker click failed";
            ++failures;
        }
        menu.close();
    }

    // Check the palette-driven drawing in a dark theme as well.
    {
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(35, 38, 41));
        dark.setColor(QPalette::WindowText, QColor(239, 240, 241));
        dark.setColor(QPalette::Base, QColor(27, 30, 32));
        dark.setColor(QPalette::AlternateBase, QColor(42, 46, 50));
        dark.setColor(QPalette::Text, QColor(239, 240, 241));
        dark.setColor(QPalette::PlaceholderText, QColor(160, 165, 170));
        dark.setColor(QPalette::Mid, QColor(90, 95, 100));
        dark.setColor(QPalette::Highlight, QColor(61, 174, 233));
        dark.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        dark.setColor(QPalette::Button, QColor(42, 46, 50));
        dark.setColor(QPalette::ButtonText, QColor(239, 240, 241));
        QApplication::setPalette(dark);

        pvf::EasingPicker picker(custom);
        picker.show();
        QApplication::processEvents();
        picker.grab().toImage().save(
            QStringLiteral("/tmp/pvforge_easing_picker_dark.png"));
        picker.close();

        pvf::BezierEditorDialog dialog(custom);
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_bezier_dark.png"));
        dialog.close();
    }

    // Easing changes go through the command stack.
    pvf::EasingSpec oldEase = custom;
    pvf::EasingSpec newEase;
    newEase.easing = pvf::Easing::EaseOutBack;
    pvf::KeyframeEasingRecord record;
    record.entity = id;
    record.property = QStringLiteral("x");
    record.frame = 10.0;
    record.oldEase = oldEase;
    record.newEase = newEase;
    QVector<pvf::KeyframeEasingRecord> records;
    records.append(record);
    project->setKeyframeEasing(id, QStringLiteral("x"), 10, newEase);
    pvf::History history;
    history.push(std::make_unique<pvf::SetKeyframeEasingCommand>(records), *project);
    history.undo(*project);
    const pvf::Keyframe* undone = project->keyframe(id, QStringLiteral("x"), 10);
    if (!undone || undone->ease != oldEase)
    {
        qCritical() << "easing-test: undo";
        ++failures;
    }
    history.redo(*project);
    const pvf::Keyframe* redone = project->keyframe(id, QStringLiteral("x"), 10);
    if (!redone || redone->ease != newEase)
    {
        qCritical() << "easing-test: redo";
        ++failures;
    }

    // The entity properties panel exposes the curve for the current frame.
    {
        auto* windowProject = new pvf::Project;
        pvf::MainWindow window(windowProject);
        pvf::Entity* entity = windowProject->addVector(0, 200, 200, Qt::red, Qt::white, 0);
        windowProject->setValue(entity->id, QStringLiteral("x"), 0, 0.0, true);
        window.goToFrame(0);
        window.setSelectedEntityForTest(entity->id);
        auto* button =
            window.findChild<QPushButton*>(QStringLiteral("keyframeEasingButton"));
        if (!button || button->isEnabled()
            || !button->text().contains(QStringLiteral("首帧")))
        {
            qCritical() << "easing-test: first keyframe must not own a curve"
                        << (button ? button->text() : QStringLiteral("missing"));
            ++failures;
        }
        windowProject->setValue(entity->id, QStringLiteral("x"), 10, 100.0, true);
        window.goToFrame(10);
        if (!button || !button->isEnabled()
            || !button->text().contains(QStringLiteral("→")))
        {
            qCritical() << "easing-test: properties easing button"
                        << (button ? button->text() : QStringLiteral("missing"));
            ++failures;
        }
    }

    qInfo() << "easing-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int cameraTest(QApplication& app)
{
    pvf::Settings::setMonitorVisible(true);
    pvf::Settings::setShowCameraFrame(true);
    auto* project = new pvf::Project;
    project->width = 800;
    project->height = 600;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(500, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        auto* monitor = window.findChild<pvf::CameraMonitor*>();
        if (!viewport || !monitor)
        {
            qCritical() << "camera-test: viewport or monitor missing";
            app.exit(1);
            return;
        }

        auto* red = project->addVector(0, 200, 200, Qt::red, Qt::white, 0);
        auto* green = project->addVector(1, 100, 100, Qt::green, Qt::white, 0);
        const pvf::EntityId redId = red->id;
        const pvf::EntityId greenId = green->id;
        project->setValue(greenId, QStringLiteral("x"), 0, 300.0, false);
        pvf::Entity* camera = project->addCamera(QStringLiteral("Camera 1"));
        const pvf::EntityId cameraId = camera->id;
        window.setSelectedEntityForTest(cameraId);

        if (project->activeCamera != cameraId || project->monitorView != cameraId)
        {
            qCritical() << "camera-test: camera not activated on selection";
            ++failures;
        }
        if (!monitor->isVisible())
        {
            qCritical() << "camera-test: monitor not visible";
            ++failures;
        }
        auto* fpsLabel = monitor->findChild<QLabel*>(QStringLiteral("monitorFpsLabel"));
        if (!fpsLabel || !fpsLabel->text().contains(QStringLiteral("FPS")))
        {
            qCritical() << "camera-test: monitor fps label missing"
                        << (fpsLabel ? fpsLabel->text() : QStringLiteral("null"));
            ++failures;
        }
        if (viewport->cameraFrameForTest().isEmpty())
        {
            qCritical() << "camera-test: viewfinder frame not drawn";
            ++failures;
        }

        // The View menu can hide the viewfinder without changing the camera.
        if (auto* frameAction =
                window.findChild<QAction*>(QStringLiteral("cameraFrameAction")))
        {
            frameAction->trigger();
            if (!viewport->cameraFrameForTest().isEmpty())
            {
                qCritical() << "camera-test: viewfinder did not hide";
                ++failures;
            }
            frameAction->trigger();
            if (viewport->cameraFrameForTest().isEmpty())
            {
                qCritical() << "camera-test: viewfinder did not return";
                ++failures;
            }
        }

        // The camera at 100% shows the whole canvas; the monitor centre must
        // show the red rectangle, not a guide or a dimmed pixel.
        QApplication::processEvents();
        QImage shot = monitor->grab().toImage();
        const int title = 28;
        const QColor center = shot.pixelColor(shot.width() / 2, title + 90);
        if (center.red() < 200)
        {
            qCritical() << "camera-test: monitor centre is not the scene" << center;
            ++failures;
        }
        window.grab().save(QStringLiteral("/tmp/pvforge_camera.png"));

        // Outside the canvas is black: rotate the full-canvas camera 45°.
        window.applyCameraForTest(cameraId, 0, 0, 1.0, 45.0);
        QApplication::processEvents();
        window.grab().save(QStringLiteral("/tmp/pvforge_camera_rotated.png"));
        shot = monitor->grab().toImage();
        const QColor corner = shot.pixelColor(44, title + 4);
        if (corner.red() > 24 || corner.green() > 24 || corner.blue() > 24)
        {
            qCritical() << "camera-test: outside canvas is not black" << corner;
            ++failures;
        }

        // Back to a 50% frame: the green entity outside is dimmed, the red
        // inside stays bright.
        window.applyCameraForTest(cameraId, 0, 0, 0.5, 0.0);
        window.setSelectedEntityForTest(cameraId);
        QApplication::processEvents();
        // The floating monitor can cover the entity being sampled; hide it
        // while grabbing the viewport so the dim check only sees the scene.
        const bool monitorShown = monitor->isVisible();
        monitor->setVisible(false);
        QApplication::processEvents();
        const QImage viewportShot = viewport->grab().toImage();
        monitor->setVisible(monitorShown);
        QApplication::processEvents();
        viewportShot.save(QStringLiteral("/tmp/pvforge_camera_dim.png"));
        const QPoint greenPoint = viewport->entityPivotForTest(greenId).toPoint();
        const QColor dimmed = viewportShot.pixelColor(greenPoint);
        if (dimmed.green() < 40 || dimmed.green() >= 200)
        {
            qCritical() << "camera-test: outside camera not dimmed" << dimmed;
            ++failures;
        }
        const QPoint redPoint = viewport->entityPivotForTest(redId).toPoint();
        const QColor clear = viewportShot.pixelColor(redPoint);
        if (clear.red() < 200)
        {
            qCritical() << "camera-test: inside camera dimmed" << clear;
            ++failures;
        }

        // Letterbox bars outside the project aspect stay pure black.
        shot = monitor->grab().toImage();
        const QColor bar = shot.pixelColor(10, title + 90);
        if (bar.red() > 24 || bar.green() > 24 || bar.blue() > 24)
        {
            qCritical() << "camera-test: letterbox not black" << bar;
            ++failures;
        }

        auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                             Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)),
                              button, buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };

        // Clicking inside the frame passes through to the entity.
        window.setSelectedEntityForTest(cameraId);
        sendMouse(QEvent::MouseButtonPress, redPoint, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, redPoint, Qt::LeftButton, Qt::NoButton);
        if (window.selectedEntityForTest() != redId)
        {
            qCritical() << "camera-test: inside click did not pass through"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Drag the top border to move the camera, then undo once.
        QPolygonF polygon = viewport->cameraFrameForTest();
        QPoint topMiddle = ((polygon[0] + polygon[1]) / 2.0).toPoint();
        window.setSelectedEntityForTest(cameraId);
        sendMouse(QEvent::MouseButtonPress, topMiddle, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, topMiddle + QPoint(40, 30), Qt::NoButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, topMiddle + QPoint(40, 30), Qt::LeftButton,
                  Qt::NoButton);
        QApplication::processEvents();
        const double movedX = project->value(cameraId, QStringLiteral("x"), 0);
        const double movedY = project->value(cameraId, QStringLiteral("y"), 0);
        if (std::abs(movedX) < 1.0 && std::abs(movedY) < 1.0)
        {
            qCritical() << "camera-test: border drag did not move camera" << movedX
                        << movedY;
            ++failures;
        }
        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(cameraId, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "camera-test: camera move was not one undo step"
                        << project->value(cameraId, QStringLiteral("x"), 0);
            ++failures;
        }

        // Corner drag zooms the camera, keeping the centre fixed.
        window.setSelectedEntityForTest(cameraId);
        polygon = viewport->cameraFrameForTest();
        const QPoint cornerPoint = polygon[1].toPoint();
        sendMouse(QEvent::MouseButtonPress, cornerPoint, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, cornerPoint + QPoint(30, 20), Qt::NoButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, cornerPoint + QPoint(30, 20), Qt::LeftButton,
                  Qt::NoButton);
        const double zoomed = project->value(cameraId, QStringLiteral("scaleX"), 0);
        if (zoomed <= 0.5 || zoomed > pvf::Camera::kMaxScale)
        {
            qCritical() << "camera-test: corner drag did not zoom" << zoomed;
            ++failures;
        }

        // Rotation handle turns the camera; the angle snaps to 90°.
        window.applyCameraForTest(cameraId, 0, 0, 0.5, 0.0);
        window.setSelectedEntityForTest(cameraId);
        polygon = viewport->cameraFrameForTest();
        const QPointF topCenter = (polygon[0] + polygon[1]) / 2.0;
        const QPointF rotateHandle = topCenter + QPointF(0, -28);
        const QPointF centerScreen = viewport->entityPivotForTest(cameraId);
        sendMouse(QEvent::MouseButtonPress, rotateHandle.toPoint(), Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseMove, (centerScreen + QPointF(100, 0)).toPoint(),
                  Qt::NoButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, (centerScreen + QPointF(100, 0)).toPoint(),
                  Qt::LeftButton, Qt::NoButton);
        const double rotation = project->value(cameraId, QStringLiteral("rotation"), 0);
        if (std::abs(std::abs(rotation) - 90.0) > 1.0)
        {
            qCritical() << "camera-test: rotation handle failed" << rotation;
            ++failures;
        }
        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();

        // A 100% frame fills the canvas, so it cannot pan.
        window.applyCameraForTest(cameraId, 0, 0, 1.0, 0.0);
        window.setSelectedEntityForTest(cameraId);
        polygon = viewport->cameraFrameForTest();
        topMiddle = ((polygon[0] + polygon[1]) / 2.0).toPoint();
        sendMouse(QEvent::MouseButtonPress, topMiddle, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, topMiddle + QPoint(40, 30), Qt::NoButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, topMiddle + QPoint(40, 30), Qt::LeftButton,
                  Qt::NoButton);
        if (std::abs(project->value(cameraId, QStringLiteral("x"), 0)) > 1e-6
            || std::abs(project->value(cameraId, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "camera-test: 100% camera panned"
                        << project->value(cameraId, QStringLiteral("x"), 0)
                        << project->value(cameraId, QStringLiteral("y"), 0);
            ++failures;
        }
        // Detach and re-dock through the monitor buttons.
        auto* detachButton =
            monitor->findChild<QToolButton*>(QStringLiteral("monitorDetachButton"));
        auto* qualityButton =
            monitor->findChild<QToolButton*>(QStringLiteral("monitorQualityButton"));
        if (!detachButton || !qualityButton)
        {
            qCritical() << "camera-test: monitor buttons missing";
            ++failures;
        }
        else
        {
            // Dragging the title out of the main window pops the monitor out.
            const QPoint grab(10, 10);
            const QPoint grabGlobal = monitor->mapToGlobal(grab);
            const QPoint outside = window.frameGeometry().topLeft() - QPoint(180, 180);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(grab),
                              QPointF(grabGlobal), Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, QPointF(grab + QPoint(30, 30)),
                             QPointF(outside), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,
                                QPointF(grab + QPoint(30, 30)), QPointF(outside),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(monitor, &press);
            QApplication::sendEvent(monitor, &move);
            QApplication::sendEvent(monitor, &release);
            if (!monitor->isDetached())
            {
                qCritical() << "camera-test: drag out did not detach";
                ++failures;
            }

            detachButton->click();
            if (monitor->isDetached())
            {
                qCritical() << "camera-test: redock failed";
                ++failures;
            }
            detachButton->click();
            if (!monitor->isDetached())
            {
                qCritical() << "camera-test: detach failed";
                ++failures;
            }
            detachButton->click();
            if (monitor->isDetached())
            {
                qCritical() << "camera-test: redock failed";
                ++failures;
            }
            const bool wasHigh = monitor->isHighQuality();
            qualityButton->click();
            if (monitor->isHighQuality() == wasHigh)
            {
                qCritical() << "camera-test: quality toggle failed";
                ++failures;
            }
            qualityButton->click();
        }

        // Save and load keeps the active camera and monitor view.
        const QString path = QStringLiteral("/tmp/pvforge_camera.pvproj");
        QString error;
        if (!project->save(path, &error))
        {
            qCritical() << "camera-test: save failed" << error;
            ++failures;
        }
        else
        {
            pvf::Project loaded;
            if (!loaded.load(path, &error))
            {
                qCritical() << "camera-test: load failed" << error;
                ++failures;
            }
            else if (loaded.activeCamera != cameraId || loaded.monitorView != cameraId)
            {
                qCritical() << "camera-test: camera state not saved"
                            << loaded.activeCamera << loaded.monitorView;
                ++failures;
            }
        }

        // Quality toggle keeps rendering the scene.
        if (qualityButton)
        {
            const bool wasHigh = monitor->isHighQuality();
            qualityButton->click();
            QApplication::processEvents();
            QImage highShot = monitor->grab().toImage();
            const QColor highCenter =
                highShot.pixelColor(highShot.width() / 2, title + 90);
            if (monitor->isHighQuality() == wasHigh || highCenter.red() < 150)
            {
                qCritical() << "camera-test: high quality render failed" << highCenter;
                ++failures;
            }
            qualityButton->click();
        }

        // Close and reopen the monitor from the View menu.
        if (auto* monitorAction = window.findChild<QAction*>(QStringLiteral("monitorAction")))
        {
            monitorAction->trigger();
            if (monitor->isVisible())
            {
                qCritical() << "camera-test: monitor close failed";
                ++failures;
            }
            monitorAction->trigger();
            if (!monitor->isVisible())
            {
                qCritical() << "camera-test: monitor reopen failed";
                ++failures;
            }
        }

        // Workbench camera page creates and activates a camera.
        auto* workbenchButton =
            window.findChild<QToolButton*>(QStringLiteral("workbenchButton"));
        auto* workbench = window.findChild<pvf::WorkbenchPanel*>();
        if (workbenchButton && workbench)
        {
            workbenchButton->click();
            auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
            auto* nameEdit =
                workbench->findChild<QLineEdit*>(QStringLiteral("cameraName"));
            auto* create =
                workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
            if (!types || !nameEdit || !create)
            {
                qCritical() << "camera-test: workbench camera widgets missing";
                ++failures;
            }
            else
            {
                types->setCurrentRow(5);   // the camera page
                nameEdit->setText(QStringLiteral("UI Camera"));
                create->click();
                QApplication::processEvents();
                pvf::EntityId uiCamera = pvf::kInvalidEntity;
                for (const pvf::Entity& entity : project->entities())
                    if (entity.isCamera() && entity.name == QStringLiteral("UI Camera"))
                        uiCamera = entity.id;
                if (uiCamera == pvf::kInvalidEntity || project->activeCamera != uiCamera
                    || project->monitorView != uiCamera)
                {
                    qCritical() << "camera-test: workbench camera not active" << uiCamera
                                << project->activeCamera << project->monitorView;
                    ++failures;
                }

                // The monitor dropdown switches the active camera and the
                // work/whole-canvas views.
                auto* combo =
                    monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"));
                if (!combo)
                {
                    qCritical() << "camera-test: camera combo missing";
                    ++failures;
                }
                else
                {
                    combo->setCurrentIndex(combo->findData(cameraId));
                    if (project->activeCamera != cameraId
                        || project->monitorView != cameraId)
                    {
                        qCritical() << "camera-test: combo did not activate camera";
                        ++failures;
                    }
                    combo->setCurrentIndex(combo->findData(pvf::kWorkCameraView));
                    if (project->monitorView != pvf::kWorkCameraView)
                    {
                        qCritical() << "camera-test: combo work view failed";
                        ++failures;
                    }
                    combo->setCurrentIndex(combo->findData(pvf::kWholeCanvasView));
                    if (project->monitorView != pvf::kWholeCanvasView)
                    {
                        qCritical() << "camera-test: combo whole view failed";
                        ++failures;
                    }
                    combo->setCurrentIndex(combo->findData(uiCamera));
                }
            }
        }

        // With no cameras left the monitor falls back to the work camera and
        // the viewfinder disappears.
        QVector<pvf::EntityId> cameras;
        for (const pvf::Entity& entity : project->entities())
            if (entity.isCamera())
                cameras.append(entity.id);
        for (pvf::EntityId id : cameras)
            project->removeEntity(id);
        QApplication::processEvents();
        auto* fallbackCombo =
            monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"));
        if (!fallbackCombo
            || fallbackCombo->currentData().toInt() != pvf::kWorkCameraView
            || !viewport->cameraFrameForTest().isEmpty())
        {
            qCritical() << "camera-test: work camera fallback failed";
            ++failures;
        }
        window.grab().save(QStringLiteral("/tmp/pvforge_camera_work.png"));

        qInfo() << "camera-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int monitorFeatureTest(QApplication& app)
{
    const QString tracePath = QStringLiteral("/tmp/pvf_trace.mp4");
    QFile::remove(tracePath);
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=red:s=320x240:d=1:r=10"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=blue:s=320x240:d=1:r=10"),
                  QStringLiteral("-filter_complex"),
                  QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0"),
                  QStringLiteral("-c:v"), QStringLiteral("libx264"),
                  QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                  QStringLiteral("-preset"), QStringLiteral("ultrafast"), tracePath});
    if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0)
    {
        qCritical() << "monitor-feature-test: cannot generate trace video";
        return 1;
    }

    pvf::Settings::setMonitorVisible(true);
    pvf::Settings::setShowCameraFrame(true);
    auto* project = new pvf::Project;
    project->width = 1920;
    project->height = 1080;
    project->fps = 10;
    pvf::Entity* camera = project->addCamera(QStringLiteral("Monitor Camera"));
    const pvf::EntityId cameraId = camera->id;
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 1.0, false);

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();

    auto* monitor = window.findChild<pvf::CameraMonitor*>();
    if (!monitor)
    {
        qCritical() << "monitor-feature-test: monitor missing";
        return 1;
    }
    auto* traceButton =
        monitor->findChild<QToolButton*>(QStringLiteral("monitorTraceButton"));
    int failures = 0;
    if (!traceButton)
    {
        qCritical() << "monitor-feature-test: monitor buttons missing";
        ++failures;
    }
    // Streaming was removed; the monitor must not expose a network button.
    if (monitor->findChild<QToolButton*>(QStringLiteral("monitorStreamButton")))
    {
        qCritical() << "monitor-feature-test: monitor still has a stream button";
        ++failures;
    }

    // Work camera mirrors the viewport but must clip outside-canvas objects.
    {
        pvf::Project clipProject;
        clipProject.width = 800;
        clipProject.height = 600;
        clipProject.background = QColor(20, 40, 60);
        pvf::Entity* outside =
            clipProject.addVector(0, 200, 200, Qt::red, Qt::red, 0);
        clipProject.setValue(outside->id, QStringLiteral("x"), 0, 500.0, false);

        QImage preview(400, 300, QImage::Format_ARGB32_Premultiplied);
        preview.fill(Qt::black);
        {
            QPainter painter(&preview);
            QTransform view;
            view.translate(200, 150);
            view.scale(0.25, 0.25);
            pvf::CameraRenderer::renderWorkCamera(
                painter, clipProject, 0, QRectF(0, 0, 400, 300), view,
                QSize(400, 300), QColor(90, 90, 90), false);
        }
        if (preview.pixelColor(325, 150) != QColor(90, 90, 90)
            || preview.pixelColor(200, 150) != QColor(20, 40, 60))
        {
            qCritical() << "monitor-feature-test: work camera leaked canvas"
                        << preview.pixelColor(325, 150)
                        << preview.pixelColor(200, 150);
            ++failures;
        }

        // A brand wall used to replace the clip with its own rect, so it
        // painted over the monitor title bar and the letterbox area.
        pvf::Project wallProject;
        wallProject.width = 400;
        wallProject.height = 300;
        wallProject.background = QColor(20, 20, 20);
        pvf::BrandWallParams wallParams;
        wallParams.width = 900;
        wallParams.height = 600;
        wallParams.columns = 6;
        wallParams.rows = 4;
        QVariantMap wallData;
        wallParams.writeTo(wallData);
        pvf::Entity* wall = wallProject.addEntity(
            pvf::EntityType::BrandWall, QStringLiteral("wall"), wallData);
        wall->values.insert(QStringLiteral("x"), 0.0);
        wall->values.insert(QStringLiteral("y"), -120.0);

        QImage wallPreview(400, 300, QImage::Format_ARGB32_Premultiplied);
        wallPreview.fill(Qt::black);
        const QRect target(0, 40, 400, 260);
        {
            QPainter painter(&wallPreview);
            QTransform view;
            view.translate(200, 150);
            pvf::CameraRenderer::renderWorkCamera(
                painter, wallProject, 0, QRectF(target), view, QSize(400, 300),
                QColor(0, 0, 0), false);
        }
        int leaked = 0;
        int visible = 0;
        for (int y = 0; y < wallPreview.height(); ++y)
            for (int x = 0; x < wallPreview.width(); ++x)
            {
                const QRgb pixel = wallPreview.pixel(x, y);
                if (!target.contains(x, y))
                {
                    if (pixel != qRgb(0, 0, 0))
                        ++leaked;
                }
                else if (pixel != qRgb(0, 0, 0))
                {
                    ++visible;
                }
            }
        if (leaked != 0 || visible == 0)
        {
            qCritical() << "monitor-feature-test: brand wall clip"
                        << "leaked" << leaked << "visible" << visible;
            ++failures;
        }
    }

    if (!monitor->loadTraceVideo(tracePath))
    {
        qCritical() << "monitor-feature-test: trace import failed";
        ++failures;
    }
    else
    {
        monitor->setTraceOpacity(1.0);
        window.goToFrame(15);
        QApplication::processEvents();
        QImage shot = monitor->grab().toImage();
        QColor color = shot.pixelColor(shot.width() / 2, 28 + (shot.height() - 28) / 2);
        if (color.blue() < 180 || color.red() > 90)
        {
            qCritical() << "monitor-feature-test: trace blue frame" << color;
            ++failures;
        }
        window.goToFrame(2);
        QApplication::processEvents();
        shot = monitor->grab().toImage();
        color = shot.pixelColor(shot.width() / 2, 28 + (shot.height() - 28) / 2);
        if (color.red() < 180 || color.blue() > 90)
        {
            qCritical() << "monitor-feature-test: trace red frame" << color;
            ++failures;
        }
        window.goToFrame(15);
        QApplication::processEvents();
        window.grab().toImage().save(QStringLiteral("/tmp/pvf_monitor_features.png"));

        monitor->clearTraceForTest();
        window.goToFrame(15);
        QApplication::processEvents();
        const QImage cleared = monitor->grab().toImage();
        const QColor clearedColor =
            cleared.pixelColor(cleared.width() / 2,
                               28 + (cleared.height() - 28) / 2);
        if (clearedColor.blue() > 90 || clearedColor.red() > 90)
        {
            qCritical() << "monitor-feature-test: trace clear failed"
                        << clearedColor;
            ++failures;
        }
    }

    qInfo() << "monitor-feature-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int cameraParentTest(QApplication& app)
{
    pvf::Settings::setMonitorVisible(true);
    pvf::Settings::setShowCameraFrame(true);
    auto* project = new pvf::Project;
    project->width = 1920;
    project->height = 1080;
    project->background = QColor(18, 22, 30);

    pvf::Entity* rig = project->addFolder(QStringLiteral("Camera Rig"));
    const pvf::EntityId rigId = rig->id;
    project->setValue(rigId, QStringLiteral("x"), 0, 200.0, false);
    project->setValue(rigId, QStringLiteral("y"), 0, -100.0, false);
    project->setValue(rigId, QStringLiteral("rotation"), 0, 20.0, false);

    pvf::Entity* camera = project->addCamera(QStringLiteral("News Camera"));
    const pvf::EntityId cameraId = camera->id;
    project->setValue(cameraId, QStringLiteral("x"), 0, 120.0, false);
    project->setValue(cameraId, QStringLiteral("y"), 0, 80.0, false);
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 0.6, false);
    project->setValue(cameraId, QStringLiteral("rotation"), 0, 15.0, false);

    pvf::Entity* bar = project->addVector(0, 1800, 100, QColor(200, 30, 40),
                                          QColor(255, 70, 70), 0);
    pvf::Entity* label = project->addText(QStringLiteral("BREAKING NEWS"),
                                          QFont().family(), 56, Qt::white,
                                          int(Qt::AlignCenter));
    const pvf::EntityId barId = bar->id;
    const pvf::EntityId labelId = label->id;
    // Appending to the project vector can reallocate; re-fetch after adding.
    camera = project->entity(cameraId);
    bar = project->entity(barId);
    int failures = 0;
    if (!project->setEntityParent(cameraId, rigId)
        || !project->setEntityParent(barId, cameraId)
        || !project->setEntityParent(labelId, cameraId))
    {
        qCritical() << "camera-parent-test: set parent";
        ++failures;
    }

    // The camera follows the rig: its local centre/rotation/scale are mapped
    // through the rig transform.
    QTransform rigTransform;
    rigTransform.translate(200.0, -100.0);
    rigTransform.rotate(20.0);
    const QPointF expectedCenter = rigTransform.map(QPointF(120.0, 80.0));
    const pvf::Camera::State cameraState =
        pvf::Camera::state(*project, cameraId, 0);
    if ((cameraState.center - expectedCenter).manhattanLength() > 1e-6
        || std::abs(cameraState.rotation - 35.0) > 1e-6
        || std::abs(cameraState.scale - 0.6) > 1e-6)
    {
        qCritical() << "camera-parent-test: camera did not follow rig"
                    << cameraState.center << cameraState.rotation
                    << cameraState.scale;
        ++failures;
    }
    project->setValue(barId, QStringLiteral("x"), 0, 0.0, false);
    project->setValue(barId, QStringLiteral("y"), 0, 220.0, false);
    project->setValue(labelId, QStringLiteral("x"), 0, 0.0, false);
    project->setValue(labelId, QStringLiteral("y"), 0, 220.0, false);

    // The camera view cancels the camera parent transform, so an overlay keeps
    // the same screen position while the camera pans, zooms and rotates.
    const QRectF target(0, 0, 960, 540);
    auto screenPoint = [project, target, cameraId](const pvf::Entity& entity) {
        const pvf::Camera::State state = pvf::Camera::state(*project, cameraId, 0);
        const QPointF world = pvf::SceneRenderer::worldTransform(*project, entity, 0)
                                  .map(QPointF(0, 0));
        QTransform view;
        view.translate(target.center().x(), target.center().y());
        view.scale(target.width() / state.size.width(),
                   target.height() / state.size.height());
        view.rotate(-state.rotation);
        view.translate(-state.center.x(), -state.center.y());
        return view.map(world);
    };
    const QPointF firstScreen = screenPoint(*bar);
    project->setValue(cameraId, QStringLiteral("x"), 0, -200.0, false);
    project->setValue(cameraId, QStringLiteral("y"), 0, 150.0, false);
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 0.4, false);
    project->setValue(cameraId, QStringLiteral("rotation"), 0, -35.0, false);
    const QPointF movedScreen = screenPoint(*bar);
    if ((firstScreen - movedScreen).manhattanLength() > 1e-6)
    {
        qCritical() << "camera-parent-test: overlay moved on screen"
                    << firstScreen << movedScreen;
        ++failures;
    }
    project->setValue(cameraId, QStringLiteral("x"), 0, 120.0, false);
    project->setValue(cameraId, QStringLiteral("y"), 0, 80.0, false);
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 0.6, false);
    project->setValue(cameraId, QStringLiteral("rotation"), 0, 15.0, false);

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    auto* monitor = window.findChild<pvf::CameraMonitor*>();
    if (!timeline || !monitor)
    {
        qCritical() << "camera-parent-test: widgets missing";
        app.exit(1);
        return 1;
    }
    if (timeline->rowCountForTest() != 4
        || timeline->rowEntityForTest(0) != rigId
        || timeline->rowEntityForTest(1) != cameraId
        || timeline->rowEntityForTest(2) != barId
        || timeline->rowEntityForTest(3) != labelId
        || timeline->rowDepthForTest(1) != 1
        || timeline->rowDepthForTest(2) != 2)
    {
        qCritical() << "camera-parent-test: tree rows" << timeline->rowCountForTest();
        ++failures;
    }

    window.setSelectedEntityForTest(barId);
    QApplication::processEvents();
    auto* parents = window.findChild<QComboBox*>(QStringLiteral("parentCombo"));
    if (!parents || parents->findData(int(cameraId)) < 0)
    {
        qCritical() << "camera-parent-test: camera missing from parent combo";
        ++failures;
    }
    else
    {
        const int worldIndex = parents->findData(int(pvf::kInvalidEntity));
        const int cameraIndex = parents->findData(int(cameraId));
        parents->setCurrentIndex(worldIndex);
        QApplication::processEvents();
        if (bar->parent != pvf::kInvalidEntity)
        {
            qCritical() << "camera-parent-test: parent to world";
            ++failures;
        }
        parents->setCurrentIndex(cameraIndex);
        QApplication::processEvents();
        if (bar->parent != cameraId)
        {
            qCritical() << "camera-parent-test: parent to camera";
            ++failures;
        }
    }

    // The camera itself can use a parent: switch between world and the rig.
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();
    auto* cameraParents =
        window.findChild<QComboBox*>(QStringLiteral("parentCombo"));
    if (!cameraParents || cameraParents->findData(int(rigId)) < 0)
    {
        qCritical() << "camera-parent-test: rig missing from camera parent combo"
                    << "selected" << window.selectedEntityForTest()
                    << "count" << (cameraParents ? cameraParents->count() : -1);
        if (cameraParents)
            for (int i = 0; i < cameraParents->count(); ++i)
                qCritical() << "  combo" << i << cameraParents->itemText(i)
                            << cameraParents->itemData(i);
        ++failures;
    }
    else
    {
        const int worldIndex = cameraParents->findData(int(pvf::kInvalidEntity));
        const int rigIndex = cameraParents->findData(int(rigId));
        cameraParents->setCurrentIndex(worldIndex);
        QApplication::processEvents();
        if (camera->parent != pvf::kInvalidEntity)
        {
            qCritical() << "camera-parent-test: camera parent to world";
            ++failures;
        }
        cameraParents->setCurrentIndex(rigIndex);
        QApplication::processEvents();
        if (camera->parent != rigId)
        {
            qCritical() << "camera-parent-test: camera parent to rig"
                        << camera->parent;
            ++failures;
        }
    }

    // Locking the rig locks the camera and its overlays.
    project->setEntityFlag(rigId, pvf::EntityFlag::Locked, true);
    window.setSelectedEntityForTest(barId);
    if (window.selectedEntityForTest() != pvf::kInvalidEntity)
    {
        qCritical() << "camera-parent-test: rig lock did not lock overlay";
        ++failures;
    }
    window.setSelectedEntityForTest(cameraId);
    if (window.selectedEntityForTest() != pvf::kInvalidEntity)
    {
        qCritical() << "camera-parent-test: rig lock did not lock camera";
        ++failures;
    }
    project->setEntityFlag(rigId, pvf::EntityFlag::Locked, false);
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();

    // The monitor should show the red lower-third inside the camera view.
    const QImage shot = monitor->grab().toImage();
    bool redFound = false;
    for (int y = shot.height() / 2; y < shot.height() - 8 && !redFound; ++y)
    {
        for (int x = shot.width() / 4; x < shot.width() * 3 / 4; ++x)
        {
            const QColor pixel = shot.pixelColor(x, y);
            if (pixel.red() > 150 && pixel.green() < 90 && pixel.blue() < 90)
            {
                redFound = true;
                break;
            }
        }
    }
    if (!redFound)
    {
        qCritical() << "camera-parent-test: monitor missing overlay";
        ++failures;
    }

    window.grab().toImage().save(QStringLiteral("/tmp/pvf_camera_parent.png"));
    qInfo() << "camera-parent-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int guideTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "guide-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* first = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId idA = first->id;
        pvf::Entity* second = project->addVector(0, 200, 120, Qt::green, Qt::white, 0);
        project->setValue(second->id, QStringLiteral("x"), 0, 500.0, false);
        window.setSelectedEntityForTest(idA);

        const double s = viewport->currentScale();
        const QPoint center = viewport->entityPivotForTest(idA).toPoint();
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto hasGuide = [viewport](const QImage& image) {
            QRect skip;
            if (auto* monitor = viewport->findChild<pvf::CameraMonitor*>())
                skip = monitor->geometry();
            for (int y = 0; y < image.height(); y += 3)
            {
                const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                {
                    if (skip.contains(x, y))
                        continue;
                    if (qBlue(line[x]) > 200 && qGreen(line[x]) > 195
                        && qRed(line[x]) < 140)
                        return true;
                }
            }
            return false;
        };

        send(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, center + QPoint(int(std::lround(245.0 * s)), 0),
             Qt::NoButton, Qt::LeftButton);

        viewport->grab().toImage().save(QStringLiteral("/tmp/pvforge_guide.png"));
        if (!hasGuide(viewport->grab().toImage()))
        {
            qCritical() << "guide-test: no guide drawn while dragging";
            ++failures;
        }
        if (std::abs(project->value(idA, QStringLiteral("x"), 0) - 250.0) > 1.5)
        {
            qCritical() << "guide-test: not snapped"
                        << project->value(idA, QStringLiteral("x"), 0);
            ++failures;
        }

        send(QEvent::MouseButtonRelease, center + QPoint(int(std::lround(245.0 * s)), 0),
             Qt::LeftButton, Qt::NoButton);
        viewport->grab().toImage().save(QStringLiteral("/tmp/pvforge_guide_after.png"));
        if (hasGuide(viewport->grab().toImage()))
        {
            qCritical() << "guide-test: guide still visible after release";
            ++failures;
        }

        qInfo() << "guide-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int entitySnapTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "entity-snap-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* first = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId idA = first->id;
        pvf::Entity* second = project->addVector(0, 200, 120, Qt::green, Qt::white, 0);
        const pvf::EntityId idB = second->id;
        project->setValue(idB, QStringLiteral("x"), 0, 500.0, false);
        window.setSelectedEntityForTest(idA);

        const double s = viewport->currentScale();
        const QPoint center = viewport->entityPivotForTest(idA).toPoint();
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        };
        auto reset = [&] {
            project->setValue(idA, QStringLiteral("x"), 0, 0.0, false);
            project->setValue(idA, QStringLiteral("y"), 0, 0.0, false);
        };

        // Edge alignment: A's right edge snaps to B's left edge (x = 250).
        drag(center, center + QPoint(int(std::lround(245.0 * s)), 0));
        if (std::abs(project->value(idA, QStringLiteral("x"), 0) - 250.0) > 1.5)
        {
            qCritical() << "entity-snap-test: edge alignment"
                        << project->value(idA, QStringLiteral("x"), 0);
            ++failures;
        }

        // Horizontal drag locks the vertical axis.
        reset();
        drag(center, center + QPoint(int(std::lround(100.0 * s)),
                                     int(std::lround(20.0 * s))));
        if (std::abs(project->value(idA, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "entity-snap-test: horizontal lock failed"
                        << project->value(idA, QStringLiteral("y"), 0);
            ++failures;
        }

        // Vertical drag locks the horizontal axis.
        reset();
        drag(center, center + QPoint(int(std::lround(10.0 * s)),
                                     int(std::lround(100.0 * s))));
        if (std::abs(project->value(idA, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "entity-snap-test: vertical lock failed"
                        << project->value(idA, QStringLiteral("x"), 0);
            ++failures;
        }

        qInfo() << "entity-snap-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int rotationSnapTest(QApplication& app)
{
    const QList<int> oldAngles = pvf::Settings::snapAngles();

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, oldAngles] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "rotation-snap-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto angleDistance = [](double a, double b) {
            return std::abs(std::remainder(a - b, 360.0));
        };
        auto rotateTo = [&](double degrees) {
            project->setValue(id, QStringLiteral("rotation"), 0, 0.0, false);
            const QPoint center = viewport->entityPivotForTest(id).toPoint();
            const double radians = degrees * M_PI / 180.0;
            const QPoint end(center.x() + int(std::lround(48.0 * std::cos(radians))),
                             center.y() + int(std::lround(48.0 * std::sin(radians))));
            send(QEvent::MouseButtonPress, center + QPoint(48, 0), Qt::LeftButton,
                 Qt::LeftButton);
            send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
            return project->value(id, QStringLiteral("rotation"), 0);
        };

        pvf::Settings::setSnapAngles({0, 90});

        double rotation = rotateTo(2.0);
        if (std::abs(rotation) > 1e-6)
        {
            qCritical() << "rotation-snap-test: 0 snap" << rotation;
            ++failures;
        }

        rotation = rotateTo(88.0);
        if (std::abs(rotation - 90.0) > 1e-6)
        {
            qCritical() << "rotation-snap-test: 90 snap" << rotation;
            ++failures;
        }

        rotation = rotateTo(45.0);
        if (std::abs(rotation - 45.0) > 1.5)
        {
            qCritical() << "rotation-snap-test: 45 should not snap" << rotation;
            ++failures;
        }

        rotation = rotateTo(268.0);
        if (angleDistance(rotation, 268.0) > 1.5)
        {
            qCritical() << "rotation-snap-test: 270 disabled" << rotation;
            ++failures;
        }

        pvf::Settings::setSnapAngles({0, 90, 270});
        rotation = rotateTo(268.0);
        if (angleDistance(rotation, 270.0) > 1e-6)
        {
            qCritical() << "rotation-snap-test: 270 snap" << rotation;
            ++failures;
        }

        pvf::Settings::setSnapAngles(oldAngles);
        qInfo() << "rotation-snap-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int dragUndoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "drag-undo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        const QPoint center = viewport->entityPivotForTest(id).toPoint();

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto hasRedAxis = [&](const QImage& image) {
            for (int y = center.y() - 2; y <= center.y() + 2; ++y)
            {
                for (int x = center.x() + 20; x <= center.x() + 60; ++x)
                {
                    const QRgb pixel = image.pixel(x, y);
                    // The gizmo axis is (226,86,86); the entity body is pure red.
                    if (qRed(pixel) > 200 && qGreen(pixel) > 60 && qGreen(pixel) < 120
                        && qBlue(pixel) < 120)
                        return true;
                }
            }
            return false;
        };

        // Clicking the entity shows the gizmo immediately.
        if (hasRedAxis(viewport->grab().toImage()))
        {
            qCritical() << "drag-undo-test: gizmo visible before selection";
            ++failures;
        }
        send(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
        if (!hasRedAxis(viewport->grab().toImage()))
        {
            qCritical() << "drag-undo-test: gizmo not drawn after click";
            ++failures;
        }

        // Dragging with many moves is a single undo step.
        send(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 10; ++i)
            send(QEvent::MouseMove, center + QPoint(i * 15, i * 8), Qt::NoButton,
                 Qt::LeftButton);
        send(QEvent::MouseButtonRelease, center + QPoint(150, 80), Qt::LeftButton,
             Qt::NoButton);

        const double endX = project->value(id, QStringLiteral("x"), 0);
        const double endY = project->value(id, QStringLiteral("y"), 0);
        if (std::abs(endX) < 1.0 || std::abs(endY) < 1.0)
        {
            qCritical() << "drag-undo-test: drag did nothing" << endX << endY;
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "drag-undo-test: one undo did not restore"
                        << project->value(id, QStringLiteral("x"), 0)
                        << project->value(id, QStringLiteral("y"), 0);
            ++failures;
        }

        qInfo() << "drag-undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int pivotGizmoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "pivot-gizmo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);
        entity->data[QStringLiteral("rotPointCustom")] = true;

        const double s = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        auto visualCentre = [&] {
            return viewport->entityTransformForTest(id).map(QPointF(0, 0));
        };
        auto gizmoPos = [&] { return viewport->entityPivotForTest(id).toPoint(); };

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, mods);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        };

        // Dragging the X axis moves the pivot; the position compensates so the
        // entity does not move visually.
        const int edgeDx = int(std::lround(145.0 * s));
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0));
        const double pivotX = project->value(id, QStringLiteral("rotPointX"), 0);
        const double posX = project->value(id, QStringLiteral("x"), 0);
        QPointF visual = visualCentre();
        if (std::abs(pivotX - 150.0) > 1.0 || std::abs(posX - 150.0) > 1.0)
        {
            qCritical() << "pivot-gizmo-test: axis pivot drag" << pivotX << posX;
            ++failures;
        }
        if (std::abs(visual.x() - center.x()) > 2.0
            || std::abs(visual.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-gizmo-test: entity moved visually" << visual;
            ++failures;
        }
        if (std::abs(gizmoPos().x() - (center.x() + int(std::lround(150.0 * s)))) > 3)
        {
            qCritical() << "pivot-gizmo-test: gizmo not on pivot" << gizmoPos();
            ++failures;
        }

        // Dragging the pivot dot moves both axes freely.
        const QPoint dot = gizmoPos();
        drag(dot, dot + QPoint(int(std::lround(60.0 * s)), int(std::lround(30.0 * s))));
        if (project->value(id, QStringLiteral("rotPointX"), 0) < 190.0
            || project->value(id, QStringLiteral("rotPointY"), 0) < 15.0)
        {
            qCritical() << "pivot-gizmo-test: free pivot drag"
                        << project->value(id, QStringLiteral("rotPointX"), 0)
                        << project->value(id, QStringLiteral("rotPointY"), 0);
            ++failures;
        }

        // Pivot snapping follows the scaled visual edge.
        window.applyPivotForTest(id, 0.0, 0.0);
        project->setValue(id, QStringLiteral("scaleX"), 0, 0.5, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 0.5, false);
        const int visualEdgeDx = int(std::lround(70.0 * s));
        drag(center + QPoint(40, 0), center + QPoint(40 + visualEdgeDx, 0));
        const double snappedPivot = project->value(id, QStringLiteral("rotPointX"), 0);
        if (std::abs(snappedPivot - 75.0) > 1.5)
        {
            qCritical() << "pivot-gizmo-test: visual edge snap" << snappedPivot;
            ++failures;
        }

        // Disabling the edit mode keeps the gizmo on the pivot.
        entity->data[QStringLiteral("rotPointCustom")] = false;
        const QPoint gizmoAfter = gizmoPos();
        const QPoint expectedGizmo(center.x() + int(std::lround(snappedPivot * s)),
                                   center.y());
        if (std::abs(gizmoAfter.x() - expectedGizmo.x()) > 3
            || std::abs(gizmoAfter.y() - expectedGizmo.y()) > 3)
        {
            qCritical() << "pivot-gizmo-test: gizmo left the pivot" << gizmoAfter;
            ++failures;
        }

        // Axis dragging now moves the entity again.
        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);
        const QPoint gizmoNow = gizmoPos();
        drag(gizmoNow + QPoint(40, 0), gizmoNow + QPoint(40 + int(std::lround(50.0 * s)), 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0)
                     - (snappedPivot + 50.0))
            > 2.0)
        {
            qCritical() << "pivot-gizmo-test: axis did not move entity"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        qInfo() << "pivot-gizmo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}


int pivotTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "pivot-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPointF center(viewport->rect().center());
        auto visualCentre = [&] {
            return viewport->entityTransformForTest(id).map(QPointF(0, 0));
        };

        // Moving the pivot compensates the position: visuals stay put.
        window.applyPivotForTest(id, 100.0, 0.0);
        const double x = project->value(id, QStringLiteral("x"), 0);
        if (std::abs(x - 100.0) > 0.5)
        {
            qCritical() << "pivot-test: position not compensated" << x;
            ++failures;
        }
        QPointF visual = visualCentre();
        if (std::abs(visual.x() - center.x()) > 2.0
            || std::abs(visual.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-test: entity moved visually" << visual;
            ++failures;
        }
        QPointF gizmo = viewport->entityPivotForTest(id);
        if (std::abs(gizmo.x() - (center.x() + 100.0 * s)) > 2.0
            || std::abs(gizmo.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-test: gizmo not on pivot" << gizmo;
            ++failures;
        }

        // Rotation orbits the pivot.
        project->setValue(id, QStringLiteral("rotation"), 0, 90.0, false);
        visual = visualCentre();
        const QPointF expected(center.x() + 100.0 * s, center.y() - 100.0 * s);
        if (std::abs(visual.x() - expected.x()) > 2.0
            || std::abs(visual.y() - expected.y()) > 2.0)
        {
            qCritical() << "pivot-test: rotation orbit" << visual << expected;
            ++failures;
        }

        // Turning off the edit mode keeps the pivot and the gizmo there.
        entity->data[QStringLiteral("rotPointCustom")] = false;
        gizmo = viewport->entityPivotForTest(id);
        if (std::abs(gizmo.x() - (center.x() + 100.0 * s)) > 2.0
            || std::abs(gizmo.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-test: gizmo left the pivot" << gizmo;
            ++failures;
        }
        visual = visualCentre();
        if (std::abs(visual.x() - expected.x()) > 2.0
            || std::abs(visual.y() - expected.y()) > 2.0)
        {
            qCritical() << "pivot-test: disabling changed rotation centre" << visual;
            ++failures;
        }

        qInfo() << "pivot-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}


int gizmoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "gizmo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double scale = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_gizmo.png"));

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        };

        // Drag the X axis: horizontal only.
        drag(center + QPoint(40, 0), center + QPoint(140, 0));
        const double xAfterAxis = project->value(id, QStringLiteral("x"), 0);
        if (xAfterAxis <= 0.0
            || std::abs(project->value(id, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "gizmo-test: x axis drag" << xAfterAxis;
            ++failures;
        }
        const double expectedX = 100.0 / scale;
        if (std::abs(xAfterAxis - expectedX) > 1.0)
        {
            qCritical() << "gizmo-test: x axis delta" << xAfterAxis << expectedX;
            ++failures;
        }

        project->setValue(id, QStringLiteral("x"), 0, 0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0, false);

        // Drag the Y axis (points up): vertical only. Stay clear of the ring.
        drag(center + QPoint(0, -30), center + QPoint(0, -140));
        const double yAfterAxis = project->value(id, QStringLiteral("y"), 0);
        if (yAfterAxis >= 0.0
            || std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "gizmo-test: y axis drag" << yAfterAxis;
            ++failures;
        }

        project->setValue(id, QStringLiteral("x"), 0, 0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0, false);
        project->setValue(id, QStringLiteral("rotation"), 0, 0, false);

        // Drag the rotation ring: changes the angle, keeps the position.
        drag(center + QPoint(48, 0), center + QPoint(0, 48));
        const double rotation = project->value(id, QStringLiteral("rotation"), 0);
        // Integer mouse coordinates against a fractional pivot give a small
        // angular quantisation error; allow a few degrees.
        if (std::abs(rotation - 90.0) > 4.0)
        {
            qCritical() << "gizmo-test: rotation" << rotation;
            ++failures;
        }
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "gizmo-test: rotation moved the entity";
            ++failures;
        }

        project->setValue(id, QStringLiteral("rotation"), 0, 0, false);

        // Dragging the body moves freely.
        drag(center, center + QPoint(60, 30));
        if (project->value(id, QStringLiteral("x"), 0) <= 0.0
            || project->value(id, QStringLiteral("y"), 0) <= 0.0)
        {
            qCritical() << "gizmo-test: body drag";
            ++failures;
        }

        qInfo() << "gizmo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

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
    registry.add(QStringLiteral("--viewport-test"), [](QApplication& app) {
        const QStringList arguments = app.arguments();
        const int index =
            arguments.indexOf(QStringLiteral("--viewport-test"));
        QString projectDir = QStringLiteral("/tmp/pvfe2e/proj");
        if (index >= 0 && index + 1 < arguments.size()
            && !arguments.at(index + 1).startsWith(QLatin1Char('-')))
            projectDir = arguments.at(index + 1);
        return viewportTest(app, projectDir);
    });
    registry.add(QStringLiteral("--snap-test"),
                 [](QApplication& app) { return snapTest(app); });
    registry.add(QStringLiteral("--easing-test"),
                 [](QApplication& app) { return easingTest(app); });
    registry.add(QStringLiteral("--scale-handle-test"),
                 [](QApplication& app) { return scaleHandleTest(app); });
    registry.add(QStringLiteral("--scale-undo-test"),
                 [](QApplication& app) { return scaleUndoTest(app); });
    registry.add(QStringLiteral("--camera-test"),
                 [](QApplication& app) { return cameraTest(app); });
    registry.add(QStringLiteral("--monitor-feature-test"),
                 [](QApplication& app) { return monitorFeatureTest(app); });
    registry.add(QStringLiteral("--camera-parent-test"),
                 [](QApplication& app) { return cameraParentTest(app); });
    registry.add(QStringLiteral("--guide-test"),
                 [](QApplication& app) { return guideTest(app); });
    registry.add(QStringLiteral("--entity-snap-test"),
                 [](QApplication& app) { return entitySnapTest(app); });
    registry.add(QStringLiteral("--rotation-snap-test"),
                 [](QApplication& app) { return rotationSnapTest(app); });
    registry.add(QStringLiteral("--drag-undo-test"),
                 [](QApplication& app) { return dragUndoTest(app); });
    registry.add(QStringLiteral("--pivot-gizmo-test"),
                 [](QApplication& app) { return pivotGizmoTest(app); });
    registry.add(QStringLiteral("--pivot-test"),
                 [](QApplication& app) { return pivotTest(app); });
    registry.add(QStringLiteral("--gizmo-test"), [](QApplication& app) {
        const int sessionResult = sessionGizmoTest(app);
        if (sessionResult != 0)
            return sessionResult;
        return gizmoTest(app);
    });
    registry.add(QStringLiteral("--inkscape-handles-test"),
                 [](QApplication& app) { return inkscapeHandlesTest(app); });
    registry.add(QStringLiteral("--camera-monitor-test"),
                 [](QApplication& app) { return cameraMonitorTest(app); });
}

} // namespace pvf::tests
