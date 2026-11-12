#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

bool writeTestPng(const QString& path, const QColor& color, const QSize& size = QSize(64, 40))
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    return image.save(path);
}

bool runTestFfmpeg(const QStringList& arguments)
{
    QProcess process;
    process.start(QStringLiteral("ffmpeg"), arguments);
    return process.waitForFinished(60000) && process.exitCode() == 0;
}

QString makeTestWav(const QString& directory)
{
    const QString path = QDir(directory).filePath(QStringLiteral("tone.wav"));
    if (!runTestFfmpeg({QStringLiteral("-y"), QStringLiteral("-v"),
                        QStringLiteral("error"), QStringLiteral("-f"),
                        QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                        path}))
        return {};
    return path;
}

QString makeTestVideo(const QString& directory, const QString& name,
                      double seconds, bool withAudio)
{
    const QString path = QDir(directory).filePath(name);
    QStringList args = {
        QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=blue:s=320x180:d=%1:r=30").arg(seconds)};
    if (withAudio)
    {
        args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
             << QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1")
                    .arg(seconds)
             << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-shortest");
    }
    args << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-preset") << QStringLiteral("ultrafast") << path;
    if (!runTestFfmpeg(args))
        return {};
    return path;
}

QString makeTestPvhtml(const QString& directory)
{
    const QString path = QDir(directory).filePath(QStringLiteral("html"));
    if (!QDir().mkpath(path))
        return {};
    QFile html(QDir(path).filePath(QStringLiteral("index.html")));
    if (!html.open(QIODevice::WriteOnly))
        return {};
    html.write("<!doctype html><html><body style='background:#123'>PVForge</body></html>");
    html.close();

    QJsonObject manifest;
    manifest.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
    manifest.insert(QStringLiteral("logicalSize"), QJsonArray{320, 180});
    manifest.insert(QStringLiteral("timeDependent"), false);
    QFile json(QDir(path).filePath(QStringLiteral("pvhtml.json")));
    if (!json.open(QIODevice::WriteOnly))
        return {};
    json.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    json.close();
    return path;
}

int panelRowForHash(const pvf::AssetLibraryPanel& panel, const QString& hash)
{
    for (int row = 0; row < panel.assetCount(); ++row)
        if (panel.assetHashAt(row) == hash)
            return row;
    return -1;
}

int assetsTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-assets.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 素材箱");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "assets-test: create failed" << error;
        return 1;
    }

    const QString alphaPath = QDir(temp.path()).filePath(QStringLiteral("alpha.png"));
    const QString betaPath = QDir(temp.path()).filePath(QStringLiteral("beta.png"));
    const QString videoPath = makeTestVideo(temp.path(), QStringLiteral("clip.mp4"), 2.0,
                                            true);
    const QString wavPath = makeTestWav(temp.path());
    const QString htmlPath = makeTestPvhtml(temp.path());
    if (!writeTestPng(alphaPath, QColor(220, 70, 70))
        || !writeTestPng(betaPath, QColor(70, 130, 220)) || videoPath.isEmpty()
        || wavPath.isEmpty() || htmlPath.isEmpty())
    {
        qCritical() << "assets-test: cannot create test media";
        return 1;
    }

    const auto alpha = session->assets().importFile(alphaPath, &error);
    const auto beta = session->assets().importFile(betaPath, &error);
    const auto video = session->assets().importVideoWithAudio(videoPath, &error);
    const auto audio = session->assets().importFile(wavPath, &error);
    const auto html = session->assets().importPvhtml(htmlPath, &error);
    if (!alpha || !beta || !video || !audio || !html)
    {
        qCritical() << "assets-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    if (!live || !panel)
    {
        qCritical() << "assets-test: panel missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "assets-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto row = [&](const QString& hash) { return panelRowForHash(*panel, hash); };

    // Visible entry points: the asset bin now sits left of the timeline and
    // keeps the create/remove buttons, not hidden test-only objects.
    auto* rightTabs = window->findChild<QTabWidget*>(QStringLiteral("rightTabs"));
    auto* timelineAssets =
        window->findChild<QSplitter*>(QStringLiteral("timelineAssetSplit"));
    auto* createButton =
        window->findChild<QPushButton*>(QStringLiteral("assetCreate"));
    auto* removeButton =
        window->findChild<QPushButton*>(QStringLiteral("assetRemove"));
    check(rightTabs && !rightTabs->isAncestorOf(panel),
          QStringLiteral("asset panel is still in the right dock"));
    check(timelineAssets && timelineAssets->isAncestorOf(panel),
          QStringLiteral("asset panel is not in the timeline asset split"));
    check(createButton && createButton->isVisible(),
          QStringLiteral("create button is not visible"));
    check(removeButton && removeButton->isVisible(),
          QStringLiteral("delete button is not visible"));

    auto* sessionTimeline = window->timelineForTest();
    auto* sessionViewport = window->viewportForTest();
    check(sessionTimeline && sessionTimeline->height() >= 190
              && sessionTimeline->height() <= 260,
          QStringLiteral("timeline height is not about 220"));
    check(sessionTimeline
              && sessionTimeline->width()
                     > window->centralWidget()->width() * 0.7,
          QStringLiteral("timeline lost most of its width"));
    check(sessionViewport && sessionViewport->width() > 1000
              && sessionViewport->height() > 520,
          QStringLiteral("viewport did not regain its v0.2.0 height"));

    if (auto* scroll =
            window->findChild<QScrollArea*>(QStringLiteral("projectScroll")))
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QApplication::processEvents();
    window->grab().save(QStringLiteral("/tmp/opencode/pvforge-session-library.png"));

    check(panel->assetCount() >= 5,
          QStringLiteral("asset list does not contain the imported assets"));
    check(row(alpha->hash) >= 0 && row(video->video.hash) >= 0
              && row(html->hash) >= 0,
          QStringLiteral("imported hashes missing from the list"));

    // Search matches name, hash and tags.
    panel->setSearchText(QStringLiteral("alpha"));
    check(panel->assetCount() == 1 && panel->assetHashAt(0) == alpha->hash,
          QStringLiteral("search did not filter by name"));
    panel->setSearchText(QString());
    check(panel->assetCount() >= 5, QStringLiteral("search clear did not restore"));

    // Tags and the tag filter.
    QString tagsError;
    check(panel->setAssetTags(alpha->hash, {QStringLiteral("intro"),
                                            QStringLiteral("red")},
                              &tagsError),
          QStringLiteral("set tags failed: %1").arg(tagsError));
    check(panel->tagFilters().contains(QStringLiteral("intro")),
          QStringLiteral("tag filter list missing the new tag"));
    panel->setTagFilter(QStringLiteral("intro"));
    check(panel->assetCount() == 1 && panel->assetHashAt(0) == alpha->hash,
          QStringLiteral("tag filter did not filter"));
    check(panel->assetTagsAt(0).contains(QStringLiteral("intro")),
          QStringLiteral("row tags missing"));
    panel->setTagFilter(QString());
    check(panel->assetCount() >= 5, QStringLiteral("tag clear did not restore"));

    // Thumbnails for every asset type.
    const QPixmap alphaThumb = panel->assetThumbnailAt(row(alpha->hash));
    const QPixmap videoThumb = panel->assetThumbnailAt(row(video->video.hash));
    const QPixmap audioThumb = panel->assetThumbnailAt(row(audio->hash));
    const QPixmap htmlThumb = panel->assetThumbnailAt(row(html->hash));
    check(!alphaThumb.isNull() && !videoThumb.isNull() && !audioThumb.isNull()
              && !htmlThumb.isNull(),
          QStringLiteral("one or more thumbnails are null"));

    // Rename writes asset.json and refreshes the list.
    check(panel->renameAsset(alpha->hash, QStringLiteral("Alpha 重命名"), &tagsError),
          QStringLiteral("rename failed: %1").arg(tagsError));
    check(panel->assetNameAt(row(alpha->hash)) == QStringLiteral("Alpha 重命名"),
          QStringLiteral("renamed asset name not shown"));

    // 创建实体 creates at the playhead through EditActions, like the legacy
    // button; dragging to the timeline stays the second entry point.
    if (auto* list = window->findChild<QListWidget*>(QStringLiteral("assetList")))
    {
        list->setCurrentRow(row(alpha->hash));
        QApplication::processEvents();
    }
    const qsizetype beforeCreate = live->project().entities.size();
    if (createButton)
        createButton->click();
    QApplication::processEvents();
    check(live->project().entities.size() == beforeCreate + 1,
          QStringLiteral("create button did not add an entity"));

    // Deleting a store entry keeps the entity and shows the offline state.
    pvf::model::Entity reference;
    reference.uuid = pvf::model::newUuid();
    reference.name = QStringLiteral("引用 beta");
    reference.start = 0;
    reference.end = 30;
    reference.content.kind = pvf::ContentKind::Asset;
    reference.content.asset.hash = beta->hash;
    reference.content.asset.type = pvf::AssetType::Image;
    const pvf::Uuid referenceUuid = reference.uuid;
    live->mutableProject().entities.insert(referenceUuid, reference);
    if (!live->document()->commitEntity(referenceUuid,
                                        QStringLiteral("p5 assets setup"), &error))
    {
        qCritical() << "assets-test: setup commit failed" << error;
        return 1;
    }

    // A missing content file (asset.json still present) is a different
    // offline state; re-importing the same bytes repairs it in place.
    const QStringList betaContent = QDir(beta->directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (!betaContent.isEmpty())
        QFile::remove(beta->directory + QLatin1Char('/') + betaContent.first());
    panel->refresh();
    const int contentMissingRow = row(beta->hash);
    check(contentMissingRow >= 0 && panel->assetMissingAt(contentMissingRow),
          QStringLiteral("content-missing asset is not offline"));
    check(panel->assetMissingReasonAt(contentMissingRow) == QStringLiteral("内容缺失"),
          QStringLiteral("content-missing reason is not 内容缺失"));
    check(panel->reimportMissing(beta->hash, betaPath, &tagsError),
          QStringLiteral("re-import did not repair missing content: %1")
              .arg(tagsError));
    const int repairedRow = row(beta->hash);
    check(repairedRow >= 0 && !panel->assetMissingAt(repairedRow),
          QStringLiteral("repaired content still shows offline"));

    check(panel->deleteAsset(beta->hash, &tagsError),
          QStringLiteral("delete asset failed: %1").arg(tagsError));
    check(live->project().entities.contains(referenceUuid),
          QStringLiteral("deleting an asset removed its entity"));
    const int goneRow = row(beta->hash);
    check(goneRow >= 0 && panel->assetMissingAt(goneRow)
              && panel->assetMissingReasonAt(goneRow) == QStringLiteral("目录缺失"),
          QStringLiteral("deleted asset is not offline"));

    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!window->grab().save(QStringLiteral("/tmp/opencode/pvforge-p5-assets.png")))
    {
        qCritical() << "assets-test: screenshot failed";
        ++failures;
    }

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "assets-test OK: list/search/tags/thumbnails/rename/delete/missing";
    return failures == 0 ? 0 : 1;
}

int assetImportTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-import.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 导入");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "asset-import-test: create failed" << error;
        return 1;
    }

    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("import.png"));
    const QString wavPath = makeTestWav(temp.path());
    const QString videoPath = makeTestVideo(temp.path(), QStringLiteral("import.mp4"),
                                            2.0, true);
    const QString htmlPath = makeTestPvhtml(temp.path());
    if (!writeTestPng(imagePath, QColor(40, 180, 90)) || wavPath.isEmpty()
        || videoPath.isEmpty() || htmlPath.isEmpty())
    {
        qCritical() << "asset-import-test: cannot create test media";
        return 1;
    }

    pvf::AssetLibraryPanel panel(session.get());
    QString lastStatus;
    QObject::connect(&panel, &pvf::AssetLibraryPanel::statusMessage,
                     [&lastStatus](const QString& text) { lastStatus = text; });

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "asset-import-test:" << message;
            ++failures;
        }
        return condition;
    };

    check(panel.importPaths({imagePath, wavPath, videoPath, htmlPath}, &error),
          QStringLiteral("importPaths failed: %1").arg(error));

    int images = 0;
    int videos = 0;
    int audios = 0;
    int htmls = 0;
    QString videoHash;
    for (const pvf::assets::StoredAsset& asset : session->assets().list())
    {
        switch (asset.type)
        {
        case pvf::AssetType::Image:
            ++images;
            break;
        case pvf::AssetType::Video:
            ++videos;
            videoHash = asset.hash;
            break;
        case pvf::AssetType::Audio:
            ++audios;
            break;
        case pvf::AssetType::Html:
            ++htmls;
            break;
        }
    }
    check(images == 1 && videos == 1 && htmls == 1,
          QStringLiteral("store types wrong: image=%1 video=%2 html=%3")
              .arg(images)
              .arg(videos)
              .arg(htmls));
    check(audios == 2, QStringLiteral("video WAV was not split (audios=%1)").arg(audios));
    check(lastStatus.contains(QStringLiteral("WAV")),
          QStringLiteral("import status does not mention the split WAV"));

    const auto videoAsset = session->assets().get(videoHash);
    const QString audioHash = videoAsset
                                  ? videoAsset->metadata.value(QStringLiteral("audioHash"))
                                        .toString()
                                  : QString();
    check(!audioHash.isEmpty() && session->assets().get(audioHash).has_value(),
          QStringLiteral("video metadata audioHash does not resolve"));

    // The video extraction queue shows a bottom progress row; cancel removes
    // it whether the job is still running or already finished.
    for (int i = 0; i < 100 && panel.progressRowCount() == 0; ++i)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    check(panel.progressRowCount() > 0,
          QStringLiteral("video extraction progress row did not appear"));
    if (panel.progressRowCount() > 0)
    {
        check(panel.progressTextAt(0).contains(QStringLiteral("抽帧")),
              QStringLiteral("progress text is not an extraction status"));
        if (auto* cancel = panel.progressCancelButtonAt(0))
        {
            cancel->click();
            for (int i = 0; i < 100 && panel.progressRowCount() > 0; ++i)
            {
                QThread::msleep(20);
                QApplication::processEvents();
            }
            check(panel.progressRowCount() == 0,
                  QStringLiteral("progress cancel did not remove the row"));
        }
    }

    // Failure path keeps the existing assets and reports through the status
    // signal and the returned error.
    lastStatus.clear();
    error.clear();
    const bool failed = panel.importPaths(
        {QDir(temp.path()).filePath(QStringLiteral("missing.png"))}, &error);
    check(!failed && error.contains(QStringLiteral("文件不存在")),
          QStringLiteral("missing file was not reported"));
    check(lastStatus.contains(QStringLiteral("文件不存在")),
          QStringLiteral("failure was not sent to the status bar"));

    if (failures == 0)
        qInfo() << "asset-import-test OK: image/video/audio/pvhtml, WAV split, failure status";
    return failures == 0 ? 0 : 1;
}

int assetDragTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-drag.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 拖拽");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "asset-drag-test: create failed" << error;
        return 1;
    }

    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("drag.png"));
    const QString videoPath = makeTestVideo(temp.path(), QStringLiteral("drag.mp4"), 2.0,
                                            true);
    const QString wavPath = makeTestWav(temp.path());
    const QString htmlPath = makeTestPvhtml(temp.path());
    if (!writeTestPng(imagePath, QColor(240, 160, 40)) || videoPath.isEmpty()
        || wavPath.isEmpty() || htmlPath.isEmpty())
    {
        qCritical() << "asset-drag-test: cannot create test media";
        return 1;
    }

    const auto image = session->assets().importFile(imagePath, &error);
    const auto video = session->assets().importVideoWithAudio(videoPath, &error);
    const auto audio = session->assets().importFile(wavPath, &error);
    const auto html = session->assets().importPvhtml(htmlPath, &error);
    if (!image || !video || !audio || !html)
    {
        qCritical() << "asset-drag-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !panel || !timeline)
    {
        qCritical() << "asset-drag-test: window parts missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "asset-drag-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto depth = [live] { return live->document()->store()->undoDepth(); };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "asset-drag-test: flush failed" << flushError;
    };
    auto mime = [panel](const QString& hash) {
        return std::unique_ptr<QMimeData>(panel->assetMimeData(hash));
    };

    const int imageDepth = depth();
    const pvf::EntityId imageId =
        timeline->dropAssetForTest(*mime(image->hash), 45, -1);
    check(imageId != pvf::kInvalidEntity, QStringLiteral("image drop returned invalid id"));
    const pvf::Uuid imageUuid = timeline->uuidForEntityId(imageId);
    const pvf::model::Entity imageEntity =
        live->project().entities.value(imageUuid);
    check(imageEntity.content.kind == pvf::ContentKind::Asset
              && imageEntity.content.asset.hash == image->hash
              && imageEntity.content.asset.type == pvf::AssetType::Image,
          QStringLiteral("image entity content wrong"));
    check(imageEntity.start == 45 && imageEntity.end == 75,
          QStringLiteral("image drop range wrong: %1..%2")
              .arg(imageEntity.start)
              .arg(imageEntity.end));
    flush();
    check(depth() == imageDepth + 1,
          QStringLiteral("image drop was not one commit"));

    const int videoDepth = depth();
    const int beforeVideo = live->project().entities.size();
    const pvf::EntityId videoId =
        timeline->dropAssetForTest(*mime(video->video.hash), 60, -1);
    check(videoId != pvf::kInvalidEntity, QStringLiteral("video drop failed"));
    check(live->project().entities.size() == beforeVideo + 2,
          QStringLiteral("video drop did not create picture + audio"));
    const pvf::Uuid videoUuid = timeline->uuidForEntityId(videoId);
    const pvf::model::Entity videoEntity =
        live->project().entities.value(videoUuid);
    check(videoEntity.start == 60 && videoEntity.end == 120,
          QStringLiteral("video drop range wrong"));
    check(videoEntity.row == 1, QStringLiteral("video auto row wrong: %1")
                                    .arg(videoEntity.row));
    const QString audioHash =
        video->video.metadata.value(QStringLiteral("audioHash")).toString();
    bool paired = false;
    for (const pvf::model::Entity& entity : live->project().entities)
        if (entity.content.kind == pvf::ContentKind::Asset
            && entity.content.asset.type == pvf::AssetType::Audio
            && entity.content.asset.hash == audioHash
            && entity.start == 60 && entity.end == 120)
            paired = true;
    check(paired, QStringLiteral("paired WAV audio entity missing"));
    flush();
    check(depth() == videoDepth + 1,
          QStringLiteral("video + audio drop was not one commit"));

    const int audioDepth = depth();
    const pvf::EntityId audioId =
        timeline->dropAssetForTest(*mime(audio->hash), 0, -1);
    check(audioId != pvf::kInvalidEntity, QStringLiteral("audio drop failed"));
    const pvf::Uuid audioUuid = timeline->uuidForEntityId(audioId);
    const pvf::model::Entity audioEntity =
        live->project().entities.value(audioUuid);
    check(audioEntity.content.asset.type == pvf::AssetType::Audio
              && audioEntity.start == 0 && audioEntity.end == 60,
          QStringLiteral("audio drop content/range wrong"));
    flush();
    check(depth() == audioDepth + 1, QStringLiteral("audio drop was not one commit"));

    const pvf::EntityId htmlId =
        timeline->dropAssetForTest(*mime(html->hash), 120, -1);
    check(htmlId != pvf::kInvalidEntity, QStringLiteral("html drop failed"));
    const pvf::model::Entity htmlEntity =
        live->project().entities.value(timeline->uuidForEntityId(htmlId));
    check(htmlEntity.content.asset.type == pvf::AssetType::Html,
          QStringLiteral("html entity type wrong"));

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "asset-drag-test OK: image/video+audio/audio/html, one commit each";
    return failures == 0 ? 0 : 1;
}

int assetReplaceTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-replace.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 替换");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "asset-replace-test: create failed" << error;
        return 1;
    }

    const QString longPath = makeTestVideo(temp.path(), QStringLiteral("long.mp4"), 4.0,
                                           false);
    const QString shortPath = makeTestVideo(temp.path(), QStringLiteral("short.mp4"),
                                            1.0, false);
    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("replace.png"));
    const QString wavPath = makeTestWav(temp.path());
    if (longPath.isEmpty() || shortPath.isEmpty() || wavPath.isEmpty()
        || !writeTestPng(imagePath, QColor(80, 200, 220)))
    {
        qCritical() << "asset-replace-test: cannot create test media";
        return 1;
    }

    const auto longVideo = session->assets().importFile(longPath, &error);
    const auto shortVideo = session->assets().importFile(shortPath, &error);
    const auto image = session->assets().importFile(imagePath, &error);
    const auto audio = session->assets().importFile(wavPath, &error);
    if (!longVideo || !shortVideo || !image || !audio)
    {
        qCritical() << "asset-replace-test: import failed" << error;
        return 1;
    }

    auto makeEntity = [&session](const QString& name, int start, int end,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.content.kind = kind;
        session->mutableProject().entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid videoEntity =
        makeEntity(QStringLiteral("长视频"), 10, 100, pvf::ContentKind::Asset);
    session->mutableProject().entities[videoEntity].content.asset.type =
        pvf::AssetType::Video;
    session->mutableProject().entities[videoEntity].content.asset.hash =
        longVideo->hash;
    session->mutableProject().entities[videoEntity].content.asset.sourceIn = 5;
    pvf::model::Track track;
    for (int frame : {0, 20, 29, 60, 89})
        track.keys.append(pvf::model::Keyframe{frame, double(frame), {}, {}});
    session->mutableProject().entities[videoEntity].tracks.insert(
        QStringLiteral("x"), track);

    const pvf::Uuid childEntity =
        makeEntity(QStringLiteral("子实体"), 10, 60, pvf::ContentKind::Empty);
    session->mutableProject().entities[childEntity].parentUuid = videoEntity;

    const pvf::Uuid imageEntity =
        makeEntity(QStringLiteral("图片"), 0, 30, pvf::ContentKind::Asset);
    session->mutableProject().entities[imageEntity].content.asset.type =
        pvf::AssetType::Image;
    session->mutableProject().entities[imageEntity].content.asset.hash =
        image->hash;

    const pvf::Uuid textEntity =
        makeEntity(QStringLiteral("文字"), 0, 30, pvf::ContentKind::Text);
    const pvf::Uuid cameraEntity =
        makeEntity(QStringLiteral("摄像机"), 0, 30, pvf::ContentKind::Camera);
    const pvf::Uuid emptyEntity =
        makeEntity(QStringLiteral("空"), 0, 30, pvf::ContentKind::Empty);
    const pvf::Uuid clampEntity =
        makeEntity(QStringLiteral("源偏移越界"), 0, 100, pvf::ContentKind::Asset);
    session->mutableProject().entities[clampEntity].content.asset.type =
        pvf::AssetType::Video;
    session->mutableProject().entities[clampEntity].content.asset.hash =
        longVideo->hash;
    session->mutableProject().entities[clampEntity].content.asset.sourceIn = 200;

    const QVector<pvf::Uuid> setup = {videoEntity, childEntity, imageEntity,
                                      textEntity, cameraEntity, emptyEntity,
                                      clampEntity};
    if (!session->document()->commitEntities(setup, QStringLiteral("p5 setup"),
                                             &error))
    {
        qCritical() << "asset-replace-test: setup commit failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    auto* properties = window->findChild<pvf::PropertiesPanel*>();
    if (!live || !panel || !properties)
    {
        qCritical() << "asset-replace-test: window parts missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "asset-replace-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto depth = [live] { return live->document()->store()->undoDepth(); };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "asset-replace-test: flush failed" << flushError;
    };

    // Incompatible groups are rejected before writing.
    error.clear();
    check(!live->editActions()->replaceAsset(imageEntity, audio->hash, &error)
              && error.contains(QStringLiteral("不兼容")),
          QStringLiteral("audio -> image was not rejected"));
    error.clear();
    check(!live->editActions()->replaceAsset(textEntity, image->hash, &error),
          QStringLiteral("generated content accepted an asset"));
    error.clear();
    check(!live->editActions()->replaceAsset(cameraEntity, image->hash, &error),
          QStringLiteral("camera accepted an asset"));

    // Empty entities accept any group.
    error.clear();
    check(live->editActions()->replaceAsset(emptyEntity, image->hash, &error),
          QStringLiteral("empty entity rejected an asset: %1").arg(error));
    check(live->project().entities.value(emptyEntity).content.kind
              == pvf::ContentKind::Asset,
          QStringLiteral("empty entity did not become an asset"));

    // A short video trims end, drops keys past the new duration and clamps
    // sourceIn. Child entities that would fall outside are rejected first.
    error.clear();
    const bool trimmed = live->editActions()->replaceAsset(videoEntity,
                                                           shortVideo->hash, &error);
    check(!trimmed && error.contains(QStringLiteral("子实体")),
          QStringLiteral("out-of-range child was not rejected"));
    live->mutableProject().entities[childEntity].end = 35;
    if (!live->document()->commitEntity(childEntity,
                                        QStringLiteral("p5 child trim"), &error))
    {
        qCritical() << "asset-replace-test: child commit failed" << error;
        return 1;
    }
    flush();

    const int beforeReplace = depth();
    error.clear();
    check(live->editActions()->replaceAsset(videoEntity, shortVideo->hash, &error),
          QStringLiteral("short video replace failed: %1").arg(error));
    const pvf::model::Entity replaced =
        live->project().entities.value(videoEntity);
    const int shortFrames =
        shortVideo->metadata.value(QStringLiteral("durationFrames")).toInt();
    check(shortFrames > 0, QStringLiteral("short video has no durationFrames"));
    const int expectedDuration = shortFrames - 5;
    check(replaced.end == replaced.start + expectedDuration,
          QStringLiteral("end was not trimmed: %1..%2 expected duration %3")
              .arg(replaced.start)
              .arg(replaced.end)
              .arg(expectedDuration));
    check(replaced.content.asset.hash == shortVideo->hash
              && replaced.content.asset.sourceIn == 5,
          QStringLiteral("asset/sourceIn not updated"));
    bool outOfRangeKey = false;
    if (const auto trackIt = replaced.tracks.constFind(QStringLiteral("x"));
        trackIt != replaced.tracks.constEnd())
        for (const pvf::model::Keyframe& key : trackIt->keys)
            if (key.frame >= expectedDuration)
                outOfRangeKey = true;
    check(!outOfRangeKey, QStringLiteral("out-of-range keys were kept"));
    flush();
    check(depth() == beforeReplace + 1,
          QStringLiteral("replace was not exactly one commit"));

    // sourceIn is clamped into the new material and the entity shrinks to the
    // single remaining frame.
    error.clear();
    check(live->editActions()->replaceAsset(clampEntity, shortVideo->hash, &error),
          QStringLiteral("sourceIn clamp replace failed: %1").arg(error));
    const pvf::model::Entity clamped =
        live->project().entities.value(clampEntity);
    check(clamped.content.asset.sourceIn == shortFrames - 1,
          QStringLiteral("sourceIn was not clamped: %1")
              .arg(clamped.content.asset.sourceIn));
    check(clamped.start == 0 && clamped.end == 1,
          QStringLiteral("clamped entity range wrong: %1..%2")
              .arg(clamped.start)
              .arg(clamped.end));

    // Asset bin "replace selected entity" uses the same path.
    live->document()->setSelection(imageEntity);
    error.clear();
    check(panel->replaceSelectedEntityWith(image->hash, &error) == false,
          QStringLiteral("replacing with the same asset should fail"));
    error.clear();
    check(panel->replaceSelectedEntityWith(longVideo->hash, &error),
          QStringLiteral("asset bin replacement path failed: %1").arg(error));
    check(live->project().entities.value(imageEntity).content.asset.hash
              == longVideo->hash,
          QStringLiteral("asset bin replacement did not write the entity"));

    // The properties panel exposes the replace button for session assets.
    live->document()->setSelection(imageEntity);
    QApplication::processEvents();
    auto* replaceButton =
        properties->findChild<QPushButton*>(QStringLiteral("replaceSourceButton"));
    check(replaceButton && replaceButton->isVisible(),
          QStringLiteral("properties replace button is hidden"));

    // Delete the short asset, keep the entity, then recover by re-importing
    // the same file (same hash).
    error.clear();
    check(panel->deleteAsset(shortVideo->hash, &error),
          QStringLiteral("delete short asset failed: %1").arg(error));
    check(live->project().entities.value(videoEntity).content.asset.hash
              == shortVideo->hash,
          QStringLiteral("deleting an asset changed the entity"));
    const int missingRow = panelRowForHash(*panel, shortVideo->hash);
    check(missingRow >= 0 && panel->assetMissingAt(missingRow),
          QStringLiteral("missing asset not shown after delete"));
    error.clear();
    check(panel->reimportMissing(shortVideo->hash, shortPath, &error),
          QStringLiteral("re-import did not recover the asset: %1").arg(error));
    check(live->assets().get(shortVideo->hash).has_value(),
          QStringLiteral("store still missing after recovery"));
    const int restoredRow = panelRowForHash(*panel, shortVideo->hash);
    check(restoredRow >= 0 && !panel->assetMissingAt(restoredRow),
          QStringLiteral("restored asset still shows offline"));

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "asset-replace-test OK: groups/trim keys/sourceIn/child guard/missing recovery";
    return failures == 0 ? 0 : 1;
}

int workbenchAssetsTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-workbench.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 工作台");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "workbench-assets-test: create failed" << error;
        return 1;
    }

    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("workbench.png"));
    if (!writeTestPng(imagePath, QColor(120, 200, 90)))
    {
        qCritical() << "workbench-assets-test: cannot create image";
        return 1;
    }
    const auto image = session->assets().importFile(imagePath, &error);
    if (!image)
    {
        qCritical() << "workbench-assets-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* workbench = window->findChild<pvf::WorkbenchPanel*>();
    auto* workbenchButton =
        window->findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    if (!live || !workbench || !workbenchButton)
    {
        qCritical() << "workbench-assets-test: window parts missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "workbench-assets-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto depth = [live] { return live->document()->store()->undoDepth(); };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "workbench-assets-test: flush failed" << flushError;
    };
    auto showWorkbench = [&] {
        workbenchButton->click();
        QApplication::processEvents();
        return workbench->isVisible();
    };
    auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
    auto* create = workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
    if (!types || !create)
    {
        qCritical() << "workbench-assets-test: controls missing";
        return 1;
    }
    auto selectType = [&](const QString& name) {
        for (int row = 0; row < types->count(); ++row)
            if (types->item(row)->text() == name)
            {
                types->setCurrentRow(row);
                QApplication::processEvents();
                return true;
            }
        return false;
    };
    auto lastEntity = [live] {
        return live->project().entities.value(live->document()->selection());
    };

    live->setPlayhead(90);

    // Media entry uses the store and creates the visual entity.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for media"))
        || !check(selectType(QStringLiteral("媒体")),
                  QStringLiteral("media page missing")))
        return 1;
    workbench->selectSessionMediaForTest(image->hash);
    QApplication::processEvents();
    check(create->isEnabled(), QStringLiteral("media create stayed disabled"));
    const int mediaDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity mediaEntity = lastEntity();
    check(mediaEntity.content.kind == pvf::ContentKind::Asset
              && mediaEntity.content.asset.hash == image->hash,
          QStringLiteral("media creation did not use the asset store"));
    check(mediaEntity.start == 90 && mediaEntity.end == 120,
          QStringLiteral("media start is not the playhead"));
    check(depth() == mediaDepth + 1,
          QStringLiteral("media creation was not one commit"));
    check(!workbench->isVisible(),
          QStringLiteral("workbench stayed open after media create"));

    // Text entry.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for text"))
        || !check(selectType(QStringLiteral("文字")), QStringLiteral("text page missing")))
        return 1;
    if (auto* textContent =
            workbench->findChild<QLineEdit*>(QStringLiteral("textContent")))
        textContent->setText(QStringLiteral("P5 标题"));
    const int textDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity textEntity = lastEntity();
    check(textEntity.content.kind == pvf::ContentKind::Text
              && textEntity.content.text.text == QStringLiteral("P5 标题"),
          QStringLiteral("text content wrong"));
    check(textEntity.start == 90 && textEntity.end == 120,
          QStringLiteral("text start is not the playhead"));
    check(textEntity.row == 1,
          QStringLiteral("text auto row wrong: %1").arg(textEntity.row));
    check(depth() == textDepth + 1,
          QStringLiteral("text creation was not one commit"));

    // Vector entry.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for vector"))
        || !check(selectType(QStringLiteral("矢量")),
                  QStringLiteral("vector page missing")))
        return 1;
    const int vectorDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity vectorEntity = lastEntity();
    check(vectorEntity.content.kind == pvf::ContentKind::Vector,
          QStringLiteral("vector content wrong"));
    check(vectorEntity.start == 90 && vectorEntity.end == 120,
          QStringLiteral("vector start is not the playhead"));
    check(vectorEntity.row == 2,
          QStringLiteral("vector auto row wrong: %1").arg(vectorEntity.row));
    check(depth() == vectorDepth + 1,
          QStringLiteral("vector creation was not one commit"));

    // Camera entry.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for camera"))
        || !check(selectType(QStringLiteral("摄像机")),
                  QStringLiteral("camera page missing")))
        return 1;
    if (auto* cameraName =
            workbench->findChild<QLineEdit*>(QStringLiteral("cameraName")))
        cameraName->setText(QStringLiteral("测试机位"));
    const int cameraDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity cameraEntity = lastEntity();
    check(cameraEntity.content.kind == pvf::ContentKind::Camera
              && cameraEntity.name == QStringLiteral("测试机位"),
          QStringLiteral("camera content/name wrong"));
    check(cameraEntity.start == 90 && cameraEntity.end == 120,
          QStringLiteral("camera start is not the playhead"));
    check(cameraEntity.row == 3,
          QStringLiteral("camera auto row wrong: %1").arg(cameraEntity.row));
    check(depth() == cameraDepth + 1,
          QStringLiteral("camera creation was not one commit"));

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "workbench-assets-test OK: media/text/vector/camera -> playhead/auto row/one commit";
    return failures == 0 ? 0 : 1;
}


} // namespace

namespace pvf::tests {

void registerAssetTests(Registry& registry)
{
    registry.add(QStringLiteral("--assets-test"),
                 [](QApplication& app) { return assetsTest(app); });
    registry.add(QStringLiteral("--asset-import-test"),
                 [](QApplication& app) { return assetImportTest(app); });
    registry.add(QStringLiteral("--asset-drag-test"),
                 [](QApplication& app) { return assetDragTest(app); });
    registry.add(QStringLiteral("--asset-replace-test"),
                 [](QApplication& app) { return assetReplaceTest(app); });
    registry.add(QStringLiteral("--workbench-assets-test"),
                 [](QApplication& app) { return workbenchAssetsTest(app); });
}

} // namespace pvf::tests
