#include "app/AppIcon.h"
#include "app/MainWindow.h"
#include "app/NewProjectDialog.h"
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

        types->setCurrentRow(2);
        content->setText(QStringLiteral("工作台创建的文字"));
        create->click();

        QTimer::singleShot(350, &window, [&app, &window, button, workbench] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_types.png"));
            const int count = window.project()->entities().size();
            qInfo() << "workbench-test: entities after text create =" << count;

            button->click();
            auto* typeList =
                workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
            typeList->setCurrentRow(3);   // vector page

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
