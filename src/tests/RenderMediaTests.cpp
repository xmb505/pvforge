#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

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

    // Past the clip duration the video must be gone, not hold its last frame.
    window.goToFrame(21);
    QApplication::processEvents();
    if (!uiVideo->image.isNull())
    {
        qCritical() << "video-test: last frame held past the clip";
        ++failures;
    }
    if (std::abs(pvf::SceneRenderer::entityLocalBounds(*uiVideo).width() - 320.0)
        > 1e-6)
    {
        qCritical() << "video-test: bounds lost after the clip";
        ++failures;
    }
    window.goToFrame(19);
    QApplication::processEvents();
    if (uiVideo->image.isNull())
    {
        qCritical() << "video-test: clip did not return in range";
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

int folderTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* first =
        project->addVector(0, 120, 90, QColor(230, 90, 90), Qt::white, 0);
    pvf::Entity* folder = project->addFolder(QStringLiteral("Group"));
    pvf::Entity* second =
        project->addVector(0, 120, 90, QColor(90, 140, 230), Qt::white, 0);
    const pvf::EntityId firstId = first->id;
    const pvf::EntityId folderId = folder->id;
    const pvf::EntityId secondId = second->id;

    int failures = 0;
    if (!project->setEntityParent(secondId, folderId)
        || second->parent != folderId)
    {
        qCritical() << "folder-test: set parent";
        ++failures;
    }
    if (project->isDescendantOf(secondId, folderId) != true
        || project->isDescendantOf(folderId, secondId))
    {
        qCritical() << "folder-test: descendant check";
        ++failures;
    }
    if (project->setEntityParent(folderId, secondId))
    {
        qCritical() << "folder-test: cycle was allowed";
        ++failures;
    }

    // Layer order is pre-order: first, folder, then its child.
    QVector<int> order = project->entityOrder();
    if (order.size() != 3
        || project->entities()[order[0]].id != firstId
        || project->entities()[order[1]].id != folderId
        || project->entities()[order[2]].id != secondId)
    {
        qCritical() << "folder-test: entity order";
        ++failures;
    }

    // Children follow the folder's coordinate system.
    project->setValue(folderId, QStringLiteral("x"), 0, 100.0, false);
    project->setValue(folderId, QStringLiteral("y"), 0, 50.0, false);
    project->setValue(secondId, QStringLiteral("x"), 0, 10.0, false);
    QPointF childWorld = pvf::SceneRenderer::worldTransform(
                             *project, *project->entity(secondId), 0)
                             .map(QPointF(0, 0));
    if (std::abs(childWorld.x() - 110.0) > 1e-6
        || std::abs(childWorld.y() - 50.0) > 1e-6)
    {
        qCritical() << "folder-test: parent translation" << childWorld;
        ++failures;
    }
    project->setValue(folderId, QStringLiteral("scaleX"), 0, 2.0, false);
    project->setValue(folderId, QStringLiteral("scaleY"), 0, 2.0, false);
    childWorld = pvf::SceneRenderer::worldTransform(
                     *project, *project->entity(secondId), 0)
                     .map(QPointF(0, 0));
    if (std::abs(childWorld.x() - 120.0) > 1e-6
        || std::abs(childWorld.y() - 50.0) > 1e-6)
    {
        qCritical() << "folder-test: parent scale" << childWorld;
        ++failures;
    }
    project->setValue(folderId, QStringLiteral("x"), 0, 0.0, false);
    project->setValue(folderId, QStringLiteral("y"), 0, 0.0, false);
    project->setValue(folderId, QStringLiteral("scaleX"), 0, 1.0, false);
    project->setValue(folderId, QStringLiteral("scaleY"), 0, 1.0, false);
    project->setValue(secondId, QStringLiteral("x"), 0, 0.0, false);

    project->setEntityFlag(folderId, pvf::EntityFlag::Locked, true);
    if (!project->isEffectivelyLocked(secondId)
        || !project->isEffectivelyLocked(folderId))
    {
        qCritical() << "folder-test: effective lock";
        ++failures;
    }
    project->setEntityFlag(folderId, pvf::EntityFlag::Locked, false);
    project->setEntityFlag(folderId, pvf::EntityFlag::Hidden, true);
    if (!project->isEffectivelyHidden(secondId))
    {
        qCritical() << "folder-test: effective hide";
        ++failures;
    }
    project->setEntityFlag(folderId, pvf::EntityFlag::Hidden, false);

    // Parent references survive save/load.
    const QString projectPath = QStringLiteral("/tmp/pvf_folder.pvproj");
    QString error;
    if (!project->save(projectPath, &error))
    {
        qCritical() << "folder-test: save" << error;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(projectPath, &error))
        {
            qCritical() << "folder-test: load" << error;
            ++failures;
        }
        else
        {
            const pvf::Entity* loadedFolder = loaded.entity(folderId);
            const pvf::Entity* loadedSecond = loaded.entity(secondId);
            if (!loadedFolder || !loadedFolder->isFolder() || !loadedSecond
                || loadedSecond->parent != folderId)
            {
                qCritical() << "folder-test: hierarchy not restored";
                ++failures;
            }
        }
    }

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window,
                       [&app, &window, project, firstId, folderId, secondId, failures]() mutable {
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!timeline || !viewport)
        {
            qCritical() << "folder-test: widgets missing";
            app.exit(1);
            return;
        }

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        if (timeline->rowCountForTest() != 3
            || timeline->rowEntityForTest(0) != firstId
            || timeline->rowEntityForTest(1) != folderId
            || timeline->rowEntityForTest(2) != secondId
            || timeline->rowDepthForTest(2) != 1)
        {
            qCritical() << "folder-test: tree rows" << timeline->rowCountForTest();
            ++failures;
        }

        // Folder arrow collapses and expands its children.
        const QPoint arrow = timeline->folderArrowRectForTest(1).center();
        sendMouse(QEvent::MouseButtonPress, arrow, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, arrow, Qt::NoButton);
        if (timeline->rowCountForTest() != 2
            || !timeline->rowCollapsedForTest(folderId))
        {
            qCritical() << "folder-test: collapse" << timeline->rowCountForTest();
            ++failures;
        }
        sendMouse(QEvent::MouseButtonPress, arrow, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, arrow, Qt::NoButton);
        if (timeline->rowCountForTest() != 3
            || timeline->rowCollapsedForTest(folderId))
        {
            qCritical() << "folder-test: expand" << timeline->rowCountForTest();
            ++failures;
        }

        // Drag the first row into the folder row.
        const QPoint from(10, timeline->rowButtonRect(0, 0).center().y());
        const QPoint to(10, timeline->rowButtonRect(1, 0).center().y());
        sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, to, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, to, Qt::NoButton);
        QApplication::processEvents();

        const pvf::Entity* first = project->entity(firstId);
        if (!first || first->parent != folderId
            || timeline->rowEntityForTest(0) != folderId
            || timeline->rowEntityForTest(1) != firstId
            || timeline->rowDepthForTest(1) != 1)
        {
            qCritical() << "folder-test: drag into folder"
                        << (first ? first->parent : -999)
                        << timeline->rowEntityForTest(0)
                        << timeline->rowEntityForTest(1);
            ++failures;
        }

        // Layer moves are undoable.
        auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redoAction = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!undoAction || !redoAction)
        {
            qCritical() << "folder-test: undo actions missing";
            ++failures;
        }
        else
        {
            undoAction->trigger();
            QApplication::processEvents();
            first = project->entity(firstId);
            if (!first || first->parent != pvf::kInvalidEntity
                || timeline->rowEntityForTest(0) != firstId
                || timeline->rowEntityForTest(1) != folderId)
            {
                qCritical() << "folder-test: move undo"
                            << (first ? first->parent : -999);
                ++failures;
            }
            redoAction->trigger();
            QApplication::processEvents();
            first = project->entity(firstId);
            if (!first || first->parent != folderId
                || timeline->rowEntityForTest(0) != folderId
                || timeline->rowEntityForTest(1) != firstId)
            {
                qCritical() << "folder-test: move redo"
                            << (first ? first->parent : -999);
                ++failures;
            }
        }

        // The parent combo switches between world and a specific entity.
        window.setSelectedEntityForTest(firstId);
        QApplication::processEvents();
        auto* parents = window.findChild<QComboBox*>(QStringLiteral("parentCombo"));
        if (!parents)
        {
            qCritical() << "folder-test: parent combo missing";
            ++failures;
        }
        else
        {
            const int worldIndex = parents->findData(int(pvf::kInvalidEntity));
            const int folderIndex = parents->findData(int(folderId));
            if (worldIndex < 0 || folderIndex < 0)
            {
                qCritical() << "folder-test: parent choices missing"
                            << parents->count();
                ++failures;
            }
            else
            {
                parents->setCurrentIndex(worldIndex);
                QApplication::processEvents();
                if (first->parent != pvf::kInvalidEntity)
                {
                    qCritical() << "folder-test: parent to world" << first->parent;
                    ++failures;
                }
                parents->setCurrentIndex(folderIndex);
                QApplication::processEvents();
                if (first->parent != folderId)
                {
                    qCritical() << "folder-test: parent to folder" << first->parent;
                    ++failures;
                }
            }
        }

        // Moving a child inside a translated folder keeps the movement in
        // parent space.
        project->setValue(folderId, QStringLiteral("x"), 0, 100.0, false);
        project->setValue(folderId, QStringLiteral("y"), 0, 50.0, false);
        project->setValue(secondId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(secondId, QStringLiteral("y"), 0, 0.0, false);
        // The top layer wins the hit test; park the red rectangle away so
        // the click reaches the child that is being dragged.
        project->setValue(firstId, QStringLiteral("x"), 0, -500.0, false);
        window.setSelectedEntityForTest(secondId);
        QApplication::processEvents();
        const double viewScale = viewport->currentScale();
        const QPoint bodyPoint = viewport->entityPivotForTest(secondId).toPoint();
        auto sendViewport = [viewport](QEvent::Type type, const QPoint& pos,
                                       Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(viewport->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        sendViewport(QEvent::MouseButtonPress, bodyPoint, Qt::LeftButton);
        sendViewport(QEvent::MouseMove, bodyPoint + QPoint(50, 0), Qt::LeftButton);
        sendViewport(QEvent::MouseButtonRelease, bodyPoint + QPoint(50, 0),
                     Qt::NoButton);
        const double childX = project->value(secondId, QStringLiteral("x"), 0);
        if (std::abs(childX - 50.0 / viewScale) > 0.5)
        {
            qCritical() << "folder-test: parent-space move" << childX
                        << 50.0 / viewScale;
            ++failures;
        }
        project->setValue(folderId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(folderId, QStringLiteral("y"), 0, 0.0, false);
        project->setValue(secondId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(secondId, QStringLiteral("y"), 0, 0.0, false);

        // Folders get a gizmo too: dragging the X axis moves the group.
        window.setSelectedEntityForTest(folderId);
        QApplication::processEvents();
        const QPointF folderPivot = viewport->entityPivotForTest(folderId);
        const double folderXBefore =
            project->value(folderId, QStringLiteral("x"), 0);
        sendViewport(QEvent::MouseButtonPress,
                     folderPivot.toPoint() + QPoint(40, 0), Qt::LeftButton);
        sendViewport(QEvent::MouseMove, folderPivot.toPoint() + QPoint(70, 0),
                     Qt::LeftButton);
        sendViewport(QEvent::MouseButtonRelease,
                     folderPivot.toPoint() + QPoint(70, 0), Qt::NoButton);
        const double folderXAfter =
            project->value(folderId, QStringLiteral("x"), 0);
        if (std::abs(folderXAfter - folderXBefore) < 1.0)
        {
            qCritical() << "folder-test: folder gizmo did not move it"
                        << folderXBefore << folderXAfter;
            ++failures;
        }
        project->setValue(folderId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(folderId, QStringLiteral("y"), 0, 0.0, false);

        // Locking the folder locks and deselects its children.
        window.setSelectedEntityForTest(secondId);
        project->setEntityFlag(folderId, pvf::EntityFlag::Locked, true);
        QApplication::processEvents();
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "folder-test: locked folder child stayed selected";
            ++failures;
        }
        window.setSelectedEntityForTest(secondId);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "folder-test: locked folder child selectable";
            ++failures;
        }
        project->setEntityFlag(folderId, pvf::EntityFlag::Locked, false);
        window.setSelectedEntityForTest(secondId);
        if (window.selectedEntityForTest() != secondId)
        {
            qCritical() << "folder-test: unlocked folder child not selectable";
            ++failures;
        }

        window.setSelectedEntityForTest(folderId);
        QApplication::processEvents();
        window.grab().toImage().save(QStringLiteral("/tmp/pvf_folder_tree.png"));
        qInfo() << "folder-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
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
        const int clipY = timeline->rowCenterYForTest(0);
        const QPoint dragFrom(300, clipY);   // middle of the 10..40 frame clip
        const QPoint dragTo(360, clipY);     // +10 frames

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

int specialDemo(QApplication& app)
{
    auto* project = new pvf::Project;
    project->name = QStringLiteral("特殊工具演示");
    project->width = 1920;
    project->height = 1080;
    project->background = QColor(16, 16, 22);

    const auto place = [](pvf::Entity* entity, double x, double y) {
        if (!entity)
            return;
        entity->values[QStringLiteral("x")] = x;
        entity->values[QStringLiteral("y")] = y;
    };

    // Press conference brand wall backdrop.
    const QString wallLogoA =
        QDir::tempPath() + QStringLiteral("/pvf_demo_wall_a.png");
    const QString wallLogoB =
        QDir::tempPath() + QStringLiteral("/pvf_demo_wall_b.png");
    {
        QImage a(128, 128, QImage::Format_ARGB32);
        a.fill(Qt::transparent);
        {
            QPainter p(&a);
            p.setRenderHint(QPainter::Antialiasing, true);
            QFont font = p.font();
            font.setPixelSize(66);
            font.setBold(true);
            p.setFont(font);
            p.setPen(QColor(96, 96, 108));
            p.drawText(a.rect(), Qt::AlignCenter, QStringLiteral("PV"));
        }
        a.save(wallLogoA);

        QImage b(128, 128, QImage::Format_ARGB32);
        b.fill(Qt::transparent);
        {
            QPainter p(&b);
            p.setRenderHint(QPainter::Antialiasing, true);
            QPolygonF star;
            for (int i = 0; i < 10; ++i)
            {
                const double angle = -M_PI / 2 + i * M_PI / 5;
                const double radius = (i % 2 == 0) ? 52.0 : 22.0;
                star << QPointF(64 + std::cos(angle) * radius,
                                64 + std::sin(angle) * radius);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(96, 96, 108));
            p.drawPolygon(star);
        }
        b.save(wallLogoB);
    }
    pvf::BrandWallParams wallParams;
    wallParams.width = project->width;
    wallParams.height = project->height;
    wallParams.columns = 6;
    wallParams.rows = 3;
    wallParams.logoOpacity = 0.55;
    wallParams.logos = QStringList{wallLogoA, wallLogoB};
    wallParams.tileColors =
        QStringList{QColor(248, 248, 250).name(QColor::HexArgb),
                    QColor(232, 232, 238).name(QColor::HexArgb)};
    // Background motion streaks.
    pvf::FlowParams flow;
    flow.count = 34;
    flow.length = 620;
    flow.spacing = 72;
    flow.thickness = 9;
    flow.area = 2400;
    flow.speed = 620;
    flow.color = QColor(120, 140, 180, 190);
    QVariantMap flowData;
    flow.writeTo(flowData);
    place(project->addEntity(pvf::EntityType::FlowLines,
                             QStringLiteral("快速流过"), flowData),
          0, 0);

    // Manga screentone shading over the wall.
    pvf::HalftoneParams halftone;
    halftone.area = 760;
    halftone.spacing = 18;
    halftone.dotSize = 8;
    halftone.angle = 45;
    halftone.gradientAngle = 135;
    halftone.minRatio = 0.0;
    halftone.jitter = 0.12;
    halftone.color = QColor(24, 24, 32, 170);
    QVariantMap halftoneData;
    halftone.writeTo(halftoneData);
    place(project->addEntity(pvf::EntityType::HalftoneDots,
                             QStringLiteral("漫画点阵"), halftoneData),
          600, 330);

    // Focus rays behind the balloons.
    pvf::RadialParams radial;
    radial.count = 56;
    radial.radius = 560;
    radial.width = 22;
    radial.color = QColor(255, 208, 96);
    radial.spin = 4.0;
    QVariantMap radialData;
    radial.writeTo(radialData);
    place(project->addEntity(pvf::EntityType::RadialLines,
                             QStringLiteral("放射状线条"), radialData),
          0, 0);

    // Comic balloons.
    const auto balloon = [&](double x, double y, const QString& text, int size,
                             int shape, double tipX, double tipY,
                             const QColor& fill) {
        pvf::TextStyle style;
        style.text = text;
        style.pixelSize = size;
        style.vertical = shape == 1;
        style.color = fill.lightness() > 140 ? QColor(28, 28, 34) : QColor(255, 255, 250);
        style.padding = 24.0;
        style.align = int(Qt::AlignCenter);
        style.lineHeight = 1.1;
        pvf::BubbleStyle bubble;
        bubble.shape = shape;
        bubble.tail = shape == 0 || shape == 2;
        bubble.fill = fill;
        bubble.stroke = QColor(24, 22, 28);
        bubble.strokeWidth = 6;
        bubble.tipX = tipX;
        bubble.tipY = tipY;
        QVariantMap data;
        style.writeTo(data);
        bubble.writeTo(data);
        place(project->addEntity(pvf::EntityType::ComicText,
                                 QStringLiteral("漫画文本框"), data),
              x, y);
    };

    balloon(-560, -190, QStringLiteral("欢迎来到\nPVForge"), 66, 0, 0.25, 1.5,
            QColor(255, 255, 255));
    balloon(-560, 290, QStringLiteral("云朵气泡"), 58, 2, -0.2, 1.4,
            QColor(255, 255, 255));
    balloon(620, -200, QStringLiteral("日式竖排\n从右到左"), 58, 1, 0, 1.3,
            QColor(255, 255, 255));
    balloon(0, 300, QStringLiteral("爆炸式！"), 66, 3, 0, 1.3,
            QColor(255, 246, 210));

    pvf::TextStyle title;
    title.text = QStringLiteral("特殊工具");
    title.pixelSize = 104;
    title.color = QColor(255, 255, 255);
    title.strokeColor = QColor(24, 22, 28);
    title.strokeWidth = 5;
    title.padding = 8;
    QVariantMap titleData;
    title.writeTo(titleData);
    place(project->addEntity(pvf::EntityType::Text,
                             QStringLiteral("文字"), titleData),
          -650, -450);

    // Created last so it sits on the bottom row of the layer list and stays
    // behind the other tools.
    project->addBrandWall(wallParams);

    auto* window = new pvf::MainWindow(project);
    window->resize(1440, 900);
    window->show();
    if (auto* viewport = window->findChild<pvf::Viewport*>())
    {
        viewport->resetView();
        qInfo() << "special-demo: viewport" << viewport << "size" << viewport->size();
    }
    QTimer::singleShot(900, window, [window] {
        window->grab().toImage().save(
            QStringLiteral("/tmp/pvforge_special_demo.png"));
    });
    return app.exec();
}

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

int exportDialogTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "export-dialog-test:" << message;
            ++failures;
        }
        return condition;
    };

    pvf::model::Project project;
    project.settings.name = QStringLiteral("dialog");
    project.settings.width = 640;
    project.settings.height = 360;
    project.settings.fps = 30;
    auto addCamera = [&project](const QString& name, int start, int end) {
        pvf::model::Entity camera;
        camera.uuid = pvf::model::newUuid();
        camera.name = name;
        camera.start = start;
        camera.end = end;
        camera.content.kind = pvf::ContentKind::Camera;
        project.entities.insert(camera.uuid, camera);
        return camera.uuid;
    };
    const pvf::Uuid cameraA = addCamera(QStringLiteral("A 机位"), 0, 60);
    const pvf::Uuid cameraB = addCamera(QStringLiteral("B 机位"), 10, 70);

    pvf::ExportDialog dialog(project, cameraB);
    dialog.show();
    QApplication::processEvents();

    auto* cameras = dialog.cameraComboBoxForTest();
    check(cameras && cameras->count() == 2,
          QStringLiteral("camera combo count is not 2"));
    check(dialog.cameraUuid() == cameraB,
          QStringLiteral("preferred camera was not selected"));
    check(dialog.startFrame() == 10 && dialog.endFrame() == 70,
          QStringLiteral("camera range getters are wrong"));
    auto* range = dialog.findChild<QLabel*>(QStringLiteral("exportRangeLabel"));
    check(range && range->text().contains(QStringLiteral("10 .. 70")),
          QStringLiteral("range label does not show the camera range"));

    auto* resolutions = dialog.resolutionComboBoxForTest();
    check(resolutions && resolutions->count() == 3,
          QStringLiteral("resolution combo does not have 3 choices"));
    check(resolutions && resolutions->itemData(0).toInt() == 1
              && resolutions->itemData(1).toInt() == 2
              && resolutions->itemData(2).toInt() == 4,
          QStringLiteral("resolution scale data is wrong"));
    check(resolutions && resolutions->itemText(0).contains(QStringLiteral("640 × 360"))
              && resolutions->itemText(1).contains(QStringLiteral("1280 × 720"))
              && resolutions->itemText(2).contains(QStringLiteral("2560 × 1440")),
          QStringLiteral("resolution combo does not show actual sizes"));

    auto* fps = dialog.fpsComboBoxForTest();
    check(fps && fps->count() == 3 && fps->itemData(0).toInt() == 1
              && fps->itemData(1).toInt() == 2 && fps->itemData(2).toInt() == 4,
          QStringLiteral("fps scale choices are wrong"));
    check(fps && fps->itemText(0).contains(QStringLiteral("30"))
              && fps->itemText(1).contains(QStringLiteral("60"))
              && fps->itemText(2).contains(QStringLiteral("120")),
          QStringLiteral("fps combo does not show actual frame rates"));

    auto* crf = dialog.crfSpinBoxForTest();
    check(crf && crf->minimum() == 0 && crf->maximum() == 51,
          QStringLiteral("CRF range is not 0..51"));
    check(dialog.crf() == 18, QStringLiteral("CRF default is not 18"));

    auto* encoder = dialog.encoderComboBoxForTest();
    check(encoder && encoder->findData(QStringLiteral("libx264")) >= 0
              && encoder->findData(QStringLiteral("h264_vaapi")) >= 0,
          QStringLiteral("encoder choices are missing"));
    check(dialog.encoder() == QStringLiteral("libx264"),
          QStringLiteral("default encoder is not libx264"));

    auto* preset = dialog.presetComboBoxForTest();
    check(preset && preset->findData(QStringLiteral("medium")) >= 0
              && preset->findData(QStringLiteral("ultrafast")) >= 0
              && preset->findData(QStringLiteral("veryslow")) >= 0,
          QStringLiteral("preset choices are missing"));
    check(dialog.preset() == QStringLiteral("medium"),
          QStringLiteral("default preset is not medium"));

    auto* path = dialog.findChild<QLineEdit*>(QStringLiteral("exportOutputPath"));
    auto* start = dialog.startButtonForTest();
    check(path && start && start->isEnabled(),
          QStringLiteral("start button is not enabled for a valid camera/path"));
    if (path)
    {
        path->setText(QString());
        check(!start->isEnabled(),
              QStringLiteral("start button stays enabled with an empty path"));
        path->setText(QStringLiteral("/tmp/pvf-dialog-output"));
        check(start->isEnabled(),
              QStringLiteral("start button did not re-enable after a path"));
        check(dialog.outputPath().endsWith(QStringLiteral(".mp4")),
              QStringLiteral("output path did not gain the .mp4 suffix"));
    }

    // Overwrite confirmation: No keeps the dialog open, Yes accepts.
    QTemporaryDir overwriteDir;
    const QString existing =
        QDir(overwriteDir.path()).filePath(QStringLiteral("existing.mp4"));
    {
        QFile file(existing);
        check(file.open(QIODevice::WriteOnly), QStringLiteral("cannot create existing file"));
        file.write("old");
    }
    dialog.setConfirmLargeExportForTest(true);
    dialog.setOverwriteDecisionForTest(false);
    if (path)
        path->setText(existing);
    dialog.accept();
    check(dialog.result() != QDialog::Accepted && !dialog.overwriteExisting(),
          QStringLiteral("declining overwrite still accepted"));
    dialog.setOverwriteDecisionForTest(true);
    dialog.accept();
    check(dialog.result() == QDialog::Accepted && dialog.overwriteExisting(),
          QStringLiteral("confirming overwrite did not accept"));

    // 4x larger than 4096 on either axis asks for confirmation.
    pvf::model::Project large;
    large.settings.width = 1920;
    large.settings.height = 1080;
    large.settings.fps = 30;
    pvf::model::Entity largeCamera;
    largeCamera.uuid = pvf::model::newUuid();
    largeCamera.name = QStringLiteral("Camera");
    largeCamera.start = 0;
    largeCamera.end = 30;
    largeCamera.content.kind = pvf::ContentKind::Camera;
    large.entities.insert(largeCamera.uuid, largeCamera);
    pvf::ExportDialog largeDialog(large, largeCamera.uuid);
    auto* largePath =
        largeDialog.findChild<QLineEdit*>(QStringLiteral("exportOutputPath"));
    if (largePath)
        largePath->setText(QDir(overwriteDir.path()).filePath(QStringLiteral("large.mp4")));
    largeDialog.resolutionComboBoxForTest()->setCurrentIndex(
        largeDialog.resolutionComboBoxForTest()->findData(4));
    largeDialog.setOverwriteDecisionForTest(true);
    largeDialog.setLargeExportDecisionForTest(false);
    largeDialog.accept();
    check(largeDialog.result() != QDialog::Accepted,
          QStringLiteral("4x >4096 accepted without confirmation"));
    largeDialog.setLargeExportDecisionForTest(true);
    largeDialog.accept();
    check(largeDialog.result() == QDialog::Accepted,
          QStringLiteral("4x >4096 did not accept after confirmation"));

    // No camera: no choices and the start button is disabled.
    pvf::model::Project empty;
    empty.settings.width = 640;
    empty.settings.height = 360;
    empty.settings.fps = 30;
    pvf::ExportDialog emptyDialog(empty);
    auto* emptyCameras = emptyDialog.cameraComboBoxForTest();
    check(emptyCameras && emptyCameras->count() == 1
              && emptyCameras->currentData().toString().isEmpty(),
          QStringLiteral("empty project camera combo is wrong"));
    check(emptyDialog.cameraUuid().isEmpty(),
          QStringLiteral("empty project returned a camera"));
    check(!emptyDialog.startButtonForTest()->isEnabled(),
          QStringLiteral("start button is enabled without a camera"));

    check(pvf::friendlyExportError(QStringLiteral("output_exists"))
              .contains(QStringLiteral("已存在")),
          QStringLiteral("output_exists message is not friendly"));
    check(pvf::friendlyExportError(QStringLiteral("encoder_unavailable"))
              .contains(QStringLiteral("VAAPI")),
          QStringLiteral("encoder_unavailable message is not friendly"));
    check(pvf::friendlyExportError(QStringLiteral("invalid_crf"))
              .contains(QStringLiteral("0–51")),
          QStringLiteral("invalid_crf message is not friendly"));

    dialog.hide();
    largeDialog.hide();
    emptyDialog.hide();

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    pvf::ExportDialog shot(project, cameraA);
    if (auto* shotPath =
            shot.findChild<QLineEdit*>(QStringLiteral("exportOutputPath")))
        shotPath->setText(QStringLiteral("/tmp/pvforge-export.mp4"));
    shot.resolutionComboBoxForTest()->setCurrentIndex(0);
    shot.fpsComboBoxForTest()->setCurrentIndex(0);
    shot.crfSpinBoxForTest()->setValue(18);
    shot.show();
    QApplication::processEvents();
    shot.grab().toImage().save(
        QStringLiteral("/tmp/opencode/pvforge-p7a-export-dialog.png"));
    shot.hide();

    qInfo() << "export-dialog-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int exportTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "export-test:" << message;
            ++failures;
        }
        return condition;
    };
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffprobe.isEmpty() || ffmpeg.isEmpty())
    {
        qInfo() << "export-test: ffmpeg/ffprobe missing, skipped";
        return 0;
    }

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString configPath =
        QDir(temp.path()).filePath(QStringLiteral("mcp.json"));
    {
        QFile config(configPath);
        if (!config.open(QIODevice::WriteOnly))
            return 1;
        config.write("{\"port\":0}");
    }
    pvf::EditorSession::setMcpConfigPathForTest(configPath);
    pvf::EditorSession::setMcpPortOverrideForTest(0);

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p7a-export.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P7a Export");
    settings.width = 320;
    settings.height = 180;
    settings.fps = 24;
    QString error;
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error, false);
    if (!session)
    {
        qCritical() << "export-test: create failed" << error;
        return 1;
    }

    const QString wavPath =
        QDir(temp.path()).filePath(QStringLiteral("tone.wav"));
    {
        QProcess generator;
        generator.start(ffmpeg,
                        {QStringLiteral("-y"), QStringLiteral("-v"),
                         QStringLiteral("error"), QStringLiteral("-f"),
                         QStringLiteral("lavfi"), QStringLiteral("-i"),
                         QStringLiteral("sine=frequency=440:duration=1"),
                         QStringLiteral("-ac"), QStringLiteral("2"),
                         QStringLiteral("-ar"), QStringLiteral("48000"),
                         wavPath});
        if (!generator.waitForFinished(30000) || generator.exitCode() != 0)
        {
            qCritical() << "export-test: cannot generate wav"
                        << generator.readAllStandardError();
            return 1;
        }
    }
    const auto imported = session->assets().importFile(wavPath, &error);
    if (!imported)
    {
        qCritical() << "export-test: audio import failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity camera;
    camera.uuid = pvf::model::newUuid();
    camera.name = QStringLiteral("Camera");
    camera.start = 0;
    camera.end = 24;
    camera.content.kind = pvf::ContentKind::Camera;
    project.entities.insert(camera.uuid, camera);

    pvf::model::Entity vector;
    vector.uuid = pvf::model::newUuid();
    vector.name = QStringLiteral("红色方块");
    vector.start = 0;
    vector.end = 24;
    vector.content.kind = pvf::ContentKind::Vector;
    vector.content.vector.width = 120;
    vector.content.vector.height = 120;
    vector.content.vector.fillColor = QStringLiteral("#e62828");
    project.entities.insert(vector.uuid, vector);

    pvf::model::Entity audio;
    audio.uuid = pvf::model::newUuid();
    audio.name = QStringLiteral("Tone");
    audio.start = 0;
    audio.end = 24;
    audio.content.kind = pvf::ContentKind::Asset;
    audio.content.asset.type = pvf::AssetType::Audio;
    audio.content.asset.hash = imported->hash;
    project.entities.insert(audio.uuid, audio);

    const QVector<pvf::Uuid> setup = {camera.uuid, vector.uuid, audio.uuid};
    if (!session->document()->commitEntities(setup, QStringLiteral("p7a setup"),
                                             &error))
    {
        qCritical() << "export-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->resize(1280, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    if (!live)
    {
        qCritical() << "export-test: no live session";
        return 1;
    }
    window->setSuppressExportCompletionDialogForTest(true);

    const QString firstPath =
        QDir(temp.path()).filePath(QStringLiteral("first.mp4"));
    pvf::export2::Options captured;
    bool capturedValid = false;
    window->setExportOptionsProbeForTest([&](const pvf::export2::Options& options) {
        captured = options;
        capturedValid = true;
    });

    auto runWindowExport = [&](const QString& path, bool audioEnabled,
                               int fpsScale, int resScale, int crf,
                               const QString& encoderName,
                               const QString& presetName, bool overwrite,
                               bool* okOut, QString* errorOut,
                               bool* lockObserved = nullptr) {
        bool finished = false;
        *okOut = false;
        errorOut->clear();
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        window->setExportFinishedHandlerForTest(
            [&](bool success, const QString& message) {
                *okOut = success;
                *errorOut = message;
                finished = true;
                loop.quit();
            });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(180000);
        window->startSessionExportForTest(camera.uuid, 0, 24, path,
                                          audioEnabled, 24 * fpsScale, resScale,
                                          crf, encoderName, presetName,
                                          overwrite);
        if (window->exportRunningForTest())
        {
            if (lockObserved)
                *lockObserved = !window->humanEditingEnabledForTest();
            loop.exec();
        }
        return finished;
    };

    bool ok = false;
    QString exportError;
    bool lockedDuringExport = false;
    check(runWindowExport(firstPath, true, 2, 1, 23, QStringLiteral("libx264"),
                          QStringLiteral("fast"), false, &ok, &exportError,
                          &lockedDuringExport),
          QStringLiteral("window export did not finish"));
    check(ok, QStringLiteral("window export failed: %1").arg(exportError));
    check(lockedDuringExport,
          QStringLiteral("human editing was not locked during export"));
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("human editing stayed locked after export"));
    auto* exportAction = window->findChild<QAction*>(QStringLiteral("exportAction"));
    check(exportAction
              && exportAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+E")),
          QStringLiteral("export action/Ctrl+E is missing"));
    check(capturedValid, QStringLiteral("export options probe was not called"));
    check(captured.crf == 23 && captured.encoder == QStringLiteral("libx264")
              && captured.preset == QStringLiteral("fast")
              && captured.outputFps == 48 && captured.resolutionScale == 1
              && captured.includeAudio && !captured.overwrite
              && captured.cameraUuid == camera.uuid,
          QStringLiteral("options were not passed through to export2"));
    check(QFileInfo::exists(firstPath), QStringLiteral("first export file missing"));

    auto probe = [&](const QString& path, QJsonObject* object) {
        QProcess process;
        process.start(ffprobe,
                      {QStringLiteral("-v"), QStringLiteral("error"),
                       QStringLiteral("-print_format"), QStringLiteral("json"),
                       QStringLiteral("-show_streams"),
                       QStringLiteral("-show_format"), path});
        if (!process.waitForFinished(30000) || process.exitCode() != 0)
        {
            qCritical() << "export-test: ffprobe failed"
                        << process.readAllStandardError();
            return false;
        }
        *object = QJsonDocument::fromJson(process.readAllStandardOutput())
                      .object();
        return true;
    };
    auto rational = [](const QString& value) {
        const QStringList parts = value.split(QLatin1Char('/'));
        if (parts.size() == 2 && parts[1].toDouble() != 0.0)
            return parts[0].toDouble() / parts[1].toDouble();
        return value.toDouble();
    };
    auto hasStream = [](const QJsonObject& object, const QString& type) {
        for (const QJsonValue& value :
             object.value(QStringLiteral("streams")).toArray())
        {
            if (value.toObject().value(QStringLiteral("codec_type")).toString()
                == type)
                return true;
        }
        return false;
    };

    QJsonObject firstProbe;
    if (check(probe(firstPath, &firstProbe), QStringLiteral("cannot probe first.mp4")))
    {
        QJsonObject videoStream;
        for (const QJsonValue& value :
             firstProbe.value(QStringLiteral("streams")).toArray())
        {
            if (value.toObject().value(QStringLiteral("codec_type")).toString()
                == QStringLiteral("video"))
                videoStream = value.toObject();
        }
        const double duration =
            firstProbe.value(QStringLiteral("format"))
                .toObject()
                .value(QStringLiteral("duration"))
                .toString()
                .toDouble();
        check(videoStream.value(QStringLiteral("width")).toInt() == 320
                  && videoStream.value(QStringLiteral("height")).toInt() == 180,
              QStringLiteral("exported resolution is wrong"));
        const double frameRate =
            rational(videoStream.value(QStringLiteral("r_frame_rate")).toString());
        check(std::abs(frameRate - 48.0) < 0.01,
              QStringLiteral("exported fps is not 48 (avg %1, r %2)")
                  .arg(videoStream.value(QStringLiteral("avg_frame_rate")).toString(),
                       videoStream.value(QStringLiteral("r_frame_rate")).toString()));
        check(duration > 0.9 && duration < 1.1,
              QStringLiteral("exported duration is not ~1s: %1").arg(duration));
        check(hasStream(firstProbe, QStringLiteral("audio")),
              QStringLiteral("exported file has no audio track"));
    }

    // Audio switch: a video-only export must have no audio stream.
    const QString silentPath =
        QDir(temp.path()).filePath(QStringLiteral("silent.mp4"));
    check(runWindowExport(silentPath, false, 1, 1, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          false, &ok, &exportError)
              && ok,
          QStringLiteral("video-only export failed: %1").arg(exportError));
    QJsonObject silentProbe;
    if (check(probe(silentPath, &silentProbe),
              QStringLiteral("cannot probe silent.mp4")))
    {
        check(!hasStream(silentProbe, QStringLiteral("audio")),
              QStringLiteral("includeAudio=false still produced audio"));
    }

    // Resolution scale: 2x must produce 640×360 for a 320×180 project.
    const QString scaledPath =
        QDir(temp.path()).filePath(QStringLiteral("scaled.mp4"));
    check(runWindowExport(scaledPath, false, 1, 2, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          false, &ok, &exportError)
              && ok,
          QStringLiteral("2x resolution export failed: %1").arg(exportError));
    QJsonObject scaledProbe;
    if (check(probe(scaledPath, &scaledProbe),
              QStringLiteral("cannot probe scaled.mp4")))
    {
        QJsonObject videoStream;
        for (const QJsonValue& value :
             scaledProbe.value(QStringLiteral("streams")).toArray())
        {
            if (value.toObject().value(QStringLiteral("codec_type")).toString()
                == QStringLiteral("video"))
                videoStream = value.toObject();
        }
        check(videoStream.value(QStringLiteral("width")).toInt() == 640
                  && videoStream.value(QStringLiteral("height")).toInt() == 360,
              QStringLiteral("2x resolution output is not 640x360"));
    }

    // Existing file: refused without overwrite, accepted with overwrite.
    check(runWindowExport(firstPath, true, 1, 1, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          false, &ok, &exportError),
          QStringLiteral("overwrite probe export did not finish"));
    check(!ok && exportError == QStringLiteral("output_exists"),
          QStringLiteral("existing output was not refused: %1").arg(exportError));
    check(QFileInfo::exists(firstPath),
          QStringLiteral("refused overwrite removed the existing file"));
    check(runWindowExport(firstPath, true, 1, 1, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          true, &ok, &exportError)
              && ok,
          QStringLiteral("overwrite export failed: %1").arg(exportError));

    // Cancellation: export2 must report "cancelled" and leave no file.
    const QString cancelPath =
        QDir(temp.path()).filePath(QStringLiteral("cancelled.mp4"));
    pvf::export2::Options cancelOptions = captured;
    cancelOptions.outputPath = cancelPath;
    cancelOptions.includeAudio = false;
    cancelOptions.overwrite = false;
    cancelOptions.progress = [](int frame, int, double) {
        return frame < 3;
    };
    const pvf::export2::Result cancelResult = pvf::export2::exportVideo(
        live->project(), live->assets(), cancelOptions);
    check(!cancelResult.ok && cancelResult.error == QStringLiteral("cancelled"),
          QStringLiteral("cancel result is wrong: %1").arg(cancelResult.error));
    check(!QFileInfo::exists(cancelPath)
              && !QFileInfo::exists(cancelPath + QStringLiteral(".part.mp4")),
          QStringLiteral("cancel left a partial file"));

    // Invalid CRF / encoder are rejected before encoding.
    pvf::export2::Options invalid = captured;
    invalid.outputPath =
        QDir(temp.path()).filePath(QStringLiteral("invalid.mp4"));
    invalid.crf = 52;
    invalid.progress = {};
    const pvf::export2::Result invalidCrf =
        pvf::export2::exportVideo(live->project(), live->assets(), invalid);
    check(invalidCrf.error == QStringLiteral("invalid_crf"),
          QStringLiteral("invalid CRF was not rejected"));
    invalid.crf = 18;
    invalid.encoder = QStringLiteral("no-such-encoder");
    const pvf::export2::Result invalidEncoder =
        pvf::export2::exportVideo(live->project(), live->assets(), invalid);
    check(invalidEncoder.error == QStringLiteral("unknown_encoder"),
          QStringLiteral("unknown encoder was not rejected"));
    check(!QFileInfo::exists(invalid.outputPath),
          QStringLiteral("invalid options left an output file"));

    // Progress dialog: frame/percent/ETA text, preview and cancel signal.
    QString decodeError;
    const QImage firstFrame = pvf::decodeVideoFirstFrame(firstPath, &decodeError);
    check(!firstFrame.isNull(), QStringLiteral("cannot decode first frame: %1").arg(decodeError));
    {
        pvf::ExportProgressDialog progressDialog;
        progressDialog.resize(640, 460);
        progressDialog.setPreview(firstFrame);
        progressDialog.setProgress(12, 48, 3.4);
        progressDialog.show();
        QApplication::processEvents();
        auto* status =
            progressDialog.findChild<QLabel*>(QStringLiteral("exportStatus"));
        check(status && status->text().contains(QStringLiteral("12 / 48"))
                  && status->text().contains(QStringLiteral("25%"))
                  && status->text().contains(QStringLiteral("剩余")),
              QStringLiteral("progress dialog text is wrong: %1")
                  .arg(status ? status->text() : QString()));
        QDir().mkpath(QStringLiteral("/tmp/opencode"));
        check(progressDialog.grab().toImage().save(
                  QStringLiteral("/tmp/opencode/pvforge-p7a-export.png")),
              QStringLiteral("progress screenshot failed"));
        bool cancelRequested = false;
        QObject::connect(&progressDialog,
                         &pvf::ExportProgressDialog::cancelRequested,
                         [&cancelRequested] { cancelRequested = true; });
        if (auto* cancelButton = progressDialog.findChild<QPushButton*>(
                QStringLiteral("exportCancelButton")))
            cancelButton->click();
        check(cancelRequested,
              QStringLiteral("progress cancel button did not emit"));
        progressDialog.finish();
    }

    delete window;
    QApplication::processEvents();
    QFile::remove(wavPath);
    qInfo() << "export-test failures" << failures;
    return failures == 0 ? 0 : 1;
}


} // namespace

namespace pvf::tests {

void registerRenderMediaTests(Registry& registry)
{
    registry.add(QStringLiteral("--video-test"),
                 [](QApplication& app) { return videoTest(app); });
    registry.add(QStringLiteral("--folder-test"),
                 [](QApplication& app) { return folderTest(app); });
    registry.add(QStringLiteral("--audio-test"),
                 [](QApplication& app) { return audioTest(app); });
    registry.add(QStringLiteral("--special-demo"),
                 [](QApplication& app) { return specialDemo(app); });
    registry.add(QStringLiteral("--demo-shot"),
                 [](QApplication& app) { return renderDemo(app); });
    registry.add(QStringLiteral("--export-dialog-test"),
                 [](QApplication& app) { return exportDialogTest(app); });
    registry.add(QStringLiteral("--export-test"),
                 [](QApplication& app) { return exportTest(app); });
    registry.add(QStringLiteral("--html-capture-test"),
                 [](QApplication&) { return pvf::html::htmlCaptureTest(); });
    registry.add(QStringLiteral("--html-layer-test"),
                 [](QApplication&) { return pvf::html::htmlLayerTest(); });
}

} // namespace pvf::tests
