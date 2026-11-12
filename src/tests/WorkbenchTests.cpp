#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

int libraryTest(QApplication& app)
{
    const QString assetPath =
        QDir::tempPath() + QStringLiteral("/pvforge_library_asset_")
        + QString(80, QLatin1Char('x')) + QStringLiteral(".png");
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
        bool layoutOk = true;
        if (auto* list = window.findChild<QListWidget*>(QStringLiteral("assetList")))
            qInfo() << "library-test: list items" << list->count();
        if (auto* scroll = window.findChild<QScrollArea*>(QStringLiteral("projectScroll")))
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs"));
        auto* panel = window.findChild<QWidget*>(QStringLiteral("timelineAssetPanel"));
        auto* list = window.findChild<QListWidget*>(QStringLiteral("assetList"));
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* moved = window.findChild<QLabel*>(QStringLiteral("assetMovedLabel"));
        if (!tabs || !panel || !list || !timeline || !moved)
        {
            qCritical() << "library-test: asset widgets missing";
            layoutOk = false;
        }
        else
        {
            const QPoint panelPos = panel->mapTo(&window, QPoint(0, 0));
            const QPoint timelinePos = timeline->mapTo(&window, QPoint(0, 0));
            qInfo() << "library-test: widths tabs" << tabs->width()
                    << "panel" << panel->width() << "panel min"
                    << panel->minimumSizeHint().width();
            layoutOk = !tabs->isAncestorOf(panel)
                       && panel->width() >= 140 && panel->width() <= 260
                       && panelPos.x() < timelinePos.x()
                       && list->count() > 0
                       && !list->item(0)->icon().isNull();
            if (!layoutOk)
                qCritical() << "library-test: asset bin is not left of timeline"
                            << panel->width() << panelPos << timelinePos;
            if (!moved->isVisible())
                qCritical() << "library-test: project tab signpost missing";
        }
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_library.png"));

        auto* create = window.findChild<QPushButton*>(QStringLiteral("assetCreate"));
        auto* remove = window.findChild<QPushButton*>(QStringLiteral("assetRemove"));
        if (!create || !remove || !create->isVisible() || !remove->isVisible())
        {
            qCritical() << "library-test: create/remove button missing or hidden"
                        << (create ? create->isVisible() : false)
                        << (remove ? remove->isVisible() : false);
            app.exit(1);
            return;
        }

        const int before = project->entities().size();
        create->click();

        QTimer::singleShot(300, &window, [&app, &window, project, before, assetPath, layoutOk] {
            const int after = project->entities().size();
            qInfo() << "library-test: entities" << before << "->" << after;
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_entity_tab.png"));

            if (auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs")))
                tabs->setCurrentIndex(0);
            QTimer::singleShot(250, &window, [&app, &window, after, before, assetPath, layoutOk] {
                window.grab().toImage().save(
                    QStringLiteral("/tmp/pvforge_projectprops.png"));
                QFile::remove(assetPath);
                app.exit(after == before + 1 && layoutOk ? 0 : 1);
            });
        });
    });
    return app.exec();
}

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
    pvf::Entity* entity =
        testProject->addVector(0, 120, 120, Qt::red, Qt::white, 0);
    const pvf::EntityId entityId = entity->id;
    pvf::MainWindow window(testProject);
    window.resize(480, 1060);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, testProject, entityId] {
        int failures = 0;
        auto* objectTab =
            window.findChild<QScrollArea*>(QStringLiteral("objectTab"));
        if (!objectTab || objectTab->isVisible())
        {
            qCritical() << "layout-test: object tab visible at startup"
                        << (objectTab ? objectTab->isVisible() : false);
            ++failures;
        }
        window.setSelectedEntityForTest(entityId);
        QApplication::processEvents();
        if (!objectTab || !objectTab->isVisible())
        {
            qCritical() << "layout-test: object tab did not appear";
            ++failures;
        }
        window.setSelectedEntityForTest(pvf::kInvalidEntity);
        QApplication::processEvents();
        if (objectTab && objectTab->isVisible())
        {
            qCritical() << "layout-test: object tab did not hide";
            ++failures;
        }

        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_layout_narrow.png"));
        qInfo() << "layout-test: size" << window.size() << "minimum" << window.minimumSize();

        // Restore the v0.2.0 vertical ratio and check the library/buttons.
        window.resize(1440, 900);
        QApplication::processEvents();

        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* viewport = window.findChild<pvf::Viewport*>();
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs"));
        auto* panel =
            window.findChild<QWidget*>(QStringLiteral("timelineAssetPanel"));
        auto* split =
            window.findChild<QSplitter*>(QStringLiteral("timelineAssetSplit"));
        auto* create = window.findChild<QPushButton*>(QStringLiteral("assetCreate"));
        auto* remove = window.findChild<QPushButton*>(QStringLiteral("assetRemove"));
        if (!timeline || !viewport || !tabs || !panel || !split)
        {
            qCritical() << "layout-test: layout widgets missing";
            ++failures;
        }
        else
        {
            qInfo() << "layout-test: 1440x900 timeline" << timeline->geometry()
                    << "asset" << panel->geometry()
                    << "viewport" << viewport->geometry()
                    << "rightTabs" << tabs->width();
            if (timeline->height() < 190 || timeline->height() > 260)
            {
                qCritical() << "layout-test: timeline height is not about 220"
                            << timeline->height();
                ++failures;
            }
            const QPoint panelPos = panel->mapTo(&window, QPoint(0, 0));
            const QPoint timelinePos = timeline->mapTo(&window, QPoint(0, 0));
            if (panelPos.x() >= timelinePos.x()
                || panel->width() < 140 || panel->width() > 260)
            {
                qCritical() << "layout-test: asset bin is not left of timeline"
                            << panel->width() << panelPos << timelinePos;
                ++failures;
            }
            if (timeline->width() < window.centralWidget()->width() * 0.7)
            {
                qCritical() << "layout-test: timeline lost too much width"
                            << timeline->width() << window.centralWidget()->width();
                ++failures;
            }
            if (tabs->isAncestorOf(panel))
            {
                qCritical() << "layout-test: asset panel is still in the right dock";
                ++failures;
            }
            if (!create || !remove || !create->isVisible() || !remove->isVisible())
            {
                qCritical() << "layout-test: asset create/remove buttons hidden";
                ++failures;
            }
        }

        // The 640x560 workbench must stay above the timeline and keep its
        // create button visible, even when the viewport shrinks.
        auto* wbButton =
            window.findChild<QToolButton*>(QStringLiteral("workbenchButton"));
        auto* workbench = window.findChild<pvf::WorkbenchPanel*>();
        const auto settle = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };
        if (wbButton && workbench)
        {
            wbButton->click();
            settle(300);
            auto* wbCreate =
                workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
            if (!workbench->isVisible() || !wbCreate || !wbCreate->isVisible())
            {
                qCritical() << "layout-test: workbench create button not visible";
                ++failures;
            }
            else
            {
                QApplication::processEvents();
                if (auto* scroll = window.findChild<QScrollArea*>(
                        QStringLiteral("projectScroll")))
                    scroll->verticalScrollBar()->setValue(
                        scroll->verticalScrollBar()->maximum());
                QApplication::processEvents();
                window.grab().toImage().save(
                    QStringLiteral("/tmp/opencode/pvforge-layout-assets.png"));
                const int createBottom =
                    wbCreate->mapTo(&window, QPoint(0, wbCreate->height())).y();
                const int timelineTop =
                    timeline->mapTo(&window, QPoint(0, 0)).y();
                if (createBottom >= timelineTop)
                {
                    qCritical() << "layout-test: workbench create button is clipped"
                                << createBottom << timelineTop;
                    ++failures;
                }
            }

            window.resize(1000, 620);
            QApplication::processEvents();
            workbench->showAt(QPoint(8, 8));
            settle(250);
            window.grab().toImage().save(
                QStringLiteral("/tmp/opencode/pvforge-layout-small.png"));
            auto* smallCreate =
                workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
            if (!workbench->isVisible() || !smallCreate
                || !smallCreate->isVisible())
            {
                qCritical() << "layout-test: small viewport hides the workbench";
                ++failures;
            }
            else if (smallCreate->mapTo(&window, QPoint(0, smallCreate->height())).y()
                     >= timeline->mapTo(&window, QPoint(0, 0)).y())
            {
                qCritical() << "layout-test: small viewport clips the create button";
                ++failures;
            }
            workbench->hidePanel();
        }
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int workbenchTest(QApplication& app)
{
    QString error;
    // The test used to rely on /tmp/pvf_test.mp4 left behind by another run.
    // Generate the fixture when it is missing so the test is self-contained.
    const QString videoPath = QStringLiteral("/tmp/pvf_test.mp4");
    if (!QFile::exists(videoPath))
    {
        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"),
                     {QStringLiteral("-y"), QStringLiteral("-v"),
                      QStringLiteral("error"), QStringLiteral("-f"),
                      QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("color=c=red:s=320x240:d=1:r=30"),
                      QStringLiteral("-c:v"), QStringLiteral("libx264"),
                      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                      QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                      videoPath});
        if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0)
        {
            qCritical() << "workbench-test: cannot generate video fixture"
                        << ffmpeg.readAllStandardError();
            return 1;
        }
    }
    const QImage videoFrame =
        pvf::decodeVideoFirstFrame(videoPath, &error);
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

    pvf::Entity* video = project->addVideo(videoPath, videoFrame);
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

namespace pvf::tests {

void registerWorkbenchTests(Registry& registry)
{
    registry.add(QStringLiteral("--library-test"),
                 [](QApplication& app) { return libraryTest(app); });
    registry.add(QStringLiteral("--play-test"),
                 [](QApplication& app) { return playTest(app); });
    registry.add(QStringLiteral("--layout-test"),
                 [](QApplication& app) { return layoutTest(app); });
    registry.add(QStringLiteral("--workbench-test"),
                 [](QApplication& app) { return workbenchTest(app); });
}

} // namespace pvf::tests
