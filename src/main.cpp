#include "app/AppIcon.h"
#include "core/Commands.h"
#include "core/History.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "app/MainWindow.h"
#include "app/NewProjectDialog.h"
#include "app/SettingsDialog.h"
#include "app/WelcomeWindow.h"
#include "core/Project.h"
#include "media/VideoDecoder.h"
#include "ui/CollapsibleSection.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>

namespace {

int selfTest()
{
    pvf::Project project;

    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString path = QDir::tempPath() + QStringLiteral("/pvforge_selftest.png");
    if (!img.save(path))
    {
        qCritical() << "self-test: failed to write" << path;
        return 1;
    }

    pvf::Entity* e = project.addImage(path);
    if (!e)
    {
        qCritical() << "self-test: addImage failed";
        return 1;
    }
    const pvf::EntityId id = e->id;

    project.setValue(id, QStringLiteral("x"), 0, 0, true);
    project.setValue(id, QStringLiteral("x"), 30, 600, true);

    const double mid = project.value(id, QStringLiteral("x"), 15);
    if (std::abs(mid - 300.0) > 1e-6)
    {
        qCritical() << "self-test: interpolation expected 300, got" << mid;
        return 1;
    }
    if (std::abs(project.value(id, QStringLiteral("x"), 0) - 0.0) > 1e-6
        || std::abs(project.value(id, QStringLiteral("x"), 30) - 600.0) > 1e-6)
    {
        qCritical() << "self-test: keyframe endpoints wrong";
        return 1;
    }
    if (std::abs(project.value(id, QStringLiteral("opacity"), 0) - 1.0) > 1e-6)
    {
        qCritical() << "self-test: opacity default wrong";
        return 1;
    }

    QFile::remove(path);
    qInfo() << "self-test OK: keyframe interpolation x(15) =" << mid;
    return 0;
}

int saveTest()
{
    pvf::Project empty;
    if (empty.timelineLength() != 0)
    {
        qCritical() << "save-test: empty project length should be 0, got"
                    << empty.timelineLength();
        return 1;
    }
    const QString imagePath = QDir::tempPath() + QStringLiteral("/pvforge_savetest.png");
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_savetest.pvproj");

    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::cyan);
    if (!img.save(imagePath))
        return 1;

    pvf::Project before;
    before.name = QStringLiteral("往返测试");
    before.author = QStringLiteral("tester");
    before.width = 1280;
    before.height = 720;
    before.fps = 30;

    pvf::Entity* image = before.addImage(imagePath);
    if (!image)
        return 1;
    const pvf::EntityId imageId = image->id;
    before.setValue(imageId, QStringLiteral("x"), 0, 0, true);
    before.setValue(imageId, QStringLiteral("x"), 90, 600, true);

    // Timeline spans the last keyframe (90), not a fixed project duration.
    if (before.timelineLength() != 90)
    {
        qCritical() << "save-test: expected length 90, got" << before.timelineLength();
        return 1;
    }

    QString error;
    if (!before.save(projectPath, &error))
    {
        qCritical() << "save-test: save failed" << error;
        return 1;
    }

    pvf::Project after;
    if (!after.load(projectPath, &error))
    {
        qCritical() << "save-test: load failed" << error;
        return 1;
    }

    if (after.name != before.name || after.fps != 30 || after.entities().size() != 1)
    {
        qCritical() << "save-test: metadata mismatch";
        return 1;
    }
    if (after.width != 1280 || after.height != 720)
    {
        qCritical() << "save-test: resolution mismatch";
        return 1;
    }
    if (after.templates().size() != 1)
    {
        qCritical() << "save-test: templates" << after.templates().size();
        return 1;
    }
    if (after.entities().first().templateId != after.templates().first().id)
    {
        qCritical() << "save-test: template association lost";
        return 1;
    }
    if (std::abs(after.value(imageId, QStringLiteral("x"), 45) - 300.0) > 1e-6)
    {
        qCritical() << "save-test: keyframe roundtrip mismatch";
        return 1;
    }
    if (after.timelineLength() != 90)
    {
        qCritical() << "save-test: length after load" << after.timelineLength();
        return 1;
    }
    if (after.entities().first().image.isNull())
    {
        qCritical() << "save-test: image not reloaded";
        return 1;
    }

    QFile::remove(projectPath);
    QFile::remove(imagePath);
    qInfo() << "save-test OK: roundtrip length" << after.timelineLength();
    return 0;
}

int welcomeTest(QApplication& app)
{
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_welcome_test.pvproj");

    pvf::Project project;
    project.name = QStringLiteral("欢迎页测试");
    project.width = 1280;
    project.height = 720;
    project.fps = 30;
    QString error;
    if (!project.save(projectPath, &error))
        return 1;

    QSettings settings;
    const QVariant oldRecent = settings.value(QStringLiteral("recentProjects"));
    settings.setValue(QStringLiteral("recentProjects"), QStringList{projectPath});

    pvf::WelcomeWindow welcome;
    welcome.show();

    QTimer::singleShot(400, &welcome, [&app, &welcome, projectPath, oldRecent] {
        welcome.grab().toImage().save(QStringLiteral("/tmp/pvforge_welcome.png"));

        auto* dialog = new pvf::NewProjectDialog(&welcome);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();

        QTimer::singleShot(300, dialog, [&app, dialog, projectPath, oldRecent] {
            dialog->grab().toImage().save(QStringLiteral("/tmp/pvforge_newproject.png"));
            qInfo() << "welcome-test OK";

            QSettings settings;
            if (oldRecent.isValid())
                settings.setValue(QStringLiteral("recentProjects"), oldRecent);
            else
                settings.remove(QStringLiteral("recentProjects"));
            QFile::remove(projectPath);
            app.exit(0);
        });
    });
    return app.exec();
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
        send(QEvent::MouseMove, QPoint(400, 40), Qt::NoButton, Qt::NoButton);
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_row_buttons.png"));

        // Rubber-band across both rows to pick up every keyframe in range.
        send(QEvent::MouseButtonPress, QPoint(200, 32), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, QPoint(560, 56), Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPoint(560, 56), Qt::LeftButton, Qt::NoButton);
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
        send(QEvent::MouseButtonPress, QPoint(500, 10), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPoint(500, 10), Qt::LeftButton, Qt::NoButton);
        const int rulerFrame = window.currentFrame();
        if (std::abs(rulerFrame - 53) > 2)
        {
            qCritical() << "timeline-interaction-test: ruler frame" << rulerFrame;
            ++failures;
        }

        // Empty track click jumps on release.
        send(QEvent::MouseButtonPress, QPoint(300, 40), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPoint(300, 40), Qt::LeftButton, Qt::NoButton);
        const int emptyFrame = window.currentFrame();
        if (std::abs(emptyFrame - 20) > 2)
        {
            qCritical() << "timeline-interaction-test: empty frame" << emptyFrame;
            ++failures;
        }

        // Keyframe click selects without moving the playhead.
        const int beforeKey = window.currentFrame();
        send(QEvent::MouseButtonPress, QPoint(540, 40), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPoint(540, 40), Qt::LeftButton, Qt::NoButton);
        const int afterKey = window.currentFrame();
        if (afterKey != beforeKey)
        {
            qCritical() << "timeline-interaction-test: keyframe moved playhead" << beforeKey
                        << afterKey;
            ++failures;
        }
        if (timeline->selectedKeyframeCount() != 1)
        {
            qCritical() << "timeline-interaction-test: keyframe not selected";
            ++failures;
        }

        // Clicking empty space clears the keyframe selection.
        send(QEvent::MouseButtonPress, QPoint(300, 40), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPoint(300, 40), Qt::LeftButton, Qt::NoButton);
        if (timeline->selectedKeyframeCount() != 0)
        {
            qCritical() << "timeline-interaction-test: selection not cleared";
            ++failures;
        }

        // Dragging rubber-bands instead of scrubbing.
        send(QEvent::MouseButtonPress, QPoint(250, 40), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, QPoint(320, 68), Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPoint(320, 68), Qt::LeftButton, Qt::NoButton);
        if (window.currentFrame() != afterKey)
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

int settingsTest(QApplication& app)
{
    const int oldSnap = pvf::Settings::snapPixels();
    const bool oldEdgeCentre = pvf::Settings::edgeCenterSnap();
    const QString oldLanguage = pvf::Settings::language();

    pvf::Settings::setSnapPixels(12);
    if (pvf::Settings::snapPixels() != 12)
    {
        qCritical() << "settings-test: snap setting not persisted";
        return 1;
    }
    pvf::Settings::setSnapPixels(oldSnap);
    pvf::Settings::setEdgeCenterSnap(true);
    if (!pvf::Settings::edgeCenterSnap())
    {
        qCritical() << "settings-test: edge snap setting not persisted";
        return 1;
    }
    pvf::Settings::setEdgeCenterSnap(oldEdgeCentre);

    pvf::Lang::load(QStringLiteral("en_US"));
    if (pvf::Lang::t(QStringLiteral("menu.file")) != QStringLiteral("File"))
    {
        qCritical() << "settings-test: english strings not loaded";
        return 1;
    }
    pvf::Lang::load(QStringLiteral("zh_CN"));
    if (pvf::Lang::t(QStringLiteral("menu.file")) != QStringLiteral("文件"))
    {
        qCritical() << "settings-test: chinese strings not loaded";
        return 1;
    }

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, oldSnap, oldEdgeCentre] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "settings-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const QPoint center = viewport->rect().center();
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

        // A larger snap distance grabs a 10px drag.
        viewport->setSnapPixels(20);
        drag(center + QPoint(40, 0), center + QPoint(50, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "settings-test: large snap did not stick";
            ++failures;
        }

        // A small snap distance ignores it.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        viewport->setSnapPixels(2);
        drag(center + QPoint(40, 0), center + QPoint(50, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) < 1.0)
        {
            qCritical() << "settings-test: small snap still stuck";
            ++failures;
        }

        // Edge-to-centre snapping obeys the preference.
        const double scale = viewport->currentScale();
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        viewport->setSnapPixels(20);
        viewport->setEdgeCenterSnap(true);
        const int edgeDx = int(std::lround(145.0 * scale));
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - 150.0) > 1e-6)
        {
            qCritical() << "settings-test: edge-centre snap on failed"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        viewport->setEdgeCenterSnap(false);
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - 145.0) > 1.0)
        {
            qCritical() << "settings-test: edge-centre snap off failed"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }
        viewport->setEdgeCenterSnap(oldEdgeCentre);
        viewport->setSnapPixels(oldSnap);

        // The settings dialog builds and can be shown.
        pvf::SettingsDialog dialog(&window);
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_settings.png"));

        qInfo() << "settings-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
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

        // Corner handle scales both axes independently.
        drag(center + QPoint(halfW, halfH), center + QPoint(2 * halfW, 3 * halfH / 2),
             Qt::NoModifier);
        const double sx1 = project->value(id, QStringLiteral("scaleX"), 0);
        const double sy1 = project->value(id, QStringLiteral("scaleY"), 0);
        if (std::abs(sx1 - 2.0) > 0.1 || std::abs(sy1 - 1.5) > 0.1)
        {
            qCritical() << "scale-handle-test: corner handle" << sx1 << sy1;
            ++failures;
        }

        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);

        // Ctrl keeps the aspect ratio.
        drag(center + QPoint(halfW, halfH), center + QPoint(2 * halfW, 3 * halfH / 2),
             Qt::ControlModifier);
        const double sx2 = project->value(id, QStringLiteral("scaleX"), 0);
        const double sy2 = project->value(id, QStringLiteral("scaleY"), 0);
        if (std::abs(sx2 - 2.0) > 0.1 || std::abs(sy2 - 2.0) > 0.1)
        {
            qCritical() << "scale-handle-test: ctrl uniform" << sx2 << sy2;
            ++failures;
        }

        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);

        // Dragging past the centre mirrors the entity.
        drag(center + QPoint(halfW, halfH), center - QPoint(halfW, halfH),
             Qt::NoModifier);
        const double sx3 = project->value(id, QStringLiteral("scaleX"), 0);
        if (sx3 >= -0.5)
        {
            qCritical() << "scale-handle-test: mirror" << sx3;
            ++failures;
        }

        qInfo() << "scale-handle-test failures" << failures;
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

        // Drag the Y axis (points up): vertical only.
        drag(center + QPoint(0, -40), center + QPoint(0, -140));
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
        if (std::abs(rotation - 90.0) > 2.0)
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

        project->setEntityFlag(id, pvf::EntityFlag::Locked, true);

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

        // Dragging the entity in the viewport is blocked.
        const QPoint center = viewport->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(center),
                          QPointF(viewport->mapToGlobal(center)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(center + QPoint(120, 0)),
                         QPointF(viewport->mapToGlobal(center + QPoint(120, 0))), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "lock-test: locked entity moved";
            ++failures;
        }

        // Row buttons stay inside the name column.
        if (timeline->rowButtonRect(0, 0).right() > 180
            || timeline->rowButtonRect(0, 2).left() < 0)
        {
            qCritical() << "lock-test: row buttons outside the name column";
            ++failures;
        }

        // Unlocking restores editing.
        project->setEntityFlag(id, pvf::EntityFlag::Locked, false);
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
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(20, 40),
                                QPointF(timeline->mapToGlobal(QPoint(20, 40))), Qt::LeftButton,
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

    QTimer::singleShot(400, &window, [&app, &window, timeline] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_before.png"));
        const double before = timeline->pixelsPerFrame();

        const QPointF pos(500, 120);
        for (int i = 0; i < 3; ++i)
        {
            QWheelEvent event(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(timeline, &event);
        }

        const double after = timeline->pixelsPerFrame();
        QTimer::singleShot(200, &window, [&app, &window, before, after] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_after.png"));
            qInfo() << "timeline-zoom-test:" << before << "->" << after;
            app.exit(after > before * 4.0 ? 0 : 1);
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

int libraryTest(QApplication& app)
{
    const QString assetPath =
        QDir::tempPath() + QStringLiteral("/pvforge_library_asset.png");
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(QColor(70, 130, 200));
    {
        QPainter p(&img);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPointSize(28);
        f.setBold(true);
        p.setFont(f);
        p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("ASSET"));
    }
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    project->name = QStringLiteral("素材库测试");
    pvf::MainWindow window(project);
    window.resize(1440, 900);
    window.show();

    project->addImage(assetPath);
    project->addText(QStringLiteral("素材库文字"), QFont().family(), 96, Qt::white,
                     int(Qt::AlignCenter));
    project->addVector(1, 220, 220, QColor(255, 120, 160), QColor(255, 255, 255), 6);

    for (auto* section : window.findChildren<pvf::CollapsibleSection*>())
        if (section->title() == QStringLiteral("素材库"))
            section->setExpanded(true);
    qInfo() << "library-test: templates" << project->templates().size()
            << "entities" << project->entities().size();

    QTimer::singleShot(400, &window, [&app, &window, project, assetPath] {
        if (auto* list = window.findChild<QListWidget*>(QStringLiteral("assetList")))
            qInfo() << "library-test: list items" << list->count();
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_library.png"));

        auto* create = window.findChild<QPushButton*>(QStringLiteral("assetCreate"));
        if (!create)
        {
            qCritical() << "library-test: create button not found";
            app.exit(1);
            return;
        }

        const int before = project->entities().size();
        create->click();

        QTimer::singleShot(300, &window, [&app, &window, project, before, assetPath] {
            const int after = project->entities().size();
            qInfo() << "library-test: entities" << before << "->" << after;
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_entity_tab.png"));

            if (auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs")))
                tabs->setCurrentIndex(0);
            QTimer::singleShot(250, &window, [&app, &window, after, before, assetPath] {
                window.grab().toImage().save(
                    QStringLiteral("/tmp/pvforge_projectprops.png"));
                QFile::remove(assetPath);
                app.exit(after == before + 1 ? 0 : 1);
            });
        });
    });
    return app.exec();
}

} // namespace

namespace {

int renderDemo(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_demo_asset.png");

    QImage asset(400, 400, QImage::Format_ARGB32);
    asset.fill(Qt::transparent);
    {
        QPainter p(&asset);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 170, 255));
        p.drawRoundedRect(20, 20, 360, 360, 48, 48);
        p.setBrush(QColor(255, 255, 255));
        p.drawEllipse(120, 90, 160, 160);
        p.setBrush(QColor(20, 30, 50));
        p.drawEllipse(160, 130, 80, 80);
        p.setPen(QPen(QColor(255, 255, 255), 6));
        QFont f = p.font();
        f.setPointSize(36);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 280, 400, 80), Qt::AlignCenter, QStringLiteral("PV"));
    }
    if (!asset.save(assetPath))
    {
        qCritical() << "demo: failed to save asset";
        return 1;
    }

    auto* testProject = new pvf::Project;
    pvf::MainWindow window(testProject);
    window.resize(1440, 900);
    window.show();

    const pvf::EntityId id = window.importImageFile(assetPath);
    if (id == pvf::kInvalidEntity)
    {
        qCritical() << "demo: import failed";
        return 1;
    }

    auto* project = window.project();
    project->setValue(id, QStringLiteral("x"), 0, -500, true);
    project->setValue(id, QStringLiteral("x"), 30, 500, true);
    project->setValue(id, QStringLiteral("rotation"), 0, -20, true);
    project->setValue(id, QStringLiteral("rotation"), 30, 20, true);
    project->setValue(id, QStringLiteral("opacity"), 0, 0.25, true);
    project->setValue(id, QStringLiteral("opacity"), 30, 1.0, true);

    QTimer::singleShot(400, &window, [&app, &window] {
        window.goToFrame(0);
        const QImage f0 = window.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        window.goToFrame(15);
        const QImage f15 = window.grab().toImage().convertToFormat(QImage::Format_ARGB32);

        f0.save(QStringLiteral("/tmp/pvforge_demo_f0.png"));
        f15.save(QStringLiteral("/tmp/pvforge_demo_f15.png"));

        int diff = 0;
        if (f0.size() == f15.size())
        {
            for (int y = 0; y < f0.height(); ++y)
            {
                const auto* a = reinterpret_cast<const QRgb*>(f0.constScanLine(y));
                const auto* b = reinterpret_cast<const QRgb*>(f15.constScanLine(y));
                for (int x = 0; x < f0.width(); ++x)
                {
                    if (std::abs(qRed(a[x]) - qRed(b[x])) + std::abs(qGreen(a[x]) - qGreen(b[x]))
                            + std::abs(qBlue(a[x]) - qBlue(b[x]))
                        > 40)
                        ++diff;
                }
            }
        }

        qInfo() << "demo: differing pixels between frame 0 and 15:" << diff;
        app.exit(diff > 1000 ? 0 : 1);
    });

    return app.exec();
}

} // namespace

namespace {

int playTest(QApplication& app)
{
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString path = QDir::tempPath() + QStringLiteral("/pvforge_playtest.png");
    if (!img.save(path))
        return 1;

    auto* testProject = new pvf::Project;
    pvf::MainWindow window(testProject);
    window.resize(1200, 800);
    window.show();

    const pvf::EntityId id = window.importImageFile(path);
    if (id == pvf::kInvalidEntity)
        return 1;
    window.project()->setValue(id, QStringLiteral("x"), 0, 0, true);
    window.project()->setValue(id, QStringLiteral("x"), 60, 600, true);

    window.goToFrame(0);
    window.togglePlayback();

    QTimer::singleShot(600, &window, [&app, &window] {
        const int frame = window.currentFrame();
        qInfo() << "play-test: frame after 600ms =" << frame;
        app.exit(frame >= 10 ? 0 : 1);
    });
    return app.exec();
}

int layoutTest(QApplication& app)
{
    auto* testProject = new pvf::Project;
    testProject->fps = 24;
    pvf::MainWindow window(testProject);
    window.resize(480, 1060);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_layout_narrow.png"));
        qInfo() << "layout-test: size" << window.size() << "minimum" << window.minimumSize();
        if (auto* timeline = window.findChild<pvf::TimelinePanel*>())
            qInfo() << "timeline geometry" << timeline->geometry()
                    << "min" << timeline->minimumSize();
        if (auto* viewport = window.findChild<pvf::Viewport*>())
            qInfo() << "viewport geometry" << viewport->geometry()
                    << "min" << viewport->minimumSize();
        app.exit(0);
    });
    return app.exec();
}

int workbenchTest(QApplication& app)
{
    QString error;
    const QImage videoFrame =
        pvf::decodeVideoFirstFrame(QStringLiteral("/tmp/pvf_test.mp4"), &error);
    if (videoFrame.isNull())
    {
        qCritical() << "workbench-test: video decode failed:" << error;
        return 1;
    }
    qInfo() << "workbench-test: video first frame" << videoFrame.size();

    auto* testProject = new pvf::Project;
    pvf::MainWindow window(testProject);
    window.resize(1440, 900);
    window.show();

    auto* project = window.project();

    pvf::Entity* circle = project->addVector(
        1, 220, 220, QColor(255, 120, 160, 200), QColor(255, 255, 255), 8);
    project->setValue(circle->id, QStringLiteral("x"), 0, 300, false);
    project->setValue(circle->id, QStringLiteral("y"), 0, -200, false);

    pvf::Entity* video = project->addVideo(QStringLiteral("/tmp/pvf_test.mp4"), videoFrame);
    project->setValue(video->id, QStringLiteral("x"), 0, -350, false);
    project->setValue(video->id, QStringLiteral("y"), 0, -200, false);
    project->setValue(video->id, QStringLiteral("scaleX"), 0, 0.6, false);
    project->setValue(video->id, QStringLiteral("scaleY"), 0, 0.6, false);

    auto* button = window.findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    auto* workbench = window.findChild<pvf::WorkbenchPanel*>();
    if (!button || !workbench)
    {
        qCritical() << "workbench-test: UI objects not found";
        return 1;
    }
    button->click();

    QTimer::singleShot(450, &window, [&app, &window, workbench, button] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_workbench.png"));

        auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
        auto* content = workbench->findChild<QLineEdit*>(QStringLiteral("textContent"));
        auto* create = workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
        if (!types || !content || !create)
        {
            qCritical() << "workbench-test: form widgets not found";
            app.exit(1);
            return;
        }

        types->setCurrentRow(1);
        content->setText(QStringLiteral("工作台创建的文字"));
        create->click();

        QTimer::singleShot(350, &window, [&app, &window, button, workbench] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_types.png"));
            const int count = window.project()->entities().size();
            qInfo() << "workbench-test: entities after text create =" << count;

            button->click();
            auto* typeList =
                workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
            typeList->setCurrentRow(2);   // vector page

            QTimer::singleShot(300, &window, [&app, &window, workbench, count,
                                              typeList] {
                window.grab().toImage().save(
                    QStringLiteral("/tmp/pvforge_workbench_vector.png"));
                auto* createButton =
                    workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
                createButton->click();

                QTimer::singleShot(250, &window, [&app, &window, count] {
                    window.grab().toImage().save(QStringLiteral("/tmp/pvforge_vector.png"));
                    const int after = window.project()->entities().size();
                    qInfo() << "workbench-test: entities after vector create =" << after;
                    app.exit(after == count + 1 ? 0 : 1);
                });
            });
        });
    });

    return app.exec();
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PVForge"));
    app.setApplicationDisplayName(QStringLiteral("PVForge"));
    app.setOrganizationName(QStringLiteral("PVForge"));
    app.setWindowIcon(pvf::makeCubeIcon());
    pvf::Lang::load(pvf::Settings::language());

    if (app.arguments().contains(QStringLiteral("--self-test")))
        return selfTest();
    if (app.arguments().contains(QStringLiteral("--save-test")))
        return saveTest();
    if (app.arguments().contains(QStringLiteral("--welcome-test")))
        return welcomeTest(app);
    if (app.arguments().contains(QStringLiteral("--library-test")))
        return libraryTest(app);
    if (app.arguments().contains(QStringLiteral("--open-timeline-test")))
        return openTimelineTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-zoom-test")))
        return timelineZoomTest(app);
    if (app.arguments().contains(QStringLiteral("--delete-test")))
        return deleteTest(app);
    if (app.arguments().contains(QStringLiteral("--undo-test")))
        return undoTest(app);
    if (app.arguments().contains(QStringLiteral("--rename-test")))
        return renameTest(app);
    if (app.arguments().contains(QStringLiteral("--pack-test")))
        return packTest(app);
    if (app.arguments().contains(QStringLiteral("--lock-test")))
        return lockTest(app);
    if (app.arguments().contains(QStringLiteral("--gizmo-test")))
        return gizmoTest(app);
    if (app.arguments().contains(QStringLiteral("--pivot-test")))
        return pivotTest(app);
    if (app.arguments().contains(QStringLiteral("--pivot-gizmo-test")))
        return pivotGizmoTest(app);
    if (app.arguments().contains(QStringLiteral("--shape-test")))
        return shapeTest(app);
    if (app.arguments().contains(QStringLiteral("--scale-handle-test")))
        return scaleHandleTest(app);
    if (app.arguments().contains(QStringLiteral("--snap-test")))
        return snapTest(app);
    if (app.arguments().contains(QStringLiteral("--settings-test")))
        return settingsTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-interaction-test")))
        return timelineInteractionTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-rowbuttons-test")))
        return timelineRowButtonsTest(app);
    if (app.arguments().contains(QStringLiteral("--demo-shot")))
        return renderDemo(app);
    if (app.arguments().contains(QStringLiteral("--play-test")))
        return playTest(app);
    if (app.arguments().contains(QStringLiteral("--workbench-test")))
        return workbenchTest(app);
    if (app.arguments().contains(QStringLiteral("--layout-test")))
        return layoutTest(app);

    pvf::WelcomeWindow welcome;
    welcome.show();
    return app.exec();
}
