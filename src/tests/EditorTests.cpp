#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

int groupTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    QVector<pvf::EntityId> ids;
    for (int i = 0; i < 3; ++i)
    {
        pvf::Entity* entity =
            project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
        ids.append(entity->id);
    }

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, ids] {
        int failures = 0;
        window.groupEntitiesForTest(ids);
        QApplication::processEvents();

        pvf::EntityId folderId = pvf::kInvalidEntity;
        int folders = 0;
        int children = 0;
        for (const pvf::Entity& entity : project->entities())
        {
            if (entity.isFolder())
            {
                ++folders;
                folderId = entity.id;
            }
        }
        for (const pvf::Entity& entity : project->entities())
            if (entity.parent == folderId)
                ++children;

        if (project->entities().size() != 4 || folders != 1 || children != 3
            || window.selectedEntityForTest() != folderId)
        {
            qCritical() << "group-test: grouping failed" << project->entities().size()
                        << folders << children << window.selectedEntityForTest();
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            int foldersAfterUndo = 0;
            for (const pvf::Entity& entity : project->entities())
                if (entity.isFolder())
                    ++foldersAfterUndo;
            if (project->entities().size() != 3 || foldersAfterUndo != 0)
            {
                qCritical() << "group-test: undo failed"
                            << project->entities().size() << foldersAfterUndo;
                ++failures;
            }
            if (auto* redo =
                    window.findChild<QAction*>(QStringLiteral("redoAction")))
            {
                redo->trigger();
                QApplication::processEvents();
                if (project->entities().size() != 4)
                {
                    qCritical() << "group-test: redo failed"
                                << project->entities().size();
                    ++failures;
                }
            }
        }

        qInfo() << "group-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int opacityTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* transparent =
        project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId transparentId = transparent->id;
    pvf::Entity* behind = project->addVector(0, 80, 80, Qt::blue, Qt::white, 0);
    const pvf::EntityId behindId = behind->id;
    pvf::Entity* folder = project->addFolder(QStringLiteral("Fade"));
    const pvf::EntityId folderId = folder->id;
    project->setEntityParent(behindId, folderId);
    project->setValue(transparentId, QStringLiteral("opacity"), 0, 0.0, false);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window,
                       [&app, &window, project, transparentId, behindId, folderId] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "opacity-test: viewport missing";
            app.exit(1);
            return;
        }

        const QPointF centre =
            viewport->worldToScreenForTest().map(QPointF(0.0, 0.0));
        if (project->effectiveOpacity(transparentId, 0) > 1e-6)
        {
            qCritical() << "opacity-test: transparent opacity is not zero";
            ++failures;
        }
        if (viewport->hitTestForTest(centre) != behindId)
        {
            qCritical() << "opacity-test: transparent entity blocks the pick"
                        << viewport->hitTestForTest(centre);
            ++failures;
        }

        project->setValue(transparentId, QStringLiteral("opacity"), 0, 1.0, false);
        if (viewport->hitTestForTest(centre) != transparentId)
        {
            qCritical() << "opacity-test: visible entity is not picked";
            ++failures;
        }

        project->setValue(transparentId, QStringLiteral("opacity"), 0, 0.0, false);
        project->setValue(folderId, QStringLiteral("opacity"), 0, 0.0, false);
        if (project->effectiveOpacity(behindId, 0) > 1e-6
            || viewport->hitTestForTest(centre) != pvf::kInvalidEntity)
        {
            qCritical() << "opacity-test: zero opacity folder is still visible";
            ++failures;
        }

        project->setValue(folderId, QStringLiteral("opacity"), 0, 1.0, false);
        if (viewport->hitTestForTest(centre) != behindId)
        {
            qCritical() << "opacity-test: folder child is not picked after fading in";
            ++failures;
        }

        qInfo() << "opacity-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int multiDragTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* folder = project->addFolder(QStringLiteral("Target"));
    const pvf::EntityId folderId = folder->id;
    QVector<pvf::EntityId> ids;
    for (int i = 0; i < 3; ++i)
        ids.append(project->addVector(0, 80, 80, Qt::red, Qt::white, 0)->id);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, folderId, ids] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        if (!timeline)
        {
            qCritical() << "multidrag-test: timeline missing";
            app.exit(1);
            return;
        }

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons,
                                    Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, modifiers);
            QApplication::sendEvent(timeline, &event);
        };

        const QPoint first(10, timeline->rowCenterYForTest(1));
        const QPoint second(10, timeline->rowCenterYForTest(2));
        const QPoint folderRow(10, timeline->rowCenterYForTest(0));

        sendMouse(QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, first, Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, second, Qt::LeftButton,
                  Qt::ControlModifier);
        sendMouse(QEvent::MouseButtonRelease, second, Qt::NoButton,
                  Qt::ControlModifier);
        QApplication::processEvents();
        if (timeline->entitySelectionCountForTest() != 2)
        {
            qCritical() << "multidrag-test: ctrl selection"
                        << timeline->entitySelectionCountForTest();
            ++failures;
        }

        // Pressing a row that is already selected keeps the whole selection,
        // so dragging drops both rows into the folder as one block.
        sendMouse(QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, folderRow, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, folderRow, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();

        const pvf::Entity* a = project->entity(ids[0]);
        const pvf::Entity* b = project->entity(ids[1]);
        const pvf::Entity* c = project->entity(ids[2]);
        if (!a || !b || !c || a->parent != folderId || b->parent != folderId
            || c->parent != pvf::kInvalidEntity)
        {
            qCritical() << "multidrag-test: group drop"
                        << (a ? a->parent : -1) << (b ? b->parent : -1)
                        << (c ? c->parent : -1);
            ++failures;
        }
        else
        {
            QVector<pvf::EntityId> order;
            for (int index : project->entityOrder())
            {
                const pvf::Entity& entity = project->entities()[index];
                if (entity.parent == folderId)
                    order.append(entity.id);
            }
            if (order != QVector<pvf::EntityId>({ids[0], ids[1]}))
            {
                qCritical() << "multidrag-test: child order" << order;
                ++failures;
            }
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            const pvf::Entity* undoneA = project->entity(ids[0]);
            const pvf::Entity* undoneB = project->entity(ids[1]);
            if (!undoneA || !undoneB
                || undoneA->parent != pvf::kInvalidEntity
                || undoneB->parent != pvf::kInvalidEntity)
            {
                qCritical() << "multidrag-test: undo did not restore the rows";
                ++failures;
            }
        }

        qInfo() << "multidrag-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int keyframeDragTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity = project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    project->setValue(id, QStringLiteral("x"), 20, 150.0, true);
    project->setValue(id, QStringLiteral("y"), 10, 50.0, true);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        if (!timeline)
        {
            qCritical() << "keyframe-drag-test: timeline missing";
            app.exit(1);
            return;
        }

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons,
                                    Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, modifiers);
            QApplication::sendEvent(timeline, &event);
        };

        const int y = timeline->rowCenterYForTest(0);
        const QPoint ten(timeline->frameXForTest(10), y);
        const QPoint twenty(timeline->frameXForTest(20), y);

        sendMouse(QEvent::MouseButtonPress, ten, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, ten, Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, twenty, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(QEvent::MouseButtonRelease, twenty, Qt::NoButton, Qt::ShiftModifier);
        QApplication::processEvents();
        if (timeline->selectedKeyframes().size() != 2)
        {
            qCritical() << "keyframe-drag-test: selection"
                        << timeline->selectedKeyframes().size();
            ++failures;
        }
        // Selecting a keyframe moves the playhead there, so the property
        // panel shows the keyframe's values.
        if (window.currentFrameForTest() != 20)
        {
            qCritical() << "keyframe-drag-test: playhead did not follow the keyframe"
                        << window.currentFrameForTest();
            ++failures;
        }

        // Drag the selected keys ten frames to the right.
        sendMouse(QEvent::MouseButtonPress, ten, Qt::LeftButton, Qt::NoModifier);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 10)
        {
            qCritical() << "keyframe-drag-test: press did not focus the keyframe"
                        << window.currentFrameForTest();
            ++failures;
        }
        sendMouse(QEvent::MouseMove, twenty, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, twenty, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 20)
        {
            qCritical() << "keyframe-drag-test: playhead did not follow the move"
                        << window.currentFrameForTest();
            ++failures;
        }

        const pvf::Keyframe* x20 = project->keyframe(id, QStringLiteral("x"), 20);
        const pvf::Keyframe* x30 = project->keyframe(id, QStringLiteral("x"), 30);
        const pvf::Keyframe* y20 = project->keyframe(id, QStringLiteral("y"), 20);
        if (!x20 || !x30 || !y20 || std::abs(x20->value - 100.0) > 1e-6
            || std::abs(x30->value - 150.0) > 1e-6
            || std::abs(y20->value - 50.0) > 1e-6
            || project->keyframe(id, QStringLiteral("x"), 10)
            || project->keyframe(id, QStringLiteral("y"), 10))
        {
            qCritical() << "keyframe-drag-test: keys did not move"
                        << (x20 ? x20->value : -1) << (x30 ? x30->value : -1)
                        << (y20 ? y20->value : -1);
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            const pvf::Keyframe* x10 = project->keyframe(id, QStringLiteral("x"), 10);
            const pvf::Keyframe* x20 = project->keyframe(id, QStringLiteral("x"), 20);
            const pvf::Keyframe* y10 = project->keyframe(id, QStringLiteral("y"), 10);
            if (!x10 || !x20 || !y10 || std::abs(x10->value - 100.0) > 1e-6
                || std::abs(x20->value - 150.0) > 1e-6
                || std::abs(y10->value - 50.0) > 1e-6
                || project->keyframe(id, QStringLiteral("x"), 30)
                || project->keyframe(id, QStringLiteral("y"), 20))
            {
                qCritical() << "keyframe-drag-test: undo did not restore keys";
                ++failures;
            }
        }

        qInfo() << "keyframe-drag-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int curveEditorTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity = project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    project->setValue(id, QStringLiteral("x"), 20, 200.0, true);
    project->setValue(id, QStringLiteral("y"), 10, 0.0, true);
    project->setValue(id, QStringLiteral("y"), 20, 50.0, true);

    pvf::EasingSpec bezier;
    bezier.easing = pvf::Easing::Bezier;
    bezier.inX = 0.8;
    bezier.inY = 0.0;
    bezier.outX = 0.2;
    bezier.outY = 1.0;
    project->setKeyframeEasing(id, QStringLiteral("x"), 20, bezier);

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "curve-editor-test:" << text;
            ++failures;
        };

        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("timelineTabs"));
        auto* curve = window.findChild<pvf::CurveEditor*>();
        if (!tabs || !curve || tabs->count() < 2)
        {
            qCritical() << "curve-editor-test: timeline tabs/curve editor missing";
            app.exit(1);
            return;
        }
        tabs->setCurrentWidget(curve);
        window.setSelectedEntityForTest(id);
        curve->setPixelsPerFrameForTest(10.0);
        curve->setCurveVisible(0, true);
        curve->setCurveVisible(1, true);
        QApplication::processEvents();

        auto sendMouse = [curve](QEvent::Type type, const QPointF& pos,
                                 Qt::MouseButtons buttons,
                                 Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, pos, curve->mapToGlobal(pos), Qt::LeftButton,
                              buttons, modifiers);
            QApplication::sendEvent(curve, &event);
        };
        auto sendKey = [curve](QEvent::Type type, int key,
                               Qt::KeyboardModifiers modifiers) {
            QKeyEvent event(type, key, modifiers);
            QApplication::sendEvent(curve, &event);
        };

        const QPointF p10 = curve->keyframePointForTest(QStringLiteral("x"), 10.0);
        const QPointF p20 = curve->keyframePointForTest(QStringLiteral("x"), 20.0);
        if (p10.isNull() || p20.isNull()
            || std::abs(p10.x() - curve->frameXForTest(10.0)) > 1.0)
        {
            fail("keyframe points not mapped to the time axis");
        }

        // Move the selected frame from 10 to 15. Every property at that frame
        // travels with it, exactly like a timeline group drag.
        sendMouse(QEvent::MouseButtonPress, p10, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, QPointF(curve->frameXForTest(15.0), p10.y()),
                  Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease,
                  QPointF(curve->frameXForTest(15.0), p10.y()), Qt::NoButton,
                  Qt::NoModifier);
        QApplication::processEvents();

        const pvf::Keyframe* movedX = project->keyframe(id, QStringLiteral("x"), 15.0);
        if (!movedX || std::abs(movedX->value - 100.0) > 1e-6
            || !project->keyframe(id, QStringLiteral("y"), 15.0)
            || project->keyframe(id, QStringLiteral("x"), 10.0)
            || project->keyframe(id, QStringLiteral("y"), 10.0))
        {
            fail("dragging a keyframe did not move the whole frame");
        }
        if (window.currentFrameForTest() != 15)
        {
            fail("playhead did not follow the dragged keyframe");
        }

        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!undo)
        {
            fail("undo action missing");
        }
        else
        {
            undo->trigger();
            QApplication::processEvents();
            if (!project->keyframe(id, QStringLiteral("x"), 10.0)
                || !project->keyframe(id, QStringLiteral("y"), 10.0)
                || project->keyframe(id, QStringLiteral("x"), 15.0))
            {
                fail("undo did not restore the dragged keyframes");
            }
        }

        // Dragging onto an occupied frame must stop one frame short instead of
        // overwriting the existing key.
        const QPointF again10 =
            curve->keyframePointForTest(QStringLiteral("x"), 10.0);
        sendMouse(QEvent::MouseButtonPress, again10, Qt::LeftButton,
                  Qt::NoModifier);
        sendMouse(QEvent::MouseMove, QPointF(curve->frameXForTest(20.0), again10.y()),
                  Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease,
                  QPointF(curve->frameXForTest(20.0), again10.y()), Qt::NoButton,
                  Qt::NoModifier);
        QApplication::processEvents();
        if (!project->keyframe(id, QStringLiteral("x"), 19.0)
            || !project->keyframe(id, QStringLiteral("x"), 20.0)
            || project->keyframe(id, QStringLiteral("x"), 10.0))
        {
            fail("keyframe drag overwrote a neighbour instead of clamping");
        }
        if (undo)
        {
            undo->trigger();
            QApplication::processEvents();
        }

        // Bezier handle drag: select the incoming key of the segment, grab the
        // in-handle and move it. The curve changes once, on release.
        const QPointF key20 = curve->keyframePointForTest(QStringLiteral("x"), 20.0);
        sendMouse(QEvent::MouseButtonPress, key20, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, key20, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
        if (curve->selectedKeyframeCount() != 1)
            fail("clicking a graph keyframe did not select it");

        const QPointF handle =
            curve->bezierHandlePointForTest(QStringLiteral("x"), 20.0, true);
        if (handle.isNull())
        {
            fail("bezier handle was not exposed for the selected keyframe");
        }
        else
        {
            sendMouse(QEvent::MouseButtonPress, handle, Qt::LeftButton,
                      Qt::NoModifier);
            sendMouse(QEvent::MouseMove, handle + QPointF(-15.0, 12.0),
                      Qt::LeftButton, Qt::NoModifier);
            sendMouse(QEvent::MouseButtonRelease, handle + QPointF(-15.0, 12.0),
                      Qt::NoButton, Qt::NoModifier);
            QApplication::processEvents();
            const pvf::Keyframe* eased =
                project->keyframe(id, QStringLiteral("x"), 20.0);
            if (!eased || eased->ease.easing != pvf::Easing::Bezier
                || (std::abs(eased->ease.inX - 0.8) < 1e-6
                    && std::abs(eased->ease.inY) < 1e-6))
            {
                fail("dragging the bezier handle did not change the easing");
            }
            if (undo)
            {
                undo->trigger();
                QApplication::processEvents();
                const pvf::Keyframe* restored =
                    project->keyframe(id, QStringLiteral("x"), 20.0);
                if (!restored || std::abs(restored->ease.inX - 0.8) > 1e-6
                    || std::abs(restored->ease.inY) > 1e-6)
                {
                    fail("undo did not restore the bezier handles");
                }
            }
        }

        // Rubber-band select both frames, then copy and paste them at frame 40.
        const QRect plot = curve->plotRectForTest();
        const QPointF boxStart(curve->frameXForTest(0.0), plot.bottom() - 2.0);
        const QPointF boxEnd(curve->frameXForTest(30.0), plot.top() + 2.0);
        sendMouse(QEvent::MouseButtonPress, boxStart, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, boxEnd, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, boxEnd, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
        if (curve->selectedKeyframeCount() < 2)
            fail("rubber-band selection did not select both keyframes");

        sendKey(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        window.goToFrame(40);
        QApplication::processEvents();
        sendKey(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::processEvents();
        const pvf::Keyframe* pastedX40 =
            project->keyframe(id, QStringLiteral("x"), 40.0);
        const pvf::Keyframe* pastedX50 =
            project->keyframe(id, QStringLiteral("x"), 50.0);
        if (!pastedX40 || !pastedX50
            || std::abs(pastedX40->value - 100.0) > 1e-6
            || std::abs(pastedX50->value - 200.0) > 1e-6
            || !project->keyframe(id, QStringLiteral("y"), 50.0))
        {
            fail("copy/paste did not recreate the selected keyframes");
        }
        if (undo)
        {
            undo->trigger();
            QApplication::processEvents();
            if (project->keyframe(id, QStringLiteral("x"), 40.0)
                || project->keyframe(id, QStringLiteral("x"), 50.0))
            {
                fail("undo did not remove pasted keyframes");
            }
        }

        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_curve_editor.png"));
        qInfo() << "curve-editor-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int audioUndoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    auto clip = std::make_shared<pvf::AudioClip>();
    clip->duration = 5.0;
    clip->peaks.fill(0.5f, 5000);
    pvf::Entity* audio = project->addAudio(QStringLiteral("/tmp/pvf_audio_undo.wav"),
                                           clip, QStringLiteral("测试音频"));
    const pvf::EntityId id = audio->id;
    // Extra lanes make the waveform page vertically scrollable.
    for (int i = 0; i < 2; ++i)
        project->addAudio(QStringLiteral("/tmp/pvf_audio_undo.wav"), clip,
                          QStringLiteral("测试音频 %1").arg(i + 2));

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "audio-undo-test:" << text;
            ++failures;
        };

        window.setSelectedEntityForTest(id);
        QApplication::processEvents();

        auto* volume = window.findChild<QSpinBox*>(QStringLiteral("audioVolume"));
        auto* trimStart =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioTrimStart"));
        auto* trimEnd =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioTrimEnd"));
        auto* fadeIn =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioFadeIn"));
        auto* fadeOut =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioFadeOut"));
        auto* start = window.findChild<QSpinBox*>(QStringLiteral("audioStartFrame"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!volume || !trimStart || !trimEnd || !fadeIn || !fadeOut || !start
            || !undo)
        {
            qCritical() << "audio-undo-test: audio controls missing";
            app.exit(1);
            return;
        }

        auto data = [project, id](const QString& key) {
            const pvf::Entity* entity = project->entity(id);
            return entity ? entity->data.value(key, 0.0).toDouble() : -1.0;
        };

        // A spin-box drag emits several valueChanged signals; they must fold
        // into one undo step.
        volume->setValue(80);
        volume->setValue(60);
        if (std::abs(data(QStringLiteral("volume")) - 0.6) > 1e-6)
            fail("volume edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("volume")) - 1.0) > 1e-6)
            fail("one undo did not revert the whole volume drag");

        trimStart->setValue(1.0);
        if (std::abs(data(QStringLiteral("trimStart")) - 1.0) > 1e-6)
            fail("trim start edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("trimStart"))) > 1e-6)
            fail("trim start undo failed");

        trimEnd->setValue(0.5);
        if (std::abs(data(QStringLiteral("trimEnd")) - 0.5) > 1e-6)
            fail("trim end edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("trimEnd"))) > 1e-6)
            fail("trim end undo failed");

        fadeIn->setValue(0.75);
        fadeOut->setValue(1.25);
        if (std::abs(data(QStringLiteral("fadeIn")) - 0.75) > 1e-6
            || std::abs(data(QStringLiteral("fadeOut")) - 1.25) > 1e-6)
            fail("fade edits were not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("fadeOut"))) > 1e-6)
            fail("fade out undo failed");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("fadeIn"))) > 1e-6)
            fail("fade in undo failed");

        start->setValue(30);
        if (std::abs(data(QStringLiteral("startFrame")) - 30.0) > 1e-6)
            fail("start frame edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("startFrame"))) > 1e-6)
            fail("start frame undo failed");

        // The "take" button trims at the playhead and moves the clip start; the
        // two data fields must undo together.
        window.goToFrame(60);
        QApplication::processEvents();
        auto* capture =
            window.findChild<QPushButton*>(QStringLiteral("audioTrimStartFromPlayhead"));
        if (!capture)
        {
            fail("trim capture button missing");
        }
        else
        {
            capture->click();
            QApplication::processEvents();
            if (std::abs(data(QStringLiteral("startFrame")) - 60.0) > 1e-6
                || std::abs(data(QStringLiteral("trimStart")) - 2.0) > 1e-6)
            {
                fail("trim capture did not move start and trim together");
            }
            undo->trigger();
            QApplication::processEvents();
            if (std::abs(data(QStringLiteral("startFrame"))) > 1e-6
                || std::abs(data(QStringLiteral("trimStart"))) > 1e-6)
            {
                fail("trim capture undo did not restore both fields");
            }
        }

        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("timelineTabs"));
        auto* wave = window.findChild<pvf::WaveformView*>();
        if (!tabs || !wave)
        {
            fail("waveform page missing");
        }
        else
        {
            tabs->setCurrentWidget(wave);
            QApplication::processEvents();
            if (wave->laneCountForTest() != 3)
                fail("waveform page did not create one lane per audio object");

            const double zoomBefore = wave->pixelsPerFrameForTest();
            const QPointF pos(500, 80);
            QWheelEvent zoomEvent(pos, wave->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                                  Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase,
                                  false);
            QApplication::sendEvent(wave, &zoomEvent);
            if (!(wave->pixelsPerFrameForTest() > zoomBefore))
                fail("ctrl+wheel did not zoom the waveform");

            wave->setPixelsPerFrameForTest(6.0);
            window.goToFrame(30);
            QApplication::processEvents();
            const QPoint seekPos(wave->frameXForTest(45.0),
                                 wave->laneRectForTest(0).center().y());
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(seekPos),
                              QPointF(wave->mapToGlobal(seekPos)), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(wave, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(seekPos),
                                QPointF(wave->mapToGlobal(seekPos)), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(wave, &release);
            QApplication::processEvents();
            if (window.currentFrameForTest() != 45)
                fail("clicking the waveform did not seek the playhead");

            window.goToFrame(0);
            QApplication::processEvents();
            const int scrollBefore = wave->horizontalScrollForTest();
            QWheelEvent scrollEvent(pos, wave->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                                    Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase,
                                    false);
            QApplication::sendEvent(wave, &scrollEvent);
            if (wave->horizontalScrollForTest() == scrollBefore)
                fail("shift+wheel did not scroll the waveform page");

            const int verticalBefore = wave->verticalScrollForTest();
            QWheelEvent verticalEvent(pos, wave->mapToGlobal(pos), QPoint(),
                                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                                      Qt::NoScrollPhase, false);
            QApplication::sendEvent(wave, &verticalEvent);
            if (wave->verticalScrollForTest() == verticalBefore)
                fail("wheel did not scroll waveform lanes");

            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_waveform_page.png"));
        }

        qInfo() << "audio-undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int clipboardTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* folder = project->addFolder(QStringLiteral("Group"));
    const pvf::EntityId folderId = folder->id;
    pvf::Entity* child = project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    const pvf::EntityId childId = child->id;
    project->setEntityParent(childId, folderId);
    project->setValue(childId, QStringLiteral("x"), 0, 50.0, true);
    project->setValue(childId, QStringLiteral("x"), 50, 150.0, true);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, folderId, childId] {
        int failures = 0;
        const int before = project->entities().size();
        window.copyEntityForTest(folderId);
        window.pasteEntityForTest();
        QApplication::processEvents();

        const pvf::EntityId newRoot = window.selectedEntityForTest();
        const pvf::Entity* root = project->entity(newRoot);
        if (project->entities().size() != before + 2 || !root || !root->isFolder())
        {
            qCritical() << "clipboard-test: paste did not copy the subtree"
                        << project->entities().size() << before
                        << (root ? root->name : QString());
            ++failures;
        }
        else
        {
            const pvf::Entity* copiedChild = nullptr;
            for (const pvf::Entity& entity : project->entities())
                if (entity.parent == newRoot
                    && entity.type == pvf::EntityType::Vector)
                    copiedChild = &entity;
            if (!copiedChild || copiedChild->id == childId
                || std::abs(project->value(copiedChild->id, QStringLiteral("x"), 25)
                            - 100.0) > 1e-6)
            {
                qCritical() << "clipboard-test: child or keyframes lost";
                ++failures;
            }
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            if (project->entities().size() != before)
            {
                qCritical() << "clipboard-test: undo did not remove the paste";
                ++failures;
            }
        }

        window.duplicateEntityForTest(folderId);
        QApplication::processEvents();
        if (project->entities().size() != before + 2)
        {
            qCritical() << "clipboard-test: duplicate failed";
            ++failures;
        }

        qInfo() << "clipboard-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int libraryPickerTest()
{
    const QString firstPath =
        QDir::tempPath() + QStringLiteral("/pvf_pick_a.png");
    const QString secondPath =
        QDir::tempPath() + QStringLiteral("/pvf_pick_b.png");
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(firstPath))
        return 1;
    image.fill(Qt::blue);
    if (!image.save(secondPath))
        return 1;

    auto* project = new pvf::Project;
    project->addImage(firstPath);
    project->addImage(secondPath);
    project->addVector(0, 80, 80, Qt::red, Qt::white, 0);

    int failures = 0;
    const QVector<int> images =
        pvf::AssetLibraryPanel::mediaTemplateIndices(*project,
                                                     pvf::EntityType::Image);
    if (images.size() != 2)
    {
        qCritical() << "library-picker-test: image entries" << images.size();
        ++failures;
    }
    for (int index : images)
        if (index < 0 || index >= project->templates().size()
            || project->templates()[index].type != pvf::EntityType::Image
            || project->templates()[index].path.isEmpty())
        {
            qCritical() << "library-picker-test: wrong entry" << index;
            ++failures;
            break;
        }

    const QVector<int> audios =
        pvf::AssetLibraryPanel::mediaTemplateIndices(*project,
                                                     pvf::EntityType::Audio);
    if (!audios.isEmpty())
    {
        qCritical() << "library-picker-test: audio should be empty"
                    << audios.size();
        ++failures;
    }

    // Open the real picker, check that it lists the two library images and
    // grab it for visual inspection, then cancel.
    QTimer::singleShot(100, [&failures] {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || !dialog->isVisible()
                || dialog->windowTitle() != QStringLiteral("更换素材"))
                continue;
            if (auto* list = dialog->findChild<QListWidget*>())
                if (list->count() != 2)
                {
                    qCritical() << "library-picker-test: dialog entries"
                                << list->count();
                    ++failures;
                }
            dialog->grab().save(QStringLiteral("/tmp/pvf_media_picker.png"));
            dialog->reject();
            return;
        }
    });
    const QString chosen = pvf::AssetLibraryPanel::pickMedia(
        nullptr, project, pvf::EntityType::Image,
        QStringLiteral("图片 (*.png *.jpg)"));
    if (!chosen.isEmpty())
    {
        qCritical() << "library-picker-test: cancel returned a path" << chosen;
        ++failures;
    }

    QFile::remove(firstPath);
    QFile::remove(secondPath);
    qInfo() << "library-picker-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int replaceSourceTest()
{
    const QString firstPath =
        QDir::tempPath() + QStringLiteral("/pvf_replace_a.png");
    const QString secondPath =
        QDir::tempPath() + QStringLiteral("/pvf_replace_b.png");
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(firstPath))
        return 1;
    image.fill(Qt::blue);
    if (!image.save(secondPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::Entity* entity = project->addImage(firstPath);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 0, 123.0, true);
    const pvf::Entity before = *project->entity(id);

    const pvf::MediaImportResult result = pvf::processMediaFile(secondPath, QString());
    QString error;
    const QVector<pvf::EntityId> changed =
        project->replaceEntitySource(id, result, &error);
    const pvf::Entity* replaced = project->entity(id);
    int failures = 0;
    if (!error.isEmpty() || changed.size() != 1 || !replaced
        || replaced->sourcePath != secondPath
        || replaced->image.pixelColor(32, 32) != QColor(Qt::blue)
        || std::abs(project->value(id, QStringLiteral("x"), 0) - 123.0) > 1e-6)
    {
        qCritical() << "replace-test: swap failed" << error
                    << (replaced ? replaced->sourcePath : QString());
        ++failures;
    }
    else
    {
        const pvf::Entity after = *replaced;
        pvf::ReplaceEntitySourcesCommand command({before}, {after});
        command.undo(*project);
        const pvf::Entity* undone = project->entity(id);
        if (!undone || undone->sourcePath != firstPath
            || undone->image.pixelColor(32, 32) != QColor(Qt::red))
        {
            qCritical() << "replace-test: undo failed";
            ++failures;
        }
        command.redo(*project);
        const pvf::Entity* redone = project->entity(id);
        if (!redone || redone->sourcePath != secondPath)
        {
            qCritical() << "replace-test: redo failed";
            ++failures;
        }
    }

    QFile::remove(firstPath);
    QFile::remove(secondPath);
    qInfo() << "replace-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int layerTest()
{
    auto* project = new pvf::Project;
    project->width = 200;
    project->height = 200;
    // The first row of the layer list is the top layer.
    project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    project->addVector(0, 100, 100, Qt::blue, Qt::white, 0);

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        QTransform worldToScreen;
        worldToScreen.translate(100, 100);
        pvf::SceneRenderer::drawEntities(painter, *project, 0, worldToScreen, false);
    }
    const QColor center = image.pixelColor(100, 100);
    qInfo() << "layer-test: overlap colour" << center;
    if (center.red() < 200 || center.blue() > 60)
    {
        qCritical() << "layer-test: bottom row is drawn on top" << center;
        return 1;
    }
    image.save(QStringLiteral("/tmp/pvforge_layers.png"));
    return 0;
}

int frameRestoreTest(QApplication& app)
{
    const QString path =
        QDir::tempPath() + QStringLiteral("/pvforge_frame_test.pvproj");
    auto* seed = new pvf::Project;
    pvf::Entity* entity = seed->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    seed->setValue(entity->id, QStringLiteral("x"), 120, 50.0, true);
    QString error;
    if (!seed->save(path, &error))
    {
        qCritical() << "frame-restore-test: save failed" << error;
        return 1;
    }

    auto* first = new pvf::Project;
    if (!first->load(path, &error))
    {
        qCritical() << "frame-restore-test: first load failed" << error;
        return 1;
    }
    auto* firstWindow = new pvf::MainWindow(first);
    firstWindow->resize(1000, 700);
    firstWindow->show();

    QTimer::singleShot(400, firstWindow, [&app, firstWindow, path] {
        firstWindow->goToFrame(42);
        firstWindow->close();

        auto* second = new pvf::Project;
        QString err;
        if (!second->load(path, &err))
        {
            qCritical() << "frame-restore-test: second load failed" << err;
            app.exit(1);
            return;
        }
        auto* secondWindow = new pvf::MainWindow(second);
        secondWindow->resize(1000, 700);
        secondWindow->show();
        QTimer::singleShot(300, secondWindow,
                           [&app, secondWindow, firstWindow, path] {
                               const int frame = secondWindow->currentFrame();
                               qInfo() << "frame-restore-test: restored frame" << frame;
                               QFile::remove(path);
                               app.exit(frame == 42 ? 0 : 1);
                           });
    });
    return app.exec();
}

int keyframeEditTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        pvf::Entity* entity = project->addVector(0, 200, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);
        QApplication::processEvents();

        auto* opacity =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("row_opacity"));
        auto* keyButton =
            window.findChild<QPushButton*>(QStringLiteral("keyframeButton"));
        if (!opacity || !keyButton)
        {
            qCritical() << "keyframe-edit-test: widgets missing";
            app.exit(1);
            return;
        }

        // Key the element at frame 10, then at frame 5, using the button
        // exactly like a user would.
        window.goToFrame(10);
        keyButton->click();
        QApplication::processEvents();
        window.goToFrame(5);
        keyButton->click();
        QApplication::processEvents();
        // Now make the element appear at frame 5: keyframe 0 at frame 4.
        window.goToFrame(4);
        opacity->setValue(0);
        QApplication::processEvents();

        const auto at = [&](int frame) {
            return project->value(id, QStringLiteral("opacity"), frame);
        };
        qInfo() << "keyframe-edit-test: opacity at 4/5/6/10" << at(4) << at(5) << at(6)
                << at(10);
        if (std::abs(at(4)) > 1e-6)
        {
            qCritical() << "keyframe-edit-test: frame 4 is not transparent";
            ++failures;
        }
        if (at(5) < 0.99 || at(6) < 0.99 || at(10) < 0.99)
        {
            qCritical() << "keyframe-edit-test: later frames became transparent";
            ++failures;
        }
        window.goToFrame(5);
        QApplication::processEvents();
        const double spinAt5 = opacity->value();
        window.goToFrame(10);
        QApplication::processEvents();
        const double spinAt10 = opacity->value();
        qInfo() << "keyframe-edit-test: spin at 5/10" << spinAt5 << spinAt10;
        if (spinAt5 < 99.0 || spinAt10 < 99.0)
        {
            qCritical() << "keyframe-edit-test: panel shows transparency later";
            ++failures;
        }

        // The diamond next to a property adds and removes a single key.
        if (auto* keyOpacity =
                window.findChild<QToolButton*>(QStringLiteral("key_opacity")))
        {
            const auto keyCountAt = [&](double frame) {
                const pvf::Entity* entity = project->entity(id);
                const auto track = entity->tracks.constFind(QStringLiteral("opacity"));
                if (track == entity->tracks.constEnd())
                    return 0;
                int count = 0;
                for (const pvf::Keyframe& key : track->keys)
                    if (std::abs(key.frame - frame) < 1e-6)
                        ++count;
                return count;
            };
            window.goToFrame(7);
            QApplication::processEvents();
            keyOpacity->click();
            QApplication::processEvents();
            if (keyCountAt(7) != 1)
            {
                qCritical() << "keyframe-edit-test: diamond did not add a key";
                ++failures;
            }
            if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            {
                undo->trigger();
                QApplication::processEvents();
                if (keyCountAt(7) != 0)
                {
                    qCritical() << "keyframe-edit-test: undo did not remove the key";
                    ++failures;
                }
            }
            window.goToFrame(5);
            QApplication::processEvents();
            keyOpacity->click();
            QApplication::processEvents();
            if (keyCountAt(5) != 0)
            {
                qCritical() << "keyframe-edit-test: diamond did not remove the key";
                ++failures;
            }
            if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            {
                undo->trigger();
                QApplication::processEvents();
                if (keyCountAt(5) != 1)
                {
                    qCritical() << "keyframe-edit-test: undo did not restore the key";
                    ++failures;
                }
            }
        }
        else
        {
            qCritical() << "keyframe-edit-test: key diamond missing";
            ++failures;
        }

        const pvf::Entity* check = project->entity(id);
        const auto it = check->tracks.constFind(QStringLiteral("opacity"));
        if (it == check->tracks.constEnd())
        {
            qCritical() << "keyframe-edit-test: opacity track missing";
            ++failures;
        }
        else
        {
            qInfo() << "keyframe-edit-test: keys" << it->keys.size();
            for (const pvf::Keyframe& key : it->keys)
                qInfo() << "   key frame" << key.frame << "value" << key.value;
        }
        window.goToFrame(5);
        QApplication::processEvents();
        window.grab().toImage().save(
            QStringLiteral("/tmp/pvforge_keyframes.png"));
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int importAsyncTest(QApplication& app)
{
    const QString dir = QDir::tempPath() + QStringLiteral("/pvforge_async");
    QDir().mkpath(dir);
    const QString imageA = QDir(dir).filePath(QStringLiteral("alpha.png"));
    const QString imageB = QDir(dir).filePath(QStringLiteral("beta.png"));
    QImage bitmapA(64, 64, QImage::Format_ARGB32);
    bitmapA.fill(Qt::red);
    QImage bitmapB(64, 64, QImage::Format_ARGB32);
    bitmapB.fill(Qt::green);
    if (!bitmapA.save(imageA) || !bitmapB.save(imageB))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    if (!project->save(QDir(dir).filePath(QStringLiteral("test.pvproj"))))
        return 1;

    QTimer::singleShot(400, &window, [&app, &window, project, imageA, imageB, dir] {
        int failures = 0;
        bool finished = false;
        QEventLoop loop;

        window.importMediaFiles(
            {imageA, imageB}, [&](const QVector<pvf::MediaImportResult>& results) {
                if (results.size() != 2)
                {
                    qCritical() << "import-async-test: result count" << results.size();
                    ++failures;
                }
                for (const pvf::MediaImportResult& result : results)
                {
                    if (!result.error.isEmpty() || result.importedPath.isEmpty())
                    {
                        qCritical() << "import-async-test: import error" << result.error;
                        ++failures;
                        continue;
                    }
                    project->addMediaTemplate(result.importedPath, result.type,
                                              result.thumbnail, result.duration,
                                              result.displayName, result.audio);
                }
                finished = true;
                loop.quit();
            });

        QTimer::singleShot(8000, &loop, &QEventLoop::quit);
        loop.exec();

        if (!finished)
        {
            qCritical() << "import-async-test: timeout";
            ++failures;
        }
        if (project->templateCount() != 2)
        {
            qCritical() << "import-async-test: templates" << project->templateCount();
            ++failures;
        }
        if (auto* bar = window.findChild<QProgressBar*>(QStringLiteral("importProgress")))
        {
            if (bar->isVisible())
            {
                qCritical() << "import-async-test: progress bar still visible";
                ++failures;
            }
        }
        bool namesOk = false;
        for (int i = 0; i < project->templateCount(); ++i)
        {
            const pvf::Template* tmpl = project->templateAt(i);
            if (tmpl && (tmpl->name == QStringLiteral("alpha")
                         || tmpl->name == QStringLiteral("beta")))
                namesOk = true;
        }
        if (!namesOk)
        {
            qCritical() << "import-async-test: display names lost";
            ++failures;
        }

        QDir(dir).removeRecursively();
        qInfo() << "import-async-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int shapeTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        const int shapes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        for (int i = 0; i < 9; ++i)
        {
            pvf::Entity* e =
                project->addVector(shapes[i], 120, 90, Qt::red, Qt::white, 3);
            if (!e)
            {
                qCritical() << "shape-test: create failed" << shapes[i];
                ++failures;
                continue;
            }
            project->setValue(e->id, QStringLiteral("x"), 0, -520.0 + (i % 3) * 520.0,
                              false);
            project->setValue(e->id, QStringLiteral("y"), 0, -260.0 + (i / 3) * 260.0,
                              false);
        }
        if (project->entities().size() != 9)
        {
            qCritical() << "shape-test: entity count" << project->entities().size();
            ++failures;
        }

        // Custom parameters survive on the entity.
        const pvf::Entity& starEntity = project->entities()[6];
        if (starEntity.vectorSides() != 5 || starEntity.vectorInnerRatio() < 0.44)
        {
            qCritical() << "shape-test: star defaults" << starEntity.vectorSides()
                        << starEntity.vectorInnerRatio();
            ++failures;
        }
        const pvf::Entity& roundedEntity = project->entities()[3];
        if (roundedEntity.cornerRadius() <= 0.0)
        {
            qCritical() << "shape-test: corner radius default";
            ++failures;
        }

        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_shapes.png"));
        qInfo() << "shape-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int lockTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!timeline || !viewport)
        {
            qCritical() << "lock-test: widgets missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        project->setValue(id, QStringLiteral("x"), 0, 0, true);
        project->setValue(id, QStringLiteral("x"), 30, 400, true);

        // A selected entity is deselected when it becomes locked.
        window.setSelectedEntityForTest(id);
        if (window.selectedEntityForTest() != id)
        {
            qCritical() << "lock-test: entity not selected before locking";
            ++failures;
        }
        project->setEntityFlag(id, pvf::EntityFlag::Locked, true);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked entity stayed selected"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Values are frozen.
        project->setValue(id, QStringLiteral("x"), 15, 999, true);
        if (std::abs(project->value(id, QStringLiteral("x"), 15) - 200.0) > 1e-6)
        {
            qCritical() << "lock-test: value changed while locked";
            ++failures;
        }

        // Keyframes cannot be deleted.
        if (!project->removeKeyframesAt(id, 30).isEmpty())
        {
            qCritical() << "lock-test: keyframe removed while locked";
            ++failures;
        }

        // Clicking a locked entity in the viewport must not select it.
        const QPoint center = viewport->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(center),
                          QPointF(viewport->mapToGlobal(center)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(center + QPoint(120, 0)),
                         QPointF(viewport->mapToGlobal(center + QPoint(120, 0))), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(center + QPoint(120, 0)),
                            QPointF(viewport->mapToGlobal(center + QPoint(120, 0))),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &release);
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "lock-test: locked entity moved";
            ++failures;
        }
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked entity selected in viewport"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Selecting it programmatically is rejected too.
        window.setSelectedEntityForTest(id);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked entity selectable programmatically";
            ++failures;
        }

        // Clicking the locked row name must not select it either.
        const QPoint rowName(10, timeline->rowButtonRect(0, 0).center().y());
        QMouseEvent rowPress(QEvent::MouseButtonPress, QPointF(rowName),
                             QPointF(timeline->mapToGlobal(rowName)), Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &rowPress);
        QMouseEvent rowRelease(QEvent::MouseButtonRelease, QPointF(rowName),
                               QPointF(timeline->mapToGlobal(rowName)), Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &rowRelease);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked row selected in timeline"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Row buttons stay inside the name column.
        if (timeline->rowButtonRect(0, 0).right() > 180
            || timeline->rowButtonRect(0, 2).left() < 0)
        {
            qCritical() << "lock-test: row buttons outside the name column";
            ++failures;
        }

        // Unlocking restores selection and editing.
        project->setEntityFlag(id, pvf::EntityFlag::Locked, false);
        window.setSelectedEntityForTest(id);
        if (window.selectedEntityForTest() != id)
        {
            qCritical() << "lock-test: unlocked entity not selectable";
            ++failures;
        }
        project->setValue(id, QStringLiteral("x"), 15, 999, true);
        if (std::abs(project->value(id, QStringLiteral("x"), 15) - 999.0) > 1e-6)
        {
            qCritical() << "lock-test: unlock did not restore editing";
            ++failures;
        }

        qInfo() << "lock-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int packTest(QApplication& app)
{
    Q_UNUSED(app);
    const QString external = QDir::tempPath() + QStringLiteral("/pvforge_external.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::green);
    if (!img.save(external))
        return 1;

    const QString projectDir = QDir::tempPath() + QStringLiteral("/pvforge_pack_project");
    QDir().mkpath(projectDir);
    const QString projectPath = QDir(projectDir).filePath(QStringLiteral("test.pvproj"));

    pvf::Project project;
    QString error;
    if (!project.save(projectPath, &error))
        return 1;

    int failures = 0;

    // Packing is off by default.
    if (project.packAssets)
    {
        qCritical() << "pack-test: packing should be off by default";
        ++failures;
    }

    // With packing off the original path is kept.
    if (project.importAsset(external) != external)
    {
        qCritical() << "pack-test: import copied while packing is off";
        ++failures;
    }

    // With packing on the file is copied into the project folder.
    project.packAssets = true;
    const QString packed = project.importAsset(external);
    if (packed == external || !QFile::exists(packed)
        || !packed.startsWith(project.assetDirectory()))
    {
        qCritical() << "pack-test: import not packed" << packed;
        ++failures;
    }

    // Saving stores the path relative to the project file.
    project.addImage(packed);
    if (!project.save(projectPath, &error))
        return 1;

    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QString stored = document.object()
                               .value(QStringLiteral("entities"))
                               .toArray()
                               .first()
                               .toObject()
                               .value(QStringLiteral("sourcePath"))
                               .toString();
    if (QFileInfo(stored).isAbsolute() || !stored.startsWith(QStringLiteral("assets")))
    {
        qCritical() << "pack-test: path not stored relative" << stored;
        ++failures;
    }

    // Loading resolves it back to an absolute path.
    pvf::Project loaded;
    if (!loaded.load(projectPath, &error))
    {
        qCritical() << "pack-test: load failed" << error;
        ++failures;
    }
    else if (loaded.entities().isEmpty()
             || loaded.entities().first().sourcePath != packed)
    {
        qCritical() << "pack-test: path not resolved";
        ++failures;
    }
    else if (loaded.entities().first().image.isNull())
    {
        qCritical() << "pack-test: packed image not reloaded";
        ++failures;
    }

    // Asset import recognises images and rejects unsupported files.
    pvf::Project importProject;
    const QString importImage = QDir::tempPath() + QStringLiteral("/pvforge_import.png");
    QImage importBitmap(320, 200, QImage::Format_ARGB32);
    importBitmap.fill(Qt::magenta);
    if (!importBitmap.save(importImage))
        return 1;
    const QString importText = QDir::tempPath() + QStringLiteral("/pvforge_import.txt");
    {
        QFile file(importText);
        file.open(QIODevice::WriteOnly);
        file.write("not a media file");
    }

    if (importProject.importMediaFile(importImage).isEmpty()
        || importProject.templateCount() != 1
        || importProject.templates().first().type != pvf::EntityType::Image)
    {
        qCritical() << "pack-test: image import failed";
        ++failures;
    }
    if (importProject.importMediaFile(importImage).isEmpty()
        || importProject.templateCount() != 1)
    {
        qCritical() << "pack-test: duplicate import not deduplicated";
        ++failures;
    }
    QString importError;
    if (!importProject.importMediaFile(importText, &importError).isEmpty()
        || importProject.templateCount() != 1)
    {
        qCritical() << "pack-test: unsupported file accepted";
        ++failures;
    }
    QFile::remove(importImage);
    QFile::remove(importText);

    // Packing deduplicates identical files and numbers same-name clashes.
    const QString sourceDirA = QDir::tempPath() + QStringLiteral("/pvforge_pack_a");
    const QString sourceDirB = QDir::tempPath() + QStringLiteral("/pvforge_pack_b");
    QDir().mkpath(sourceDirA);
    QDir().mkpath(sourceDirB);
    const QString sameNameA = QDir(sourceDirA).filePath(QStringLiteral("same.png"));
    const QString sameNameB = QDir(sourceDirB).filePath(QStringLiteral("same.png"));
    QImage redImage(64, 64, QImage::Format_ARGB32);
    redImage.fill(Qt::red);
    QImage greenImage(64, 64, QImage::Format_ARGB32);
    greenImage.fill(Qt::green);
    if (!redImage.save(sameNameA) || !greenImage.save(sameNameB))
        return 1;

    const QString dedupeDir = QDir::tempPath() + QStringLiteral("/pvforge_pack_dedupe");
    QDir().mkpath(dedupeDir);
    pvf::Project dedupeProject;
    if (!dedupeProject.save(QDir(dedupeDir).filePath(QStringLiteral("test.pvproj"))))
        return 1;
    dedupeProject.packAssets = true;

    const QString packedFirst = dedupeProject.importAsset(sameNameA);
    const QString packedAgain = dedupeProject.importAsset(sameNameA);
    const QString packedSecond = dedupeProject.importAsset(sameNameB);
    if (packedFirst != packedAgain)
    {
        qCritical() << "pack-test: repeat import created another copy";
        ++failures;
    }
    if (packedFirst == packedSecond)
    {
        qCritical() << "pack-test: same-name content collision";
        ++failures;
    }
    if (!QFile::exists(packedFirst) || !QFile::exists(packedSecond))
    {
        qCritical() << "pack-test: packed files missing";
        ++failures;
    }
    const int packedFiles =
        QDir(dedupeProject.assetDirectory()).entryList(QDir::Files).size();
    if (packedFiles != 2)
    {
        qCritical() << "pack-test: asset folder exploded" << packedFiles;
        ++failures;
    }
    if (dedupeProject.copyIntoProject(packedFirst) != packedFirst)
    {
        qCritical() << "pack-test: already packed file copied again";
        ++failures;
    }

    // Stored names are content hashes.
    if (QFileInfo(packedFirst).completeBaseName().size() != 64)
    {
        qCritical() << "pack-test: asset is not content addressed" << packedFirst;
        ++failures;
    }

    // Identical content with a different file name maps to the same asset.
    const QString contentCopy = QDir(sourceDirB).filePath(QStringLiteral("copy.png"));
    if (!QFile::copy(sameNameA, contentCopy))
        return 1;
    if (dedupeProject.importAsset(contentCopy) != packedFirst)
    {
        qCritical() << "pack-test: identical content not deduplicated";
        ++failures;
    }
    QFile::remove(contentCopy);

    // The library keeps the original display name despite the hash path.
    QString mediaError;
    const QString mediaPath = dedupeProject.importMediaFile(sameNameA, &mediaError);
    bool displayNameOk = false;
    for (int i = 0; i < dedupeProject.templateCount(); ++i)
    {
        const pvf::Template* tmpl = dedupeProject.templateAt(i);
        if (tmpl && tmpl->path == mediaPath && tmpl->name == QStringLiteral("same"))
            displayNameOk = true;
    }
    if (!displayNameOk)
    {
        qCritical() << "pack-test: display name lost";
        ++failures;
    }
    QDir(sourceDirA).removeRecursively();
    QDir(sourceDirB).removeRecursively();
    QDir(dedupeDir).removeRecursively();

    // Library templates keep a small thumbnail, never the full-size bitmap.
    const QString bigPath = QDir::tempPath() + QStringLiteral("/pvforge_big.png");
    QImage big(1600, 1200, QImage::Format_ARGB32);
    big.fill(Qt::blue);
    if (!big.save(bigPath))
        return 1;
    pvf::Project bigProject;
    bigProject.addImage(bigPath);
    const QImage thumb = bigProject.templates().first().thumbnail;
    if (thumb.width() > 320 || thumb.height() > 240)
    {
        qCritical() << "pack-test: thumbnail not downscaled" << thumb.size();
        ++failures;
    }
    QFile::remove(bigPath);

    QDir(projectDir).removeRecursively();
    QFile::remove(external);
    qInfo() << "pack-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int projectLimitTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;

    QTemporaryDir tempDir;
    QTemporaryDir outsideDir;
    if (!tempDir.isValid() || !outsideDir.isValid())
    {
        qCritical() << "project-limit-test: cannot create temp directories";
        return 1;
    }

    const QString projectRoot = tempDir.path();
    const QString projectPath = QDir(projectRoot).filePath(QStringLiteral("limit.pvproj"));

    auto makeProject = [](int version) {
        QJsonObject root;
        root[QStringLiteral("format")] = QStringLiteral("pvforge");
        root[QStringLiteral("version")] = version;
        root[QStringLiteral("name")] = QStringLiteral("limit test");
        root[QStringLiteral("entities")] = QJsonArray();
        root[QStringLiteral("templates")] = QJsonArray();
        root[QStringLiteral("scenes")] = QJsonArray();
        root[QStringLiteral("annotations")] = QJsonArray();
        return root;
    };
    auto makeEntity = [](int id, const QString& type) {
        QJsonObject entity;
        entity[QStringLiteral("id")] = id;
        entity[QStringLiteral("type")] = type;
        entity[QStringLiteral("parent")] = -1;
        entity[QStringLiteral("name")] = QStringLiteral("entity %1").arg(id);
        entity[QStringLiteral("sourcePath")] = QString();
        entity[QStringLiteral("data")] = QJsonObject();
        entity[QStringLiteral("values")] = QJsonObject();
        entity[QStringLiteral("tracks")] = QJsonObject();
        return entity;
    };
    auto writeProject = [](const QString& path, const QJsonObject& root) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
        return file.write(data) == data.size();
    };
    auto writeRaw = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(data) == data.size();
    };
    auto rejected = [&](const QString& label, const QJsonObject& root,
                        const QString& expectedFragment) {
        if (!writeProject(projectPath, root))
        {
            qCritical() << "project-limit-test:" << label << "write failed";
            ++failures;
            return;
        }
        pvf::Project project;
        QString error;
        if (project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test:" << label << "was accepted";
            ++failures;
        }
        else if (!expectedFragment.isEmpty() && !error.contains(expectedFragment))
        {
            qCritical() << "project-limit-test:" << label << "unexpected error" << error;
            ++failures;
        }
    };

    // A safe relative path inside the project directory still loads.
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const QString assetsDir = QDir(projectRoot).filePath(QStringLiteral("assets"));
    QDir().mkpath(assetsDir);
    const QString packedImage = QDir(assetsDir).filePath(QStringLiteral("pic.png"));
    if (!image.save(packedImage))
        return 1;
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("image"));
        entity[QStringLiteral("sourcePath")] = QStringLiteral("assets/pic.png");
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;

        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test: safe relative path rejected" << error;
            ++failures;
        }
        else if (project.entities().size() != 1
                 || project.entities().first().sourcePath != packedImage
                 || project.entities().first().image.isNull())
        {
            qCritical() << "project-limit-test: safe relative path not resolved";
            ++failures;
        }
    }

    // Empty, malformed and non-object files are reported clearly.
    {
        const QString emptyPath = QDir(projectRoot).filePath(QStringLiteral("empty.pvproj"));
        if (!writeRaw(emptyPath, QByteArray()))
            return 1;
        pvf::Project project;
        QString error;
        if (project.load(emptyPath, &error) || !error.contains(QStringLiteral("为空")))
        {
            qCritical() << "project-limit-test: empty file accepted" << error;
            ++failures;
        }
    }
    {
        const QString badPath = QDir(projectRoot).filePath(QStringLiteral("bad.pvproj"));
        if (!writeRaw(badPath, QByteArrayLiteral("{not json")))
            return 1;
        pvf::Project project;
        QString error;
        if (project.load(badPath, &error) || error.isEmpty())
        {
            qCritical() << "project-limit-test: invalid JSON accepted";
            ++failures;
        }
    }
    {
        const QString arrayPath = QDir(projectRoot).filePath(QStringLiteral("array.pvproj"));
        if (!writeRaw(arrayPath, QByteArrayLiteral("[]")))
            return 1;
        pvf::Project project;
        QString error;
        if (project.load(arrayPath, &error) || !error.contains(QStringLiteral("根节点")))
        {
            qCritical() << "project-limit-test: non-object root accepted" << error;
            ++failures;
        }
    }

    // Files larger than 64 MiB are rejected before parsing.
    {
        const QString bigPath = QDir(projectRoot).filePath(QStringLiteral("big.pvproj"));
        QFile bigFile(bigPath);
        if (!bigFile.open(QIODevice::WriteOnly)
            || !bigFile.resize(64LL * 1024 * 1024 + 1))
        {
            qCritical() << "project-limit-test: cannot create oversized file";
            return 1;
        }
        bigFile.close();
        pvf::Project project;
        QString error;
        if (project.load(bigPath, &error)
            || !error.contains(QStringLiteral("过大")))
        {
            qCritical() << "project-limit-test: oversized file accepted" << error;
            ++failures;
        }
        QFile::remove(bigPath);
    }

    // Entity count limit.
    {
        QJsonObject root = makeProject(4);
        QJsonArray entities;
        for (int i = 1; i <= 5001; ++i)
            entities.append(makeEntity(i, QStringLiteral("folder")));
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("5001 entities"), root, QStringLiteral("实体数量"));
    }

    // Total keyframe limit (20 full tracks plus one extra key).
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("folder"));
        QJsonObject tracks;
        for (int track = 0; track < 20; ++track)
        {
            QJsonArray keys;
            for (int key = 0; key < 10000; ++key)
            {
                QJsonObject keyframe;
                keyframe[QStringLiteral("f")] = key;
                keyframe[QStringLiteral("v")] = 0;
                keys.append(keyframe);
            }
            tracks[QStringLiteral("p%1").arg(track)] = keys;
        }
        QJsonArray extra;
        QJsonObject extraKey;
        extraKey[QStringLiteral("f")] = 0;
        extraKey[QStringLiteral("v")] = 0;
        extra.append(extraKey);
        tracks[QStringLiteral("extra")] = extra;
        entity[QStringLiteral("tracks")] = tracks;
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("200001 keyframes"), root, QStringLiteral("关键帧总数"));
    }

    // Per-track keyframe and template count limits.
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("folder"));
        QJsonArray keys;
        for (int key = 0; key < 10001; ++key)
        {
            QJsonObject keyframe;
            keyframe[QStringLiteral("f")] = key;
            keyframe[QStringLiteral("v")] = 0;
            keys.append(keyframe);
        }
        QJsonObject tracks;
        tracks[QStringLiteral("x")] = keys;
        entity[QStringLiteral("tracks")] = tracks;
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("10001 keys per track"), root,
                 QStringLiteral("关键帧数量"));
    }
    {
        QJsonObject root = makeProject(4);
        QJsonArray templates;
        for (int i = 0; i < 1001; ++i)
        {
            QJsonObject tmpl;
            tmpl[QStringLiteral("id")] = QStringLiteral("t%1").arg(i);
            tmpl[QStringLiteral("type")] = QStringLiteral("folder");
            templates.append(tmpl);
        }
        root[QStringLiteral("templates")] = templates;
        rejected(QStringLiteral("1001 templates"), root,
                 QStringLiteral("模板数量"));
    }

    // Single string limit.
    {
        QJsonObject root = makeProject(4);
        root[QStringLiteral("description")] =
            QString(1024 * 1024 + 1, QLatin1Char('x'));
        rejected(QStringLiteral("oversized string"), root, QStringLiteral("字符串长度"));
    }

    // Version 5 and unknown entity types are rejected, not coerced.
    {
        QJsonObject root = makeProject(5);
        rejected(QStringLiteral("version 5"), root, QStringLiteral("过新"));
    }
    {
        QJsonObject root = makeProject(4);
        QJsonArray entities;
        entities.append(makeEntity(1, QStringLiteral("hologram")));
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("unknown entity type"), root,
                 QStringLiteral("未知类型"));
    }
    {
        QJsonObject root = makeProject(4);
        QJsonObject tmpl;
        tmpl[QStringLiteral("id")] = QStringLiteral("t1");
        tmpl[QStringLiteral("type")] = QStringLiteral("hologram");
        QJsonArray templates;
        templates.append(tmpl);
        root[QStringLiteral("templates")] = templates;
        rejected(QStringLiteral("unknown template type"), root,
                 QStringLiteral("未知类型"));
    }

    // Relative paths may not escape the project directory.
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("image"));
        entity[QStringLiteral("sourcePath")] = QStringLiteral("../outside.png");
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("../ escape"), root, QStringLiteral("超出工程目录"));
    }

    // A symlink inside the project pointing outside is rejected.
    // Absolute paths stay supported for the user's own external media.
    const QString outsideImage =
        QDir(outsideDir.path()).filePath(QStringLiteral("outside.png"));
    if (!image.save(outsideImage))
        return 1;
    {
        const QString linkPath = QDir(assetsDir).filePath(QStringLiteral("escape.png"));
        QFile::remove(linkPath);
        if (QFile::link(outsideImage, linkPath)
            && QFileInfo(linkPath).isSymLink())
        {
            QJsonObject root = makeProject(4);
            QJsonObject entity = makeEntity(1, QStringLiteral("image"));
            entity[QStringLiteral("sourcePath")] = QStringLiteral("assets/escape.png");
            QJsonArray entities;
            entities.append(entity);
            root[QStringLiteral("entities")] = entities;
            rejected(QStringLiteral("symlink escape"), root,
                     QStringLiteral("符号链接"));
        }
        QFile::remove(linkPath);
    }
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("image"));
        entity[QStringLiteral("sourcePath")] = outsideImage;
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;
        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test: absolute path rejected" << error;
            ++failures;
        }
        else if (project.entities().size() != 1
                 || project.entities().first().sourcePath != outsideImage
                 || project.entities().first().image.isNull())
        {
            qCritical() << "project-limit-test: absolute path not kept";
            ++failures;
        }
    }

    // A rejected load must not partially replace the current project.
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("folder"));
        entity[QStringLiteral("name")] = QStringLiteral("original");
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;

        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test: baseline load failed" << error;
            ++failures;
        }
        else
        {
            QJsonObject bad = makeProject(4);
            QJsonArray badEntities;
            badEntities.append(makeEntity(1, QStringLiteral("hologram")));
            bad[QStringLiteral("entities")] = badEntities;
            if (!writeProject(projectPath, bad))
                return 1;
            if (project.load(projectPath, &error))
            {
                qCritical() << "project-limit-test: invalid load accepted";
                ++failures;
            }
            else if (project.entities().size() != 1
                     || project.entities().first().name != QStringLiteral("original"))
            {
                qCritical() << "project-limit-test: failed load partially applied";
                ++failures;
            }
        }
    }

    // v1 projects still migrate (default scene is created).
    {
        QJsonObject root = makeProject(1);
        QJsonArray entities;
        entities.append(makeEntity(1, QStringLiteral("folder")));
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;
        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error)
            || project.scenes().size() != 1)
        {
            qCritical() << "project-limit-test: v1 migration failed" << error;
            ++failures;
        }
    }

    // Content-addressed copy: the file name must match the copied bytes, and
    // a tampered target is repaired instead of being trusted.
    {
        const QString hashDir = QDir(projectRoot).filePath(QStringLiteral("hash-assets"));
        const QString sourcePath = QDir(projectRoot).filePath(QStringLiteral("payload.bin"));
        const QByteArray payload = QByteArrayLiteral("pvforge hash integrity payload");
        if (!writeRaw(sourcePath, payload))
            return 1;

        QCryptographicHash hasher(QCryptographicHash::Sha256);
        hasher.addData(payload);
        const QString expected = QString::fromLatin1(hasher.result().toHex());
        auto fileHash = [](const QString& path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return QString();
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&file);
            return QString::fromLatin1(hash.result().toHex());
        };

        const QString copied = pvf::copyIntoAssetDirectory(sourcePath, hashDir);
        if (QFileInfo(copied).completeBaseName() != expected
            || fileHash(copied) != expected)
        {
            qCritical() << "project-limit-test: copied asset hash mismatch" << copied;
            ++failures;
        }
        if (pvf::copyIntoAssetDirectory(sourcePath, hashDir) != copied)
        {
            qCritical() << "project-limit-test: identical asset not deduplicated";
            ++failures;
        }

        if (!writeRaw(copied, QByteArrayLiteral("tampered")))
            return 1;
        if (pvf::copyIntoAssetDirectory(sourcePath, hashDir) != copied
            || fileHash(copied) != expected)
        {
            qCritical() << "project-limit-test: tampered asset not repaired";
            ++failures;
        }
    }

    qInfo() << "project-limit-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int renameTest(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_rename.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, assetPath] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!timeline || !undo)
        {
            qCritical() << "rename-test: widgets missing";
            app.exit(1);
            return;
        }
        auto* renameEdit = timeline->findChild<QLineEdit*>(QStringLiteral("renameEdit"));
        if (!renameEdit)
        {
            qCritical() << "rename-test: rename field missing";
            app.exit(1);
            return;
        }

        if (window.importImageFile(assetPath) == pvf::kInvalidEntity)
        {
            qCritical() << "rename-test: import failed";
            app.exit(1);
            return;
        }
        const pvf::EntityId id = project->entities().first().id;
        const QString oldName = project->entities().first().name;

        // Double click the name to start renaming.
        const QPoint namePoint(20, timeline->rowCenterYForTest(0));
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(namePoint),
                                QPointF(timeline->mapToGlobal(namePoint)), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &doubleClick);
        if (!renameEdit->isVisible())
        {
            qCritical() << "rename-test: rename field not shown";
            ++failures;
        }

        renameEdit->setText(QStringLiteral("新名字"));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(renameEdit, &enter);
        if (project->entity(id)->name != QStringLiteral("新名字"))
        {
            qCritical() << "rename-test: name not applied";
            ++failures;
        }

        // Renaming is undoable.
        undo->trigger();
        if (project->entity(id)->name != oldName)
        {
            qCritical() << "rename-test: undo rename failed";
            ++failures;
        }

        // The title tracks the saved/unsaved state.
        if (!window.windowTitle().contains(QStringLiteral("未保存")))
        {
            qCritical() << "rename-test: title not marked unsaved" << window.windowTitle();
            ++failures;
        }

        // Saving clears the modified flag and updates the title.
        const QString projectPath = QDir::tempPath() + QStringLiteral("/pvforge_rename.pvproj");
        QString error;
        if (!project->save(projectPath, &error) || project->modified)
        {
            qCritical() << "rename-test: modified flag not cleared";
            ++failures;
        }
        if (auto* saveAction = window.findChild<QAction*>(QStringLiteral("saveAction")))
            saveAction->trigger();
        if (!window.windowTitle().contains(QStringLiteral("已保存")))
        {
            qCritical() << "rename-test: title not marked saved" << window.windowTitle();
            ++failures;
        }

        // Closing a modified project asks before discarding.
        project->setValue(id, QStringLiteral("x"), 0, 120, true);
        if (!project->modified)
        {
            qCritical() << "rename-test: modified flag not set";
            ++failures;
        }
        QTimer::singleShot(400, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->accept();
        });
        window.close();
        if (window.isVisible())
        {
            qCritical() << "rename-test: close prompt did not appear";
            ++failures;
        }

        QFile::remove(assetPath);
        QFile::remove(projectPath);
        qInfo() << "rename-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int undoTest(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_undo.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, assetPath] {
        int failures = 0;
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!undo || !redo)
        {
            qCritical() << "undo-test: actions missing";
            app.exit(1);
            return;
        }

        // Creating an entity is undoable.
        if (window.importImageFile(assetPath) == pvf::kInvalidEntity)
        {
            qCritical() << "undo-test: import failed";
            app.exit(1);
            return;
        }
        if (project->entities().size() != 1)
        {
            qCritical() << "undo-test: entity not created";
            ++failures;
        }

        // Deleting the selected entity is undoable.
        QKeyEvent deleteEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(&window, &deleteEvent);
        if (!project->entities().isEmpty())
        {
            qCritical() << "undo-test: delete failed";
            ++failures;
        }
        undo->trigger();
        if (project->entities().size() != 1)
        {
            qCritical() << "undo-test: undo delete failed";
            ++failures;
        }

        // Undoing again removes the creation.
        undo->trigger();
        if (!project->entities().isEmpty())
        {
            qCritical() << "undo-test: undo create failed";
            ++failures;
        }

        // Redo brings it back.
        redo->trigger();
        if (project->entities().size() != 1)
        {
            qCritical() << "undo-test: redo create failed";
            ++failures;
        }

        // Keyframe removal is undoable on the history level.
        pvf::History history;
        if (project->entities().isEmpty())
        {
            qCritical() << "undo-test: no entity for keyframe test";
            app.exit(1);
            return;
        }
        const pvf::EntityId id = project->entities().first().id;
        project->setValue(id, QStringLiteral("x"), 0, 0, true);
        project->setValue(id, QStringLiteral("x"), 60, 400, true);
        const auto records = project->removeKeyframesAt(id, 60);
        history.push(std::make_unique<pvf::RemoveKeyframesCommand>(records), *project);
        if (!records.isEmpty() && project->value(id, QStringLiteral("x"), 60) == 400.0)
        {
            qCritical() << "undo-test: keyframe not removed";
            ++failures;
        }
        history.undo(*project);
        if (std::abs(project->value(id, QStringLiteral("x"), 60) - 400.0) > 1e-6)
        {
            qCritical() << "undo-test: keyframe not restored";
            ++failures;
        }

        QFile::remove(assetPath);
        qInfo() << "undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int deleteTest(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_delete.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.show();

    if (window.importImageFile(assetPath) == pvf::kInvalidEntity)
        return 1;
    if (project->entities().size() != 1)
        return 1;

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(&window, &event);

    const bool removed = project->entities().isEmpty();
    int tabCount = 0;
    if (auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs")))
        tabCount = tabs->count();

    QFile::remove(assetPath);
    qInfo() << "delete-test: entities after delete" << project->entities().size()
            << "tabs" << tabCount;
    app.exit(removed && tabCount == 1 ? 0 : 1);
    return 0;
}


} // namespace

namespace pvf::tests {

void registerEditorTests(Registry& registry)
{
    registry.add(QStringLiteral("--group-test"),
                 [](QApplication& app) { return groupTest(app); });
    registry.add(QStringLiteral("--opacity-test"),
                 [](QApplication& app) { return opacityTest(app); });
    registry.add(QStringLiteral("--multidrag-test"),
                 [](QApplication& app) { return multiDragTest(app); });
    registry.add(QStringLiteral("--keyframe-drag-test"),
                 [](QApplication& app) { return keyframeDragTest(app); });
    registry.add(QStringLiteral("--curve-editor-test"),
                 [](QApplication& app) { return curveEditorTest(app); });
    registry.add(QStringLiteral("--audio-undo-test"),
                 [](QApplication& app) { return audioUndoTest(app); });
    registry.add(QStringLiteral("--clipboard-test"),
                 [](QApplication& app) { return clipboardTest(app); });
    registry.add(QStringLiteral("--library-picker-test"),
                 [](QApplication&) { return libraryPickerTest(); });
    registry.add(QStringLiteral("--replace-test"),
                 [](QApplication&) { return replaceSourceTest(); });
    registry.add(QStringLiteral("--layer-test"),
                 [](QApplication&) { return layerTest(); });
    registry.add(QStringLiteral("--frame-restore-test"),
                 [](QApplication& app) { return frameRestoreTest(app); });
    registry.add(QStringLiteral("--keyframe-edit-test"),
                 [](QApplication& app) { return keyframeEditTest(app); });
    registry.add(QStringLiteral("--import-async-test"),
                 [](QApplication& app) { return importAsyncTest(app); });
    registry.add(QStringLiteral("--shape-test"),
                 [](QApplication& app) { return shapeTest(app); });
    registry.add(QStringLiteral("--lock-test"),
                 [](QApplication& app) { return lockTest(app); });
    registry.add(QStringLiteral("--pack-test"),
                 [](QApplication& app) { return packTest(app); });
    registry.add(QStringLiteral("--project-limit-test"),
                 [](QApplication& app) { return projectLimitTest(app); });
    registry.add(QStringLiteral("--rename-test"),
                 [](QApplication& app) { return renameTest(app); });
    registry.add(QStringLiteral("--undo-test"),
                 [](QApplication& app) { return undoTest(app); });
    registry.add(QStringLiteral("--delete-test"),
                 [](QApplication& app) { return deleteTest(app); });
}

} // namespace pvf::tests
