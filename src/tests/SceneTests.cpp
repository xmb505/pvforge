#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJsonObject(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
}

bool rewriteProjectVersion(const QString& source, const QString& target, int version,
                           bool dropScenes)
{
    QJsonObject root = readJsonObject(source);
    if (root.isEmpty())
        return false;
    root[QStringLiteral("version")] = version;
    if (dropScenes)
    {
        root.remove(QStringLiteral("scenes"));
        root.remove(QStringLiteral("annotations"));
    }
    return writeJsonObject(target, root);
}

QVector<pvf::EntityId> drawableOrder(const pvf::Project& project)
{
    QVector<pvf::EntityId> order;
    for (int index : project.entityOrder())
    {
        const pvf::Entity& entity = project.entities()[index];
        if (!entity.isFolder())
            order.append(entity.id);
    }
    return order;
}

bool isIdentityFolder(const pvf::Project& project, pvf::EntityId id)
{
    const pvf::Entity* folder = project.entity(id);
    if (!folder || !folder->isFolder())
        return false;
    const auto zero = [&project, id](const QString& prop) {
        return std::abs(project.value(id, prop, 0)) < 1e-9;
    };
    return zero(QStringLiteral("x")) && zero(QStringLiteral("y"))
           && zero(QStringLiteral("rotation")) && zero(QStringLiteral("rotPointX"))
           && zero(QStringLiteral("rotPointY"))
           && std::abs(project.value(id, QStringLiteral("scaleX"), 0) - 1.0) < 1e-9
           && std::abs(project.value(id, QStringLiteral("scaleY"), 0) - 1.0) < 1e-9
           && std::abs(project.value(id, QStringLiteral("opacity"), 0) - 1.0) < 1e-9
           && folder->tracks.isEmpty();
}

bool transformNear(const QTransform& a, const QTransform& b)
{
    const QPointF points[] = {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1),
                              QPointF(37, -19)};
    for (const QPointF& point : points)
        if ((a.map(point) - b.map(point)).manhattanLength() > 1e-9)
            return false;
    return true;
}

int sceneTest()
{
    int failures = 0;
    const QString dir = QDir::tempPath();
    const QString basePath = dir + QStringLiteral("/pvforge_scene_base.pvproj");
    const QString v3Path = dir + QStringLiteral("/pvforge_scene_v3.pvproj");
    const QString v1Path = dir + QStringLiteral("/pvforge_scene_v1.pvproj");
    const QString v2Path = dir + QStringLiteral("/pvforge_scene_v2.pvproj");
    const QString tooNewPath = dir + QStringLiteral("/pvforge_scene_v5.pvproj");
    const QString emptyPath = dir + QStringLiteral("/pvforge_scene_empty.pvproj");
    const QString emptyV3Path = dir + QStringLiteral("/pvforge_scene_empty_v3.pvproj");
    QString error;

    // ---- Scene API, ranges and ordering --------------------------------
    pvf::Project project;
    project.fps = 30;
    pvf::Entity* a =
        project.addVector(0, 120, 90, QColor(230, 90, 90), Qt::white, 0);
    const pvf::EntityId aId = a->id;
    pvf::Entity* b =
        project.addVector(0, 120, 90, QColor(90, 140, 230), Qt::white, 0);
    const pvf::EntityId bId = b->id;
    pvf::Entity* camera = project.addCamera();
    const pvf::EntityId cameraId = camera->id;
    project.setValue(aId, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(aId, QStringLiteral("x"), 120, 300.0, true);
    // Outside the project-space clamp so migration must preserve clamping.
    project.setValue(cameraId, QStringLiteral("x"), 0, 400.0, false);

    if (project.timelineLength() != 120)
    {
        qCritical() << "scene-test: setup length" << project.timelineLength();
        return 1;
    }

    pvf::Scene* first =
        project.addScene(QStringLiteral("开场"), 0, 60, QColor(255, 80, 80),
                         QStringLiteral("第一段"), &error);
    if (!first)
    {
        qCritical() << "scene-test: add first scene" << error;
        return 1;
    }
    const QString firstId = first->id;
    const pvf::EntityId firstFolder = first->folderId;
    if (!isIdentityFolder(project, firstFolder))
    {
        qCritical() << "scene-test: first folder is not identity";
        ++failures;
    }
    if (!project.entity(firstFolder) || !project.entity(firstFolder)->isFolder())
    {
        qCritical() << "scene-test: scene folder missing";
        ++failures;
    }

    error.clear();
    if (project.addScene(QStringLiteral("重叠"), 50, 90, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: overlapping scene was accepted";
        return 1;
    }
    error.clear();
    if (project.addScene(QStringLiteral("负数"), -1, 10, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: negative start was accepted";
        return 1;
    }
    error.clear();
    if (project.addScene(QStringLiteral("倒序"), 20, 10, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: reversed range was accepted";
        return 1;
    }
    error.clear();
    if (project.addScene(QStringLiteral("越界"), 100, 130, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: out-of-timeline range was accepted";
        return 1;
    }
    if (project.scenes().size() != 1)
    {
        qCritical() << "scene-test: rejected scenes leaked into the list"
                    << project.scenes().size();
        ++failures;
    }

    pvf::Scene* second =
        project.addScene(QStringLiteral("后段"), 70, 120, QColor(80, 200, 120),
                         QStringLiteral("第二段"), &error);
    if (!second)
    {
        qCritical() << "scene-test: add second scene" << error;
        return 1;
    }
    const QString secondId = second->id;
    const pvf::EntityId secondFolder = second->folderId;
    if (project.scenes().size() != 2
        || project.scenes()[0].id != firstId
        || project.scenes()[1].id != secondId)
    {
        qCritical() << "scene-test: scenes are not sorted by start frame";
        ++failures;
    }

    auto expectSceneAt = [&](int frame, const QString& expected) {
        const pvf::Scene* scene = project.sceneAt(frame);
        const QString actual = scene ? scene->id : QString();
        if (actual != expected)
        {
            qCritical() << "scene-test: sceneAt" << frame << actual << expected;
            ++failures;
        }
    };
    expectSceneAt(0, firstId);
    expectSceneAt(60, firstId);
    expectSceneAt(61, QString());
    expectSceneAt(69, QString());
    expectSceneAt(70, secondId);
    expectSceneAt(120, secondId);
    expectSceneAt(121, QString());
    expectSceneAt(-1, QString());
    if (project.sceneFolderAt(30) != firstFolder
        || project.sceneFolderAt(65) != pvf::kInvalidEntity
        || project.sceneFolderAt(80) != secondFolder)
    {
        qCritical() << "scene-test: sceneFolderAt boundary";
        ++failures;
    }
    if (!project.sceneById(firstId) || project.sceneById(QStringLiteral("missing")))
    {
        qCritical() << "scene-test: sceneById";
        ++failures;
    }

    if (!project.updateScene(firstId, QStringLiteral("开场改"), 0, 65,
                             QColor(10, 20, 30), QStringLiteral("改过"), &error))
    {
        qCritical() << "scene-test: update scene" << error;
        ++failures;
    }
    const pvf::Scene* updatedFirst = project.sceneById(firstId);
    if (!updatedFirst || updatedFirst->name != QStringLiteral("开场改")
        || updatedFirst->endFrame != 65
        || updatedFirst->color.name(QColor::HexArgb)
               != QColor(10, 20, 30).name(QColor::HexArgb)
        || updatedFirst->note != QStringLiteral("改过"))
    {
        qCritical() << "scene-test: updated fields mismatch";
        ++failures;
    }
    error.clear();
    if (project.updateScene(firstId, QStringLiteral("开场改"), 0, 75, QColor(),
                            QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: overlapping update was accepted";
        ++failures;
    }
    error.clear();
    if (project.updateScene(firstId, QStringLiteral("开场改"), 0, 130, QColor(),
                            QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: out-of-range update was accepted";
        ++failures;
    }
    error.clear();
    if (project.updateScene(QStringLiteral("missing"), QStringLiteral("x"), 0, 1,
                            QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: missing scene update was accepted";
        ++failures;
    }

    // Removing a scene keeps its folder and children as ordinary data.
    if (!project.setEntityParent(bId, secondFolder)
        || !project.removeScene(secondId))
    {
        qCritical() << "scene-test: remove scene failed";
        ++failures;
    }
    const pvf::Entity* orphanedFolder = project.entity(secondFolder);
    const pvf::Entity* movedB = project.entity(bId);
    if (project.sceneById(secondId) || !orphanedFolder || !movedB
        || movedB->parent != secondFolder)
    {
        qCritical() << "scene-test: remove scene deleted entities";
        ++failures;
    }
    if (project.removeScene(secondId))
    {
        qCritical() << "scene-test: removing twice succeeded";
        ++failures;
    }

    if (!project.setKeyframeNote(aId, QStringLiteral("x"), 120.0,
                                 QStringLiteral("收尾")))
    {
        qCritical() << "scene-test: keyframe note set failed";
        ++failures;
    }
    if (project.keyframeNote(aId, QStringLiteral("x"), 120.0)
        != QStringLiteral("收尾"))
    {
        qCritical() << "scene-test: keyframe note query";
        ++failures;
    }

    // ---- v4 roundtrip --------------------------------------------------
    error.clear();
    if (!project.save(basePath, &error))
    {
        qCritical() << "scene-test: save v4" << error;
        return 1;
    }
    const QJsonObject savedRoot = readJsonObject(basePath);
    if (savedRoot.value(QStringLiteral("version")).toInt() != pvf::kProjectVersion
        || !savedRoot.contains(QStringLiteral("scenes"))
        || !savedRoot.contains(QStringLiteral("annotations"))
        || savedRoot.value(QStringLiteral("scenes")).toArray().size() != 1
        || !savedRoot.value(QStringLiteral("annotations")).toArray().isEmpty())
    {
        qCritical() << "scene-test: v4 json shape";
        ++failures;
    }
    bool noteAt120 = false;
    bool noteAt0 = true;
    for (const QJsonValue& value : savedRoot.value(QStringLiteral("entities")).toArray())
    {
        const QJsonObject entity = value.toObject();
        if (entity.value(QStringLiteral("id")).toInt() != aId)
            continue;
        const QJsonArray keys =
            entity.value(QStringLiteral("tracks")).toObject()
                .value(QStringLiteral("x")).toArray();
        for (const QJsonValue& keyValue : keys)
        {
            const QJsonObject key = keyValue.toObject();
            const double frame = key.value(QStringLiteral("f")).toDouble();
            if (std::abs(frame - 120.0) < 1e-6)
                noteAt120 = key.value(QStringLiteral("note")).toString()
                            == QStringLiteral("收尾");
            if (std::abs(frame) < 1e-6)
                noteAt0 = !key.contains(QStringLiteral("note"));
        }
    }
    if (!noteAt120 || !noteAt0)
    {
        qCritical() << "scene-test: keyframe note json" << noteAt120 << noteAt0;
        ++failures;
    }

    pvf::Project loaded;
    if (!loaded.load(basePath, &error))
    {
        qCritical() << "scene-test: load v4" << error;
        return 1;
    }
    if (loaded.scenes().size() != 1
        || loaded.scenes().first().id != firstId
        || loaded.scenes().first().folderId != firstFolder
        || loaded.scenes().first().endFrame != 65
        || loaded.timelineLength() != 120
        || !isIdentityFolder(loaded, firstFolder))
    {
        qCritical() << "scene-test: v4 scene roundtrip";
        ++failures;
    }
    if (const pvf::Entity* loadedB = loaded.entity(bId);
        !loadedB || loadedB->parent != secondFolder)
    {
        qCritical() << "scene-test: entity parent roundtrip";
        ++failures;
    }
    if (loaded.keyframeNote(aId, QStringLiteral("x"), 120.0)
        != QStringLiteral("收尾"))
    {
        qCritical() << "scene-test: keyframe note roundtrip";
        ++failures;
    }

    // ---- v3 migration: one default scene, identity folder --------------
    pvf::Project v3;
    v3.fps = 30;
    pvf::Entity* rootA =
        v3.addVector(0, 100, 80, QColor(200, 100, 100), Qt::white, 0);
    const pvf::EntityId rootAId = rootA->id;
    pvf::Entity* group = v3.addFolder(QStringLiteral("Group"));
    const pvf::EntityId groupId = group->id;
    pvf::Entity* nested =
        v3.addVector(0, 100, 80, QColor(100, 200, 100), Qt::white, 0);
    const pvf::EntityId nestedId = nested->id;
    pvf::Entity* v3Camera = v3.addCamera();
    const pvf::EntityId v3CameraId = v3Camera->id;
    pvf::Entity* rootB =
        v3.addVector(0, 100, 80, QColor(100, 100, 200), Qt::white, 0);
    const pvf::EntityId rootBId = rootB->id;
    v3.setEntityParent(nestedId, groupId);
    v3.setValue(rootAId, QStringLiteral("x"), 0, 0.0, true);
    v3.setValue(rootAId, QStringLiteral("x"), 45, 200.0, true);
    v3.setValue(rootAId, QStringLiteral("x"), 90, 600.0, true);
    v3.setValue(v3CameraId, QStringLiteral("x"), 0, 400.0, false);

    const int beforeLength = v3.timelineLength();
    if (beforeLength != 90)
    {
        qCritical() << "scene-test: migration setup length" << beforeLength;
        return 1;
    }
    const int frames[] = {0, 45, 90};
    const QVector<pvf::EntityId> migrationIds = {rootAId, groupId, nestedId,
                                                 v3CameraId, rootBId};
    QVector<QTransform> beforeTransforms;
    QVector<pvf::Camera::State> beforeCameras;
    for (int frame : frames)
    {
        for (pvf::EntityId id : migrationIds)
        {
            const pvf::Entity* entity = v3.entity(id);
            if (!entity)
            {
                qCritical() << "scene-test: migration entity missing" << id;
                return 1;
            }
            beforeTransforms.append(
                pvf::SceneRenderer::worldTransform(v3, *entity, frame));
        }
        beforeCameras.append(pvf::Camera::state(v3, v3CameraId, frame));
    }
    const QVector<pvf::EntityId> beforeOrder = drawableOrder(v3);

    error.clear();
    if (!v3.save(basePath, &error))
    {
        qCritical() << "scene-test: save migration source" << error;
        return 1;
    }
    if (!rewriteProjectVersion(basePath, v3Path, 3, true))
    {
        qCritical() << "scene-test: rewrite v3 json";
        return 1;
    }

    pvf::Project migrated;
    error.clear();
    if (!migrated.load(v3Path, &error))
    {
        qCritical() << "scene-test: load v3" << error;
        return 1;
    }
    if (migrated.scenes().size() != 1)
    {
        qCritical() << "scene-test: v3 default scene count"
                    << migrated.scenes().size();
        return 1;
    }
    const pvf::Scene& defaultScene = migrated.scenes().first();
    const pvf::EntityId sceneFolder = defaultScene.folderId;
    if (defaultScene.startFrame != 0 || defaultScene.endFrame != beforeLength
        || !isIdentityFolder(migrated, sceneFolder))
    {
        qCritical() << "scene-test: v3 default scene range/folder";
        ++failures;
    }
    if (migrated.timelineLength() != beforeLength
        || migrated.entities().size() != v3.entities().size() + 1)
    {
        qCritical() << "scene-test: v3 migration changed content"
                    << migrated.timelineLength() << migrated.entities().size();
        ++failures;
    }
    for (pvf::EntityId id : {rootAId, groupId, v3CameraId, rootBId})
    {
        const pvf::Entity* entity = migrated.entity(id);
        if (!entity || entity->parent != sceneFolder)
        {
            qCritical() << "scene-test: root not moved into default scene" << id;
            ++failures;
        }
    }
    if (const pvf::Entity* entity = migrated.entity(nestedId);
        !entity || entity->parent != groupId)
    {
        qCritical() << "scene-test: nested entity reparented";
        ++failures;
    }
    if (migrated.sceneFolderAt(45) != sceneFolder
        || migrated.sceneAt(90) != migrated.sceneById(defaultScene.id))
    {
        qCritical() << "scene-test: migrated scene lookup";
        ++failures;
    }

    int pose = 0;
    for (int frame : frames)
    {
        for (pvf::EntityId id : migrationIds)
        {
            const pvf::Entity* entity = migrated.entity(id);
            if (!entity
                || !transformNear(beforeTransforms[pose],
                                  pvf::SceneRenderer::worldTransform(migrated,
                                                                     *entity, frame)))
            {
                qCritical() << "scene-test: transform changed" << id << frame;
                ++failures;
            }
            ++pose;
        }
        const pvf::Camera::State state = pvf::Camera::state(migrated, v3CameraId, frame);
        const pvf::Camera::State before = beforeCameras[frame == 0 ? 0
                                                       : frame == 45    ? 1
                                                                        : 2];
        if ((state.center - before.center).manhattanLength() > 1e-9
            || std::abs(state.rotation - before.rotation) > 1e-9
            || std::abs(state.scale - before.scale) > 1e-9)
        {
            qCritical() << "scene-test: camera changed" << frame;
            ++failures;
        }
    }
    if (drawableOrder(migrated) != beforeOrder)
    {
        qCritical() << "scene-test: draw order changed";
        ++failures;
    }

    // v1/v2 files are accepted and migrated the same way.
    for (const auto& pair : {std::pair<int, QString>{1, v1Path},
                             std::pair<int, QString>{2, v2Path}})
    {
        if (!rewriteProjectVersion(basePath, pair.second, pair.first, true))
        {
            qCritical() << "scene-test: rewrite version" << pair.first;
            ++failures;
            continue;
        }
        pvf::Project old;
        error.clear();
        if (!old.load(pair.second, &error) || old.scenes().size() != 1
            || !isIdentityFolder(old, old.scenes().first().folderId))
        {
            qCritical() << "scene-test: load version" << pair.first << error;
            ++failures;
        }
    }

    // Future versions are rejected with a clear error.
    if (!rewriteProjectVersion(basePath, tooNewPath, 5, false))
    {
        qCritical() << "scene-test: rewrite v5";
        ++failures;
    }
    else
    {
        pvf::Project future;
        error.clear();
        if (future.load(tooNewPath, &error) || !error.contains(QStringLiteral("过新")))
        {
            qCritical() << "scene-test: version 5 not rejected" << error;
            ++failures;
        }
    }

    // Empty v3 project: a single frame 0..0 scene.
    pvf::Project empty;
    error.clear();
    if (!empty.save(emptyPath, &error)
        || !rewriteProjectVersion(emptyPath, emptyV3Path, 3, true))
    {
        qCritical() << "scene-test: empty migration setup" << error;
        ++failures;
    }
    else
    {
        pvf::Project emptyLoaded;
        error.clear();
        if (!emptyLoaded.load(emptyV3Path, &error)
            || emptyLoaded.scenes().size() != 1
            || emptyLoaded.scenes().first().startFrame != 0
            || emptyLoaded.scenes().first().endFrame != 0
            || emptyLoaded.timelineLength() != 0
            || emptyLoaded.entities().size() != 1
            || !isIdentityFolder(emptyLoaded,
                                 emptyLoaded.scenes().first().folderId))
        {
            qCritical() << "scene-test: empty v3 migration" << error;
            ++failures;
        }
    }

    for (const QString& path : {basePath, v3Path, v1Path, v2Path, tooNewPath,
                                emptyPath, emptyV3Path})
    {
        QFile::remove(path);
    }
    qInfo() << "scene-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int annotationTest()
{
    int failures = 0;
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_annotation.pvproj");
    const QString v3Path =
        QDir::tempPath() + QStringLiteral("/pvforge_annotation_v3.pvproj");
    QString error;

    pvf::Project project;
    project.fps = 30;
    pvf::Entity* entity =
        project.addVector(0, 100, 80, QColor(220, 120, 90), Qt::white, 0);
    const pvf::EntityId entityId = entity->id;
    project.setValue(entityId, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(entityId, QStringLiteral("x"), 30, 300.0, true);
    const int length = project.timelineLength();
    if (length != 30)
    {
        qCritical() << "annotation-test: setup length" << length;
        return 1;
    }

    pvf::Annotation* late =
        project.addAnnotation(30, QStringLiteral("副歌"), QStringLiteral("**Markdown**"),
                              QStringList{QStringLiteral("副歌"),
                                          QStringLiteral("镜头")},
                              QColor(255, 60, 60), &error);
    const QString lateId = late ? late->id : QString();
    pvf::Annotation* early =
        project.addAnnotation(10, QStringLiteral("开场"), QStringLiteral("first"),
                              QStringList{QStringLiteral("字幕")},
                              QColor(60, 220, 60), &error);
    const QString earlyId = early ? early->id : QString();
    pvf::Annotation* middle =
        project.addAnnotation(20, QStringLiteral("过渡"), QString(),
                              QStringList(), QColor(60, 60, 255), &error);
    const QString middleId = middle ? middle->id : QString();
    if (!late || !early || !middle)
    {
        qCritical() << "annotation-test: add" << error;
        return 1;
    }
    if (project.annotations().size() != 3
        || project.annotations()[0].id != earlyId
        || project.annotations()[1].id != middleId
        || project.annotations()[2].id != lateId)
    {
        qCritical() << "annotation-test: annotations are not sorted";
        ++failures;
    }
    const pvf::Annotation* earlyLoaded = project.annotationById(earlyId);
    if (!earlyLoaded || earlyLoaded->title != QStringLiteral("开场")
        || earlyLoaded->note != QStringLiteral("first")
        || earlyLoaded->tags != QStringList{QStringLiteral("字幕")}
        || earlyLoaded->color.name(QColor::HexArgb)
               != QColor(60, 220, 60).name(QColor::HexArgb))
    {
        qCritical() << "annotation-test: annotation fields";
        ++failures;
    }

    pvf::Annotation* far =
        project.addAnnotation(1000, QStringLiteral("远处"), QString(),
                              QStringList{QStringLiteral("场景")},
                              QColor(200, 200, 0), &error);
    if (!far)
    {
        qCritical() << "annotation-test: add far" << error;
        return 1;
    }
    const QString farId = far->id;
    if (project.timelineLength() != length)
    {
        qCritical() << "annotation-test: annotation extended timeline"
                    << project.timelineLength();
        ++failures;
    }
    if (project.annotations().size() != 4
        || project.annotations().last().id != farId)
    {
        qCritical() << "annotation-test: far annotation not last";
        ++failures;
    }

    pvf::Annotation changed = *project.annotationById(middleId);
    changed.frame = 5;
    changed.title = QStringLiteral("修改");
    changed.note = QStringLiteral("**changed**");
    changed.tags = QStringList{QStringLiteral("场景")};
    changed.color = QColor(1, 2, 3);
    if (!project.updateAnnotation(changed, &error))
    {
        qCritical() << "annotation-test: update" << error;
        ++failures;
    }
    if (project.annotations().size() != 4
        || project.annotations()[0].id != middleId
        || project.annotations()[0].frame != 5
        || project.annotations()[1].id != earlyId
        || project.annotations()[2].id != lateId)
    {
        qCritical() << "annotation-test: update did not re-sort";
        ++failures;
    }
    const pvf::Annotation* middleLoaded = project.annotationById(middleId);
    if (!middleLoaded || middleLoaded->title != QStringLiteral("修改")
        || middleLoaded->note != QStringLiteral("**changed**")
        || middleLoaded->tags != QStringList{QStringLiteral("场景")}
        || middleLoaded->color.name(QColor::HexArgb)
               != QColor(1, 2, 3).name(QColor::HexArgb))
    {
        qCritical() << "annotation-test: updated fields";
        ++failures;
    }

    changed.frame = -1;
    error.clear();
    if (project.updateAnnotation(changed, &error) || error.isEmpty())
    {
        qCritical() << "annotation-test: negative update accepted";
        ++failures;
    }
    if (project.annotationById(middleId)->frame != 5)
    {
        qCritical() << "annotation-test: failed update changed data";
        ++failures;
    }
    error.clear();
    if (project.addAnnotation(-1, QStringLiteral("bad"), QString(), QStringList(),
                              QColor(), &error)
        || error.isEmpty())
    {
        qCritical() << "annotation-test: negative add accepted";
        ++failures;
    }

    if (!project.removeAnnotation(earlyId) || project.annotationById(earlyId)
        || project.removeAnnotation(earlyId))
    {
        qCritical() << "annotation-test: remove";
        ++failures;
    }
    if (project.annotations().size() != 3)
    {
        qCritical() << "annotation-test: remove count"
                    << project.annotations().size();
        ++failures;
    }

    // ---- Keyframe notes ------------------------------------------------
    if (!project.setKeyframeNote(entityId, QStringLiteral("x"), 30.0,
                                 QStringLiteral("收尾"))
        || project.keyframeNote(entityId, QStringLiteral("x"), 30.0)
               != QStringLiteral("收尾"))
    {
        qCritical() << "annotation-test: set keyframe note";
        ++failures;
    }
    if (project.setKeyframeNote(entityId, QStringLiteral("x"), 99.0,
                                QStringLiteral("missing")))
    {
        qCritical() << "annotation-test: missing keyframe note accepted";
        ++failures;
    }
    // Updating a value at an existing keyframe keeps its note.
    project.setValue(entityId, QStringLiteral("x"), 30, 200.0, true);
    if (project.keyframeNote(entityId, QStringLiteral("x"), 30.0)
        != QStringLiteral("收尾"))
    {
        qCritical() << "annotation-test: value update cleared note";
        ++failures;
    }

    error.clear();
    if (!project.save(projectPath, &error))
    {
        qCritical() << "annotation-test: save" << error;
        return 1;
    }
    const QJsonObject root = readJsonObject(projectPath);
    if (root.value(QStringLiteral("version")).toInt() != pvf::kProjectVersion)
    {
        qCritical() << "annotation-test: version";
        ++failures;
    }
    const QJsonArray savedAnnotations =
        root.value(QStringLiteral("annotations")).toArray();
    if (savedAnnotations.size() != 3
        || savedAnnotations[0].toObject().value(QStringLiteral("id")).toString()
               != middleId
        || savedAnnotations[1].toObject().value(QStringLiteral("id")).toString()
               != lateId
        || savedAnnotations[2].toObject().value(QStringLiteral("id")).toString()
               != farId)
    {
        qCritical() << "annotation-test: annotation json order";
        ++failures;
    }
    const QJsonObject savedMiddle = savedAnnotations[0].toObject();
    if (savedMiddle.value(QStringLiteral("title")).toString()
            != QStringLiteral("修改")
        || savedMiddle.value(QStringLiteral("tags")).toArray().size() != 1
        || savedMiddle.value(QStringLiteral("tags")).toArray()[0].toString()
               != QStringLiteral("场景"))
    {
        qCritical() << "annotation-test: annotation json fields";
        ++failures;
    }
    bool noteAt30 = false;
    bool noteAt0 = true;
    for (const QJsonValue& value : root.value(QStringLiteral("entities")).toArray())
    {
        const QJsonObject savedEntity = value.toObject();
        if (savedEntity.value(QStringLiteral("id")).toInt() != entityId)
            continue;
        for (const QJsonValue& keyValue :
             savedEntity.value(QStringLiteral("tracks")).toObject()
                 .value(QStringLiteral("x")).toArray())
        {
            const QJsonObject key = keyValue.toObject();
            const double frame = key.value(QStringLiteral("f")).toDouble();
            if (std::abs(frame - 30.0) < 1e-6)
                noteAt30 = key.value(QStringLiteral("note")).toString()
                           == QStringLiteral("收尾");
            if (std::abs(frame) < 1e-6)
                noteAt0 = !key.contains(QStringLiteral("note"));
        }
    }
    if (!noteAt30 || !noteAt0)
    {
        qCritical() << "annotation-test: keyframe note json" << noteAt30
                    << noteAt0;
        ++failures;
    }

    pvf::Project loaded;
    error.clear();
    if (!loaded.load(projectPath, &error))
    {
        qCritical() << "annotation-test: load" << error;
        return 1;
    }
    if (loaded.annotations().size() != 3
        || loaded.annotations()[0].id != middleId
        || loaded.annotations()[1].id != lateId
        || loaded.annotations()[2].id != farId
        || loaded.annotations()[2].frame != 1000
        || loaded.timelineLength() != length)
    {
        qCritical() << "annotation-test: annotation roundtrip";
        ++failures;
    }
    if (const pvf::Annotation* loadedMiddle = loaded.annotationById(middleId);
        !loadedMiddle || loadedMiddle->frame != 5
        || loadedMiddle->tags != QStringList{QStringLiteral("场景")}
        || loadedMiddle->color.name(QColor::HexArgb)
               != QColor(1, 2, 3).name(QColor::HexArgb))
    {
        qCritical() << "annotation-test: annotation roundtrip fields";
        ++failures;
    }
    if (loaded.keyframeNote(entityId, QStringLiteral("x"), 30.0)
            != QStringLiteral("收尾")
        || !loaded.keyframeNote(entityId, QStringLiteral("x"), 0.0).isEmpty())
    {
        qCritical() << "annotation-test: keyframe note roundtrip";
        ++failures;
    }

    // v3 files get empty annotations.
    if (!rewriteProjectVersion(projectPath, v3Path, 3, true))
    {
        qCritical() << "annotation-test: rewrite v3";
        ++failures;
    }
    else
    {
        pvf::Project old;
        error.clear();
        if (!old.load(v3Path, &error) || !old.annotations().isEmpty()
            || old.scenes().size() != 1)
        {
            qCritical() << "annotation-test: v3 annotations" << error;
            ++failures;
        }
    }

    QFile::remove(projectPath);
    QFile::remove(v3Path);
    qInfo() << "annotation-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int sceneUiTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* content =
        project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId contentId = content->id;
    project->setValue(contentId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(contentId, QStringLiteral("x"), 120, 600.0, true);

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, contentId] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "scene-ui-test:" << text;
            ++failures;
        };

        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!timeline || !undo || !redo)
        {
            qCritical() << "scene-ui-test: timeline or undo actions missing";
            app.exit(1);
            return;
        }
        timeline->setPixelsPerFrameForTest(6.0);

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton
                                                               : Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        const QString firstId = window.addSceneForTest(
            QStringLiteral("开场"), 10, 40, QColor(200, 80, 80),
            QStringLiteral("第一段"));
        if (firstId.isEmpty() || project->scenes().size() != 1)
        {
            fail("scene creation through the UI path failed");
            qInfo() << "scene-ui-test failures" << failures;
            app.exit(1);
            return;
        }
        const pvf::Scene* scene = project->sceneById(firstId);
        const pvf::EntityId sceneFolder =
            scene ? scene->folderId : pvf::kInvalidEntity;
        if (!scene || !project->entity(sceneFolder))
            fail("scene folder missing");

        const QRect band = timeline->sceneBandRectForTest(firstId);
        if (!band.isValid() || band.width() <= 0)
            fail("scene band is not visible");

        // Clicking the band selects it and jumps the playhead to its start.
        const QPoint bandPoint(band.center().x(), timeline->sceneBandCenterYForTest());
        sendMouse(QEvent::MouseButtonPress, bandPoint);
        sendMouse(QEvent::MouseButtonRelease, bandPoint);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 10)
            fail("clicking the scene band did not jump to the start frame");
        if (timeline->selectedSceneForTest() != firstId)
            fail("clicking the scene band did not select the scene");
        if (!timeline->sceneActiveForTest(firstId))
            fail("the playhead scene is not marked active in the band");

        // A new folder with no explicit parent lands in the active scene.
        window.goToFrame(25);
        const pvf::EntityId created = window.createFolderForTest();
        const pvf::Entity* createdFolder = project->entity(created);
        if (!createdFolder || createdFolder->parent != sceneFolder)
            fail("new entity did not land in the active scene folder");

        const int beforeUndo = int(project->entities().size());
        undo->trigger();
        QApplication::processEvents();
        if (project->entity(created) || int(project->entities().size()) != beforeUndo - 1)
            fail("undo did not remove the auto-parented entity");
        redo->trigger();
        QApplication::processEvents();
        createdFolder = project->entity(created);
        if (!createdFolder || createdFolder->parent != sceneFolder)
            fail("redo did not restore the auto-parented entity");

        // Range/name edits and their undo/redo.
        QString error;
        if (!window.updateSceneForTest(firstId, QStringLiteral("开场改"), 5, 45,
                                       QColor(10, 20, 30), QStringLiteral("改过"),
                                       &error))
            fail("scene update failed");
        const pvf::Scene* updated = project->sceneById(firstId);
        if (!updated || updated->name != QStringLiteral("开场改")
            || updated->startFrame != 5 || updated->endFrame != 45
            || updated->note != QStringLiteral("改过"))
            fail("scene update fields are wrong");
        if (const pvf::Entity* renamed = project->entity(sceneFolder);
            !renamed || renamed->name != QStringLiteral("开场改"))
            fail("scene folder did not follow the scene name");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Scene* restored = project->sceneById(firstId);
        if (!restored || restored->name != QStringLiteral("开场")
            || restored->startFrame != 10 || restored->endFrame != 40)
            fail("undo did not restore the scene range/name");
        if (const pvf::Entity* renamed = project->entity(sceneFolder);
            !renamed || renamed->name != QStringLiteral("开场"))
            fail("undo did not restore the scene folder name");
        redo->trigger();
        QApplication::processEvents();
        updated = project->sceneById(firstId);
        if (!updated || updated->startFrame != 5 || updated->endFrame != 45)
            fail("redo did not reapply the scene edit");
        if (const pvf::Entity* renamed = project->entity(sceneFolder);
            !renamed || renamed->name != QStringLiteral("开场改"))
            fail("redo did not rename the scene folder again");

        const QString secondId = window.addSceneForTest(
            QStringLiteral("后段"), 70, 100, QColor(80, 200, 120),
            QStringLiteral("第二段"));
        if (secondId.isEmpty())
            fail("second scene creation failed");
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_scene_ui.png"));
        error.clear();
        if (window.updateSceneForTest(firstId, QStringLiteral("重叠"), 0, 100,
                                      QColor(), QString(), &error)
            || error.isEmpty())
            fail("overlapping scene range was accepted");

        // Retirement moves the folder under _retired, fades and locks it.
        if (!window.setSceneRetiredForTest(firstId, true))
            fail("retiring the scene failed");
        const pvf::Scene* retiredScene = project->sceneById(firstId);
        pvf::EntityId retiredRoot = pvf::kInvalidEntity;
        for (const pvf::Entity& entity : project->entities())
        {
            if (entity.parent == pvf::kInvalidEntity && entity.isFolder()
                && entity.name == QStringLiteral("_retired"))
                retiredRoot = entity.id;
        }
        const pvf::Entity* retiredFolder = project->entity(sceneFolder);
        if (!retiredScene || retiredRoot == pvf::kInvalidEntity || !retiredFolder
            || retiredFolder->parent != retiredRoot)
            fail("retired scene folder is not under _retired");
        if (!retiredFolder || !retiredFolder->locked
            || project->value(sceneFolder, QStringLiteral("opacity"), 0) > 1e-9)
            fail("retired folder is not transparent and locked");
        if (!pvf::sceneIsRetired(*project, *retiredScene))
            fail("scene record is not marked retired");

        undo->trigger();
        QApplication::processEvents();
        const pvf::Entity* restoredRetire = project->entity(sceneFolder);
        if (!restoredRetire || restoredRetire->parent != pvf::kInvalidEntity
            || restoredRetire->locked
            || project->value(sceneFolder, QStringLiteral("opacity"), 0) < 0.999)
            fail("undo did not restore the retired folder");
        const pvf::Scene* afterUndoRetire = project->sceneById(firstId);
        if (!afterUndoRetire || pvf::sceneIsRetired(*project, *afterUndoRetire))
            fail("undo did not clear the retired mark");
        redo->trigger();
        QApplication::processEvents();

        // Human restore is independent from undo.
        if (!window.setSceneRetiredForTest(firstId, false))
            fail("restoring the scene failed");
        restoredRetire = project->entity(sceneFolder);
        if (!restoredRetire || restoredRetire->parent != pvf::kInvalidEntity
            || restoredRetire->locked
            || std::abs(project->value(sceneFolder, QStringLiteral("opacity"), 0)
                        - 1.0) > 1e-6)
            fail("restore did not move the folder back and reset opacity/lock");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Scene* undoRestore = project->sceneById(firstId);
        if (!undoRestore || !pvf::sceneIsRetired(*project, *undoRestore))
            fail("undo did not return the scene to retired");
        redo->trigger();
        QApplication::processEvents();

        // Removing a scene record keeps the folder; undo rebinds the same id.
        if (!window.removeSceneForTest(firstId))
            fail("removing the scene failed");
        if (project->sceneById(firstId) || !project->entity(sceneFolder))
            fail("scene removal deleted the folder");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Scene* reAdded = project->sceneById(firstId);
        if (!reAdded || reAdded->folderId != sceneFolder
            || !project->entity(sceneFolder))
            fail("undo did not restore the scene record on the same folder");
        redo->trigger();
        QApplication::processEvents();
        if (project->sceneById(firstId))
            fail("redo did not remove the scene record again");

        qInfo() << "scene-ui-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int annotationUiTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity = project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    project->setValue(id, QStringLiteral("x"), 20, 200.0, true);
    project->setValue(id, QStringLiteral("y"), 10, 0.0, true);
    project->setValue(id, QStringLiteral("y"), 20, 50.0, true);
    const int lengthBefore = project->timelineLength();

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id, lengthBefore] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "annotation-ui-test:" << text;
            ++failures;
        };

        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!timeline || !undo || !redo)
        {
            qCritical() << "annotation-ui-test: timeline or undo actions missing";
            app.exit(1);
            return;
        }
        timeline->setPixelsPerFrameForTest(6.0);

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton
                                                               : Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        const QString later = window.addAnnotationForTest(
            30, QStringLiteral("副歌"), QStringLiteral("**副歌** 开始"),
            {QStringLiteral("副歌")}, QColor(255, 120, 60));
        const QString earlier = window.addAnnotationForTest(
            10, QStringLiteral("开场"), QStringLiteral("第一帧"),
            {QStringLiteral("镜头")}, QColor(90, 200, 255));
        if (later.isEmpty() || earlier.isEmpty() || project->annotations().size() != 2)
        {
            fail("annotation creation through the UI path failed");
            qInfo() << "annotation-ui-test failures" << failures;
            app.exit(1);
            return;
        }
        if (project->annotations()[0].id != earlier
            || project->annotations()[1].id != later)
            fail("annotations are not sorted by frame");
        if (project->timelineLength() != lengthBefore)
            fail("annotations changed the timeline length");

        const QRect marker = timeline->annotationRectForTest(later);
        if (!marker.isValid())
            fail("annotation marker is not visible");
        const QPoint markerPoint(marker.center().x(),
                                 timeline->annotationLaneCenterYForTest());
        sendMouse(QEvent::MouseButtonPress, markerPoint);
        sendMouse(QEvent::MouseButtonRelease, markerPoint);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 30)
            fail("clicking an annotation marker did not jump to its frame");

        // Editing moves the marker and re-sorts the list.
        QString error;
        if (!window.updateAnnotationForTest(earlier, 5, QStringLiteral("开场改"),
                                            QStringLiteral("改过的备注"),
                                            {QStringLiteral("镜头"),
                                             QStringLiteral("字幕")},
                                            QColor(20, 30, 40), &error))
            fail("annotation update failed");
        const pvf::Annotation* updated = project->annotationById(earlier);
        if (!updated || updated->frame != 5
            || updated->title != QStringLiteral("开场改")
            || updated->note != QStringLiteral("改过的备注")
            || updated->tags.size() != 2)
            fail("annotation update fields are wrong");
        if (project->annotations().first().id != earlier)
            fail("annotation update did not re-sort");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Annotation* restored = project->annotationById(earlier);
        if (!restored || restored->frame != 10
            || restored->title != QStringLiteral("开场"))
            fail("undo did not restore the annotation");
        redo->trigger();
        QApplication::processEvents();
        updated = project->annotationById(earlier);
        if (!updated || updated->frame != 5)
            fail("redo did not reapply the annotation edit");

        if (!window.removeAnnotationForTest(earlier))
            fail("annotation removal failed");
        if (project->annotationById(earlier))
            fail("annotation was not removed");
        undo->trigger();
        QApplication::processEvents();
        if (!project->annotationById(earlier))
            fail("undo did not restore the annotation id");
        redo->trigger();
        QApplication::processEvents();
        if (project->annotationById(earlier))
            fail("redo did not remove the annotation again");

        // Keyframe notes cover every property at the frame and undo fully.
        if (!window.setKeyframeNoteForTest(id, 10, QStringLiteral("第一帧")))
            fail("setting the keyframe note failed");
        if (project->keyframeNote(id, QStringLiteral("x"), 10)
                != QStringLiteral("第一帧")
            || project->keyframeNote(id, QStringLiteral("y"), 10)
                   != QStringLiteral("第一帧"))
            fail("keyframe note was not applied to every property");
        if (!timeline->keyframeHasNoteForTest(id, 10))
            fail("keyframe note dot is not exposed");
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_annotation_ui.png"));
        undo->trigger();
        QApplication::processEvents();
        if (!project->keyframeNote(id, QStringLiteral("x"), 10).isEmpty()
            || !project->keyframeNote(id, QStringLiteral("y"), 10).isEmpty())
            fail("undo did not clear the keyframe note");
        redo->trigger();
        QApplication::processEvents();
        if (project->keyframeNote(id, QStringLiteral("x"), 10)
            != QStringLiteral("第一帧"))
            fail("redo did not restore the keyframe note");

        if (!window.setKeyframeNoteForTest(id, 10, QString()))
            fail("clearing the keyframe note failed");
        if (!project->keyframeNote(id, QStringLiteral("x"), 10).isEmpty())
            fail("keyframe note was not cleared");
        undo->trigger();
        QApplication::processEvents();
        if (project->keyframeNote(id, QStringLiteral("x"), 10)
            != QStringLiteral("第一帧"))
            fail("undo did not restore the cleared note");
        redo->trigger();
        QApplication::processEvents();
        if (!project->keyframeNote(id, QStringLiteral("x"), 10).isEmpty())
            fail("redo did not clear the note again");

        qInfo() << "annotation-ui-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}


} // namespace

namespace pvf::tests {

void registerSceneTests(Registry& registry)
{
    registry.add(QStringLiteral("--scene-test"),
                 [](QApplication&) { return sceneTest(); });
    registry.add(QStringLiteral("--annotation-test"),
                 [](QApplication&) { return annotationTest(); });
    registry.add(QStringLiteral("--scene-ui-test"),
                 [](QApplication& app) { return sceneUiTest(app); });
    registry.add(QStringLiteral("--annotation-ui-test"),
                 [](QApplication& app) { return annotationUiTest(app); });
}

} // namespace pvf::tests
