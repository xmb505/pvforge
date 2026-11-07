#include "app/AppIcon.h"
#include "core/Commands.h"
#include "core/Camera.h"
#include "core/History.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "media/MediaImporter.h"
#include "media/AudioDecoder.h"
#include "media/VideoPlayer.h"
#include "media/VideoProxy.h"
#include "media/DecoderInfo.h"
#include "render/SceneRenderer.h"
#include "audio/AudioEngine.h"
#include "core/Settings.h"
#include "app/MainWindow.h"
#include "app/NewProjectDialog.h"
#include "app/SettingsDialog.h"
#include "app/SystemInfo.h"
#include "app/WelcomeWindow.h"
#include "core/Project.h"
#include "media/VideoDecoder.h"
#include "ui/BezierEditorDialog.h"
#include "ui/CameraMonitor.h"
#include "ui/EasingPicker.h"
#include "ui/CollapsibleSection.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QDataStream>
#include <QFont>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QJsonDocument>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QEventLoop>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
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

    const pvf::DecoderInfo decoder = pvf::decoderInfo();
    if (decoder.ffmpegVersion.isEmpty() || decoder.mode.isEmpty()
        || decoder.video.isEmpty() || decoder.audioOutput.isEmpty())
    {
        qCritical() << "settings-test: decoder info incomplete";
        return 1;
    }

    const pvf::SystemInfo system = pvf::collectSystemInfo();
    if (system.cpuModel.isEmpty() || system.cpuModel == QStringLiteral("未知处理器")
        || system.physicalCores <= 0 || system.memoryBytes == 0
        || system.osName.isEmpty() || system.qtVersion.isEmpty()
        || system.gpus.isEmpty())
    {
        qCritical() << "settings-test: system info incomplete" << system.cpuModel
                    << system.physicalCores << system.memoryBytes << system.osName
                    << system.gpus;
        return 1;
    }

    {
        pvf::SettingsDialog dialog;
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvf_settings.png"));

        auto* categories =
            dialog.findChild<QListWidget*>(QStringLiteral("settingsCategories"));
        if (categories)
            categories->setCurrentRow(5);
        QApplication::processEvents();
        auto* videoValue =
            dialog.findChild<QLabel*>(QStringLiteral("decoderVideoValue"));
        auto* modeValue =
            dialog.findChild<QLabel*>(QStringLiteral("decoderModeValue"));
        if (!videoValue || !videoValue->text().contains(QStringLiteral("软件解码"))
            || videoValue->width() < 120 || !modeValue
            || !modeValue->text().contains(QStringLiteral("软件解码")))
        {
            qCritical() << "settings-test: decoder value not visible"
                        << (videoValue ? videoValue->text() : QString())
                        << (videoValue ? videoValue->width() : -1);
            return 1;
        }
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvf_settings_decode.png"));

        dialog.showAboutPage();
        QApplication::processEvents();
        auto* about = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aboutText"));
        const QString text = about ? about->toPlainText() : QString();
        if (!about || !text.contains(QStringLiteral("处理器"))
            || !text.contains(QStringLiteral("FFmpeg"))
            || !text.contains(QStringLiteral("Qt "))
            || !text.contains(QStringLiteral("PVForge")))
        {
            qCritical() << "settings-test: about page incomplete";
            return 1;
        }
        auto* copy = dialog.findChild<QPushButton*>(QStringLiteral("aboutCopy"));
        if (!copy)
        {
            qCritical() << "settings-test: about copy button missing";
            return 1;
        }
        if (!categories || !categories->currentItem()
            || categories->currentItem()->text() != QStringLiteral("关于"))
        {
            qCritical() << "settings-test: about category not selected";
            return 1;
        }
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvf_settings_about.png"));
        dialog.close();
    }
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

int videoTest(QApplication& app)
{
    int failures = 0;
    const QString path = QStringLiteral("/tmp/pvf_video.mp4");

    QFile::remove(path);
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=red:s=320x240:d=1:r=30"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=blue:s=320x240:d=1:r=30"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                  QStringLiteral("-filter_complex"),
                  QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0[v]"),
                  QStringLiteral("-map"), QStringLiteral("[v]"),
                  QStringLiteral("-map"), QStringLiteral("2:a"),
                  QStringLiteral("-c:v"), QStringLiteral("libx264"),
                  QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                  QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                  QStringLiteral("-c:a"), QStringLiteral("aac"),
                  QStringLiteral("-shortest"), path});
    if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0)
    {
        qCritical() << "video-test: cannot generate test video"
                    << ffmpeg.readAllStandardError();
        return 1;
    }

    QString error;
    double duration = 0.0;
    const QImage first = pvf::decodeVideoFirstFrame(path, &error, &duration);
    if (first.isNull() || duration < 1.8 || duration > 2.2)
    {
        qCritical() << "video-test: first frame" << error << duration;
        return 1;
    }

    auto centerColor = [](const QImage& image) {
        if (image.isNull())
            return QColor();
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };
    auto isRed = [](const QColor& color) {
        return color.red() > 180 && color.green() < 90 && color.blue() < 90;
    };
    auto isBlue = [](const QColor& color) {
        return color.blue() > 180 && color.red() < 90 && color.green() < 90;
    };

    pvf::Project project;
    project.fps = 10;
    pvf::Entity* video = project.addVideo(path, first, duration, QStringLiteral("Video"));
    pvf::VideoPlayer player;

    player.update(project, 2);
    QColor color = centerColor(video->image);
    if (!isRed(color))
    {
        qCritical() << "video-test: frame 2 is not red" << color;
        ++failures;
    }
    player.update(project, 15);
    color = centerColor(video->image);
    if (!isBlue(color))
    {
        qCritical() << "video-test: frame 15 is not blue" << color;
        ++failures;
    }
    player.update(project, 3);
    color = centerColor(video->image);
    if (!isRed(color))
    {
        qCritical() << "video-test: backward seek is not red" << color;
        ++failures;
    }
    player.update(project, 10);
    color = centerColor(video->image);
    if (!isBlue(color))
    {
        qCritical() << "video-test: frame 10 is not blue" << color;
        ++failures;
    }

    // Background proxy: a short-GOP low-resolution copy makes timeline seeks
    // cheap. The source here is 320x240, so the 960-wide proxy is upscaled.
    const QString proxyPath = pvf::videoProxyCachePath(path);
    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
    {
        QFile::remove(proxyPath);
        pvf::VideoProxyGenerator generator;
        bool proxyDone = false;
        bool proxyOk = false;
        QEventLoop proxyLoop;
        QObject::connect(&generator, &pvf::VideoProxyGenerator::finished, &proxyLoop,
                         [&](const QString& source, bool success) {
                             if (source != path)
                                 return;
                             proxyDone = true;
                             proxyOk = success;
                             proxyLoop.quit();
                         });
        generator.ensure(path);
        if (!proxyDone)
        {
            QTimer::singleShot(30000, &proxyLoop, &QEventLoop::quit);
            proxyLoop.exec();
        }
        if (!proxyOk || !pvf::videoProxyReady(path)
            || pvf::videoDecodePath(path) == path)
        {
            qCritical() << "video-test: proxy generation failed" << proxyOk
                        << pvf::videoProxyCachePath(path);
            ++failures;
        }
        else
        {
            double proxyDuration = 0.0;
            const QImage proxyFirst =
                pvf::decodeVideoFirstFrame(pvf::videoDecodePath(path), nullptr,
                                           &proxyDuration);
            if (proxyFirst.isNull() || proxyFirst.width() != 960)
            {
                qCritical() << "video-test: proxy frame" << proxyFirst.size();
                ++failures;
            }
            else
            {
                // Force the player to reopen on the proxy path.
                player.clear();
                player.update(project, 2);
                color = centerColor(video->image);
                const QRectF bounds = pvf::SceneRenderer::entityLocalBounds(*video);
                if (video->image.width() != 960 || !isRed(color)
                    || std::abs(bounds.width() - 320.0) > 1e-6
                    || std::abs(bounds.height() - 240.0) > 1e-6)
                {
                    qCritical() << "video-test: player did not use the proxy"
                                << video->image.size() << color << bounds;
                    ++failures;
                }
            }
        }
    }
    else
    {
        qInfo() << "video-test: ffmpeg CLI missing, proxy test skipped";
    }

    const pvf::MediaImportResult imported =
        pvf::processMediaFile(path, QString());
    if (imported.type != pvf::EntityType::Video || !imported.audio
        || imported.audio->duration < 1.8 || imported.audio->peaks.isEmpty())
    {
        qCritical() << "video-test: audio track was not extracted"
                    << imported.error
                    << (imported.audio ? imported.audio->duration : -1.0);
        ++failures;
    }
    else
    {
        float peak = 0.0f;
        for (float sample : imported.audio->peaks)
            peak = std::max(peak, sample);
        if (peak < 0.05f)
        {
            qCritical() << "video-test: extracted audio is silent" << peak;
            ++failures;
        }
    }

    pvf::Project importProject;
    importProject.fps = 10;
    if (!importProject.addImportedMedia(imported))
    {
        qCritical() << "video-test: addImportedMedia failed";
        ++failures;
    }
    else
    {
        const auto& templates = importProject.templates();
        if (templates.size() != 2)
        {
            qCritical() << "video-test: expected two templates" << templates.size();
            ++failures;
        }
        const pvf::Template* videoTemplate = nullptr;
        const pvf::Template* audioTemplate = nullptr;
        for (const pvf::Template& tmpl : templates)
        {
            if (tmpl.type == pvf::EntityType::Video)
                videoTemplate = &tmpl;
            else if (tmpl.type == pvf::EntityType::Audio)
                audioTemplate = &tmpl;
        }
        if (!videoTemplate || !audioTemplate || !audioTemplate->audio
            || !audioTemplate->name.endsWith(QStringLiteral(" · 音频")))
        {
            qCritical() << "video-test: audio template missing";
            ++failures;
        }
        else
        {
            const pvf::Template videoCopy = *videoTemplate;
            pvf::Entity* createdVideo = importProject.createFromTemplate(videoCopy);
            const auto& entities = importProject.entities();
            const pvf::Entity* createdAudio = nullptr;
            for (const pvf::Entity& e : entities)
                if (e.type == pvf::EntityType::Audio)
                    createdAudio = &e;
            if (!createdVideo || entities.size() != 2 || !createdAudio
                || !createdAudio->audio
                || createdAudio->data.value(QStringLiteral("pairedVideo")).toInt()
                       != createdVideo->id
                || createdAudio->sourcePath != path)
            {
                qCritical() << "video-test: paired audio entity missing"
                            << entities.size();
                ++failures;
            }
        }
    }

    const QString projectPath = QStringLiteral("/tmp/pvf_video_audio.pvproj");
    QString saveError;
    if (!importProject.save(projectPath, &saveError))
    {
        qCritical() << "video-test: save project" << saveError;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(projectPath, &saveError))
        {
            qCritical() << "video-test: load project" << saveError;
            ++failures;
        }
        else
        {
            bool hasVideoTemplate = false;
            bool hasAudioTemplate = false;
            bool hasAudioEntity = false;
            for (const pvf::Template& tmpl : loaded.templates())
            {
                if (tmpl.type == pvf::EntityType::Video)
                    hasVideoTemplate = true;
                if (tmpl.type == pvf::EntityType::Audio && tmpl.audio)
                    hasAudioTemplate = true;
            }
            for (const pvf::Entity& e : loaded.entities())
                if (e.type == pvf::EntityType::Audio && e.audio)
                    hasAudioEntity = true;
            if (!hasVideoTemplate || !hasAudioTemplate || !hasAudioEntity)
            {
                qCritical() << "video-test: audio pairing not restored"
                            << hasVideoTemplate << hasAudioTemplate
                            << hasAudioEntity;
                ++failures;
            }
        }
    }

    auto* windowProject = new pvf::Project;
    windowProject->fps = 10;
    pvf::Entity* uiVideo =
        windowProject->addVideo(path, first, duration, QStringLiteral("Video"));
    pvf::MainWindow window(windowProject);
    window.resize(900, 700);
    window.show();
    QApplication::processEvents();

    window.goToFrame(15);
    QApplication::processEvents();
    color = centerColor(uiVideo->image);
    if (!isBlue(color))
    {
        qCritical() << "video-test: main window frame 15 is not blue" << color;
        ++failures;
    }
    window.grab().save(QStringLiteral("/tmp/pvf_video_frame.png"));

    window.goToFrame(2);
    QApplication::processEvents();
    color = centerColor(uiVideo->image);
    if (!isRed(color))
    {
        qCritical() << "video-test: main window frame 2 is not red" << color;
        ++failures;
    }

    // The play timer must advance both the playhead and the video frame.
    window.goToFrame(0);
    window.togglePlayback();
    {
        QEventLoop loop;
        QTimer::singleShot(1300, &loop, &QEventLoop::quit);
        loop.exec();
    }
    window.togglePlayback();
    color = centerColor(uiVideo->image);
    if (window.currentFrame() < 10 || !isBlue(color))
    {
        qCritical() << "video-test: playback did not reach the blue frame"
                    << window.currentFrame() << color;
        ++failures;
    }

    // Library flow: creating the video asset must also create the extracted
    // audio entity, and undo/redo must treat both as one step.
    auto* pairProject = new pvf::Project;
    pairProject->fps = 10;
    pairProject->addImportedMedia(imported);
    pvf::MainWindow pairWindow(pairProject);
    pairWindow.resize(900, 700);
    pairWindow.show();
    QApplication::processEvents();

    auto* assetList =
        pairWindow.findChild<QListWidget*>(QStringLiteral("assetList"));
    auto* assetCreate =
        pairWindow.findChild<QPushButton*>(QStringLiteral("assetCreate"));
    if (!assetList || !assetCreate || assetList->count() < 1)
    {
        qCritical() << "video-test: asset library controls missing";
        ++failures;
    }
    else
    {
        assetList->setCurrentRow(0);
        assetCreate->click();
        QApplication::processEvents();

        auto countAudio = [pairProject]() {
            int count = 0;
            for (const pvf::Entity& e : pairProject->entities())
                if (e.type == pvf::EntityType::Audio)
                    ++count;
            return count;
        };
        if (pairProject->entities().size() != 2 || countAudio() != 1)
        {
            qCritical() << "video-test: video asset did not create audio"
                        << pairProject->entities().size() << countAudio();
            ++failures;
        }

        auto* undoAction =
            pairWindow.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redoAction =
            pairWindow.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!undoAction || !redoAction)
        {
            qCritical() << "video-test: undo actions missing";
            ++failures;
        }
        else
        {
            undoAction->trigger();
            QApplication::processEvents();
            if (!pairProject->entities().isEmpty())
            {
                qCritical() << "video-test: paired undo left entities"
                            << pairProject->entities().size();
                ++failures;
            }
            redoAction->trigger();
            QApplication::processEvents();
            if (pairProject->entities().size() != 2 || countAudio() != 1)
            {
                qCritical() << "video-test: paired redo failed"
                            << pairProject->entities().size() << countAudio();
                ++failures;
            }
            pairWindow.grab().save(QStringLiteral("/tmp/pvf_video_pair.png"));
        }
    }

    if (!proxyPath.isEmpty())
        QFile::remove(proxyPath);

    Q_UNUSED(app);
    qInfo() << "video-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int audioTest(QApplication& app)
{
    int failures = 0;
    const QString wavPath = QStringLiteral("/tmp/pvf_audio.wav");
    {
        QFile file(wavPath);
        if (!file.open(QIODevice::WriteOnly))
        {
            qCritical() << "audio-test: cannot write wav";
            return 1;
        }
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        QVector<qint16> samples(48000, 0);
        for (int i = 0; i < 100; ++i)
        {
            const qint16 click = qint16(30000 * (1.0 - i / 100.0));
            samples[12000 + i] = click;   // 250 ms
            samples[36000 + i] = click;   // 750 ms
        }
        const quint32 dataSize = quint32(samples.size() * 2);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + dataSize);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(48000)
            << quint32(96000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << dataSize;
        for (qint16 sample : samples)
            out << sample;
    }

    const pvf::AudioDecodeResult decoded = pvf::decodeAudioFile(wavPath);
    if (!decoded.clip)
    {
        qCritical() << "audio-test: decode failed" << decoded.error;
        return 1;
    }
    const auto& clip = decoded.clip;
    if (std::abs(clip->duration - 1.0) > 0.02 || clip->peaks.size() < 900)
    {
        qCritical() << "audio-test: wrong duration/peaks" << clip->duration
                    << clip->peaks.size();
        ++failures;
    }
    const auto peakAt = [&clip](int ms) {
        return ms >= 0 && ms < clip->peaks.size() ? clip->peaks[ms] : 0.0f;
    };
    if (peakAt(250) < 0.5f || peakAt(750) < 0.5f || peakAt(500) > 0.05f)
    {
        qCritical() << "audio-test: peak positions" << peakAt(250) << peakAt(750)
                    << peakAt(500);
        ++failures;
    }

    const pvf::MediaImportResult imported = pvf::processMediaFile(wavPath, QString());
    if (!imported.error.isEmpty() || imported.type != pvf::EntityType::Audio
        || !imported.audio || imported.thumbnail.isNull())
    {
        qCritical() << "audio-test: media import" << imported.error
                    << int(imported.type);
        ++failures;
    }

    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* audio = project->addAudio(wavPath, clip, QStringLiteral("Audio"));
    const pvf::EntityId audioId = audio->id;
    audio->data[QStringLiteral("startFrame")] = 10.0;
    audio->data[QStringLiteral("trimStart")] = 0.25;
    audio->data[QStringLiteral("trimEnd")] = 0.25;
    project->notifyChanged();
    if (project->timelineLength() != 25)
    {
        qCritical() << "audio-test: timeline length" << project->timelineLength();
        ++failures;
    }

    {
        pvf::Project mixProject;
        mixProject.fps = 30;
        mixProject.addAudio(wavPath, clip, QStringLiteral("Mix"));
        const QByteArray pcm = pvf::AudioEngine::mixRange(mixProject, 0.0, 1.0);
        if (pcm.size() != 48000 * 2 * 2)
        {
            qCritical() << "audio-test: mix size" << pcm.size();
            ++failures;
        }
        else
        {
            const auto* samples = reinterpret_cast<const qint16*>(pcm.constData());
            const int click = std::abs(int(samples[12000 * 2]));
            const int silence = std::abs(int(samples[24000 * 2]));
            if (click < 20000 || silence > 50)
            {
                qCritical() << "audio-test: mix timeline" << click << silence;
                ++failures;
            }
        }
    }

    const QString projectPath = QStringLiteral("/tmp/pvf_audio.pvproj");
    QString error;
    if (!project->save(projectPath, &error))
    {
        qCritical() << "audio-test: save" << error;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(projectPath, &error))
        {
            qCritical() << "audio-test: load" << error;
            ++failures;
        }
        else
        {
            const pvf::Entity* loadedAudio = loaded.entity(audioId);
            if (!loadedAudio || loadedAudio->type != pvf::EntityType::Audio
                || !loadedAudio->audio || loadedAudio->audio->peaks.isEmpty())
            {
                qCritical() << "audio-test: audio not restored";
                ++failures;
            }
        }
    }

    {
        pvf::Project libraryProject;
        pvf::Template* tmpl = libraryProject.addMediaTemplate(
            wavPath, pvf::EntityType::Audio, imported.thumbnail, clip->duration,
            QStringLiteral("Library Audio"), clip);
        const pvf::Entity* created =
            tmpl ? libraryProject.createFromTemplate(*tmpl) : nullptr;
        if (!created || created->type != pvf::EntityType::Audio || !created->audio)
        {
            qCritical() << "audio-test: library audio creation failed";
            ++failures;
        }
    }

    // Timeline waveform smoke test.
    auto* uiProject = new pvf::Project;
    pvf::MainWindow window(uiProject);
    window.resize(1200, 800);
    auto* uiAudio = uiProject->addAudio(wavPath, clip, QStringLiteral("Audio"));
    window.setSelectedEntityForTest(uiAudio->id);
    window.show();
    QApplication::processEvents();
    if (auto* timeline = window.findChild<pvf::TimelinePanel*>())
    {
        timeline->grab().toImage().save(QStringLiteral("/tmp/pvf_audio_timeline.png"));
    }
    else
    {
        qCritical() << "audio-test: timeline missing";
        ++failures;
    }

    // Narrow properties panel with a long unbroken asset path.
    uiAudio->name = QStringLiteral("永遠にゲームで対戦したいキリタン");
    uiAudio->sourcePath = QStringLiteral(
        "/home/xmb505/视频/PVForge/assets/"
        "71a0fbde5227bfd7658893e46cabc6ee0660945f71a0fbde5227bfd7658893e46cabc6ee.wav");
    uiProject->notifyChanged();
    window.setSelectedEntityForTest(uiAudio->id);
    window.resize(1100, 800);
    QApplication::processEvents();
    window.grab().save(QStringLiteral("/tmp/pvf_audio_properties.png"));

    auto* startButton = window.findChild<QPushButton*>(
        QStringLiteral("audioStartFromPlayhead"));
    auto* trimStartButton = window.findChild<QPushButton*>(
        QStringLiteral("audioTrimStartFromPlayhead"));
    auto* trimEndButton = window.findChild<QPushButton*>(
        QStringLiteral("audioTrimEndFromPlayhead"));
    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!startButton || !trimStartButton || !trimEndButton || !timeline)
    {
        qCritical() << "audio-test: playhead capture controls missing";
        ++failures;
    }
    else
    {
        // Drag the audio clip on the timeline: 60 px at 6 px/frame = 10 frames.
        uiAudio->data[QStringLiteral("startFrame")] = 10.0;
        uiAudio->data[QStringLiteral("trimStart")] = 0.0;
        uiAudio->data[QStringLiteral("trimEnd")] = 0.0;
        uiProject->notifyChanged();
        window.goToFrame(0);
        timeline->setPixelsPerFrameForTest(6.0);
        QApplication::processEvents();

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };
        const QPoint dragFrom(300, 56);   // middle of the 10..40 frame clip
        const QPoint dragTo(360, 56);     // +10 frames

        sendMouse(QEvent::MouseMove, dragFrom, Qt::NoButton);
        if (timeline->cursor().shape() != Qt::OpenHandCursor)
        {
            qCritical() << "audio-test: no open-hand cursor over the clip";
            ++failures;
        }
        sendMouse(QEvent::MouseButtonPress, dragFrom, Qt::LeftButton);
        if (timeline->cursor().shape() != Qt::ClosedHandCursor)
        {
            qCritical() << "audio-test: no closed-hand cursor while dragging";
            ++failures;
        }
        sendMouse(QEvent::MouseMove, dragTo, Qt::LeftButton);
        timeline->grab().toImage().save(QStringLiteral("/tmp/pvf_audio_drag.png"));
        sendMouse(QEvent::MouseButtonRelease, dragTo, Qt::NoButton);
        if (timeline->cursor().shape() != Qt::OpenHandCursor)
        {
            qCritical() << "audio-test: cursor not restored after drag";
            ++failures;
        }

        double startFrame =
            uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        if (std::abs(startFrame - 20.0) > 1e-6 || window.currentFrame() != 0)
        {
            qCritical() << "audio-test: timeline drag" << startFrame
                        << window.currentFrame();
            ++failures;
        }
        auto* undoAction =
            window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!undoAction || !undoAction->isEnabled())
        {
            qCritical() << "audio-test: audio drag is not undoable";
            ++failures;
        }
        else
        {
            undoAction->trigger();
            startFrame =
                uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
            if (std::abs(startFrame - 10.0) > 1e-6)
            {
                qCritical() << "audio-test: audio drag undo" << startFrame;
                ++failures;
            }
        }

        // A click without dragging still positions the playhead.
        window.goToFrame(0);
        sendMouse(QEvent::MouseButtonPress, dragFrom, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, dragFrom, Qt::NoButton);
        if (window.currentFrame() != 20)
        {
            qCritical() << "audio-test: audio click did not move the playhead"
                        << window.currentFrame();
            ++failures;
        }

        // Capturing the playhead inside the clip moves the start there; it
        // must not fall back to frame 0.
        window.goToFrame(30);
        startButton->click();
        if (window.currentFrame() != 30)
        {
            qCritical() << "audio-test: capture moved the playhead"
                        << window.currentFrame();
            ++failures;
        }
        window.goToFrame(40);
        trimStartButton->click();
        startFrame =
            uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        double trimStart =
            uiAudio->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        if (std::abs(startFrame - 40.0) > 1e-6
            || std::abs(trimStart - 1.0 / 3.0) > 1e-3
            || std::abs(startFrame + (1.0 - trimStart) * 30.0 - 60.0) > 1e-3)
        {
            qCritical() << "audio-test: trim start moved the remaining audio"
                        << startFrame << trimStart;
            ++failures;
        }
        timeline->grab().toImage().save(QStringLiteral("/tmp/pvf_audio_trim.png"));
        window.goToFrame(50);
        trimEndButton->click();
        startFrame =
            uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        trimStart =
            uiAudio->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double trimEnd =
            uiAudio->data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
        if (std::abs(startFrame - 40.0) > 1e-6
            || std::abs(trimStart - 1.0 / 3.0) > 1e-3
            || std::abs(trimEnd - 1.0 / 3.0) > 1e-3
            || std::abs(startFrame
                        + (1.0 - trimStart - trimEnd) * 30.0 - 50.0)
                   > 1e-3)
        {
            qCritical() << "audio-test: trim end" << startFrame << trimStart
                        << trimEnd;
            ++failures;
        }
        auto* startSpin =
            window.findChild<QSpinBox*>(QStringLiteral("audioStartFrame"));
        if (!startSpin || startSpin->value() != 40)
        {
            qCritical() << "audio-test: start frame field"
                        << (startSpin ? startSpin->value() : -1);
            ++failures;
        }
    }

    qInfo() << "audio-test failures" << failures;
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
        const QImage viewportShot = viewport->grab().toImage();
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
                types->setCurrentRow(3);
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
    // The current media pipeline decodes video first frames and audio with
    // FFmpeg software decoders. Keep Qt Multimedia's video backends from
    // probing VDPAU/VAAPI until real video playback is implemented; this also
    // avoids misleading driver warnings on machines without NVIDIA support.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray(","));

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
    if (app.arguments().contains(QStringLiteral("--import-async-test")))
        return importAsyncTest(app);
    if (app.arguments().contains(QStringLiteral("--drag-undo-test")))
        return dragUndoTest(app);
    if (app.arguments().contains(QStringLiteral("--rotation-snap-test")))
        return rotationSnapTest(app);
    if (app.arguments().contains(QStringLiteral("--entity-snap-test")))
        return entitySnapTest(app);
    if (app.arguments().contains(QStringLiteral("--guide-test")))
        return guideTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-frame-test")))
        return timelineFrameTest(app);
    if (app.arguments().contains(QStringLiteral("--camera-test")))
        return cameraTest(app);
    if (app.arguments().contains(QStringLiteral("--easing-test")))
        return easingTest(app);
    if (app.arguments().contains(QStringLiteral("--audio-test")))
        return audioTest(app);
    if (app.arguments().contains(QStringLiteral("--video-test")))
        return videoTest(app);
    if (app.arguments().contains(QStringLiteral("--scale-undo-test")))
        return scaleUndoTest(app);
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
