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
#include "render/CameraRenderer.h"
#include "render/SceneRenderer.h"
#include "render/VideoExporter.h"
#include "audio/AudioEngine.h"
#include "core/Settings.h"
#include "app/MainWindow.h"
#include "app/NewProjectDialog.h"
#include "app/ExportDialog.h"
#include "app/ExportProgressDialog.h"
#include "app/SettingsDialog.h"
#include "app/SystemInfo.h"
#include "app/WelcomeWindow.h"
#include "html/HtmlCaptureTest.h"
#include "html/HtmlLayerTest.h"
#include "core/Project.h"
#include "core/CheckpointManager.h"
#include "core/ComicBox.h"
#include "core/PerfMonitor.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "ui/BrandWallDialog.h"
#include "ui/PropertiesPanel.h"
#include "ui/TextLayoutDialog.h"
#include "ui/WorkbenchPanel.h"
#include "media/VideoDecoder.h"
#include "mcp/McpServer.h"
#include "mcp/StdioBridgeTest.h"
#include "mcp/McpCommandService.h"
#include "ui/BezierEditorDialog.h"
#include "ui/CameraMonitor.h"
#include "ui/EasingPicker.h"
#include "ui/CollapsibleSection.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CurveEditor.h"
#include "ui/McpLogPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WaveformView.h"
#include "ui/WorkbenchPanel.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QClipboard>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QCheckBox>
#include <QDataStream>
#include <QDoubleSpinBox>
#include <QFont>
#include <QHostAddress>
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
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QWidgetAction>
#include <QWheelEvent>
#include <cmath>
#include <functional>

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

int autoSaveTest()
{
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_autosave_test.pvproj");
    const QString imagePath =
        QDir::tempPath() + QStringLiteral("/pvforge_autosave_test.png");
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    if (!img.save(imagePath))
        return 1;

    auto* project = new pvf::Project;
    project->name = QStringLiteral("自动保存测试");
    project->addImage(imagePath);
    QString error;
    if (!project->save(projectPath, &error))
    {
        qCritical() << "autosave-test: initial save failed" << error;
        return 1;
    }

    int failures = 0;
    if (!pvf::Settings::autosaveEnabled()
        || pvf::Settings::autosaveMinutes() != 5)
    {
        qCritical() << "autosave-test: defaults are not enabled / 5 minutes"
                    << pvf::Settings::autosaveEnabled()
                    << pvf::Settings::autosaveMinutes();
        ++failures;
    }

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();

    if (!window.autosaveActiveForTest()
        || window.autosaveIntervalForTest() != 5 * 60 * 1000)
    {
        qCritical() << "autosave-test: timer not running at five minutes"
                    << window.autosaveActiveForTest()
                    << window.autosaveIntervalForTest();
        ++failures;
    }

    const int before = project->entities().size();
    project->addText(QStringLiteral("自动保存"), QFont().family(), 64, Qt::white,
                     int(Qt::AlignCenter));
    if (!project->modified)
    {
        qCritical() << "autosave-test: edit did not mark the project modified";
        ++failures;
    }
    window.autoSaveNowForTest();
    if (project->modified)
    {
        qCritical() << "autosave-test: autosave did not clear the modified flag";
        ++failures;
    }

    pvf::Project loaded;
    if (!loaded.load(projectPath, &error))
    {
        qCritical() << "autosave-test: reload failed" << error;
        ++failures;
    }
    else if (loaded.entities().size() != before + 1)
    {
        qCritical() << "autosave-test: autosaved entity missing"
                    << loaded.entities().size() << before + 1;
        ++failures;
    }

    pvf::Settings::setAutosaveEnabled(false);
    window.reloadAutosaveSettingsForTest();
    if (window.autosaveActiveForTest())
    {
        qCritical() << "autosave-test: timer still runs when disabled";
        ++failures;
    }
    project->addText(QStringLiteral("不要保存"), QFont().family(), 64,
                     Qt::white, int(Qt::AlignCenter));
    window.autoSaveNowForTest();
    if (!project->modified)
    {
        qCritical() << "autosave-test: disabled autosave still wrote the file";
        ++failures;
    }

    pvf::Settings::setAutosaveEnabled(true);
    pvf::Settings::setAutosaveMinutes(2);
    window.reloadAutosaveSettingsForTest();
    if (!window.autosaveActiveForTest()
        || window.autosaveIntervalForTest() != 2 * 60 * 1000)
    {
        qCritical() << "autosave-test: interval setting not applied"
                    << window.autosaveActiveForTest()
                    << window.autosaveIntervalForTest();
        ++failures;
    }

    QFile::remove(projectPath);
    QFile::remove(imagePath);
    qInfo() << "autosave-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

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

int specialTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->width = 800;
    project->height = 600;
    project->fps = 30;

    int failures = 0;

    const auto render = [&](int frame) {
        QImage image(400, 300, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        QTransform worldToScreen;
        worldToScreen.scale(0.5, 0.5);
        worldToScreen.translate(200, 150);
        pvf::SceneRenderer::drawEntities(painter, *project, frame, worldToScreen, false);
        return image;
    };
    const auto ink = [](const QImage& image) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qGray(image.pixel(x, y)) > 40)
                    ++count;
        return count;
    };

    // Comic balloons in all four shapes, each rendered on its own.
    for (int shape = 0; shape < 4; ++shape)
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("台词\n第二行");
        style.pixelSize = 48;
        style.padding = 14;
        style.color = QColor(30, 30, 36);
        QVariantMap data;
        style.writeTo(data);
        pvf::BubbleStyle bubble;
        bubble.shape = shape;
        bubble.tail = shape == 0 || shape == 2;
        bubble.writeTo(data);

        pvf::Entity* comic = project->addEntity(pvf::EntityType::ComicText,
                                                QStringLiteral("漫画文本框"), data);
        if (!comic)
        {
            qCritical() << "special-test: comic creation failed";
            ++failures;
            continue;
        }
        comic->values[QStringLiteral("x")] = -200.0 + shape * 200.0;
        // Render this balloon alone.
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = other.id != comic->id;
        const QRectF bounds = pvf::SceneRenderer::entityLocalBounds(*comic);
        if (bounds.width() < 10 || bounds.height() < 10)
        {
            qCritical() << "special-test: comic bounds too small" << bounds;
            ++failures;
        }
        QImage shot(400, 300, QImage::Format_ARGB32);
        shot.fill(Qt::black);
        {
            QPainter painter(&shot);
            QTransform worldToScreen;
            worldToScreen.scale(0.5, 0.5);
            worldToScreen.translate(400, 300);
            pvf::SceneRenderer::drawEntities(painter, *project, 0, worldToScreen, false);
        }
        shot.save(QStringLiteral("/tmp/pvforge_comic_%1.png").arg(shape));
        if (ink(shot) <= 0)
        {
            qCritical() << "special-test: comic shape" << shape << " drew nothing";
            ++failures;
        }
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = false;
    }

    // Vertical tategaki inside an ellipse: the whole text block has to fit,
    // not just its centre line.
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("我看旁边的小日子\n过得不错的\n霓虹酱");
        style.pixelSize = 48;
        style.color = QColor(30, 30, 36);
        style.padding = 18;
        style.vertical = true;
        pvf::BubbleStyle bubble;
        bubble.shape = 0;
        bubble.tail = true;

        const pvf::ComicBox box = pvf::ComicBox::build(style, bubble);
        const pvf::TextBlock block = pvf::TextBlock::measure(style);
        if (box.bodySize().width() < block.contentSize().width() * 1.35
            || box.bodySize().height() < block.contentSize().height() * 1.35)
        {
            qCritical() << "special-test: ellipse too tight for vertical text"
                        << box.bodySize() << block.contentSize();
            ++failures;
        }

        pvf::TextStyle spacedStyle = style;
        spacedStyle.letterSpacing = 12;
        if (pvf::TextBlock::measure(spacedStyle).contentSize().height()
            <= block.contentSize().height() + 1.0)
        {
            qCritical() << "special-test: vertical letter spacing did not expand";
            ++failures;
        }

        QImage image(420, 800, QImage::Format_ARGB32);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.translate(image.width() / 2.0, image.height() / 2.0);
            box.paint(painter);
        }
        image.save(QStringLiteral("/tmp/pvf_comic_vertical.png"));
        if (ink(image) <= 0)
        {
            qCritical() << "special-test: vertical balloon drew nothing";
            ++failures;
        }
    }

    // Balloon bounds must include half the stroke, or the rasterised image
    // clips the outline at its top/bottom extremes.
    {
        QVariantMap data;
        data.insert(QStringLiteral("text"), QStringLiteral("真是个变态啊"));
        data.insert(QStringLiteral("size"), 96);
        const pvf::ComicBox box = pvf::ComicBox::build(
            pvf::TextStyle::fromData(data), pvf::BubbleStyle::fromData(data));

        QImage canvas(1600, 1600, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        {
            QPainter painter(&canvas);
            painter.translate(800, 800);
            box.paint(painter);
        }
        const QRectF bounds = box.bounds();
        const QRect area(int(std::floor(bounds.left())) + 800,
                         int(std::floor(bounds.top())) + 800,
                         int(std::ceil(bounds.width())),
                         int(std::ceil(bounds.height())));
        int edge = 0;
        const QRect edgeRect = area.adjusted(0, 0, -1, -1);
        for (int x = edgeRect.left(); x <= edgeRect.right(); ++x)
        {
            if (qAlpha(canvas.pixel(x, edgeRect.top())) > 0)
                ++edge;
            if (qAlpha(canvas.pixel(x, edgeRect.bottom())) > 0)
                ++edge;
        }
        for (int y = edgeRect.top(); y <= edgeRect.bottom(); ++y)
        {
            if (qAlpha(canvas.pixel(edgeRect.left(), y)) > 0)
                ++edge;
            if (qAlpha(canvas.pixel(edgeRect.right(), y)) > 0)
                ++edge;
        }
        canvas.save(QStringLiteral("/tmp/pvf_bubble_bounds.png"));
        if (edge >= 50)
        {
            qCritical() << "special-test: balloon stroke clipped at bounds"
                        << edge << bounds;
            ++failures;
        }
    }

    // Glyph outline: stroke colour appears around the fill.
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("描边");
        style.pixelSize = 64;
        style.color = Qt::white;
        style.strokeColor = QColor(220, 30, 60);
        style.strokeWidth = 6;
        QImage image(320, 200, QImage::Format_ARGB32);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            pvf::TextBlock::measure(style).paint(painter, QPointF(160, 100));
        }
        image.save(QStringLiteral("/tmp/pvf_text_stroke.png"));
        int fill = 0;
        int outline = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor color = image.pixelColor(x, y);
                if (color.red() > 200 && color.green() > 200 && color.blue() > 200)
                    ++fill;
                else if (color.red() > 150 && color.green() < 100)
                    ++outline;
            }
        if (fill == 0 || outline == 0)
        {
            qCritical() << "special-test: text outline missing" << fill << outline;
            ++failures;
        }
    }

    // Radial rays.
    {
        pvf::RadialParams params;
        params.count = 32;
        params.radius = 200;
        params.width = 12;
        QVariantMap data;
        params.writeTo(data);
        pvf::Entity* radial = project->addEntity(pvf::EntityType::RadialLines,
                                                 QStringLiteral("放射状线条"), data);
        if (!radial)
        {
            qCritical() << "special-test: radial creation failed";
            ++failures;
        }
        else
        {
            radial->values[QStringLiteral("x")] = 260.0;
            const QImage first = render(0);
            if (ink(first) <= 0)
            {
                qCritical() << "special-test: radial lines drew nothing";
                ++failures;
            }
            if (first != render(0))
            {
                qCritical() << "special-test: radial lines are not deterministic";
                ++failures;
            }
            if (first == render(8))
            {
                qCritical() << "special-test: radial flicker does not animate";
                ++failures;
            }
        }
    }

    // Flow streaks move with the frame.
    {
        pvf::FlowParams params;
        params.count = 12;
        params.length = 300;
        params.spacing = 60;
        params.area = 900;
        params.speed = 900;
        QVariantMap data;
        params.writeTo(data);
        pvf::Entity* flow = project->addEntity(pvf::EntityType::FlowLines,
                                               QStringLiteral("快速流过"), data);
        if (!flow)
        {
            qCritical() << "special-test: flow creation failed";
            ++failures;
        }
        else
        {
            flow->values[QStringLiteral("x")] = 0.0;
            flow->values[QStringLiteral("y")] = 200.0;
            const QImage first = render(0);
            if (ink(first) <= 0)
            {
                qCritical() << "special-test: flow lines drew nothing";
                ++failures;
            }
            if (first == render(30))
            {
                qCritical() << "special-test: flow lines do not animate";
                ++failures;
            }
        }
    }

    // Halftone screentone: a dot grid that fades along the gradient angle.
    {
        pvf::HalftoneParams params;
        params.area = 600;
        params.spacing = 16;
        params.dotSize = 8;
        QVariantMap data;
        params.writeTo(data);
        pvf::Entity* halftone = project->addEntity(pvf::EntityType::HalftoneDots,
                                                   QStringLiteral("漫画点阵"), data);
        if (!halftone)
        {
            qCritical() << "special-test: halftone creation failed";
            ++failures;
        }
        else
        {
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = other.id != halftone->id;
            const QImage shot = render(0);
            const QImage again = render(0);
            if (halftone->halftonePixmap.isNull()
                || halftone->halftoneData.value(QStringLiteral("spacing")).toDouble()
                       != 16.0)
            {
                qCritical() << "special-test: halftone pattern was not cached";
                ++failures;
            }
            else
            {
                // A second frame reuses the raster; a parameter edit rebuilds it.
                const qint64 cached = halftone->halftonePixmap.cacheKey();
                render(0);
                if (halftone->halftonePixmap.cacheKey() != cached)
                {
                    qCritical() << "special-test: halftone cache rebuilt every frame";
                    ++failures;
                }
                halftone->data[QStringLiteral("spacing")] = 24.0;
                render(0);
                if (halftone->halftonePixmap.cacheKey() == cached
                    || halftone->halftoneData.value(QStringLiteral("spacing"))
                               .toDouble()
                           != 24.0)
                {
                    qCritical() << "special-test: halftone cache not invalidated";
                    ++failures;
                }
            }
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = false;
            shot.save(QStringLiteral("/tmp/pvf_halftone.png"));

            const auto density = [&](int top, int bottom) {
                int count = 0;
                for (int y = top; y < bottom; ++y)
                    for (int x = 0; x < shot.width(); ++x)
                        if (qGray(shot.pixel(x, y)) > 40)
                            ++count;
                return count;
            };
            if (ink(shot) <= 0)
            {
                qCritical() << "special-test: halftone drew nothing";
                ++failures;
            }
            else if (density(0, shot.height() / 2)
                     <= density(shot.height() / 2, shot.height()))
            {
                qCritical() << "special-test: halftone gradient does not fade";
                ++failures;
            }
            if (shot != again)
            {
                qCritical() << "special-test: halftone is not deterministic";
                ++failures;
            }
        }
    }

    // Brand tile wall: a checkerboard of two logos.
    {
        const QString logoA =
            QDir::tempPath() + QStringLiteral("/pvf_wall_logo_a.png");
        const QString logoB =
            QDir::tempPath() + QStringLiteral("/pvf_wall_logo_b.svg");
        QImage a(96, 96, QImage::Format_ARGB32);
        a.fill(Qt::transparent);
        {
            QPainter p(&a);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(10, 10, 76, 76);
        }
        if (!a.save(logoA))
        {
            qCritical() << "special-test: wall logo not saved";
            ++failures;
        }
        // The second slot is an SVG at a tiny intrinsic size; it has to be
        // rasterised at a usable resolution.
        QFile svgFile(logoB);
        if (svgFile.open(QIODevice::WriteOnly))
        {
            svgFile.write(
                "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' "
                "viewBox='0 0 24 24'><path d='M4 4 L20 20 M20 4 L4 20' "
                "stroke='white' stroke-width='3' stroke-linecap='round'/></svg>");
            svgFile.close();
        }
        const QImage svgLogo = pvf::BrandWall::loadLogo(logoB);
        if (svgLogo.isNull()
            || std::max(svgLogo.width(), svgLogo.height()) != 1024)
        {
            qCritical() << "special-test: svg logo not rasterised" << svgLogo.size();
            ++failures;
        }
        pvf::BrandWallParams wallParams;
        wallParams.width = 800;
        wallParams.height = 450;
        wallParams.columns = 8;
        wallParams.rows = 5;
        wallParams.gap = 6;
        wallParams.logoOpacity = 0.9;
        wallParams.logos = QStringList{logoA, logoB};
        wallParams.tileColors =
            QStringList{QColor(210, 70, 160).name(QColor::HexArgb),
                        QColor(232, 232, 238).name(QColor::HexArgb)};
        pvf::Entity* wall = project->addBrandWall(wallParams);
        if (!wall)
        {
            qCritical() << "special-test: brand wall creation failed";
            ++failures;
        }
        else if (std::abs(pvf::SceneRenderer::entityLocalBounds(*wall).width() - 800.0)
                     > 0.01
                 || wall->wallLogos.size() != 2 || wall->wallLogos[0].isNull()
                 || wall->wallLogos[1].isNull())
        {
            qCritical() << "special-test: brand wall bounds or logos wrong"
                        << pvf::SceneRenderer::entityLocalBounds(*wall);
            ++failures;
        }
        else
        {
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = other.id != wall->id;
            const QImage shot = render(0);
            shot.save(QStringLiteral("/tmp/pvforge_brandwall.png"));
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = false;
            if (ink(shot) <= 0)
            {
                qCritical() << "special-test: brand wall drew nothing";
                ++failures;
            }
            // Checkerboard: neighbouring tiles use different slots, also
            // across rows (the old row * columns indexing broke this).
            const QColor tile00 = shot.pixelColor(96, 94);
            const QColor tile01 = shot.pixelColor(146, 94);
            const QColor tile10 = shot.pixelColor(96, 139);
            if (tile00 == tile01 || tile00 == tile10)
            {
                qCritical() << "special-test: brand wall is not a checkerboard"
                            << tile00 << tile01 << tile10;
                ++failures;
            }
        }
    }

    // Japanese vertical text: columns run right to left.
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("日式竖排\n从右到左");
        style.pixelSize = 48;
        style.vertical = true;
        const pvf::TextBlock block = pvf::TextBlock::measure(style);
        if (block.contentSize().width() <= 0
            || block.contentSize().height() <= block.contentSize().width())
        {
            qCritical() << "special-test: vertical text is not column shaped"
                        << block.contentSize();
            ++failures;
        }
    }

    render(0).save(QStringLiteral("/tmp/pvforge_special.png"));

    // The special types survive a save and load round trip.
    {
        const QString path = QDir::tempPath() +
                             QStringLiteral("/pvforge_special_test.pvproj");
        QString error;
        if (!project->save(path, &error))
        {
            qCritical() << "special-test: save failed" << error;
            ++failures;
        }
        else
        {
            pvf::Project loaded;
            if (!loaded.load(path, &error))
            {
                qCritical() << "special-test: load failed" << error;
                ++failures;
            }
            else
            {
                int comic = 0;
                int radial = 0;
                int flow = 0;
                int halftone = 0;
                int wall = 0;
                for (const pvf::Entity& entity : loaded.entities())
                {
                    if (entity.type == pvf::EntityType::ComicText)
                        ++comic;
                    else if (entity.type == pvf::EntityType::RadialLines)
                        ++radial;
                    else if (entity.type == pvf::EntityType::FlowLines)
                        ++flow;
                    else if (entity.type == pvf::EntityType::HalftoneDots)
                        ++halftone;
                    else if (entity.type == pvf::EntityType::BrandWall)
                        ++wall;
                }
                if (comic != 4 || radial != 1 || flow != 1 || halftone != 1
                    || wall != 1)
                {
                    qCritical() << "special-test: types lost on reload" << comic
                                << radial << flow << halftone << wall;
                    ++failures;
                }
            }
            QFile::remove(path);
        }
    }

    // Performance monitor keeps the latest rolling timings.
    {
        auto& perf = pvf::PerfMonitor::instance();
        perf.setEnabled(true);
        perf.recordScene(7.5);
        perf.recordViewport(3.0);
        perf.recordTimeline(2.0);
        perf.recordVideo(1.0);
        perf.setEntityCount(53);
        const pvf::PerfSnapshot snapshot = perf.snapshot();
        if (!snapshot.enabled || std::abs(snapshot.sceneMs - 7.5) > 1e-6
            || std::abs(snapshot.viewportMs - 3.0) > 1e-6
            || std::abs(snapshot.timelineMs - 2.0) > 1e-6
            || std::abs(snapshot.videoMs - 1.0) > 1e-6
            || snapshot.entities != 53)
        {
            qCritical() << "special-test: performance monitor values wrong"
                        << snapshot.sceneMs << snapshot.viewportMs
                        << snapshot.timelineMs << snapshot.videoMs;
            ++failures;
        }
        perf.setEnabled(false);
        if (perf.snapshot().enabled)
        {
            qCritical() << "special-test: performance monitor did not disable";
            ++failures;
        }
    }

    // The workbench offers the special tools and the layout dialog opens.
    auto* uiProject = new pvf::Project;
    uiProject->width = 1280;
    uiProject->height = 720;
    pvf::MainWindow uiWindow(uiProject);
    uiWindow.resize(1400, 900);
    uiWindow.show();
    QTimer::singleShot(500, &uiWindow, [&app, &uiWindow, uiProject, failures] {
        int local = failures;
        auto* workbench = uiWindow.findChild<pvf::WorkbenchPanel*>();
        auto* types = workbench
            ? workbench->findChild<QListWidget*>(QStringLiteral("typeList"))
            : nullptr;
        auto* special = workbench
            ? workbench->findChild<QListWidget*>(QStringLiteral("specialTypes"))
            : nullptr;
        auto* create = workbench
            ? workbench->findChild<QPushButton*>(QStringLiteral("createButton"))
            : nullptr;
        if (!types || !special || !create)
        {
            qCritical() << "special-test: workbench widgets missing";
            ++local;
        }
        else
        {
            auto* textBoxes =
                workbench->findChild<QListWidget*>(QStringLiteral("textBoxTypes"));
            types->setCurrentRow(2);   // the "text box" page
            QApplication::processEvents();
            for (int row = 0; textBoxes && row < textBoxes->count(); ++row)
            {
                textBoxes->setCurrentRow(row);
                QApplication::processEvents();
                const int before = uiProject->entities().size();
                create->click();
                QApplication::processEvents();
                if (uiProject->entities().size() != before + 1)
                {
                    qCritical() << "special-test: text box row did not create" << row;
                    ++local;
                }
            }
            if (textBoxes)
                textBoxes->setCurrentRow(1);
            QApplication::processEvents();
            workbench->grab().toImage().save(
                QStringLiteral("/tmp/pvforge_special_workbench.png"));

            types->setCurrentRow(4);   // the "special" page
            QApplication::processEvents();
            for (int row = 0; row < special->count(); ++row)
            {
                special->setCurrentRow(row);
                QApplication::processEvents();
                const int before = uiProject->entities().size();
                create->click();
                QApplication::processEvents();
                if (uiProject->entities().size() != before + 1)
                {
                    qCritical() << "special-test: workbench did not create row" << row;
                    ++local;
                }
            }
            special->setCurrentRow(2);   // halftone, for the shot
            QApplication::processEvents();
            workbench->grab().toImage().save(
                QStringLiteral("/tmp/pvforge_halftone_workbench.png"));
            special->setCurrentRow(3);   // brand wall, for the shot
            QApplication::processEvents();
            workbench->grab().toImage().save(
                QStringLiteral("/tmp/pvforge_wall_workbench.png"));
        }

        // The comic spike can be dragged to a new tip position.
        auto* viewport = uiWindow.findChild<pvf::Viewport*>();
        pvf::EntityId comicId = pvf::kInvalidEntity;
        for (const pvf::Entity& entity : uiProject->entities())
        {
            if (entity.type == pvf::EntityType::ComicText)
            {
                comicId = entity.id;
                break;
            }
        }
        if (!viewport || comicId == pvf::kInvalidEntity)
        {
            qCritical() << "special-test: comic entity or viewport missing";
            ++local;
        }
        else
        {
            uiWindow.setSelectedEntityForTest(comicId);
            QApplication::processEvents();
            const QPointF tip = viewport->comicTipForTest(comicId);
            const pvf::Entity* comic = uiProject->entity(comicId);
            const double beforeX = comic->data.value(QStringLiteral("bubbleTipX")).toDouble();
            const auto send = [&](QEvent::Type type, const QPointF& pos) {
                QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)),
                                  Qt::LeftButton,
                                  type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                    : Qt::LeftButton,
                                  Qt::NoModifier);
                QApplication::sendEvent(viewport, &event);
            };
            send(QEvent::MouseButtonPress, tip);
            send(QEvent::MouseMove, tip + QPointF(20, 0));
            send(QEvent::MouseMove, tip + QPointF(40, 0));
            send(QEvent::MouseMove, tip + QPointF(60, 0));
            send(QEvent::MouseButtonRelease, tip + QPointF(60, 0));
            QApplication::processEvents();
            const pvf::Entity* moved = uiProject->entity(comicId);
            const double afterX = moved->data.value(QStringLiteral("bubbleTipX")).toDouble();
            if (std::abs(afterX - beforeX) < 0.01)
            {
                qCritical() << "special-test: comic spike did not move" << beforeX
                            << afterX;
                ++local;
            }
            if (auto* undo = uiWindow.findChild<QAction*>(QStringLiteral("undoAction")))
            {
                undo->trigger();
                QApplication::processEvents();
                const pvf::Entity* undone = uiProject->entity(comicId);
                if (std::abs(undone->data.value(QStringLiteral("bubbleTipX")).toDouble()
                             - beforeX) > 0.01)
                {
                    qCritical() << "special-test: comic spike undo failed";
                    ++local;
                }
            }
        }

        // The layout dialog opens with the style it was given.
        pvf::TextStyle style;
        style.text = QStringLiteral("排版预览\n第二行");
        style.pixelSize = 48;
        style.color = QColor(30, 30, 36);
        style.letterSpacing = 8;
        style.boxWidth = 260;
        pvf::TextLayoutDialog dialog(style);
        dialog.show();
        QApplication::processEvents();
        if (!dialog.findChild<pvf::TextPreview*>(QStringLiteral("layoutPreview")))
        {
            qCritical() << "special-test: layout preview missing";
            ++local;
        }
        auto* letterSpacing =
            dialog.findChild<QDoubleSpinBox*>(QStringLiteral("layoutLetterSpacing"));
        if (!letterSpacing || std::abs(letterSpacing->value() - 8.0) > 1e-6)
        {
            qCritical() << "special-test: layout letter spacing missing";
            ++local;
        }
        if (dialog.style().text != style.text || dialog.style().boxWidth != style.boxWidth)
        {
            qCritical() << "special-test: layout dialog changed the style";
            ++local;
        }
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_text_layout.png"));

        // The status bar performance monitor toggles from the view menu.
        if (auto* perfAction =
                uiWindow.findChild<QAction*>(QStringLiteral("perfMonitorAction")))
        {
            auto* perfLabel =
                uiWindow.findChild<QLabel*>(QStringLiteral("perfInfo"));
            perfAction->setChecked(true);
            QApplication::processEvents();
            if (!perfLabel || !perfLabel->isVisible()
                || !perfLabel->text().contains(QStringLiteral("FPS")))
            {
                qCritical() << "special-test: performance monitor label missing";
                ++local;
            }
            perfAction->setChecked(false);
            QApplication::processEvents();
        }
        else
        {
            qCritical() << "special-test: performance monitor action missing";
            ++local;
        }

        // The wall editor opens with its slots and can add more logos.
        pvf::BrandWallParams wallParams;
        wallParams.width = 640;
        wallParams.height = 360;
        wallParams.columns = 8;
        wallParams.rows = 5;
        wallParams.gap = 4;
        wallParams.logos = QStringList{QString(), QString()};
        wallParams.tileColors = QStringList{QColor(210, 70, 160).name(),
                                            QColor(232, 232, 238).name()};
        pvf::BrandWallDialog wallDialog(wallParams,
                                        QVector<QImage>{QImage(), QImage()});
        wallDialog.show();
        QApplication::processEvents();
        if (!wallDialog.findChild<pvf::BrandWallPreview*>(
                QStringLiteral("wallPreview"))
            || wallDialog.params().logos.size() != 2
            || wallDialog.params().columns != 8
            || wallDialog.params().rows != 5)
        {
            qCritical() << "special-test: wall dialog state wrong";
            ++local;
        }
        wallDialog.grab().toImage().save(
            QStringLiteral("/tmp/pvforge_wall_dialog.png"));

        // A clean scene shot: only the selected comic balloon.
        for (pvf::Entity& entity : uiProject->mutableEntities())
            entity.hidden = entity.id != comicId;
        uiWindow.setSelectedEntityForTest(comicId);
        QApplication::processEvents();
        if (auto* props = uiWindow.findChild<pvf::PropertiesPanel*>())
            if (auto* scroll = qobject_cast<QScrollArea*>(
                    props->parentWidget() ? props->parentWidget()->parentWidget()
                                          : nullptr))
                scroll->verticalScrollBar()->setValue(
                    scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        uiWindow.grab().toImage().save(QStringLiteral("/tmp/pvforge_special_scene.png"));
        qInfo() << "special-test failures" << local;
        app.exit(local == 0 ? 0 : 1);
    });
    return app.exec();
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
        send(QEvent::MouseMove, QPoint(400, timeline->rowCenterYForTest(0)),
             Qt::NoButton, Qt::NoButton);
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_row_buttons.png"));

        // Rubber-band across both rows to pick up every keyframe in range.
        const QPoint boxStart(200, timeline->rowCenterYForTest(0));
        const QPoint boxEnd(560, timeline->rowCenterYForTest(1));
        send(QEvent::MouseButtonPress, boxStart, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, boxEnd, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, boxEnd, Qt::LeftButton, Qt::NoButton);
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
        const QPoint emptyRow(300, timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, emptyRow, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, emptyRow, Qt::LeftButton, Qt::NoButton);
        const int emptyFrame = window.currentFrame();
        if (std::abs(emptyFrame - 20) > 2)
        {
            qCritical() << "timeline-interaction-test: empty frame" << emptyFrame;
            ++failures;
        }

        // Keyframe click selects it and moves the playhead to the keyframe,
        // so the property panel shows the values stored in that keyframe.
        const int beforeKey = window.currentFrame();
        const QPoint keyPoint(540, timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, keyPoint, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, keyPoint, Qt::LeftButton, Qt::NoButton);
        const int afterKey = window.currentFrame();
        if (afterKey != 60)
        {
            qCritical() << "timeline-interaction-test: keyframe did not focus"
                        << beforeKey << afterKey;
            ++failures;
        }
        if (timeline->selectedKeyframeCount() != 1)
        {
            qCritical() << "timeline-interaction-test: keyframe not selected";
            ++failures;
        }

        // Clicking empty space clears the keyframe selection.
        send(QEvent::MouseButtonPress, emptyRow, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, emptyRow, Qt::LeftButton, Qt::NoButton);
        if (timeline->selectedKeyframeCount() != 0)
        {
            qCritical() << "timeline-interaction-test: selection not cleared";
            ++failures;
        }

        // Dragging rubber-bands instead of scrubbing.
        const QPoint dragStart(250, timeline->rowCenterYForTest(0));
        const QPoint dragEnd(320, timeline->rowCenterYForTest(1));
        send(QEvent::MouseButtonPress, dragStart, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, dragEnd, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, dragEnd, Qt::LeftButton, Qt::NoButton);
        if (window.currentFrame() != emptyFrame)
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

        auto* autosave = dialog.findChild<QCheckBox*>(QStringLiteral("autosaveEnabled"));
        auto* autosaveMinutes =
            dialog.findChild<QSpinBox*>(QStringLiteral("autosaveMinutes"));
        if (!autosave || !autosave->isChecked() || !autosaveMinutes
            || autosaveMinutes->value() != 5)
        {
            qCritical() << "settings-test: autosave default is not on / 5 minutes"
                        << (autosave ? autosave->isChecked() : false)
                        << (autosaveMinutes ? autosaveMinutes->value() : -1);
            return 1;
        }

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
    auto* streamButton =
        monitor->findChild<QToolButton*>(QStringLiteral("monitorStreamButton"));
    int failures = 0;
    if (!traceButton || !streamButton)
    {
        qCritical() << "monitor-feature-test: monitor buttons missing";
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

    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
    {
        const bool started = monitor->startStreamingForTest();
        if (!started || !monitor->isStreaming())
        {
            qCritical() << "monitor-feature-test: RTP stream did not start";
            ++failures;
        }
        else if (monitor->height() > 60)
        {
            qCritical() << "monitor-feature-test: monitor not collapsed"
                        << monitor->height();
            ++failures;
        }
        else
        {
            // Verify the default HTTP mode actually serves MPEG-TS.
            const QUrl url(monitor->streamUrlForTest());
            QTcpSocket socket;
            socket.connectToHost(QStringLiteral("127.0.0.1"),
                                 quint16(url.port(8080)));
            if (!socket.waitForConnected(2000))
            {
                qCritical() << "monitor-feature-test: HTTP stream connect failed";
                ++failures;
            }
            else
            {
                socket.write("GET /stream.ts HTTP/1.0\r\nHost: localhost\r\n\r\n");
                QByteArray response;
                QEventLoop loop;
                QObject::connect(&socket, &QTcpSocket::readyRead, &loop,
                                 [&response, &socket] { response += socket.readAll(); });
                QTimer::singleShot(2000, &loop, &QEventLoop::quit);
                loop.exec();
                const int headerEnd = response.indexOf("\r\n\r\n");
                if (!response.contains("200 OK") || headerEnd < 0
                    || response.indexOf('\x47', headerEnd) < 0)
                {
                    qCritical() << "monitor-feature-test: HTTP stream payload"
                                << response.size();
                    ++failures;
                }
            }
        }
        monitor->stopStreamingForTest();
        QApplication::processEvents();
        if (monitor->isStreaming() || monitor->height() < 100)
        {
            qCritical() << "monitor-feature-test: RTP stream did not stop"
                        << monitor->height();
            ++failures;
        }
    }
    else
    {
        qInfo() << "monitor-feature-test: ffmpeg missing, streaming skipped";
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

    for (int i = 0; i < 30; ++i)
        project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    // Give the time axis some content so it can actually scroll horizontally.
    if (const pvf::Entity* first = project->entities().isEmpty()
                                        ? nullptr
                                        : &project->entities().first())
        project->setValue(first->id, QStringLiteral("x"), 400, 100.0, true);

    QTimer::singleShot(400, &window, [&app, &window, timeline, project] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_before.png"));
        const double before = timeline->pixelsPerFrame();

        const QPointF pos(500, 120);
        for (int i = 0; i < 3; ++i)
        {
            QWheelEvent event(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(timeline, &event);
        }

        int failures = 0;
        const double after = timeline->pixelsPerFrame();
        if (!(after > before * 4.0))
        {
            qCritical() << "timeline-zoom-test: ctrl wheel did not zoom" << before
                        << after;
            ++failures;
        }
        if (!timeline->verticalScrollVisibleForTest())
        {
            qCritical() << "timeline-zoom-test: vertical scrollbar not visible";
            ++failures;
        }
        const int verticalBefore = timeline->verticalScrollForTest();
        QWheelEvent plain(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(timeline, &plain);
        if (timeline->verticalScrollForTest() <= verticalBefore)
        {
            qCritical() << "timeline-zoom-test: wheel did not scroll rows"
                        << verticalBefore << timeline->verticalScrollForTest();
            ++failures;
        }
        const int horizontalBefore = timeline->horizontalScrollForTest();
        QWheelEvent shifted(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                            Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(timeline, &shifted);
        if (timeline->horizontalScrollForTest() == horizontalBefore)
        {
            qCritical() << "timeline-zoom-test: shift wheel did not scroll time"
                        << horizontalBefore << timeline->horizontalScrollForTest();
            ++failures;
        }

        QTimer::singleShot(200, &window, [&app, &window, before, after, failures] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_after.png"));
            qInfo() << "timeline-zoom-test:" << before << "->" << after
                    << "failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    });
    return app.exec();
}

int clipTimelineTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::Entity* group = project->addFolder(QStringLiteral("场景组"));
    const pvf::EntityId groupId = group->id;
    pvf::Entity* rect =
        project->addVector(0, 120, 80, QColor(235, 87, 87), Qt::white, 0);
    rect->name = QStringLiteral("红色矩形");
    const pvf::EntityId rectId = rect->id;
    pvf::Entity* text = project->addText(QStringLiteral("标题文字"),
                                         QStringLiteral("Sans"), 64, Qt::white,
                                         Qt::AlignCenter);
    text->name = QStringLiteral("标题文字");
    const pvf::EntityId textId = text->id;
    project->setEntityParent(rectId, groupId);
    project->setEntityParent(textId, groupId);
    project->entity(groupId)->data[QStringLiteral("clipStart")] = 0;
    project->entity(groupId)->data[QStringLiteral("clipEnd")] = 150;
    project->entity(rectId)->data[QStringLiteral("clipStart")] = 0;
    project->entity(rectId)->data[QStringLiteral("clipEnd")] = 120;
    project->entity(textId)->data[QStringLiteral("clipStart")] = 30;
    project->entity(textId)->data[QStringLiteral("clipEnd")] = 150;

    auto audio = std::make_shared<pvf::AudioClip>();
    audio->duration = 4.0;
    audio->peaks.resize(4000);
    for (int i = 0; i < audio->peaks.size(); ++i)
        audio->peaks[i] =
            0.12f + 0.88f * float(std::abs(std::sin(i * 0.006)));
    pvf::Entity* bgm =
        project->addAudio(QStringLiteral("bgm.wav"), audio, QStringLiteral("BGM"));
    project->setEntityParent(bgm->id, groupId);

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "clip-timeline-test: no timeline";
        return 1;
    }
    QTimer::singleShot(300, &window, [&app, &window, timeline, groupId, rectId] {
        timeline->setPixelsPerFrameForTest(8.0);
        timeline->setCurrentFrame(45);
        timeline->openPopup(groupId);
        QTimer::singleShot(400, &window,
                           [&app, &window, groupId, rectId] {
            int failures = 0;
            pvf::TimelinePanel* popup = nullptr;
            for (pvf::TimelinePanel* panel :
                 window.findChildren<pvf::TimelinePanel*>())
            {
                if (panel->owner() == groupId)
                {
                    popup = panel;
                    break;
                }
            }
            if (!popup)
            {
                qCritical() << "clip-timeline-test: popup missing";
                app.exit(1);
                return;
            }
            pvf::Project* project = window.project();
            if (!project)
            {
                qCritical() << "clip-timeline-test: project missing";
                app.exit(1);
                return;
            }
            project->addKeyframe(rectId, QStringLiteral("x"), 110, 0.0);
            const QRect before = popup->clipRectFor(rectId);
            const int row = 0;
            auto send = [popup](QEvent::Type type, const QPoint& pos,
                                Qt::MouseButtons buttons) {
                QMouseEvent event(type, QPointF(pos),
                                  QPointF(popup->mapToGlobal(pos)),
                                  Qt::LeftButton, buttons, Qt::NoModifier);
                QApplication::sendEvent(popup, &event);
            };
            const QPoint from(before.right() - 3,
                              popup->rowCenterYForTest(row));
            const QPoint to(from.x() - 30 * 8, from.y());
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
            QApplication::processEvents();
            const int clippedEnd = project->clipEndFrame(rectId);
            const bool keyGone = project->keyframe(rectId, QStringLiteral("x"),
                                                   110.0)
                                 == nullptr;
            const QRect clipped = popup->clipRectFor(rectId);
            const QPoint expandFrom(clipped.right() - 3,
                                    popup->rowCenterYForTest(row));
            const QPoint expandTo(expandFrom.x() + 30 * 8, expandFrom.y());
            send(QEvent::MouseButtonPress, expandFrom, Qt::LeftButton);
            send(QEvent::MouseMove, expandTo, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, expandTo, Qt::NoButton);
            QApplication::processEvents();
            const int expandedEnd = project->clipEndFrame(rectId);
            const bool keyStayedGone =
                project->keyframe(rectId, QStringLiteral("x"), 110.0) == nullptr;
            int undoEnd1 = -1;
            int undoEnd2 = -1;
            bool keyRestored = false;
            for (QAction* action : window.findChildren<QAction*>())
            {
                if (!action->text().contains(QStringLiteral("撤销")))
                    continue;
                action->trigger();
                undoEnd1 = project->clipEndFrame(rectId);
                action->trigger();
                undoEnd2 = project->clipEndFrame(rectId);
                keyRestored =
                    project->keyframe(rectId, QStringLiteral("x"), 110.0) != nullptr;
                break;
            }
            if (clippedEnd >= 110 || !keyGone || expandedEnd <= clippedEnd
                || !keyStayedGone || undoEnd1 != clippedEnd
                || undoEnd2 != before.width() / 8 || !keyRestored)
            {
                qCritical() << "clip-timeline-test: trim" << before << clippedEnd
                            << expandedEnd << keyGone << keyStayedGone << "undo"
                            << undoEnd1 << undoEnd2 << keyRestored;
                ++failures;
            }

            // Clicking a keyframe must feed the property panel's existing
            // easing button (the previous milestone's keyframe page).
            project->addKeyframe(rectId, QStringLiteral("x"), 0, 0.0);
            project->addKeyframe(rectId, QStringLiteral("x"), 60, 0.0);
            QApplication::processEvents();
            const QPoint keyPoint(popup->frameXForTest(60),
                                  popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonPress, keyPoint, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, keyPoint, Qt::NoButton);
            QApplication::processEvents();
            auto* easeButton =
                window.findChild<QPushButton*>(QStringLiteral("keyframeEasingButton"));
            const bool easingReady =
                easeButton && easeButton->isEnabled()
                && easeButton->text().contains(QStringLiteral("缓动"));
            if (window.currentFrame() != 60 || !easingReady)
            {
                qCritical() << "clip-timeline-test: easing" << window.currentFrame()
                            << (easeButton ? easeButton->text() : QStringLiteral("none"))
                            << (easeButton && easeButton->isEnabled());
                ++failures;
            }
            window.grab().toImage().save(
                QStringLiteral("/tmp/pvforge_clip_timeline.png"));
            qInfo() << "clip-timeline-test: popup panels"
                    << window.findChildren<pvf::TimelinePanel*>().size()
                    << "trim" << clippedEnd << "expand" << expandedEnd
                    << "key gone" << keyGone << "failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
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
        auto* panel = window.findChild<QWidget*>(QStringLiteral("assetPanel"));
        auto* info = window.findChild<QLabel*>(QStringLiteral("assetInfo"));
        if (!tabs || !panel || !info)
        {
            qCritical() << "library-test: asset widgets missing";
            layoutOk = false;
        }
        else
        {
            qInfo() << "library-test: widths tabs" << tabs->width()
                    << "panel" << panel->width() << "panel min"
                    << panel->minimumSizeHint().width() << "info min"
                    << info->minimumSizeHint().width();
            layoutOk = tabs->width() <= 440 && panel->width() <= tabs->width()
                       && info->width() <= panel->width()
                       && info->text().contains(QChar(0x2026))
                       && info->toolTip() == assetPath;
            if (!layoutOk)
                qCritical() << "library-test: long path broke the sidebar"
                            << info->text() << info->toolTip();
        }
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

} // namespace

namespace {

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
        if (auto* timeline = window.findChild<pvf::TimelinePanel*>())
            qInfo() << "timeline geometry" << timeline->geometry()
                    << "min" << timeline->minimumSize();
        if (auto* viewport = window.findChild<pvf::Viewport*>())
            qInfo() << "viewport geometry" << viewport->geometry()
                    << "min" << viewport->minimumSize();
        app.exit(failures == 0 ? 0 : 1);
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

int asyncExportTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    if (pvf::VideoExporter::ffmpegExecutable().isEmpty())
    {
        qInfo() << "async-export-test: ffmpeg missing, skipped";
        return 0;
    }

    pvf::Project project;
    project.name = QStringLiteral("async-export");
    project.width = 320;
    project.height = 180;
    project.fps = 24;
    project.background = QColor(10, 20, 40);
    project.addVector(0, 80, 80, QColor(230, 40, 40), QColor(0, 0, 0, 0),
                      0.0);

    auto clip = std::make_shared<pvf::AudioClip>();
    clip->duration = 0.5;
    clip->sampleRate = 48000;
    clip->channels = 2;
    clip->samples.resize(24000 * 2);
    for (int i = 0; i < 24000; ++i)
    {
        const qint16 value =
            qint16(8000.0 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
        clip->samples[i * 2] = value;
        clip->samples[i * 2 + 1] = value;
    }
    clip->peaks.resize(500);
    project.addAudio(QStringLiteral("/tmp/pvf_async_export_tone.wav"), clip,
                     QStringLiteral("tone"));

    const int frameCount = 12;
    auto render = [&project](int frame) {
        pvf::Camera::State state;
        state.scale = 1.0;
        state.size = QSizeF(project.width, project.height);
        return pvf::CameraRenderer::renderCameraImage(
            project, frame, state, QSize(320, 180), false);
    };

    struct Result
    {
        bool reported = false;
        bool success = false;
        QString error;
        QVector<int> progress;
        int previews = 0;
        int heartbeats = 0;
    };

    auto runExport = [&project](pvf::VideoExporter& exporter,
                                const pvf::ExportOptions& options,
                                pvf::VideoExporter::FrameProvider provider,
                                Result& result, int timeoutMs,
                                int cancelAfterMs = -1) {
        QEventLoop loop;
        QTimer timeout;
        QTimer heartbeat;
        timeout.setSingleShot(true);
        QObject::connect(&exporter, &pvf::VideoExporter::finished, &loop,
                         [&](bool success, const QString& error) {
                             result.reported = true;
                             result.success = success;
                             result.error = error;
                             loop.quit();
                         });
        QObject::connect(&exporter, &pvf::VideoExporter::progress, &loop,
                         [&](int done, int) { result.progress.append(done); });
        QObject::connect(&exporter, &pvf::VideoExporter::previewFrame, &loop,
                         [&](const QImage&) { ++result.previews; });
        QObject::connect(&heartbeat, &QTimer::timeout, &loop,
                         [&] { ++result.heartbeats; });
        heartbeat.start(10);
        QObject::connect(&timeout, &QTimer::timeout, &loop,
                         &QEventLoop::quit);

        QString error;
        if (!exporter.start(project, options, provider, &error))
        {
            result.reported = true;
            result.error = error;
            return false;
        }
        if (cancelAfterMs >= 0)
        {
            QTimer::singleShot(cancelAfterMs, &exporter,
                               [&exporter] { exporter.cancel(); });
        }
        timeout.start(timeoutMs);
        loop.exec();
        return result.reported && result.success;
    };

    auto pump = [](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };

    pvf::ExportOptions baseOptions;
    baseOptions.size = QSize(320, 180);
    baseOptions.fps = 24;
    baseOptions.frameCount = frameCount;
    baseOptions.crf = 23;
    baseOptions.includeAudio = true;

    // 1. A provider that answers synchronously must produce a normal MP4 with
    //    the expected first frame and audio track.
    const QString syncPath = QStringLiteral("/tmp/pvf_async_export_sync.mp4");
    QFile::remove(syncPath);
    {
        pvf::ExportOptions options = baseOptions;
        options.outputPath = syncPath;
        pvf::VideoExporter exporter;
        Result result;
        int inFlight = 0;
        bool concurrent = false;
        auto provider = [&](int frame, pvf::VideoExporter::FrameDone done) {
            if (++inFlight > 1)
                concurrent = true;
            done(render(frame));
            --inFlight;
        };
        const bool ok = runExport(exporter, options, provider, result, 60000);
        if (!ok || exporter.isRunning() || concurrent
            || result.progress.size() != frameCount
            || result.progress.last() != frameCount || result.previews <= 0)
        {
            qCritical() << "async-export-test: sync provider" << ok
                        << exporter.isRunning() << concurrent
                        << result.progress.size() << result.error;
            ++failures;
        }

        QString decodeError;
        double duration = 0.0;
        const QImage first =
            pvf::decodeVideoFirstFrame(syncPath, &decodeError, &duration);
        if (first.isNull() || first.size() != QSize(320, 180)
            || duration < 0.35 || duration > 0.65)
        {
            qCritical() << "async-export-test: sync video" << decodeError
                        << first.size() << duration;
            ++failures;
        }
        else
        {
            const QColor center = first.pixelColor(160, 90);
            const QColor background = first.pixelColor(10, 10);
            if (!(center.red() > 150 && center.green() < 100)
                || !(background.blue() > background.red()
                     && background.red() < 100))
            {
                qCritical() << "async-export-test: sync colours" << center
                            << background;
                ++failures;
            }
        }

        const pvf::AudioDecodeResult audio = pvf::decodeAudioFile(syncPath);
        if (!audio.clip || audio.clip->duration < 0.4)
        {
            qCritical() << "async-export-test: sync audio" << audio.error;
            ++failures;
        }
        else
        {
            float peak = 0.0f;
            for (float value : audio.clip->peaks)
                peak = std::max(peak, value);
            if (peak < 0.1f)
            {
                qCritical() << "async-export-test: sync audio silent" << peak;
                ++failures;
            }
        }
    }

    // 2. A provider that answers 50 ms later must still export successfully,
    //    one frame at a time, without blocking progress or the event loop.
    const QString delayedPath =
        QStringLiteral("/tmp/pvf_async_export_delayed.mp4");
    QFile::remove(delayedPath);
    {
        pvf::ExportOptions options = baseOptions;
        options.outputPath = delayedPath;
        pvf::VideoExporter exporter;
        Result result;
        int inFlight = 0;
        int maxInFlight = 0;
        int requests = 0;
        auto provider = [&](int frame, pvf::VideoExporter::FrameDone done) {
            ++inFlight;
            ++requests;
            maxInFlight = std::max(maxInFlight, inFlight);
            QTimer::singleShot(50, [&render, &inFlight, frame, done] {
                --inFlight;
                done(render(frame));
            });
        };
        QElapsedTimer elapsed;
        elapsed.start();
        const bool ok = runExport(exporter, options, provider, result, 60000);
        if (!ok || exporter.isRunning() || requests != frameCount
            || maxInFlight != 1
            || result.progress.size() != frameCount
            || result.progress.last() != frameCount
            || result.heartbeats <= 5 || elapsed.elapsed() < 400)
        {
            qCritical() << "async-export-test: delayed provider" << ok
                        << exporter.isRunning() << requests << maxInFlight
                        << result.progress.size() << result.heartbeats
                        << elapsed.elapsed() << result.error;
            ++failures;
        }
        QString decodeError;
        const QImage first =
            pvf::decodeVideoFirstFrame(delayedPath, &decodeError);
        if (first.isNull() || first.size() != QSize(320, 180))
        {
            qCritical() << "async-export-test: delayed video" << decodeError
                        << first.size();
            ++failures;
        }
    }

    // 3. A provider that never answers must fail on the per-frame timeout and
    //    leave no output or .part.mp4 behind.
    const QString timeoutPath =
        QStringLiteral("/tmp/pvf_async_export_timeout.mp4");
    QFile::remove(timeoutPath);
    {
        pvf::ExportOptions options = baseOptions;
        options.outputPath = timeoutPath;
        options.frameCount = 6;
        options.includeAudio = false;
        options.frameTimeoutMs = 200;
        pvf::VideoExporter exporter;
        Result result;
        int requests = 0;
        auto provider = [&](int, pvf::VideoExporter::FrameDone) { ++requests; };
        const bool ok = runExport(exporter, options, provider, result, 10000);
        if (ok || !result.reported || exporter.isRunning()
            || result.error.isEmpty()
            || !result.error.contains(QStringLiteral("超时"))
            || requests != 1 || !result.progress.isEmpty()
            || QFile::exists(timeoutPath)
            || QFile::exists(timeoutPath + QStringLiteral(".part.mp4")))
        {
            qCritical() << "async-export-test: timeout" << ok
                        << result.reported << exporter.isRunning()
                        << result.error << requests << result.progress.size()
                        << QFile::exists(timeoutPath)
                        << QFile::exists(timeoutPath
                                         + QStringLiteral(".part.mp4"));
            ++failures;
        }
    }

    // 4. Cancelling while a request is in flight must report cancellation and
    //    ignore the provider's late answer.
    const QString cancelPath =
        QStringLiteral("/tmp/pvf_async_export_cancel.mp4");
    QFile::remove(cancelPath);
    {
        pvf::ExportOptions options = baseOptions;
        options.outputPath = cancelPath;
        options.frameCount = 60;
        options.includeAudio = false;
        pvf::VideoExporter exporter;
        Result result;
        int requests = 0;
        auto provider = [&](int frame, pvf::VideoExporter::FrameDone done) {
            ++requests;
            QTimer::singleShot(400, [&render, frame, done] {
                done(render(frame));
            });
        };
        const bool ok =
            runExport(exporter, options, provider, result, 10000, 120);
        const int progressAfterCancel = result.progress.size();
        if (ok || !result.reported || result.success
            || !result.error.isEmpty() || exporter.isRunning()
            || requests != 1 || QFile::exists(cancelPath)
            || QFile::exists(cancelPath + QStringLiteral(".part.mp4")))
        {
            qCritical() << "async-export-test: cancel" << ok
                        << result.reported << result.success << result.error
                        << exporter.isRunning() << requests
                        << QFile::exists(cancelPath)
                        << QFile::exists(cancelPath
                                         + QStringLiteral(".part.mp4"));
            ++failures;
        }
        // Let the in-flight callback fire after the cancellation.
        pump(700);
        if (exporter.isRunning()
            || result.progress.size() != progressAfterCancel
            || QFile::exists(cancelPath)
            || QFile::exists(cancelPath + QStringLiteral(".part.mp4")))
        {
            qCritical() << "async-export-test: late callback"
                        << exporter.isRunning() << result.progress.size()
                        << progressAfterCancel << QFile::exists(cancelPath);
            ++failures;
        }
    }

    QFile::remove(syncPath);
    QFile::remove(delayedPath);
    QFile::remove(timeoutPath);
    QFile::remove(cancelPath);
    qInfo() << "async-export-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int exportTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    if (pvf::VideoExporter::ffmpegExecutable().isEmpty())
    {
        qInfo() << "export-test: ffmpeg missing, skipped";
        return 0;
    }

    const QString path = QStringLiteral("/tmp/pvf_export_test.mp4");
    QFile::remove(path);

    pvf::Project project;
    project.name = QStringLiteral("export");
    project.width = 320;
    project.height = 180;
    project.fps = 24;
    project.background = QColor(10, 20, 40);

    // A real video source exercises the export decode path (which must skip
    // the 960-wide preview proxy).
    const QString videoSource = QStringLiteral("/tmp/pvf_export_source.mp4");
    QFile::remove(videoSource);
    {
        QProcess generator;
        generator.start(
            QStringLiteral("ffmpeg"),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"),
             QStringLiteral("-i"),
             QStringLiteral("color=c=green:s=320x180:d=0.6:r=24"),
             QStringLiteral("-c:v"), QStringLiteral("libx264"),
             QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
             QStringLiteral("-preset"), QStringLiteral("ultrafast"), videoSource});
        if (!generator.waitForFinished(30000) || generator.exitCode() != 0)
        {
            qCritical() << "export-test: cannot generate source video"
                        << generator.readAllStandardError();
            ++failures;
        }
    }
    QString videoError;
    double videoDuration = 0.0;
    const QImage videoFirst =
        pvf::decodeVideoFirstFrame(videoSource, &videoError, &videoDuration);
    if (videoFirst.isNull())
    {
        qCritical() << "export-test: source video" << videoError;
        ++failures;
    }
    else
    {
        pvf::Entity* video = project.addVideo(videoSource, videoFirst, videoDuration,
                                              QStringLiteral("screen"));
        video->values.insert(QStringLiteral("x"), 0.0);
        video->values.insert(QStringLiteral("y"), 0.0);
    }

    pvf::Entity* rect = project.addVector(0, 120, 80, QColor(230, 40, 40),
                                          QColor(0, 0, 0, 0), 0.0);
    project.addKeyframe(rect->id, QStringLiteral("x"), 0, -80.0);
    project.addKeyframe(rect->id, QStringLiteral("x"), 24, 80.0);

    auto clip = std::make_shared<pvf::AudioClip>();
    clip->duration = 1.0;
    clip->sampleRate = 48000;
    clip->channels = 2;
    clip->samples.resize(48000 * 2);
    for (int i = 0; i < 48000; ++i)
    {
        const qint16 value =
            qint16(8000.0 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
        clip->samples[i * 2] = value;
        clip->samples[i * 2 + 1] = value;
    }
    clip->peaks.resize(1000);
    project.addAudio(QStringLiteral("/tmp/pvf_export_tone.wav"), clip,
                     QStringLiteral("tone"));

    pvf::ExportOptions options;
    options.outputPath = path;
    options.size = QSize(320, 180);
    options.fps = 24;
    options.frameCount = project.timelineLength();
    options.crf = 23;
    options.includeAudio = true;

    // The dialog must resolve the same frame count, project size and active
    // camera that the exporter expects.
    pvf::Entity* camera = project.addCamera(QStringLiteral("Camera"));
    project.activeCamera = camera->id;
    {
        pvf::ExportDialog dialog(project);
        const pvf::ExportOptions fromDialog = dialog.options();
        if (fromDialog.frameCount != project.timelineLength()
            || fromDialog.cameraId != camera->id
            || fromDialog.size != QSize(project.width, project.height)
            || !fromDialog.outputPath.endsWith(QStringLiteral(".mp4")))
        {
            qCritical() << "export-test: dialog options" << fromDialog.frameCount
                        << fromDialog.cameraId << fromDialog.size
                        << fromDialog.outputPath;
            ++failures;
        }
        auto* width =
            dialog.findChild<QSpinBox*>(QStringLiteral("exportWidth"));
        auto* height =
            dialog.findChild<QSpinBox*>(QStringLiteral("exportHeight"));
        if (!width || !height)
        {
            qCritical() << "export-test: dialog size fields missing";
            ++failures;
        }
        else
        {
            width->setValue(640);
            if (height->value() != 360 || dialog.options().size != QSize(640, 360))
            {
                qCritical() << "export-test: aspect lock" << width->value()
                            << height->value() << dialog.options().size;
                ++failures;
            }
        }
        auto* encoder =
            dialog.findChild<QComboBox*>(QStringLiteral("exportEncoder"));
        if (!encoder || encoder->count() < 1
            || (pvf::VideoExporter::vaapiAvailable()
                && encoder->findData(1) < 0))
        {
            qCritical() << "export-test: encoder choices"
                        << (encoder ? encoder->count() : 0);
            ++failures;
        }
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(
            QStringLiteral("/tmp/pvf_export_dialog.png"));
        dialog.hide();
    }

    pvf::VideoPlayer videoPlayer;
    auto render = [&project, &videoPlayer](int frame) {
        videoPlayer.update(project, frame, false);
        pvf::Camera::State state;
        state.scale = 1.0;
        state.size = QSizeF(project.width, project.height);
        return pvf::CameraRenderer::renderCameraImage(
            project, frame, state, QSize(320, 180), false);
    };
    auto provider = [&render](int frame, pvf::VideoExporter::FrameDone done) {
        done(render(frame));
    };
    pvf::VideoExporter exporter;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool exportOk = false;
    QString exportError;
    int previews = 0;
    QSize previewSize;
    QObject::connect(&exporter, &pvf::VideoExporter::previewFrame, &loop,
                     [&](const QImage& image) {
                         ++previews;
                         previewSize = image.size();
                     });
    QObject::connect(&exporter, &pvf::VideoExporter::finished, &loop,
                     [&](bool success, const QString& error) {
                         exportOk = success;
                         exportError = error;
                         loop.quit();
                     });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QString error;
    if (!exporter.start(project, options, provider, &error))
    {
        qCritical() << "export-test: start failed" << error;
        return 1;
    }
    timeout.start(60000);
    loop.exec();
    if (!exportOk || exporter.isRunning())
    {
        exporter.cancel();
        qCritical() << "export-test: export failed" << exportError;
        return 1;
    }

    QString decodeError;
    double duration = 0.0;
    const QImage first = pvf::decodeVideoFirstFrame(path, &decodeError, &duration);
    if (first.isNull() || duration < 0.9 || duration > 1.1)
    {
        qCritical() << "export-test: video" << decodeError << duration;
        ++failures;
    }
    else
    {
        first.save(QStringLiteral("/tmp/pvf_export_first.png"));
        if (first.size() != QSize(320, 180))
        {
            qCritical() << "export-test: size" << first.size();
            ++failures;
        }
        const QColor left = first.pixelColor(80, 90);
        if (!(left.red() > 150 && left.green() < 100))
        {
            qCritical() << "export-test: first frame colour" << left;
            ++failures;
        }
        const QColor background = first.pixelColor(300, 20);
        if (!(background.blue() > background.red() && background.red() < 100))
        {
            qCritical() << "export-test: background colour" << background;
            ++failures;
        }
        const QColor videoPixel = first.pixelColor(160, 90);
        if (!(videoPixel.green() > 120 && videoPixel.red() < 100
              && videoPixel.blue() < 100))
        {
            qCritical() << "export-test: video frame colour" << videoPixel;
            ++failures;
        }
    }

    const pvf::AudioDecodeResult audio = pvf::decodeAudioFile(path);
    if (!audio.clip || audio.clip->duration < 0.8)
    {
        qCritical() << "export-test: audio track" << audio.error;
        ++failures;
    }
    else
    {
        float peak = 0.0f;
        for (float value : audio.clip->peaks)
            peak = std::max(peak, value);
        if (peak < 0.1f)
        {
            qCritical() << "export-test: exported audio is silent" << peak;
            ++failures;
        }
    }

    if (previews <= 0 || previewSize != QSize(320, 180))
    {
        qCritical() << "export-test: preview frames" << previews << previewSize;
        ++failures;
    }

    // The preview window must show the rendered frame, progress, speed and a
    // remaining-time estimate.
    {
        pvf::ExportProgressDialog previewDialog;
        previewDialog.resize(600, 420);
        previewDialog.setPreview(first);
        previewDialog.setProgress(6, 24);
        previewDialog.show();
        QApplication::processEvents();
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < 300)
            QApplication::processEvents();
        previewDialog.setProgress(12, 24);
        QApplication::processEvents();
        previewDialog.grab().toImage().save(
            QStringLiteral("/tmp/pvf_export_progress.png"));
        auto* label =
            previewDialog.findChild<QLabel*>(QStringLiteral("exportPreview"));
        auto* status =
            previewDialog.findChild<QLabel*>(QStringLiteral("exportStatus"));
        if (!label || label->pixmap().isNull() || !status
            || !status->text().contains(QStringLiteral("fps"))
            || !status->text().contains(QStringLiteral("剩余")))
        {
            qCritical() << "export-test: preview dialog"
                        << (label ? !label->pixmap().isNull() : false)
                        << (status ? status->text() : QString());
            ++failures;
        }
        previewDialog.finish();
    }

    // VAAPI runs on the GPU when the machine exposes a render node.
    if (pvf::VideoExporter::vaapiAvailable())
    {
        const QString vaapiPath = QStringLiteral("/tmp/pvf_export_vaapi.mp4");
        QFile::remove(vaapiPath);
        pvf::VideoExporter vaapi;
        pvf::ExportOptions vaapiOptions = options;
        vaapiOptions.outputPath = vaapiPath;
        vaapiOptions.includeAudio = false;
        vaapiOptions.encoder = pvf::ExportEncoder::H264Vaapi;
        vaapiOptions.frameCount = 8;

        QEventLoop vaapiLoop;
        QTimer vaapiTimeout;
        vaapiTimeout.setSingleShot(true);
        bool vaapiOk = false;
        QString vaapiError;
        QObject::connect(&vaapi, &pvf::VideoExporter::finished, &vaapiLoop,
                         [&](bool success, const QString& message) {
                             vaapiOk = success;
                             vaapiError = message;
                             vaapiLoop.quit();
                         });
        QObject::connect(&vaapiTimeout, &QTimer::timeout, &vaapiLoop,
                         &QEventLoop::quit);
        if (!vaapi.start(project, vaapiOptions, provider, &error))
        {
            qCritical() << "export-test: vaapi start failed" << error;
            ++failures;
        }
        else
        {
            vaapiTimeout.start(60000);
            vaapiLoop.exec();
            QString vaapiDecodeError;
            const QImage vaapiFirst =
                pvf::decodeVideoFirstFrame(vaapiPath, &vaapiDecodeError);
            if (!vaapiOk || vaapi.isRunning() || vaapiFirst.isNull()
                || vaapiFirst.size() != QSize(320, 180))
            {
                qCritical() << "export-test: vaapi" << vaapiOk << vaapiError
                            << vaapiDecodeError << vaapiFirst.size();
                ++failures;
            }
            vaapi.cancel();
            QFile::remove(vaapiPath);
        }
        qInfo() << "export-test: vaapi export checked";
    }
    else
    {
        qInfo() << "export-test: vaapi unavailable, skipped";
    }

    // Cancelling must not leave a partial file behind.
    const QString cancelled = QStringLiteral("/tmp/pvf_export_cancel.mp4");
    QFile::remove(cancelled);
    pvf::VideoExporter cancelling;
    pvf::ExportOptions cancelOptions = options;
    cancelOptions.outputPath = cancelled;
    cancelOptions.frameCount = project.fps * 30;
    QEventLoop cancelLoop;
    QTimer cancelTimeout;
    cancelTimeout.setSingleShot(true);
    bool cancelReported = false;
    bool cancelSucceeded = false;
    QObject::connect(&cancelling, &pvf::VideoExporter::finished, &cancelLoop,
                     [&](bool success, const QString&) {
                         cancelReported = true;
                         cancelSucceeded = success;
                         cancelLoop.quit();
                     });
    QObject::connect(&cancelTimeout, &QTimer::timeout, &cancelLoop,
                     &QEventLoop::quit);
    if (!cancelling.start(project, cancelOptions, provider, &error))
    {
        qCritical() << "export-test: cancel start failed" << error;
        ++failures;
    }
    else
    {
        QTimer::singleShot(120, &cancelling, [&cancelling] { cancelling.cancel(); });
        cancelTimeout.start(15000);
        cancelLoop.exec();
        if (!cancelReported || cancelSucceeded || QFile::exists(cancelled))
        {
            qCritical() << "export-test: cancel state" << cancelReported
                        << cancelSucceeded << QFile::exists(cancelled);
            cancelling.cancel();
            QFile::remove(cancelled);
            ++failures;
        }
    }

    QFile::remove(path);
    QFile::remove(videoSource);
    qInfo() << "export-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

struct McpHttpResponse
{
    int status = -1;
    int expectedBody = 0;
    QByteArray headers;
    QByteArray body;
    bool timedOut = false;
};

McpHttpResponse mcpExchange(quint16 port, const QByteArray& request,
                            int timeoutMs = 5000)
{
    McpHttpResponse response;
    QTcpSocket socket;
    QByteArray received;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&timeout, &QTimer::timeout, &loop, [&response, &loop] {
        response.timedOut = true;
        loop.quit();
    });
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] {
        socket.write(request);
        socket.flush();
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] {
        received += socket.readAll();
        const int headerEnd = received.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        if (response.status < 0)
        {
            const QByteArray head = received.left(headerEnd);
            response.headers = head;
            const QList<QByteArray> statusLine =
                head.split('\n').first().trimmed().split(' ');
            if (statusLine.size() >= 2)
                response.status = statusLine.at(1).toInt();
            for (const QByteArray& rawLine : head.split('\n'))
            {
                QByteArray line = rawLine;
                if (line.endsWith('\r'))
                    line.chop(1);
                const int colon = line.indexOf(':');
                if (colon > 0
                    && line.left(colon).trimmed().toLower()
                           == QByteArrayLiteral("content-length"))
                    response.expectedBody =
                        line.mid(colon + 1).trimmed().toInt();
            }
        }
        if (received.size() >= headerEnd + 4 + response.expectedBody)
            loop.quit();
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop,
                     [&loop](QAbstractSocket::SocketError) { loop.quit(); });

    timeout.start(timeoutMs);
    socket.connectToHost(QHostAddress::LocalHost, port);
    loop.exec();

    const int headerEnd = received.indexOf("\r\n\r\n");
    if (headerEnd >= 0)
        response.body = received.mid(headerEnd + 4);
    socket.abort();
    return response;
}

QByteArray mcpPostRequest(quint16 port, const QByteArray& payload,
                          const QByteArray& token, const QByteArray& contentType,
                          qint64 declaredLength, const QByteArray& extraHeaders,
                          const QByteArray& hostHeader)
{
    QByteArray request;
    request += "POST /mcp HTTP/1.1\r\n";
    request += "Host: "
               + (hostHeader.isEmpty()
                      ? QByteArrayLiteral("127.0.0.1:")
                            + QByteArray::number(port)
                      : hostHeader)
               + "\r\n";
    request += "Authorization: Bearer " + token + "\r\n";
    request += "Content-Type: " + contentType + "\r\n";
    request += "Accept: application/json\r\n";
    request += "Content-Length: "
               + QByteArray::number(declaredLength >= 0 ? declaredLength
                                                        : qint64(payload.size()))
               + "\r\n";
    request += extraHeaders;
    request += "Connection: close\r\n\r\n";
    request += payload;
    return request;
}

McpHttpResponse mcpHttpPost(quint16 port, const QByteArray& payload,
                            const QByteArray& token,
                            const QByteArray& contentType =
                                QByteArrayLiteral("application/json"),
                            qint64 declaredLength = -1,
                            const QByteArray& extraHeaders = QByteArray(),
                            const QByteArray& hostHeader = QByteArray())
{
    return mcpExchange(port,
                       mcpPostRequest(port, payload, token, contentType,
                                      declaredLength, extraHeaders, hostHeader));
}

bool mcpWaitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs)
    {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

int mcpStatusFromRaw(const QByteArray& received)
{
    const int headerEnd = received.indexOf("\r\n\r\n");
    const QByteArray head =
        headerEnd >= 0 ? received.left(headerEnd) : received;
    const QList<QByteArray> statusLine =
        head.split('\n').first().trimmed().split(' ');
    return statusLine.size() >= 2 ? statusLine.at(1).toInt() : -1;
}

QJsonObject mcpRpc(quint16 port, const QByteArray& token, const QJsonObject& request,
                   int* statusOut = nullptr)
{
    const McpHttpResponse response = mcpHttpPost(
        port, QJsonDocument(request).toJson(QJsonDocument::Compact), token);
    if (statusOut)
        *statusOut = response.status;
    return QJsonDocument::fromJson(response.body).object();
}

int mcpReadonlyTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-readonly-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP 测试工程");
    project->width = 1280;
    project->height = 720;
    project->fps = 24;
    pvf::Entity* folder = project->addFolder(QStringLiteral("组"));
    pvf::Entity* child = project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    child->name = QStringLiteral("移动矩形");
    const pvf::EntityId childId = child->id;
    project->setEntityParent(childId, folder->id);
    project->setValue(childId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(childId, QStringLiteral("x"), 48, 480.0, true);
    pvf::Entity* camera = project->addCamera(QStringLiteral("主摄像机"));
    project->activeCamera = camera->id;
    const int entityCount = project->entities().size();

    pvf::McpServer server(project);
    server.setToken(QByteArrayLiteral("test-token"));
    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-endpoint-XXXXXX"));
    server.setEndpointFilePath(endpointTemp.path()
                               + QStringLiteral("/mcp-endpoint.json"));

    int signalled = 0;
    int signalledFailures = 0;
    QString lastSignalName;
    QString lastArgsJson;
    QString lastResultJson;
    qint64 lastDuration = -1;
    bool lastSignalOk = false;
    QObject::connect(
        &server, &pvf::McpServer::toolInvoked,
        [&](const QString& name, const QString& argsJson, const QString& resultJson,
            qint64 durationMs, bool ok) {
            ++signalled;
            if (!ok)
                ++signalledFailures;
            lastSignalName = name;
            lastArgsJson = argsJson;
            lastResultJson = resultJson;
            lastDuration = durationMs;
            lastSignalOk = ok;
        });

    QString error;
    const quint16 startedPort = server.start(0, &error);
    if (startedPort == 0)
    {
        qCritical() << "mcp-readonly-test: start failed" << error;
        delete project;
        return 1;
    }
    failIf(server.port() != startedPort,
           QStringLiteral("start() port and port() disagree"));
    failIf(server.port() == 0, QStringLiteral("start(0) did not report a port"));
    failIf(!server.isRunning(), QStringLiteral("server is not running after start"));
    failIf(server.endpointUrl().isEmpty(), QStringLiteral("endpoint URL is empty"));

    const quint16 port = startedPort;
    const QByteArray token = server.token();
    failIf(token.isEmpty(), QStringLiteral("token is empty"));

    // initialize
    QJsonObject initParams;
    initParams[QStringLiteral("protocolVersion")] = QStringLiteral("2025-06-18");
    initParams[QStringLiteral("capabilities")] = QJsonObject();
    initParams[QStringLiteral("clientInfo")] =
        QJsonObject{{QStringLiteral("name"), QStringLiteral("test-client")},
                    {QStringLiteral("version"), QStringLiteral("0.0")}};
    QJsonObject initRequest;
    initRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    initRequest[QStringLiteral("id")] = 1;
    initRequest[QStringLiteral("method")] = QStringLiteral("initialize");
    initRequest[QStringLiteral("params")] = initParams;

    const McpHttpResponse initHttp = mcpHttpPost(
        port, QJsonDocument(initRequest).toJson(QJsonDocument::Compact), token);
    failIf(initHttp.status != 200,
           QStringLiteral("initialize HTTP status %1").arg(initHttp.status));
    failIf(!initHttp.headers.toLower().contains(
               QByteArrayLiteral("content-type: application/json")),
           QStringLiteral("initialize response is not application/json"));
    const QJsonObject initResult =
        QJsonDocument::fromJson(initHttp.body).object().value(QStringLiteral("result"))
            .toObject();
    failIf(initResult.value(QStringLiteral("protocolVersion")).toString().isEmpty(),
           QStringLiteral("initialize did not return protocolVersion"));
    const QJsonObject capabilities =
        initResult.value(QStringLiteral("capabilities")).toObject();
    failIf(!capabilities.contains(QStringLiteral("tools")),
           QStringLiteral("initialize missing tools capability"));
    failIf(!capabilities.contains(QStringLiteral("resources")),
           QStringLiteral("initialize missing resources capability"));
    failIf(initResult.value(QStringLiteral("serverInfo"))
                   .toObject()
                   .value(QStringLiteral("name"))
                   .toString()
               != QStringLiteral("PVForge"),
           QStringLiteral("initialize serverInfo.name mismatch"));

    // tools/list
    QJsonObject listRequest;
    listRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    listRequest[QStringLiteral("id")] = 2;
    listRequest[QStringLiteral("method")] = QStringLiteral("tools/list");
    const QJsonObject listResponse = mcpRpc(port, token, listRequest);
    const QJsonArray tools =
        listResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("tools"))
            .toArray();
    QStringList toolNames;
    for (const QJsonValue& value : tools)
        toolNames.append(value.toObject().value(QStringLiteral("name")).toString());
    for (const QString& required :
         {QStringLiteral("project.summary"), QStringLiteral("timeline.get"),
          QStringLiteral("entity.get"), QStringLiteral("assets.list"),
          QStringLiteral("guide.get")})
        failIf(!toolNames.contains(required),
               QStringLiteral("tools/list missing %1").arg(required));
    failIf(toolNames.contains(QStringLiteral("entity.create")),
           QStringLiteral("tools/list advertises a write tool"));

    const auto callTool = [&](int id, const QString& name, const QJsonObject& args,
                              int* statusOut = nullptr) {
        QJsonObject params;
        params[QStringLiteral("name")] = name;
        params[QStringLiteral("arguments")] = args;
        QJsonObject request;
        request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        request[QStringLiteral("id")] = id;
        request[QStringLiteral("method")] = QStringLiteral("tools/call");
        request[QStringLiteral("params")] = params;
        return mcpRpc(port, token, request, statusOut);
    };

    // project.summary
    int status = 0;
    const QJsonObject summaryResponse =
        callTool(3, QStringLiteral("project.summary"), QJsonObject(), &status);
    failIf(status != 200, QStringLiteral("project.summary HTTP %1").arg(status));
    const QJsonObject summaryResult =
        summaryResponse.value(QStringLiteral("result")).toObject();
    failIf(summaryResult.value(QStringLiteral("isError")).toBool(),
           QStringLiteral("project.summary returned isError"));
    const QJsonObject summaryStructured =
        summaryResult.value(QStringLiteral("structuredContent")).toObject();
    failIf(!summaryStructured.value(QStringLiteral("ok")).toBool(),
           QStringLiteral("project.summary structured ok is false"));
    const QJsonObject summaryData =
        summaryStructured.value(QStringLiteral("data")).toObject();
    failIf(summaryData.value(QStringLiteral("width")).toInt() != 1280,
           QStringLiteral("project.summary width mismatch"));
    failIf(summaryData.value(QStringLiteral("height")).toInt() != 720,
           QStringLiteral("project.summary height mismatch"));
    failIf(summaryData.value(QStringLiteral("fps")).toInt() != 24,
           QStringLiteral("project.summary fps mismatch"));
    failIf(summaryData.value(QStringLiteral("durationFrames")).toInt()
               != project->timelineLength(),
           QStringLiteral("project.summary duration mismatch"));
    failIf(summaryData.value(QStringLiteral("activeCamera"))
                   .toObject()
                   .value(QStringLiteral("id"))
                   .toInt()
               != camera->id,
           QStringLiteral("project.summary active camera mismatch"));
    failIf(summaryData.value(QStringLiteral("scenesAvailable")).toBool(),
           QStringLiteral("project.summary pretends scenes exist"));
    failIf(summaryResult.value(QStringLiteral("content")).toArray().isEmpty(),
           QStringLiteral("project.summary content is empty"));

    // timeline.get
    QJsonObject timelineArgs;
    timelineArgs[QStringLiteral("limit")] = 10;
    const QJsonObject timelineResponse =
        callTool(4, QStringLiteral("timeline.get"), timelineArgs);
    const QJsonObject timelineStructured =
        timelineResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject();
    const QJsonObject timelineData =
        timelineStructured.value(QStringLiteral("data")).toObject();
    const QJsonArray timelineEntities =
        timelineData.value(QStringLiteral("entities")).toArray();
    failIf(timelineEntities.size() != entityCount,
           QStringLiteral("timeline.get entity count %1 != %2")
               .arg(timelineEntities.size())
               .arg(entityCount));
    failIf(timelineStructured.value(QStringLiteral("page"))
                   .toObject()
                   .value(QStringLiteral("total"))
                   .toInt()
               != entityCount,
           QStringLiteral("timeline.get page total mismatch"));
    if (!timelineEntities.isEmpty())
    {
        const QJsonObject first = timelineEntities.first().toObject();
        failIf(!first.contains(QStringLiteral("tracks")),
               QStringLiteral("timeline.get omitted tracks"));
    }

    bool foundChild = false;
    for (const QJsonValue& value : timelineEntities)
    {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("id")).toInt() != childId)
            continue;
        foundChild = true;
        const QJsonObject tracks = object.value(QStringLiteral("tracks")).toObject();
        failIf(tracks.value(QStringLiteral("x")).toArray().size() != 2,
               QStringLiteral("timeline.get x track key count mismatch"));
    }
    failIf(!foundChild, QStringLiteral("timeline.get did not include the child"));

    // entity.get
    QJsonObject entityArgs;
    entityArgs[QStringLiteral("id")] = childId;
    entityArgs[QStringLiteral("frame")] = 24;
    const QJsonObject entityResponse =
        callTool(5, QStringLiteral("entity.get"), entityArgs);
    const QJsonObject entityStructured =
        entityResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject();
    const QJsonObject entityData =
        entityStructured.value(QStringLiteral("data")).toObject();
    failIf(entityData.value(QStringLiteral("name")).toString()
               != QStringLiteral("移动矩形"),
           QStringLiteral("entity.get name mismatch"));
    failIf(entityData.value(QStringLiteral("tracks"))
                   .toObject()
                   .value(QStringLiteral("x"))
                   .toArray()
                   .size()
               != 2,
           QStringLiteral("entity.get track mismatch"));
    failIf(std::abs(entityData.value(QStringLiteral("evaluated"))
                            .toObject()
                            .value(QStringLiteral("x"))
                            .toDouble()
                        - 240.0)
               > 1e-6,
           QStringLiteral("entity.get x at frame 24 is not 240"));

    // Write tools are rejected, and nothing changes.
    const QJsonObject writeResponse = callTool(
        6, QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}});
    const QJsonObject writeResult =
        writeResponse.value(QStringLiteral("result")).toObject();
    failIf(!writeResult.value(QStringLiteral("isError")).toBool(),
           QStringLiteral("entity.create was not rejected"));
    failIf(writeResult.value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("permission_denied"),
           QStringLiteral("entity.create did not return permission_denied"));
    failIf(project->entities().size() != entityCount,
           QStringLiteral("rejected write tool changed the project"));

    // Missing required id -> invalid_argument.
    const QJsonObject invalidResponse =
        callTool(7, QStringLiteral("entity.get"), QJsonObject());
    const QJsonObject invalidResult =
        invalidResponse.value(QStringLiteral("result")).toObject();
    failIf(invalidResult.value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("invalid_argument"),
           QStringLiteral("entity.get without id did not return invalid_argument"));

    // Deferred scene tools -> not_available.
    const QJsonObject sceneResponse =
        callTool(8, QStringLiteral("scene.list"), QJsonObject());
    failIf(sceneResponse.value(QStringLiteral("result"))
                   .toObject()
                   .value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("not_available"),
           QStringLiteral("scene.list did not return not_available"));

    // guide.get
    const QJsonObject guideResponse =
        callTool(9, QStringLiteral("guide.get"), QJsonObject());
    const QString guideText =
        guideResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject()
            .value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("markdown"))
            .toString();
    failIf(guideText.isEmpty(), QStringLiteral("guide.get returned empty text"));

    // resources/list + resources/read
    QJsonObject resourcesRequest;
    resourcesRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    resourcesRequest[QStringLiteral("id")] = 10;
    resourcesRequest[QStringLiteral("method")] = QStringLiteral("resources/list");
    const QJsonObject resourcesResponse = mcpRpc(port, token, resourcesRequest);
    const QJsonArray resources =
        resourcesResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("resources"))
            .toArray();
    QStringList resourceUris;
    for (const QJsonValue& value : resources)
        resourceUris.append(value.toObject().value(QStringLiteral("uri")).toString());
    failIf(!resourceUris.contains(QStringLiteral("pvforge://guide/authoring")),
           QStringLiteral("resources/list missing authoring guide"));
    failIf(!resourceUris.contains(QStringLiteral("pvforge://project/schema")),
           QStringLiteral("resources/list missing project schema"));

    QJsonObject readParams;
    readParams[QStringLiteral("uri")] = QStringLiteral("pvforge://guide/authoring");
    QJsonObject readRequest;
    readRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    readRequest[QStringLiteral("id")] = 11;
    readRequest[QStringLiteral("method")] = QStringLiteral("resources/read");
    readRequest[QStringLiteral("params")] = readParams;
    const QJsonObject readResponse = mcpRpc(port, token, readRequest);
    const QJsonArray contents =
        readResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("contents"))
            .toArray();
    failIf(contents.isEmpty()
               || contents.first()
                      .toObject()
                      .value(QStringLiteral("text"))
                      .toString()
                      .isEmpty(),
           QStringLiteral("resources/read returned no text"));

    // ping
    QJsonObject pingRequest;
    pingRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    pingRequest[QStringLiteral("id")] = 12;
    pingRequest[QStringLiteral("method")] = QStringLiteral("ping");
    const QJsonObject pingResponse = mcpRpc(port, token, pingRequest);
    failIf(!pingResponse.value(QStringLiteral("result")).isObject(),
           QStringLiteral("ping did not return an object result"));
    failIf(!pingResponse.value(QStringLiteral("result")).toObject().isEmpty(),
           QStringLiteral("ping result is not empty"));

    // Notifications are accepted with 202 and no body.
    QJsonObject noteRequest;
    noteRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    noteRequest[QStringLiteral("method")] = QStringLiteral("notifications/cancelled");
    noteRequest[QStringLiteral("params")] =
        QJsonObject{{QStringLiteral("requestId"), 99},
                    {QStringLiteral("reason"), QStringLiteral("test")}};
    const McpHttpResponse noteResponse = mcpHttpPost(
        port, QJsonDocument(noteRequest).toJson(QJsonDocument::Compact), token);
    failIf(noteResponse.status != 202,
           QStringLiteral("notification HTTP %1 != 202").arg(noteResponse.status));
    failIf(!noteResponse.body.isEmpty(),
           QStringLiteral("notification returned a body"));

    // Authentication and transport rejection.
    const McpHttpResponse badToken = mcpHttpPost(
        port, QJsonDocument(initRequest).toJson(QJsonDocument::Compact),
        QByteArrayLiteral("wrong-token"));
    failIf(badToken.status != 401,
           QStringLiteral("bad token HTTP %1 != 401").arg(badToken.status));
    failIf(!badToken.headers.toLower().contains(QByteArrayLiteral("www-authenticate")),
           QStringLiteral("401 response lacks WWW-Authenticate"));

    QJsonObject simpleRequest;
    simpleRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    simpleRequest[QStringLiteral("id")] = 13;
    simpleRequest[QStringLiteral("method")] = QStringLiteral("ping");
    const McpHttpResponse wrongType = mcpHttpPost(
        port, QJsonDocument(simpleRequest).toJson(QJsonDocument::Compact), token,
        QByteArrayLiteral("text/plain"));
    failIf(wrongType.status != 415,
           QStringLiteral("wrong content type HTTP %1 != 415").arg(wrongType.status));

    const McpHttpResponse tooLarge = mcpHttpPost(
        port, QByteArrayLiteral("{}"), token, QByteArrayLiteral("application/json"),
        server.maxBodyBytes() + 1);
    failIf(tooLarge.status != 413,
           QStringLiteral("oversized body HTTP %1 != 413").arg(tooLarge.status));

    const McpHttpResponse malformed =
        mcpHttpPost(port, QByteArrayLiteral("{"), token);
    failIf(malformed.status != 200,
           QStringLiteral("malformed JSON HTTP %1 != 200").arg(malformed.status));
    const QJsonObject malformedResponse =
        QJsonDocument::fromJson(malformed.body).object();
    failIf(malformedResponse.value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toInt()
               != -32700,
           QStringLiteral("malformed JSON did not return -32700"));

    // Every tools/call emitted one log signal with the required fields.
    failIf(signalled < 7,
           QStringLiteral("toolInvoked emitted %1 times, expected at least 7")
               .arg(signalled));
    failIf(signalledFailures < 3,
           QStringLiteral("toolInvoked did not report the rejected calls"));
    failIf(lastSignalName != QStringLiteral("guide.get"),
           QStringLiteral("toolInvoked last name is %1").arg(lastSignalName));
    failIf(lastArgsJson.isEmpty() || lastResultJson.isEmpty(),
           QStringLiteral("toolInvoked JSON payloads are empty"));
    failIf(lastDuration < 0, QStringLiteral("toolInvoked duration is negative"));
    failIf(!lastSignalOk, QStringLiteral("toolInvoked last call is not ok"));

    server.stop();
    failIf(server.isRunning(), QStringLiteral("server still running after stop"));
    failIf(server.port() != 0, QStringLiteral("port not cleared after stop"));

    delete project;
    qInfo() << "mcp-readonly-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpHttpTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-http-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP HTTP 测试工程");
    project->width = 640;
    project->height = 360;
    project->fps = 24;

    pvf::McpServer server(project);
    server.setToken(QByteArrayLiteral("http-test-token"));
    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-http-endpoint-XXXXXX"));
    server.setEndpointFilePath(endpointTemp.path()
                               + QStringLiteral("/server.json"));

    const QByteArray pingBody =
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");

    QString error;
    const quint16 port = server.start(0, &error);
    if (port == 0)
    {
        qCritical() << "mcp-http-test: start failed" << error;
        delete project;
        return 1;
    }

    const QByteArray token = server.token();
    const QByteArray host =
        QByteArrayLiteral("127.0.0.1:") + QByteArray::number(port);
    const QByteArray localhostHost =
        QByteArrayLiteral("localhost:") + QByteArray::number(port);
    const QByteArray allowedOrigin = QByteArrayLiteral("http://") + host;

    failIf(server.endpointUrl().contains(QString::fromLatin1(token)),
           QStringLiteral("endpointUrl leaks the token"));

    // Correct Host + Bearer token works and never grants CORS.
    const McpHttpResponse normal = mcpHttpPost(port, pingBody, token);
    failIf(normal.status != 200,
           QStringLiteral("normal request HTTP %1 != 200").arg(normal.status));
    failIf(normal.headers.toLower().contains(
               QByteArrayLiteral("access-control-allow-origin")),
           QStringLiteral("normal response contains Access-Control-Allow-Origin"));

    const McpHttpResponse localhost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), localhostHost);
    failIf(localhost.status != 200,
           QStringLiteral("localhost Host HTTP %1 != 200").arg(localhost.status));

    const McpHttpResponse upperLocalhost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), localhostHost.toUpper());
    failIf(upperLocalhost.status != 200,
           QStringLiteral("uppercase localhost Host HTTP %1 != 200")
               .arg(upperLocalhost.status));

    // DNS rebinding: foreign or mismatched Host is rejected.
    const McpHttpResponse evilHost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), QByteArrayLiteral("evil.com"));
    failIf(evilHost.status != 403,
           QStringLiteral("evil Host HTTP %1 != 403").arg(evilHost.status));
    failIf(evilHost.body.contains(token),
           QStringLiteral("rejected Host response leaks the token"));
    failIf(evilHost.body.contains(QByteArrayLiteral("evil.com")),
           QStringLiteral("rejected Host response echoes the Host header"));

    const quint16 otherPort = port == 65535 ? quint16(port - 1)
                                            : quint16(port + 1);
    const McpHttpResponse wrongPortHost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(),
        QByteArrayLiteral("127.0.0.1:") + QByteArray::number(otherPort));
    failIf(wrongPortHost.status != 403,
           QStringLiteral("mismatched Host port HTTP %1 != 403")
               .arg(wrongPortHost.status));

    const QByteArray noHost =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nAuthorization: Bearer ")
        + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: ")
        + QByteArray::number(pingBody.size()) + QByteArrayLiteral("\r\n\r\n")
        + pingBody;
    failIf(mcpExchange(port, noHost).status != 403,
           QStringLiteral("missing Host was not rejected with 403"));

    // Origin rules: foreign and opaque origins are rejected, loopback is
    // accepted, and clients without Origin (non-browser) keep working.
    const McpHttpResponse evilOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: http://evil.com\r\n"));
    failIf(evilOrigin.status != 403,
           QStringLiteral("evil Origin HTTP %1 != 403").arg(evilOrigin.status));
    failIf(evilOrigin.body.contains(QByteArrayLiteral("evil.com")),
           QStringLiteral("rejected Origin response echoes the Origin header"));
    failIf(evilOrigin.body.contains(token),
           QStringLiteral("rejected Origin response leaks the token"));

    const McpHttpResponse nullOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: null\r\n"));
    failIf(nullOrigin.status != 403,
           QStringLiteral("Origin: null HTTP %1 != 403").arg(nullOrigin.status));

    const McpHttpResponse httpsOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: https://127.0.0.1:") + QByteArray::number(port)
            + QByteArrayLiteral("\r\n"));
    failIf(httpsOrigin.status != 403,
           QStringLiteral("https Origin HTTP %1 != 403").arg(httpsOrigin.status));

    const McpHttpResponse loopbackOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: ") + allowedOrigin + QByteArrayLiteral("\r\n"));
    failIf(loopbackOrigin.status != 200,
           QStringLiteral("loopback Origin HTTP %1 != 200")
               .arg(loopbackOrigin.status));

    const McpHttpResponse localhostOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: http://") + localhostHost
            + QByteArrayLiteral("\r\n"));
    failIf(localhostOrigin.status != 200,
           QStringLiteral("localhost Origin HTTP %1 != 200")
               .arg(localhostOrigin.status));

    const McpHttpResponse crossSite = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Sec-Fetch-Site: cross-site\r\n"));
    failIf(crossSite.status != 403,
           QStringLiteral("cross-site Sec-Fetch-Site HTTP %1 != 403")
               .arg(crossSite.status));

    // Preflight is refused and never answered with CORS headers.
    const QByteArray optionsRequest =
        QByteArrayLiteral("OPTIONS /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAccess-Control-Request-Method: POST\r\n"
                            "Content-Length: 0\r\n\r\n");
    const McpHttpResponse options = mcpExchange(port, optionsRequest);
    failIf(options.status != 405,
           QStringLiteral("OPTIONS HTTP %1 != 405").arg(options.status));
    failIf(options.headers.toLower().contains(
               QByteArrayLiteral("access-control-allow-origin")),
           QStringLiteral("OPTIONS response contains Access-Control-Allow-Origin"));

    // Request framing and size limits.
    const QByteArray oversizedHeader =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nX-Filler: ") + QByteArray(17000, 'a')
        + QByteArrayLiteral("\r\n\r\n");
    const McpHttpResponse bigHeader = mcpExchange(port, oversizedHeader);
    failIf(bigHeader.status != 431,
           QStringLiteral("oversized header HTTP %1 != 431")
               .arg(bigHeader.status));

    const QByteArray duplicateLength =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: 2\r\nContent-Length: 3\r\n\r\n{}");
    const McpHttpResponse doubleLength = mcpExchange(port, duplicateLength);
    failIf(doubleLength.status != 400,
           QStringLiteral("duplicate Content-Length HTTP %1 != 400")
               .arg(doubleLength.status));

    const QByteArray bothFramings =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "Content-Length: 2\r\n\r\n{}");
    const McpHttpResponse smuggling = mcpExchange(port, bothFramings);
    failIf(smuggling.status != 400,
           QStringLiteral("Content-Length + Transfer-Encoding HTTP %1 != 400")
               .arg(smuggling.status));

    const QByteArray chunkedOnly =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    failIf(mcpExchange(port, chunkedOnly).status != 400,
           QStringLiteral("chunked body was not rejected with 400"));

    const QByteArray negativeLength =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: -1\r\n\r\n");
    failIf(mcpExchange(port, negativeLength).status != 400,
           QStringLiteral("negative Content-Length was not rejected with 400"));

    const McpHttpResponse tooLarge = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"),
        server.maxBodyBytes() + 1);
    failIf(tooLarge.status != 413,
           QStringLiteral("oversized body HTTP %1 != 413").arg(tooLarge.status));

    // Authentication failures never echo either token.
    const McpHttpResponse badToken =
        mcpHttpPost(port, pingBody, QByteArrayLiteral("wrong-secret-token"));
    failIf(badToken.status != 401,
           QStringLiteral("bad token HTTP %1 != 401").arg(badToken.status));
    failIf(badToken.body.contains(QByteArrayLiteral("wrong-secret-token")),
           QStringLiteral("401 response echoes the submitted token"));
    failIf(badToken.body.contains(token),
           QStringLiteral("401 response leaks the configured token"));

    // An empty token must never leave the server unauthenticated.
    pvf::McpServer generated(project);
    generated.setToken(QByteArray());
    failIf(generated.token().isEmpty(),
           QStringLiteral("empty token was not replaced with a random token"));
    failIf(generated.token().size() < 32,
           QStringLiteral("generated token is too short"));

    // Connection cap: the third concurrent connection is refused.
    pvf::McpServer limited(project);
    limited.setToken(QByteArrayLiteral("limit-token"));
    limited.setEndpointFilePath(endpointTemp.path()
                                + QStringLiteral("/limited.json"));
    pvf::McpServer::HttpLimits limits = limited.httpLimits();
    limits.maxConnections = 2;
    limits.headerTimeoutMs = 5000;
    limits.requestTimeoutMs = 5000;
    limited.setHttpLimits(limits);
    const quint16 limitedPort = limited.start(0, &error);
    failIf(limitedPort == 0,
           QStringLiteral("limited server failed to start: %1").arg(error));
    if (limitedPort != 0)
    {
        QTcpSocket first;
        QTcpSocket second;
        first.connectToHost(QHostAddress::LocalHost, limitedPort);
        second.connectToHost(QHostAddress::LocalHost, limitedPort);
        failIf(!mcpWaitUntil(
                   [&limited] { return limited.activeConnectionCount() == 2; },
                   3000),
               QStringLiteral("connection limit server did not accept two peers"));

        QTcpSocket third;
        third.connectToHost(QHostAddress::LocalHost, limitedPort);
        failIf(!mcpWaitUntil(
                   [&third] {
                       return third.bytesAvailable() > 0
                              || third.state() == QAbstractSocket::UnconnectedState;
                   },
                   3000),
               QStringLiteral("third connection was neither refused nor answered"));
        const int thirdStatus = mcpStatusFromRaw(third.readAll());
        failIf(thirdStatus != 503,
               QStringLiteral("third connection HTTP %1 != 503")
                   .arg(thirdStatus));

        first.abort();
        second.abort();
        third.abort();
        limited.stop();
        failIf(limited.activeConnectionCount() != 0,
               QStringLiteral("connection counter is not empty after stop"));
    }

    // Header and body deadlines are configurable so the test stays fast.
    pvf::McpServer slow(project);
    slow.setToken(QByteArrayLiteral("slow-token"));
    slow.setEndpointFilePath(endpointTemp.path() + QStringLiteral("/slow.json"));
    pvf::McpServer::HttpLimits slowLimits = slow.httpLimits();
    slowLimits.headerTimeoutMs = 150;
    slowLimits.requestTimeoutMs = 150;
    slow.setHttpLimits(slowLimits);
    const quint16 slowPort = slow.start(0, &error);
    failIf(slowPort == 0,
           QStringLiteral("timeout server failed to start: %1").arg(error));
    if (slowPort != 0)
    {
        const QByteArray slowHost =
            QByteArrayLiteral("127.0.0.1:") + QByteArray::number(slowPort);
        const QByteArray partialHeader =
            QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + slowHost
            + QByteArrayLiteral("\r\nAuthorization: Bearer slow-token\r\n"
                                "Content-Type: application/json\r\n");
        const McpHttpResponse headerTimeout =
            mcpExchange(slowPort, partialHeader, 3000);
        failIf(headerTimeout.timedOut,
               QStringLiteral("client timed out before the header deadline"));
        failIf(headerTimeout.status != 408,
               QStringLiteral("header timeout HTTP %1 != 408")
                   .arg(headerTimeout.status));

        const QByteArray partialBody =
            QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + slowHost
            + QByteArrayLiteral("\r\nAuthorization: Bearer slow-token\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: 100\r\n\r\n");
        const McpHttpResponse bodyTimeout =
            mcpExchange(slowPort, partialBody, 3000);
        failIf(bodyTimeout.timedOut,
               QStringLiteral("client timed out before the body deadline"));
        failIf(bodyTimeout.status != 408,
               QStringLiteral("body timeout HTTP %1 != 408")
                   .arg(bodyTimeout.status));

        slow.stop();
    }

    server.stop();
    delete project;
    qInfo() << "mcp-http-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int checkpointTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "checkpoint-test:" << message;
        ++failures;
    };
    const auto expect = [&fail](bool condition, const QString& message) {
        if (!condition)
            fail(message);
        return condition;
    };
    const auto sha256 = [](const QByteArray& data) {
        return QString::fromLatin1(
            QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    };
    const auto readAll = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const auto isObjectId = [](const QString& value) {
        if (value.size() != 40 && value.size() != 64)
            return false;
        for (const QChar character : value)
        {
            const ushort code = character.unicode();
            if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')))
                return false;
        }
        return true;
    };
    const auto ownerOnly = [](const QString& path, bool directory) {
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        const QFileDevice::Permissions group =
            QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup;
        const QFileDevice::Permissions other =
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        bool ok = permissions.testFlag(QFileDevice::ReadOwner)
                  && permissions.testFlag(QFileDevice::WriteOwner)
                  && (permissions & (group | other)) == QFileDevice::Permissions();
        if (directory)
            ok = ok && permissions.testFlag(QFileDevice::ExeOwner);
        return ok;
    };
    const auto runGit = [](const QString& directory, const QStringList& arguments,
                           QByteArray* output, int* status) {
        QProcess process;
        process.setWorkingDirectory(directory);
        process.start(QStringLiteral("git"), arguments);
        if (!process.waitForStarted(5000))
            return false;
        process.closeWriteChannel();
        if (!process.waitForFinished(20000))
        {
            process.kill();
            process.waitForFinished(2000);
            return false;
        }
        if (output)
            *output = process.readAllStandardOutput();
        if (status)
            *status = process.exitCode();
        return process.exitStatus() == QProcess::NormalExit
               && process.exitCode() == 0;
    };

    QTemporaryDir projectTemp(QDir::tempPath()
                              + QStringLiteral("/pvforge-checkpoint-project-XXXXXX"));
    QTemporaryDir cacheTemp(QDir::tempPath()
                            + QStringLiteral("/pvforge-checkpoint-cache-XXXXXX"));
    QTemporaryDir corruptCache(QDir::tempPath()
                               + QStringLiteral("/pvforge-checkpoint-corrupt-XXXXXX"));
    QTemporaryDir fallbackCache(QDir::tempPath()
                                + QStringLiteral("/pvforge-checkpoint-fallback-XXXXXX"));
    if (!projectTemp.isValid() || !cacheTemp.isValid() || !corruptCache.isValid()
        || !fallbackCache.isValid())
    {
        fail(QStringLiteral("could not create temporary directories"));
        return 1;
    }

    const QString projectDirectory = projectTemp.path();
    const QString projectPath = projectDirectory + QStringLiteral("/checkpoint.pvproj");
    const QString imagePath = projectDirectory + QStringLiteral("/still.png");
    const QString missingPath = projectDirectory + QStringLiteral("/missing.png");

    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(imagePath))
    {
        fail(QStringLiteral("could not write test image"));
        return 1;
    }

    pvf::Project project;
    project.name = QStringLiteral("检查点测试");
    project.width = 640;
    project.height = 360;
    project.fps = 30;
    pvf::Entity* entity = project.addImage(imagePath, QStringLiteral("still"));
    if (!entity)
    {
        fail(QStringLiteral("could not add test entity"));
        return 1;
    }
    const pvf::EntityId entityId = entity->id;
    project.setValue(entityId, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(entityId, QStringLiteral("x"), 60, 100.0, true);
    QString error;
    if (!project.addScene(QStringLiteral("Intro"), 0, 60, QColor(90, 170, 255),
                          QString(), &error))
    {
        fail(QStringLiteral("scene setup failed: %1").arg(error));
        return 1;
    }
    if (!project.save(projectPath, &error))
    {
        fail(QStringLiteral("project save failed: %1").arg(error));
        return 1;
    }

    const QByteArray json1 = readAll(projectPath);
    QJsonObject root1 = QJsonDocument::fromJson(json1).object();
    if (root1.isEmpty())
    {
        fail(QStringLiteral("saved project JSON is invalid"));
        return 1;
    }
    QJsonObject root2 = root1;
    root2[QStringLiteral("name")] = QStringLiteral("second");
    const QByteArray json2 = QJsonDocument(root2).toJson(QJsonDocument::Indented);
    QJsonObject root3 = root1;
    root3[QStringLiteral("name")] = QStringLiteral("third");
    QJsonArray entities = root3.value(QStringLiteral("entities")).toArray();
    if (!entities.isEmpty())
    {
        QJsonObject first = entities.at(0).toObject();
        first[QStringLiteral("sourcePath")] = missingPath;
        entities.replace(0, first);
        root3[QStringLiteral("entities")] = entities;
    }
    const QByteArray json3 = QJsonDocument(root3).toJson(QJsonDocument::Indented);

    // A real repository in the project folder proves that checkpoints never
    // touch or pollute the user's own git state.
    const bool systemGit =
        !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
    QByteArray projectRepoStatus;
    QByteArray projectRepoHead;
    bool projectRepoReady = false;
    if (systemGit
        && runGit(projectDirectory,
                  {QStringLiteral("init"), QStringLiteral("-q")}, nullptr, nullptr)
        && runGit(projectDirectory,
                  {QStringLiteral("add"), QStringLiteral("-A")}, nullptr, nullptr)
        && runGit(projectDirectory,
                  {QStringLiteral("-c"), QStringLiteral("user.name=Checkpoint Test"),
                   QStringLiteral("-c"),
                   QStringLiteral("user.email=test@example.invalid"),
                   QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false"),
                   QStringLiteral("commit"), QStringLiteral("-q"),
                   QStringLiteral("-m"), QStringLiteral("baseline")},
                  nullptr, nullptr))
    {
        projectRepoReady = true;
        runGit(projectDirectory, {QStringLiteral("status"), QStringLiteral("--porcelain")},
               &projectRepoStatus, nullptr);
        runGit(projectDirectory, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
               &projectRepoHead, nullptr);
    }
    const QByteArray projectFileBefore = readAll(projectPath);

    pvf::CheckpointManager manager(cacheTemp.path());
    expect(manager.isAvailable(), QStringLiteral("checkpoint manager is not available"));
    const bool gitBackend = manager.gitAvailable();
    qInfo() << "checkpoint-test:" << (gitBackend ? "git" : "snapshot") << "backend";
    if (!systemGit)
        expect(!gitBackend, QStringLiteral("git should be unavailable without git"));

    QString id1;
    if (!manager.createCheckpoint(projectPath, json1, QStringLiteral("first"),
                                  &error, &id1))
    {
        fail(QStringLiteral("create first checkpoint failed: %1").arg(error));
        return 1;
    }
    expect(!id1.isEmpty(), QStringLiteral("first checkpoint id is empty"));
    expect(ownerOnly(cacheTemp.path(), true),
           QStringLiteral("cache root permissions are not 0700"));

    QVector<pvf::CheckpointInfo> listed = manager.listCheckpoints(projectPath);
    bool usedGitBackend = false;
    QString hookMarker;
    if (!expect(listed.size() == 1, QStringLiteral("expected 1 checkpoint after first")))
        qCritical() << "checkpoint-test: listed" << listed.size();
    else
    {
        usedGitBackend = listed.first().gitBacked;
        expect(listed.first().id == id1, QStringLiteral("listed id differs"));
        expect(listed.first().summary == QStringLiteral("first"),
               QStringLiteral("summary was not stored"));
        expect(listed.first().fileSha256 == sha256(json1),
               QStringLiteral("sidecar SHA-256 differs"));
        expect(listed.first().fileSize == json1.size(),
               QStringLiteral("sidecar size differs"));
        expect(!listed.first().projectUuid.isEmpty(),
               QStringLiteral("project UUID is empty"));
        expect(listed.first().scenes.contains(QStringLiteral("Intro")),
               QStringLiteral("scene list was not stored"));
        expect(listed.first().assets.size() == 1,
               QStringLiteral("asset reference list size is %1")
                   .arg(listed.first().assets.size()));
        expect(listed.first().missingAssets.isEmpty(),
               QStringLiteral("existing asset was flagged missing"));
        if (usedGitBackend)
        {
            const QString repo = cacheTemp.path() + QLatin1Char('/')
                                 + pvf::CheckpointManager::projectPathHash(projectPath)
                                 + QStringLiteral(".git");
            expect(ownerOnly(repo, true),
                   QStringLiteral("checkpoint repo permissions are not 0700"));
        }
        else
        {
            const QString directory = cacheTemp.path() + QLatin1Char('/')
                                      + pvf::CheckpointManager::projectPathHash(projectPath)
                                      + QStringLiteral(".snapshots");
            expect(ownerOnly(directory, true),
                   QStringLiteral("snapshot directory permissions are not 0700"));
            expect(ownerOnly(directory + QLatin1Char('/') + id1
                                 + QStringLiteral(".json"),
                             false),
                   QStringLiteral("snapshot sidecar permissions are not 0600"));
        }

        // A hostile hook placed in the bare repository must never run.
        if (usedGitBackend)
        {
            const QString repo = cacheTemp.path() + QLatin1Char('/')
                                 + pvf::CheckpointManager::projectPathHash(projectPath)
                                 + QStringLiteral(".git");
            const QString hooks = repo + QStringLiteral("/hooks");
            QDir().mkpath(hooks);
            hookMarker = cacheTemp.path() + QStringLiteral("/hook-ran");
            QFile hook(hooks + QStringLiteral("/pre-commit"));
            if (hook.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                hook.write(
                    QStringLiteral("#!/bin/sh\ntouch '%1'\n").arg(hookMarker).toUtf8());
                hook.close();
                QFile::setPermissions(hooks + QStringLiteral("/pre-commit"),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner);
            }
        }
    }

    QString id2;
    if (!manager.createCheckpoint(projectPath, json2, QStringLiteral("second"),
                                  &error, &id2))
    {
        fail(QStringLiteral("create second checkpoint failed: %1").arg(error));
        return 1;
    }
    if (usedGitBackend)
        expect(!QFileInfo::exists(hookMarker),
               QStringLiteral("git hook executed during checkpoint"));
    QString id3;
    if (!manager.createCheckpoint(projectPath, json3, QStringLiteral("third"),
                                  &error, &id3))
    {
        fail(QStringLiteral("create third checkpoint failed: %1").arg(error));
        return 1;
    }
    expect(id1 != id2 && id2 != id3 && id1 != id3,
           QStringLiteral("checkpoint ids are not unique"));

    listed = manager.listCheckpoints(projectPath);
    expect(listed.size() == 3,
           QStringLiteral("expected 3 checkpoints, got %1").arg(listed.size()));
    if (!listed.isEmpty())
    {
        const pvf::CheckpointInfo& newest = listed.first();
        expect(newest.id == id3, QStringLiteral("newest checkpoint is not the third"));
        expect(newest.missingAssets.contains(missingPath),
               QStringLiteral("missing asset was not recorded"));
    }

    QByteArray restored;
    if (!expect(manager.restoreCheckpoint(projectPath, id2, &restored, &error),
                QStringLiteral("restore middle checkpoint failed: %1").arg(error)))
        qCritical() << "checkpoint-test: restore error" << error;
    else
        expect(restored == json2,
               QStringLiteral("middle checkpoint content differs"));

    restored.clear();
    if (expect(manager.restoreCheckpoint(projectPath, id1, &restored, &error),
               QStringLiteral("restore first checkpoint failed: %1").arg(error)))
        expect(restored == json1, QStringLiteral("first checkpoint content differs"));

    // Keep the newest two; the oldest disappears from the list.
    manager.prune(projectPath, 2);
    listed = manager.listCheckpoints(projectPath);
    expect(listed.size() == 2,
           QStringLiteral("after prune expected 2 checkpoints, got %1")
               .arg(listed.size()));
    bool hasSecond = false;
    bool hasThird = false;
    for (const pvf::CheckpointInfo& info : listed)
    {
        hasSecond = hasSecond || info.id == id2;
        hasThird = hasThird || info.id == id3;
    }
    expect(hasSecond && hasThird,
           QStringLiteral("prune removed the wrong checkpoints"));

    // Corrupting or deleting the repository must not crash listing, and a
    // damaged payload must be refused by restore.
    pvf::CheckpointManager corruptManager(corruptCache.path());
    QString corruptId;
    if (!corruptManager.createCheckpoint(projectPath, json1,
                                         QStringLiteral("corrupt me"), &error,
                                         &corruptId))
    {
        fail(QStringLiteral("corrupt-backend checkpoint failed: %1").arg(error));
    }
    else
    {
        QVector<pvf::CheckpointInfo> corruptList =
            corruptManager.listCheckpoints(projectPath);
        const bool usedGit = !corruptList.isEmpty() && corruptList.first().gitBacked;
        if (usedGit)
        {
            const QString repo = corruptCache.path() + QLatin1Char('/')
                                 + pvf::CheckpointManager::projectPathHash(projectPath)
                                 + QStringLiteral(".git");
            QByteArray blob;
            if (runGit(corruptCache.path(),
                       {QStringLiteral("--git-dir=") + repo,
                        QStringLiteral("rev-parse"), corruptId
                            + QStringLiteral(":project.pvproj")},
                       &blob, nullptr))
            {
                const QString hash = QString::fromLatin1(blob).trimmed();
                if (isObjectId(hash))
                {
                    const QString object =
                        repo + QStringLiteral("/objects/") + hash.left(2)
                        + QLatin1Char('/') + hash.mid(2);
                    expect(QFile::remove(object),
                           QStringLiteral("could not corrupt git object"));
                    QByteArray broken;
                    expect(!corruptManager.restoreCheckpoint(projectPath, corruptId,
                                                             &broken, &error),
                           QStringLiteral("restore accepted a deleted git object"));
                }
            }
            QDir(repo).removeRecursively();
        }
        else
        {
            const QString directory = corruptCache.path() + QLatin1Char('/')
                                      + pvf::CheckpointManager::projectPathHash(projectPath)
                                      + QStringLiteral(".snapshots");
            QFile payload(directory + QLatin1Char('/') + corruptId
                          + QStringLiteral(".pvproj"));
            if (payload.open(QIODevice::ReadWrite))
            {
                payload.seek(0);
                payload.write("X");
                payload.close();
            }
            QByteArray broken;
            expect(!corruptManager.restoreCheckpoint(projectPath, corruptId, &broken,
                                                     &error),
                   QStringLiteral("restore accepted a tampered snapshot"));
            QDir(directory).removeRecursively();
        }

        corruptList = corruptManager.listCheckpoints(projectPath);
        expect(corruptList.isEmpty(),
               QStringLiteral("listing a deleted store returned %1 entries")
                   .arg(corruptList.size()));
        QString newId;
        expect(corruptManager.createCheckpoint(projectPath, json2,
                                               QStringLiteral("after damage"),
                                               &error, &newId),
               QStringLiteral("checkpoint after damage failed: %1").arg(error));
        expect(corruptManager.listCheckpoints(projectPath).size() == 1,
               QStringLiteral("store did not recover after damage"));
    }

    // Missing git must transparently use the snapshot backend.
    pvf::CheckpointManager fallbackManager(fallbackCache.path(),
                                           QStringLiteral("/nonexistent/pvforge-git"));
    expect(!fallbackManager.gitAvailable(),
           QStringLiteral("fake git path reported git available"));
    expect(fallbackManager.isAvailable(),
           QStringLiteral("snapshot backend reported unavailable"));
    QString snapshotId;
    if (!expect(fallbackManager.createCheckpoint(projectPath, json1,
                                                 QStringLiteral("snapshot"),
                                                 &error, &snapshotId),
                QStringLiteral("snapshot fallback create failed: %1").arg(error)))
        qCritical() << "checkpoint-test: snapshot error" << error;
    else
    {
        const QVector<pvf::CheckpointInfo> snapshots =
            fallbackManager.listCheckpoints(projectPath);
        expect(snapshots.size() == 1,
               QStringLiteral("snapshot fallback list size %1").arg(snapshots.size()));
        if (!snapshots.isEmpty())
            expect(!snapshots.first().gitBacked,
                   QStringLiteral("fallback checkpoint claims git backend"));
        QByteArray snapshotJson;
        if (expect(fallbackManager.restoreCheckpoint(projectPath, snapshotId,
                                                     &snapshotJson, &error),
                   QStringLiteral("snapshot restore failed: %1").arg(error)))
            expect(snapshotJson == json1,
                   QStringLiteral("snapshot fallback content differs"));
        expect(QDir(fallbackCache.path() + QLatin1Char('/')
                    + pvf::CheckpointManager::projectPathHash(projectPath)
                    + QStringLiteral(".snapshots"))
                   .exists(),
               QStringLiteral("snapshot directory does not exist"));
    }

    if (gitBackend)
    {
        pvf::CheckpointManager mixedManager(fallbackCache.path());
        QString mixedId;
        expect(mixedManager.createCheckpoint(projectPath, json2,
                                             QStringLiteral("git and snapshot"),
                                             &error, &mixedId),
               QStringLiteral("mixed backend create failed: %1").arg(error));
        expect(mixedManager.listCheckpoints(projectPath).size() == 2,
               QStringLiteral("list did not merge git and snapshot checkpoints"));
    }

    // The user's own repository and project file must be byte-identical.
    if (projectRepoReady)
    {
        QByteArray statusAfter;
        QByteArray headAfter;
        runGit(projectDirectory, {QStringLiteral("status"), QStringLiteral("--porcelain")},
               &statusAfter, nullptr);
        runGit(projectDirectory, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
               &headAfter, nullptr);
        expect(statusAfter == projectRepoStatus,
               QStringLiteral("user repository status changed"));
        expect(headAfter == projectRepoHead,
               QStringLiteral("user repository HEAD changed"));
        expect(readAll(projectPath) == projectFileBefore,
               QStringLiteral("project file was modified by checkpointing"));
    }

    qInfo() << "checkpoint-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpUiTest(QApplication& app)
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-ui-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity =
        project->addVector(0, 100, 80, Qt::red, Qt::white, 0);
    entity->name = QStringLiteral("MCP 测试矩形");
    const pvf::EntityId entityId = entity->id;
    project->setValue(entityId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(entityId, QStringLiteral("x"), 30, 300.0, true);

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-ui-endpoint-XXXXXX"));
    window.mcpServerForTest()->setEndpointFilePath(
        endpointTemp.path() + QStringLiteral("/mcp-endpoint.json"));

    QTimer::singleShot(
        300, &window, [&app, &window, &failures, entityId, fail, failIf] {
            auto* server = window.mcpServerForTest();
            auto* panel = window.mcpLogPanelForTest();
            auto* statusButton =
                window.findChild<QToolButton*>(QStringLiteral("mcpStatusButton"));
            if (!server || !panel || !statusButton)
            {
                qCritical() << "mcp-ui-test: server/log/status missing" << server
                            << panel << statusButton;
                app.exit(1);
                return;
            }

            auto treeText = [panel] {
                QString text;
                auto* tree =
                    panel->findChild<QTreeWidget*>(QStringLiteral("mcpLogTree"));
                if (!tree)
                    return text;
                for (int i = 0; i < tree->topLevelItemCount(); ++i)
                {
                    QTreeWidgetItem* item = tree->topLevelItem(i);
                    for (int column = 0; column < item->columnCount(); ++column)
                        text += item->text(column) + QLatin1Char('\n');
                    for (int childIndex = 0; childIndex < item->childCount();
                         ++childIndex)
                        for (int column = 0;
                             column < item->child(childIndex)->columnCount();
                             ++column)
                            text += item->child(childIndex)->text(column)
                                    + QLatin1Char('\n');
                }
                return text;
            };

            failIf(server->isRunning(), QStringLiteral("server auto-started"));
            failIf(panel->isVisible(),
                   QStringLiteral("log visible before MCP was enabled"));
            failIf(!statusButton->isVisible(),
                   QStringLiteral("status entry is hidden at startup"));
            failIf(!statusButton->text().contains(QStringLiteral("MCP")),
                   QStringLiteral("status entry has no MCP text"));

            auto* dialog = window.openSettingsDialogForTest();
            if (!dialog)
            {
                fail(QStringLiteral("settings dialog was not created"));
                app.exit(1);
                return;
            }
            QApplication::processEvents();

            auto* categories =
                dialog->findChild<QListWidget*>(QStringLiteral("settingsCategories"));
            bool hasMcpCategory = false;
            if (categories)
            {
                for (int i = 0; i < categories->count(); ++i)
                    if (categories->item(i)->text() == QStringLiteral("MCP"))
                    {
                        hasMcpCategory = true;
                        categories->setCurrentRow(i);
                    }
            }
            failIf(!hasMcpCategory, QStringLiteral("settings has no MCP page"));

            auto* enabledCheck =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpEnableCheck"));
            auto* autoPort =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpAutoPort"));
            auto* portSpin =
                dialog->findChild<QSpinBox*>(QStringLiteral("mcpPortSpin"));
            auto* tokenEdit =
                dialog->findChild<QLineEdit*>(QStringLiteral("mcpTokenEdit"));
            auto* statusLabel =
                dialog->findChild<QLabel*>(QStringLiteral("mcpStatusLabel"));
            auto* errorLabel =
                dialog->findChild<QLabel*>(QStringLiteral("mcpErrorLabel"));
            auto* startButton =
                dialog->findChild<QPushButton*>(QStringLiteral("mcpStartButton"));
            auto* stopButton =
                dialog->findChild<QPushButton*>(QStringLiteral("mcpStopButton"));
            auto* logToggle =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpLogEnabled"));
            auto* permissionCombo =
                dialog->findChild<QComboBox*>(QStringLiteral("mcpPermissionCombo"));
            auto* copyConfig = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpCopyConfigButton"));
            auto* resetToken = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpResetTokenButton"));
            if (!enabledCheck || !autoPort || !portSpin || !tokenEdit
                || !statusLabel || !errorLabel || !startButton || !stopButton
                || !logToggle || !permissionCombo || !copyConfig || !resetToken)
            {
                qCritical() << "mcp-ui-test: MCP controls missing";
                app.exit(1);
                return;
            }

            failIf(enabledCheck->isChecked(),
                   QStringLiteral("MCP enable switch defaults to on"));
            failIf(!autoPort->isChecked(),
                   QStringLiteral("port should default to automatic"));
            failIf(!(portSpin->minimum() <= 1024 && portSpin->maximum() >= 65535),
                   QStringLiteral("manual port range is too narrow"));
            failIf(permissionCombo->currentIndex() != 0,
                   QStringLiteral("permission mode is not read-only"));
            failIf(!logToggle->isChecked(),
                   QStringLiteral("log window should be enabled by default"));
            failIf(tokenEdit->text()
                       != QString::fromLatin1(server->token()),
                   QStringLiteral("settings token does not match the server"));
            failIf(statusLabel->text().contains(
                       QString::fromLatin1(server->token())),
                   QStringLiteral("token is echoed in the settings status"));
            failIf(!errorLabel->isHidden(),
                   QStringLiteral("error label is visible before an error"));

            // Only a human action starts the server.
            enabledCheck->setChecked(true);
            QApplication::processEvents();
            failIf(!server->isRunning(),
                   QStringLiteral("enable switch did not start the server"));
            failIf(server->port() == 0,
                   QStringLiteral("enabled server has no listening port"));
            const quint16 port = server->port();
            const QByteArray token = server->token();
            failIf(!statusLabel->text().contains(QStringLiteral("监听中")),
                   QStringLiteral("settings status does not show listening"));
            failIf(!statusLabel->text().contains(QString::number(port)),
                   QStringLiteral("settings status does not show the actual port"));
            failIf(statusButton->text().contains(QStringLiteral("关闭")),
                   QStringLiteral("status bar was not updated after start"));
            failIf(!tokenEdit->text().contains(QString::fromLatin1(token)),
                   QStringLiteral("token field did not refresh after start"));

            stopButton->click();
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("dialog stop button did not stop the server"));
            failIf(server->port() != 0,
                   QStringLiteral("port not cleared after dialog stop"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status not reset after stop"));
            startButton->click();
            QApplication::processEvents();
            failIf(!server->isRunning(),
                   QStringLiteral("dialog start button did not start the server"));

            copyConfig->click();
            QApplication::processEvents();
            const QJsonObject config =
                QJsonDocument::fromJson(
                    QApplication::clipboard()->text().toUtf8())
                    .object();
            const QJsonObject configuredServer =
                config.value(QStringLiteral("mcpServers"))
                    .toObject()
                    .value(QStringLiteral("pvforge"))
                    .toObject();
            failIf(!configuredServer.value(QStringLiteral("url"))
                        .toString()
                        .contains(QString::number(server->port())),
                   QStringLiteral("copied config has no live endpoint"));
            failIf(!configuredServer.value(QStringLiteral("headers"))
                        .toObject()
                        .value(QStringLiteral("Authorization"))
                        .toString()
                        .contains(QString::fromLatin1(token)),
                   QStringLiteral("copied config has no bearer token"));

            const quint16 livePort = server->port();
            QJsonObject summaryParams;
            summaryParams[QStringLiteral("name")] =
                QStringLiteral("project.summary");
            summaryParams[QStringLiteral("arguments")] = QJsonObject();
            QJsonObject summaryRequest;
            summaryRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            summaryRequest[QStringLiteral("id")] = 100;
            summaryRequest[QStringLiteral("method")] = QStringLiteral("tools/call");
            summaryRequest[QStringLiteral("params")] = summaryParams;
            const QJsonObject summaryResponse =
                mcpRpc(livePort, token, summaryRequest);
            failIf(summaryResponse.value(QStringLiteral("result")).isUndefined(),
                   QStringLiteral("project.summary returned no result"));
            QApplication::processEvents();

            failIf(panel->entryCountForTest() < 1,
                   QStringLiteral("toolInvoked did not add a log row"));
            failIf(!panel->isVisible(),
                   QStringLiteral("first tool call did not unfold the log"));
            failIf(!statusButton->text().contains(QStringLiteral("已启用")),
                   QStringLiteral("status bar entry does not show MCP enabled"));
            dialog->grab().toImage().save(
                QStringLiteral("/tmp/pvf_mcp_settings.png"));
            panel->grab().toImage().save(
                QStringLiteral("/tmp/pvf_mcp_log.png"));

            QJsonObject entityArgs;
            entityArgs[QStringLiteral("id")] = entityId;
            entityArgs[QStringLiteral("frame")] = 12;
            QJsonObject entityParams;
            entityParams[QStringLiteral("name")] = QStringLiteral("entity.get");
            entityParams[QStringLiteral("arguments")] = entityArgs;
            QJsonObject entityRequest;
            entityRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            entityRequest[QStringLiteral("id")] = 101;
            entityRequest[QStringLiteral("method")] = QStringLiteral("tools/call");
            entityRequest[QStringLiteral("params")] = entityParams;
            const QJsonObject entityResponse =
                mcpRpc(livePort, token, entityRequest);
            failIf(entityResponse.value(QStringLiteral("result")).isUndefined(),
                   QStringLiteral("entity.get returned no result"));
            QApplication::processEvents();

            failIf(panel->entryCountForTest() < 2,
                   QStringLiteral("second tool call did not add a log row"));
            const QString target = panel->entryTargetForTest(1);
            failIf(!target.contains(QStringLiteral("实体 #")),
                   QStringLiteral("log target has no entity: %1").arg(target));
            failIf(!target.contains(QStringLiteral("帧 12")),
                   QStringLiteral("log target has no frame: %1").arg(target));
            const QString detail = panel->entryDetailForTest(1);
            failIf(!detail.contains(QStringLiteral("参数")),
                   QStringLiteral("expanded row has no args"));
            failIf(!detail.contains(QStringLiteral("结果")),
                   QStringLiteral("expanded row has no result"));

            auto* tree =
                panel->findChild<QTreeWidget*>(QStringLiteral("mcpLogTree"));
            if (tree && tree->topLevelItem(1))
            {
                tree->topLevelItem(1)->setExpanded(true);
                failIf(!tree->topLevelItem(1)->isExpanded(),
                       QStringLiteral("log row cannot expand"));
            }
            else
            {
                fail(QStringLiteral("log tree row missing"));
            }

            auto* detachButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogDetachButton"));
            if (detachButton)
            {
                detachButton->click();
                QApplication::processEvents();
                failIf(!panel->isDetached(),
                       QStringLiteral("detach button did not pop the log out"));
                detachButton->click();
                QApplication::processEvents();
                failIf(panel->isDetached(),
                       QStringLiteral("detach button did not dock the log"));
            }
            else
            {
                fail(QStringLiteral("detach button missing"));
            }

            const QString logText = treeText();
            failIf(logText.contains(QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the log"));
            failIf(statusButton->text().contains(QString::fromLatin1(token))
                       || statusButton->toolTip().contains(
                           QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the status entry"));
            failIf(statusLabel->text().contains(QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the settings status"));

            panel->activateEntryForTest(1);
            QApplication::processEvents();
            failIf(window.selectedEntityForTest() != entityId,
                   QStringLiteral("clicking the log row did not select the entity"));
            failIf(window.currentFrameForTest() != 12,
                   QStringLiteral("clicking the log row did not jump to the frame"));

            bool stopSignalled = false;
            QObject::connect(panel, &pvf::McpLogPanel::stopAiRequested,
                             [&stopSignalled] { stopSignalled = true; });
            auto* stopAi = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogStopButton"));
            failIf(!stopAi || !stopAi->isEnabled(),
                   QStringLiteral("stop AI button is missing or disabled"));
            if (stopAi)
                stopAi->click();
            QApplication::processEvents();
            failIf(!stopSignalled,
                   QStringLiteral("stop AI button did not emit the signal"));

            const QByteArray oldToken = server->token();
            resetToken->click();
            QApplication::processEvents();
            failIf(server->token().isEmpty() || server->token() == oldToken,
                   QStringLiteral("token reset did not generate a new token"));
            failIf(tokenEdit->text() != QString::fromLatin1(server->token()),
                   QStringLiteral("token field did not update after reset"));
            failIf(treeText().contains(QString::fromLatin1(server->token())),
                   QStringLiteral("new token is echoed in the log"));

            auto* clearButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogClearButton"));
            if (clearButton)
                clearButton->click();
            QApplication::processEvents();
            failIf(panel->entryCountForTest() != 0,
                   QStringLiteral("clear did not empty the log"));

            enabledCheck->setChecked(false);
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("disabling MCP did not stop the server"));
            failIf(server->port() != 0,
                   QStringLiteral("port not cleared after disabling MCP"));
            failIf(statusButton->text().contains(QStringLiteral("已启用")),
                   QStringLiteral("status bar still says MCP is enabled"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status is not stopped"));

            // A busy manual port shows an error without leaking the token,
            // and retrying on the same port works once it is free.
            QTcpServer busyPortServer;
            if (!busyPortServer.listen(QHostAddress::LocalHost, 0))
            {
                fail(QStringLiteral("could not reserve a busy port"));
            }
            else
            {
                const quint16 busyPort = busyPortServer.serverPort();
                autoPort->setChecked(false);
                portSpin->setValue(busyPort);
                enabledCheck->setChecked(true);
                QApplication::processEvents();
                failIf(server->isRunning(),
                       QStringLiteral("server started on an occupied port"));
                failIf(errorLabel->isHidden() || errorLabel->text().isEmpty(),
                       QStringLiteral("start failure did not show an error"));
                failIf(errorLabel->text().contains(
                           QString::fromLatin1(server->token())),
                       QStringLiteral("token is echoed in the error text"));
                failIf(!statusButton->text().contains(QStringLiteral("错误")),
                       QStringLiteral("status bar does not show the MCP error"));
                busyPortServer.close();
                QApplication::processEvents();
                startButton->click();
                QApplication::processEvents();
                failIf(!server->isRunning(),
                       QStringLiteral("retry after freeing the port failed"));
                failIf(server->port() != busyPort,
                       QStringLiteral("manual port was not honoured: %1 != %2")
                           .arg(server->port())
                           .arg(busyPort));
                failIf(!errorLabel->isHidden(),
                       QStringLiteral("error text did not clear on success"));
                enabledCheck->setChecked(false);
                QApplication::processEvents();
                failIf(server->isRunning(),
                       QStringLiteral("manual-port server did not stop"));
            }

            panel->showFromMenu();
            QApplication::processEvents();
            panel->close();
            QApplication::processEvents();
            failIf(panel->isVisible(),
                   QStringLiteral("closing the log window did not hide it"));
            failIf(!statusButton->isVisible(),
                   QStringLiteral("status entry disappeared after closing the log"));
            failIf(!statusButton->text().contains(QStringLiteral("MCP")),
                   QStringLiteral("status entry lost its MCP label"));
            statusButton->click();
            QApplication::processEvents();
            failIf(!panel->isVisible(),
                   QStringLiteral("status entry did not reopen the log"));
            panel->close();
            QApplication::processEvents();

            dialog->close();
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("closing settings started the server"));

            qInfo() << "mcp-ui-test failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    return app.exec();
}


int mcpWriteTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-write-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };
    const auto expectOk = [&](const pvf::McpWriteResult& result, const QString& label) {
        if (result.ok)
            return true;
        fail(QStringLiteral("%1: %2 (%3)")
                 .arg(label, result.message,
                      pvf::McpWriteResult::codeName(result.error)));
        return false;
    };
    const auto expectCode = [&](const pvf::McpWriteResult& result,
                                pvf::McpErrorCode code, const QString& label) {
        if (result.ok || result.error != code)
        {
            fail(QStringLiteral("%1: expected %2, got %3 (%4)")
                     .arg(label, pvf::McpWriteResult::codeName(code),
                          pvf::McpWriteResult::codeName(result.error),
                          result.message));
            return false;
        }
        return true;
    };

    pvf::Project project;
    project.fps = 30;
    project.width = 1280;
    project.height = 720;

    pvf::Entity* holder =
        project.addVector(0, 100, 100, QColor(80, 80, 80), Qt::white, 0);
    if (!holder)
        return 1;
    project.setValue(holder->id, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(holder->id, QStringLiteral("x"), 240, 240.0, true);

    QString error;
    pvf::Scene* sceneA = project.addScene(QStringLiteral("A"), 0, 120,
                                          QColor(255, 80, 80),
                                          QStringLiteral("场景 A"), &error);
    if (!sceneA)
    {
        qCritical() << "mcp-write-test: add scene A" << error;
        return 1;
    }
    const QString sceneAId = sceneA->id;
    const pvf::EntityId sceneAFolder = sceneA->folderId;

    pvf::Scene* sceneB = project.addScene(QStringLiteral("B"), 150, 240,
                                          QColor(80, 200, 120),
                                          QStringLiteral("场景 B"), &error);
    if (!sceneB)
    {
        qCritical() << "mcp-write-test: add scene B" << error;
        return 1;
    }
    const pvf::EntityId sceneBFolder = sceneB->folderId;

    pvf::Entity* aEntity =
        project.addVector(0, 120, 90, QColor(230, 90, 90), Qt::white, 0);
    aEntity->name = QStringLiteral("场景 A 矩形");
    const pvf::EntityId aId = aEntity->id;
    project.setEntityParent(aId, sceneAFolder);
    pvf::Entity* bEntity =
        project.addVector(0, 120, 90, QColor(90, 140, 230), Qt::white, 0);
    bEntity->name = QStringLiteral("场景 B 矩形");
    const pvf::EntityId bId = bEntity->id;
    project.setEntityParent(bId, sceneBFolder);
    pvf::Entity* camera = project.addCamera(QStringLiteral("主摄像机"));
    const pvf::EntityId cameraId = camera->id;
    project.activeCamera = cameraId;

    pvf::History history;
    pvf::McpCommandService service(&project, &history);
    failIf(service.isArmed(), QStringLiteral("service starts armed"));
    failIf(service.isStopped(), QStringLiteral("service starts stopped"));

    const auto entityName = [&project](pvf::EntityId id) {
        const pvf::Entity* entity = project.entity(id);
        return entity ? entity->name : QString();
    };

    // ---- Unarmed writes are refused -------------------------------------
    const int countBeforeUnarmed = int(project.entities().size());
    const pvf::McpWriteResult unarmedResult = service.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("未解锁")}});
    expectCode(unarmedResult, pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("unarmed create"));
    failIf(project.entities().size() != countBeforeUnarmed,
           QStringLiteral("unarmed create changed the project"));
    const QJsonObject unarmedJson = unarmedResult.toJson();
    failIf(unarmedJson.value(QStringLiteral("ok")).toBool(),
           QStringLiteral("error result claims ok"));
    failIf(unarmedJson.value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("permission_denied"),
           QStringLiteral("error result code mismatch"));

    // ---- Arm the scene and create/update/move/keyframe -------------------
    failIf(!service.arm(sceneAFolder, &error),
           QStringLiteral("arm scene A: %1").arg(error));
    failIf(service.sceneId() != sceneAId,
           QStringLiteral("arm did not snapshot scene A"));
    failIf(service.sceneRootId() != sceneAFolder,
           QStringLiteral("arm did not snapshot the folder"));

    const QJsonObject createArgs{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("name"), QStringLiteral("AI 文本")},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("text"), QStringLiteral("你好")},
                     {QStringLiteral("size"), 48}}},
        {QStringLiteral("transform"),
         QJsonObject{{QStringLiteral("x"), 10.0},
                     {QStringLiteral("y"), 20.0}}},
        {QStringLiteral("frame"), 0.0}};
    const pvf::McpWriteResult createResult =
        service.execute(QStringLiteral("entity.create"), createArgs);
    int textId = pvf::kInvalidEntity;
    if (expectOk(createResult, QStringLiteral("create text")))
    {
        textId = createResult.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* created = project.entity(textId);
        failIf(!created, QStringLiteral("created entity is missing"));
        if (created)
        {
            failIf(created->type != pvf::EntityType::Text,
                   QStringLiteral("created type is not Text"));
            failIf(created->parent != sceneAFolder,
                   QStringLiteral("created entity did not default to scene A"));
            failIf(created->data.value(QStringLiteral("text")).toString()
                       != QStringLiteral("你好"),
                   QStringLiteral("created text data mismatch"));
        }
        failIf(!qFuzzyCompare(project.value(textId, QStringLiteral("x"), 0) + 1.0,
                              11.0),
               QStringLiteral("created transform x mismatch"));
        failIf(!history.canUndo(), QStringLiteral("create did not reach History"));
        failIf(history.undoText().isEmpty(),
               QStringLiteral("create History text is empty"));
        history.undo(project);
        failIf(project.entity(textId) != nullptr,
               QStringLiteral("undo did not remove the created entity"));
        history.redo(project);
        failIf(project.entity(textId) == nullptr,
               QStringLiteral("redo did not restore the created entity"));
    }

    const QJsonObject updateArgs{
        {QStringLiteral("id"), textId},
        {QStringLiteral("name"), QStringLiteral("AI 文本 2")},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("text"), QStringLiteral("更新")}}},
        {QStringLiteral("transform"),
         QJsonObject{{QStringLiteral("x"), 30.0}}},
        {QStringLiteral("frame"), 0.0}};
    const pvf::McpWriteResult updateResult =
        service.execute(QStringLiteral("entity.update"), updateArgs);
    if (expectOk(updateResult, QStringLiteral("update text")))
    {
        const pvf::Entity* updated = project.entity(textId);
        failIf(!updated, QStringLiteral("updated entity is missing"));
        if (updated)
        {
            failIf(updated->name != QStringLiteral("AI 文本 2"),
                   QStringLiteral("update name mismatch"));
            failIf(updated->data.value(QStringLiteral("text")).toString()
                       != QStringLiteral("更新"),
                   QStringLiteral("update data mismatch"));
        }
        failIf(!qFuzzyCompare(project.value(textId, QStringLiteral("x"), 0) + 1.0,
                              31.0),
               QStringLiteral("update transform x mismatch"));
    }

    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("type"),
                                QStringLiteral("vector")}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("entity.update must not change type"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("data"),
                                QJsonObject{{QStringLiteral("sourcePath"),
                                             QStringLiteral("/etc/passwd")}}}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("entity.update path injection"));

    QJsonArray keyframes;
    keyframes.append(QJsonObject{{QStringLiteral("frame"), 0.0},
                                 {QStringLiteral("value"), 0.0}});
    keyframes.append(QJsonObject{{QStringLiteral("frame"), 60.0},
                                 {QStringLiteral("value"), 300.0},
                                 {QStringLiteral("ease"), QStringLiteral("easeinquad")}});
    const pvf::McpWriteResult keyframeResult = service.execute(
        QStringLiteral("keyframes.set"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("property"), QStringLiteral("x")},
                    {QStringLiteral("keys"), keyframes}});
    if (expectOk(keyframeResult, QStringLiteral("set keyframes")))
    {
        const pvf::Keyframe* key = project.keyframe(textId, QStringLiteral("x"), 60);
        failIf(!key, QStringLiteral("keyframe at 60 is missing"));
        if (key)
        {
            failIf(!qFuzzyCompare(key->value + 1.0, 301.0),
                   QStringLiteral("keyframe value mismatch"));
            failIf(key->ease.easing != pvf::Easing::EaseInQuad,
                   QStringLiteral("keyframe ease mismatch"));
        }
    }

    const pvf::McpWriteResult easingResult = service.execute(
        QStringLiteral("easing.set"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("property"), QStringLiteral("x")},
                    {QStringLiteral("frame"), 60.0},
                    {QStringLiteral("ease"), QStringLiteral("easeoutquad")}});
    if (expectOk(easingResult, QStringLiteral("set easing")))
    {
        const pvf::Keyframe* key = project.keyframe(textId, QStringLiteral("x"), 60);
        failIf(!key || key->ease.easing != pvf::Easing::EaseOutQuad,
               QStringLiteral("easing.set did not update the keyframe"));
    }

    const pvf::McpWriteResult folderResult = service.execute(
        QStringLiteral("folder.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("组")}});
    int folderId = pvf::kInvalidEntity;
    if (expectOk(folderResult, QStringLiteral("create folder")))
    {
        folderId = folderResult.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* folder = project.entity(folderId);
        failIf(!folder || !folder->isFolder(),
               QStringLiteral("created folder is missing"));
        failIf(folder && folder->parent != sceneAFolder,
               QStringLiteral("created folder did not default to scene A"));
    }

    const pvf::McpWriteResult moveResult = service.execute(
        QStringLiteral("entity.move"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("parent"), folderId},
                    {QStringLiteral("index"), 0}});
    if (expectOk(moveResult, QStringLiteral("move entity")))
    {
        const pvf::Entity* moved = project.entity(textId);
        failIf(!moved || moved->parent != folderId,
               QStringLiteral("entity.move did not update the parent"));
    }

    // ---- Scene boundary ---------------------------------------------------
    const int countBeforeScope = int(project.entities().size());
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), bId},
                               {QStringLiteral("name"), QStringLiteral("越界")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("update entity in scene B"));
    expectCode(service.execute(
                   QStringLiteral("keyframes.set"),
                   QJsonObject{{QStringLiteral("id"), bId},
                               {QStringLiteral("property"), QStringLiteral("x")},
                               {QStringLiteral("keys"),
                                QJsonArray{QJsonObject{
                                    {QStringLiteral("frame"), 0.0},
                                    {QStringLiteral("value"), 1.0}}}}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("keyframes in scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.move"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("parent"), sceneBFolder}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("move entity into scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("越界")},
                               {QStringLiteral("parent"), sceneBFolder}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create entity in scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("根层")},
                               {QStringLiteral("parent"), -1}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create entity at the root"));
    expectCode(service.execute(
                   QStringLiteral("folder.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("根层组")},
                               {QStringLiteral("parent"), -1}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create root folder"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), cameraId},
                               {QStringLiteral("name"), QStringLiteral("越界摄像机")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("update global camera"));
    expectCode(service.execute(QStringLiteral("entities.retire"),
                               QJsonObject{{QStringLiteral("id"), bId}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("retire entity in scene B"));
    failIf(project.entities().size() != countBeforeScope,
           QStringLiteral("rejected scope writes changed the project"));
    failIf(entityName(aId) != QStringLiteral("场景 A 矩形"),
           QStringLiteral("rejected scope write renamed scene A entity"));

    service.setAllowGlobal(true);
    const pvf::McpWriteResult cameraResult = service.execute(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("id"), cameraId},
                    {QStringLiteral("name"), QStringLiteral("主摄像机 2")}});
    failIf(!expectOk(cameraResult, QStringLiteral("update camera with allowGlobal")),
           QStringLiteral("allowGlobal did not unlock the camera"));
    failIf(entityName(cameraId) != QStringLiteral("主摄像机 2"),
           QStringLiteral("camera rename did not apply"));
    service.setAllowGlobal(false);

    // ---- Retire -----------------------------------------------------------
    const int countBeforeRetire = int(project.entities().size());
    const pvf::McpWriteResult retireResult = service.execute(
        QStringLiteral("entities.retire"),
        QJsonObject{{QStringLiteral("id"), textId}});
    if (expectOk(retireResult, QStringLiteral("retire entity")))
    {
        const pvf::Entity* retired = project.entity(textId);
        failIf(!retired, QStringLiteral("retire deleted the entity"));
        if (retired)
        {
            failIf(!retired->locked, QStringLiteral("retired entity is not locked"));
            const pvf::Entity* sceneFolder = project.entity(retired->parent);
            failIf(!sceneFolder || sceneFolder->name != QStringLiteral("A"),
                   QStringLiteral("retired entity is not in _retired/A"));
            const pvf::Entity* retiredRoot =
                sceneFolder ? project.entity(sceneFolder->parent) : nullptr;
            failIf(!retiredRoot || retiredRoot->name != QStringLiteral("_retired"),
                   QStringLiteral("retired scene folder has no _retired parent"));
        }
        failIf(!qFuzzyIsNull(project.value(textId, QStringLiteral("opacity"), 0)),
               QStringLiteral("retired opacity at frame 0 is not 0"));
        failIf(!qFuzzyIsNull(project.effectiveOpacity(textId, 60)),
               QStringLiteral("retired effective opacity is not 0"));
        failIf(int(project.entities().size()) < countBeforeRetire,
               QStringLiteral("retire shrank the project"));
        history.undo(project);
        const pvf::Entity* restored = project.entity(textId);
        failIf(!restored || restored->locked,
               QStringLiteral("undo did not restore the retired entity"));
        failIf(restored && restored->parent != folderId,
               QStringLiteral("undo did not restore the old parent"));
        history.redo(project);
        const pvf::Entity* retiredAgain = project.entity(textId);
        failIf(!retiredAgain || !retiredAgain->locked,
               QStringLiteral("redo did not retire the entity again"));
    }

    // ---- Batch atomicity --------------------------------------------------
    const QString aOriginalName = entityName(aId);
    const int countBeforeFailedBatch = int(project.entities().size());
    const QString undoBeforeFailedBatch = history.undoText();
    QJsonArray failedOps;
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次矩形")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.update")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("id"), aId},
                     {QStringLiteral("name"), QStringLiteral("批次改名")}}}});
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.update")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("id"), 999999},
                     {QStringLiteral("name"), QStringLiteral("不存在")}}}});
    const pvf::McpWriteResult failedBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), failedOps}});
    expectCode(failedBatch, pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("failed batch"));
    failIf(project.entities().size() != countBeforeFailedBatch,
           QStringLiteral("failed batch did not roll back entity count"));
    failIf(entityName(aId) != aOriginalName,
           QStringLiteral("failed batch kept an entity rename"));
    failIf(history.undoText() != undoBeforeFailedBatch,
           QStringLiteral("failed batch pushed a History step"));
    for (const pvf::Entity& entity : project.entities())
        failIf(entity.name == QStringLiteral("批次矩形"),
               QStringLiteral("failed batch kept a created entity"));

    const int countBeforeGoodBatch = int(project.entities().size());
    QJsonArray goodOps;
    goodOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次 1")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    goodOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次 2")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    const pvf::McpWriteResult goodBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), goodOps}});
    if (expectOk(goodBatch, QStringLiteral("successful batch")))
    {
        failIf(project.entities().size() != countBeforeGoodBatch + 2,
               QStringLiteral("successful batch did not create both entities"));
        failIf(history.undoText() != QStringLiteral("AI 批量操作"),
               QStringLiteral("batch did not push one History step"));
        history.undo(project);
        failIf(project.entities().size() != countBeforeGoodBatch,
               QStringLiteral("batch undo did not remove both entities"));
        history.redo(project);
        failIf(project.entities().size() != countBeforeGoodBatch + 2,
               QStringLiteral("batch redo did not restore both entities"));
    }

    QJsonArray tooManyOps;
    for (int index = 0; index < pvf::McpCommandService::kMaxBatchOperations + 1;
         ++index)
    {
        tooManyOps.append(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("entity.update")},
            {QStringLiteral("args"),
             QJsonObject{{QStringLiteral("id"), aId},
                         {QStringLiteral("name"), QStringLiteral("限额")}}}});
    }
    expectCode(service.execute(QStringLiteral("batch.run"),
                               QJsonObject{{QStringLiteral("operations"), tooManyOps}}),
               pvf::McpErrorCode::LimitExceeded,
               QStringLiteral("batch over 200 operations"));
    failIf(entityName(aId) != aOriginalName,
           QStringLiteral("over-limit batch changed the project"));

    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}}),
               pvf::McpErrorCode::NotAvailable,
               QStringLiteral("create image without a path"));
    expectCode(service.execute(QStringLiteral("html.create_layer"), QJsonObject()),
               pvf::McpErrorCode::NotAvailable,
               QStringLiteral("HTML layer write"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), 999999},
                               {QStringLiteral("name"), QStringLiteral("不存在")}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("update missing entity"));

    // ---- Stop during a single call ---------------------------------------
    const int countBeforeStop = int(project.entities().size());
    const QString undoBeforeStop = history.undoText();
    bool stopFired = false;
    QMetaObject::Connection stopConnection = QObject::connect(
        &project, &pvf::Project::changed, [&]() {
            if (stopFired)
                return;
            stopFired = true;
            service.stop();
        });
    const pvf::McpWriteResult stoppedCreate = service.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("急停文本")}});
    QObject::disconnect(stopConnection);
    expectCode(stoppedCreate, pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("stop during single create"));
    failIf(!stopFired, QStringLiteral("stop signal did not fire"));
    failIf(project.entities().size() != countBeforeStop,
           QStringLiteral("stopped create was not rolled back"));
    failIf(history.undoText() != undoBeforeStop,
           QStringLiteral("stopped create pushed a History step"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("急停后")}}),
               pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("write after stop"));

    // ---- Stop during a batch ---------------------------------------------
    failIf(!service.arm(sceneAFolder, &error),
           QStringLiteral("re-arm after stop: %1").arg(error));
    failIf(service.isStopped(), QStringLiteral("re-arm did not clear stopped"));
    const QString aBeforeStoppedBatch = entityName(aId);
    const QString undoBeforeStoppedBatch = history.undoText();
    stopFired = false;
    int changedCount = 0;
    stopConnection = QObject::connect(
        &project, &pvf::Project::changed, [&]() {
            ++changedCount;
            if (stopFired || changedCount < 3)
                return;
            stopFired = true;
            service.stop();
        });
    QJsonArray stoppedOps;
    for (int index = 0; index < 10; ++index)
    {
        stoppedOps.append(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("entity.update")},
            {QStringLiteral("args"),
             QJsonObject{{QStringLiteral("id"), aId},
                         {QStringLiteral("name"),
                          QStringLiteral("急停批次 %1").arg(index)}}}});
    }
    const pvf::McpWriteResult stoppedBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), stoppedOps}});
    QObject::disconnect(stopConnection);
    expectCode(stoppedBatch, pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("stop during batch"));
    failIf(!stopFired, QStringLiteral("batch stop signal did not fire"));
    failIf(entityName(aId) != aBeforeStoppedBatch,
           QStringLiteral("stopped batch was not rolled back"));
    failIf(history.undoText() != undoBeforeStoppedBatch,
           QStringLiteral("stopped batch pushed a History step"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("急停后 2")}}),
               pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("write after batch stop"));

    const QStringList operations = pvf::McpCommandService::operationNames();
    for (const QString& required :
         {QStringLiteral("entity.create"), QStringLiteral("entity.update"),
          QStringLiteral("keyframes.set"), QStringLiteral("easing.set"),
          QStringLiteral("entity.move"), QStringLiteral("folder.create"),
          QStringLiteral("entities.retire"), QStringLiteral("scene.create"),
          QStringLiteral("batch.run")})
        failIf(!operations.contains(required),
               QStringLiteral("operationNames missing %1").arg(required));

    // ---- scene.create appends and does not widen the scope ---------------
    pvf::Project sceneProject;
    pvf::Entity* sceneHolder =
        sceneProject.addVector(0, 100, 100, Qt::gray, Qt::white, 0);
    sceneProject.setValue(sceneHolder->id, QStringLiteral("x"), 0, 0.0, true);
    sceneProject.setValue(sceneHolder->id, QStringLiteral("x"), 300, 300.0, true);
    pvf::Scene* firstScene = sceneProject.addScene(
        QStringLiteral("S1"), 0, 100, QColor(90, 170, 255), QString(), &error);
    if (!firstScene)
    {
        qCritical() << "mcp-write-test: add scene S1" << error;
        return 1;
    }
    const pvf::EntityId firstSceneFolder = firstScene->folderId;
    pvf::History sceneHistory;
    pvf::McpCommandService sceneService(&sceneProject, &sceneHistory);
    failIf(!sceneService.arm(firstSceneFolder, &error),
           QStringLiteral("arm scene S1: %1").arg(error));

    expectCode(sceneService.execute(
                   QStringLiteral("scene.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("S2")},
                               {QStringLiteral("startFrame"), 50},
                               {QStringLiteral("endFrame"), 300}}),
               pvf::McpErrorCode::Conflict,
               QStringLiteral("scene.create before the last scene"));
    failIf(sceneProject.scenes().size() != 1,
           QStringLiteral("rejected scene.create changed the scene list"));

    const pvf::McpWriteResult sceneResult = sceneService.execute(
        QStringLiteral("scene.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("S2")},
                    {QStringLiteral("startFrame"), 150},
                    {QStringLiteral("endFrame"), 300}});
    QString newSceneId;
    pvf::EntityId newSceneFolder = pvf::kInvalidEntity;
    if (expectOk(sceneResult, QStringLiteral("scene.create append")))
    {
        newSceneId = sceneResult.data.value(QStringLiteral("id")).toString();
        newSceneFolder =
            sceneResult.data.value(QStringLiteral("folderId")).toInt();
        failIf(sceneProject.scenes().size() != 2,
               QStringLiteral("scene.create did not append"));
        failIf(sceneProject.scenes().last().id != newSceneId,
               QStringLiteral("scene.create did not append at the end"));
        sceneHistory.undo(sceneProject);
        failIf(sceneProject.scenes().size() != 1,
               QStringLiteral("scene.create undo did not remove the scene"));
        sceneHistory.redo(sceneProject);
        failIf(sceneProject.scenes().size() != 2,
               QStringLiteral("scene.create redo did not restore the scene"));
        if (sceneProject.scenes().size() == 2)
        {
            const pvf::EntityId redoneFolder =
                sceneProject.scenes().last().folderId;
            const pvf::Entity* redone = sceneProject.entity(redoneFolder);
            failIf(!redone || !redone->isFolder(),
                   QStringLiteral("scene.create redo left a dangling folder"));
            failIf(redoneFolder == newSceneFolder,
                   QStringLiteral("scene.create redo reused the removed folder"));
        }
    }

    const pvf::McpWriteResult scopedCreate = sceneService.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("作用域")}});
    if (expectOk(scopedCreate, QStringLiteral("create after scene.create")))
    {
        const pvf::EntityId createdId =
            scopedCreate.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* created = sceneProject.entity(createdId);
        failIf(!created || created->parent != firstSceneFolder,
               QStringLiteral("scene.create widened the write scope"));
        failIf(created && created->parent == newSceneFolder,
               QStringLiteral("new entity landed in the new scene"));
    }

    // ---- No scene requires an explicit human unlock -----------------------
    pvf::Project noSceneProject;
    pvf::Entity* noSceneEntity =
        noSceneProject.addVector(0, 100, 100, Qt::gray, Qt::white, 0);
    Q_UNUSED(noSceneEntity);
    pvf::History noSceneHistory;
    pvf::McpCommandService noSceneService(&noSceneProject, &noSceneHistory,
                                          pvf::kInvalidEntity, true);
    expectCode(noSceneService.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("无场景")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("armed without a scene"));
    expectCode(noSceneService.execute(
                   QStringLiteral("scene.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("无场景")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("scene.create without a scene"));
    failIf(!noSceneService.armUnscoped(&error),
           QStringLiteral("arm unscoped no-scene project: %1").arg(error));
    expectOk(noSceneService.execute(
                 QStringLiteral("entity.create"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("name"), QStringLiteral("显式解锁")}}),
             QStringLiteral("create after explicit unscoped unlock"));

    // ---- Project entity limit --------------------------------------------
    pvf::Project fullProject;
    QVector<pvf::Entity> manyEntities;
    manyEntities.reserve(pvf::McpCommandService::kMaxProjectEntities);
    for (int index = 1; index <= pvf::McpCommandService::kMaxProjectEntities;
         ++index)
    {
        pvf::Entity entity;
        entity.id = index;
        entity.type = pvf::EntityType::Folder;
        entity.name = QStringLiteral("填充 %1").arg(index);
        manyEntities.append(entity);
    }
    fullProject.setEntities(manyEntities);
    pvf::McpCommandService fullService(&fullProject);
    failIf(!fullService.armUnscoped(&error),
           QStringLiteral("arm unscoped full project: %1").arg(error));
    expectCode(fullService.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("超限")}}),
               pvf::McpErrorCode::LimitExceeded,
               QStringLiteral("create beyond 5000 entities"));
    failIf(fullProject.entities().size()
               != pvf::McpCommandService::kMaxProjectEntities,
           QStringLiteral("limit rejection changed the full project"));

    qInfo() << "mcp-write-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpIntegrationTest(QApplication& app)
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-integration-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };
    const auto ownerOnly = [](const QString& path, bool directory) {
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        const QFileDevice::Permissions group =
            QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup;
        const QFileDevice::Permissions other =
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        bool ok = permissions.testFlag(QFileDevice::ReadOwner)
                  && permissions.testFlag(QFileDevice::WriteOwner)
                  && (permissions & (group | other)) == QFileDevice::Permissions();
        if (directory)
            ok = ok && permissions.testFlag(QFileDevice::ExeOwner);
        return ok;
    };

    QTemporaryDir projectTemp(QDir::tempPath()
                              + QStringLiteral("/pvforge-mcp-integration-XXXXXX"));
    QTemporaryDir endpointTemp(
        QDir::tempPath()
        + QStringLiteral("/pvforge-mcp-integration-endpoint-XXXXXX"));
    QTemporaryDir checkpointTemp(
        QDir::tempPath()
        + QStringLiteral("/pvforge-mcp-integration-checkpoints-XXXXXX"));
    if (!projectTemp.isValid() || !endpointTemp.isValid()
        || !checkpointTemp.isValid())
    {
        fail(QStringLiteral("could not create temporary directories"));
        return 1;
    }

    qputenv("PVFORGE_CHECKPOINT_DIR", checkpointTemp.path().toUtf8());
    const QString projectPath =
        projectTemp.path() + QStringLiteral("/integration.pvproj");
    const QString endpointPath =
        endpointTemp.path() + QStringLiteral("/mcp-endpoint.json");

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP 集成测试");
    project->width = 640;
    project->height = 360;
    project->fps = 30;

    pvf::Entity* holder =
        project->addVector(0, 100, 80, QColor(120, 120, 120), Qt::white, 0);
    if (!holder)
    {
        fail(QStringLiteral("could not add a scene entity"));
        return 1;
    }
    holder->name = QStringLiteral("场景占位");
    project->setValue(holder->id, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(holder->id, QStringLiteral("x"), 300, 120.0, true);

    QString error;
    pvf::Scene* scene = project->addScene(QStringLiteral("场景 A"), 0, 60,
                                          QColor(90, 170, 255),
                                          QStringLiteral("集成测试场景"), &error);
    if (!scene)
    {
        qCritical() << "mcp-integration-test: add scene failed" << error;
        return 1;
    }
    const pvf::EntityId sceneFolder = scene->folderId;
    project->setEntityParent(holder->id, sceneFolder);
    if (!project->save(projectPath, &error))
    {
        qCritical() << "mcp-integration-test: save failed" << error;
        return 1;
    }

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::McpServer* server = window.mcpServerForTest();
    if (!server)
    {
        fail(QStringLiteral("main window has no MCP server"));
        return 1;
    }
    server->setEndpointFilePath(endpointPath);

    QTimer::singleShot(
        300, &window,
        [&app, &window, &fail, &failIf, &ownerOnly, &checkpointTemp, &failures,
         projectPath, endpointPath, sceneFolder] {
            auto* dialog = window.openSettingsDialogForTest();
            if (!dialog)
            {
                fail(QStringLiteral("settings dialog was not created"));
                app.exit(1);
                return;
            }
            QApplication::processEvents();

            auto* enable =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpEnableCheck"));
            auto* permission = dialog->findChild<QComboBox*>(
                QStringLiteral("mcpPermissionCombo"));
            auto* errorLabel = dialog->findChild<QLabel*>(
                QStringLiteral("mcpErrorLabel"));
            auto* statusLabel = dialog->findChild<QLabel*>(
                QStringLiteral("mcpStatusLabel"));
            auto* logEnabled = dialog->findChild<QCheckBox*>(
                QStringLiteral("mcpLogEnabled"));
            auto* stdioConfigButton = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpCopyStdioConfigButton"));
            if (!enable || !permission || !errorLabel || !statusLabel
                || !logEnabled || !stdioConfigButton)
            {
                qCritical() << "mcp-integration-test: MCP controls missing";
                app.exit(1);
                return;
            }

            pvf::Project* current = window.project();
            pvf::McpServer* server = window.mcpServerForTest();
            pvf::McpLogPanel* panel = window.mcpLogPanelForTest();
            auto* statusButton = window.findChild<QToolButton*>(
                QStringLiteral("mcpStatusButton"));
            if (!current || !server || !panel || !statusButton)
            {
                fail(QStringLiteral("main window MCP objects missing"));
                app.exit(1);
                return;
            }

            const auto logText = [panel] {
                QString text;
                auto* tree = panel->findChild<QTreeWidget*>(
                    QStringLiteral("mcpLogTree"));
                if (!tree)
                    return text;
                for (int i = 0; i < tree->topLevelItemCount(); ++i)
                {
                    QTreeWidgetItem* item = tree->topLevelItem(i);
                    for (int column = 0; column < item->columnCount(); ++column)
                        text += item->text(column) + QLatin1Char('\n');
                    for (int child = 0; child < item->childCount(); ++child)
                        for (int column = 0;
                             column < item->child(child)->columnCount(); ++column)
                            text += item->child(child)->text(column)
                                    + QLatin1Char('\n');
                }
                return text;
            };

            // Only a human action starts the server.
            enable->setChecked(true);
            QApplication::processEvents();
            if (!server->isRunning())
            {
                fail(QStringLiteral("MCP did not start"));
                app.exit(1);
                return;
            }
            const quint16 port = server->port();
            const QByteArray token = server->token();
            const QByteArray tokenText = token;

            // The endpoint file follows the published contract and is private.
            const QFileInfo endpointInfo(endpointPath);
            failIf(!endpointInfo.exists(),
                   QStringLiteral("endpoint file was not written"));
            failIf(!ownerOnly(endpointPath, false),
                   QStringLiteral("endpoint file is not 0600"));
            failIf(!ownerOnly(endpointInfo.absolutePath(), true),
                   QStringLiteral("endpoint directory is not 0700"));
            QFile endpointFile(endpointPath);
            QJsonObject endpointObject;
            if (endpointFile.open(QIODevice::ReadOnly))
            {
                endpointObject =
                    QJsonDocument::fromJson(endpointFile.readAll()).object();
                endpointFile.close();
            }
            failIf(endpointObject.value(QStringLiteral("format")).toString()
                       != QStringLiteral("pvforge-mcp-endpoint"),
                   QStringLiteral("endpoint format mismatch"));
            failIf(endpointObject.value(QStringLiteral("version")).toInt() != 1,
                   QStringLiteral("endpoint version mismatch"));
            failIf(endpointObject.value(QStringLiteral("url")).toString()
                       != server->endpointUrl(),
                   QStringLiteral("endpoint URL mismatch"));
            failIf(endpointObject.value(QStringLiteral("token")).toString()
                       != QString::fromLatin1(token),
                   QStringLiteral("endpoint token mismatch"));
            failIf(qint64(endpointObject.value(QStringLiteral("pid")).toDouble())
                       != QCoreApplication::applicationPid(),
                   QStringLiteral("endpoint pid mismatch"));
            failIf(server->endpointUrl().contains(QString::fromLatin1(token)),
                   QStringLiteral("endpoint URL leaks the token"));

            // An unwritable endpoint file is only a warning: the authenticated
            // HTTP server must still come up.
            QFile endpointBlocker(projectPath + QStringLiteral(".blocker"));
            if (endpointBlocker.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                pvf::McpServer withoutEndpoint(current);
                withoutEndpoint.setEndpointFilePath(
                    endpointBlocker.fileName()
                    + QStringLiteral("/mcp-endpoint.json"));
                QString withoutEndpointError;
                const quint16 withoutEndpointPort =
                    withoutEndpoint.start(0, &withoutEndpointError);
                failIf(withoutEndpointPort == 0,
                       QStringLiteral("server refused to start when the endpoint "
                                      "file was unwritable: %1")
                           .arg(withoutEndpointError));
                withoutEndpoint.stop();
                endpointBlocker.close();
            }

            const auto toolNames = [&] {
                QStringList names;
                QJsonObject request;
                request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
                request[QStringLiteral("id")] = 1;
                request[QStringLiteral("method")] = QStringLiteral("tools/list");
                const QJsonObject response = mcpRpc(port, token, request);
                const QJsonArray tools =
                    response.value(QStringLiteral("result"))
                        .toObject()
                        .value(QStringLiteral("tools"))
                        .toArray();
                for (const QJsonValue& value : tools)
                    names.append(value.toObject()
                                     .value(QStringLiteral("name"))
                                     .toString());
                return names;
            };
            const auto callTool = [&](int id, const QString& name,
                                      const QJsonObject& args) {
                QJsonObject params;
                params[QStringLiteral("name")] = name;
                params[QStringLiteral("arguments")] = args;
                QJsonObject request;
                request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
                request[QStringLiteral("id")] = id;
                request[QStringLiteral("method")] = QStringLiteral("tools/call");
                request[QStringLiteral("params")] = params;
                const QJsonObject response = mcpRpc(port, token, request);
                const QJsonObject result =
                    response.value(QStringLiteral("result")).toObject();
                return result.value(QStringLiteral("structuredContent"))
                    .toObject();
            };
            const auto errorCode = [](const QJsonObject& structured) {
                return structured.value(QStringLiteral("error"))
                    .toObject()
                    .value(QStringLiteral("code"))
                    .toString();
            };

            // Read-only: no write tool is advertised and a direct call is refused.
            QStringList names = toolNames();
            for (const QString& required :
                 {QStringLiteral("project.summary"), QStringLiteral("timeline.get"),
                  QStringLiteral("entity.get"), QStringLiteral("assets.list"),
                  QStringLiteral("guide.get")})
                failIf(!names.contains(required),
                       QStringLiteral("read-only tools/list missing %1")
                           .arg(required));
            for (const QString& write : pvf::McpServer::writeToolNames())
                failIf(names.contains(write),
                       QStringLiteral("read-only tools/list advertises %1")
                           .arg(write));
            const QJsonObject denied = callTool(
                2, QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("name"), QStringLiteral("只读拒绝")}});
            failIf(errorCode(denied) != QStringLiteral("permission_denied"),
                   QStringLiteral("read-only write call did not return "
                                  "permission_denied"));

            // A project without a saved path must not arm.
            const QString savedPath = current->filePath;
            current->filePath.clear();
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            failIf(window.mcpCommandsForTest() != nullptr,
                   QStringLiteral("unsaved project armed writes"));
            failIf(errorLabel->isHidden()
                       || !errorLabel->text().contains(QStringLiteral("保存")),
                   QStringLiteral("unsaved project did not explain the save "
                                  "requirement"));
            failIf(permission->currentIndex() != 0,
                   QStringLiteral("permission combo did not revert after failure"));
            current->filePath = savedPath;

            // A playhead in a scene gap must not arm.
            window.goToFrame(90);
            QApplication::processEvents();
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            failIf(window.mcpCommandsForTest() != nullptr,
                   QStringLiteral("scene gap armed writes"));
            failIf(!errorLabel->text().contains(QStringLiteral("场景")),
                   QStringLiteral("scene gap did not explain the scene requirement"));
            failIf(permission->currentIndex() != 0,
                   QStringLiteral("permission combo did not revert after gap "
                                  "failure"));
            window.goToFrame(10);
            QApplication::processEvents();

            pvf::CheckpointManager checkpoints(checkpointTemp.path());
            failIf(!checkpoints.listCheckpoints(projectPath).isEmpty(),
                   QStringLiteral("failed arm attempts created checkpoints"));

            // With a saved project and an active scene, arming succeeds only
            // after a durable checkpoint.
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            pvf::McpCommandService* commands = window.mcpCommandsForTest();
            failIf(!commands || !commands->isArmed(),
                   QStringLiteral("saved scene did not arm writes"));
            if (commands)
            {
                failIf(commands->sceneRootId() != sceneFolder,
                       QStringLiteral("armed scope is not the scene folder"));
                failIf(commands->sceneName() != QStringLiteral("场景 A"),
                       QStringLiteral("armed scope has the wrong scene name"));
                failIf(commands->allowGlobal(),
                       QStringLiteral("global objects unlocked without UI"));
            }
            failIf(!errorLabel->isHidden(),
                   QStringLiteral("arm success left an error visible"));
            const QVector<pvf::CheckpointInfo> firstCheckpoints =
                checkpoints.listCheckpoints(projectPath);
            failIf(firstCheckpoints.isEmpty(),
                   QStringLiteral("arm did not create a checkpoint"));
            failIf(!logText().contains(QStringLiteral("已武装")),
                   QStringLiteral("log does not show the armed scene"));
            failIf(!statusButton->text().contains(QStringLiteral("武装")),
                   QStringLiteral("status bar does not show armed state"));

            names = toolNames();
            for (const QString& write : pvf::McpServer::writeToolNames())
                failIf(!names.contains(write),
                       QStringLiteral("armed tools/list missing %1").arg(write));

            // HTTP write tools go straight to McpCommandService.
            const QJsonObject created = callTool(
                10, QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("name"), QStringLiteral("AI 标题")}});
            failIf(!created.value(QStringLiteral("ok")).toBool(),
                   QStringLiteral("entity.create failed: %1")
                       .arg(QString::fromUtf8(
                           QJsonDocument(created).toJson(QJsonDocument::Compact))));
            const pvf::EntityId createdId =
                created.value(QStringLiteral("data"))
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toInt();
            const pvf::Entity* createdEntity = current->entity(createdId);
            failIf(!createdEntity || createdEntity->parent != sceneFolder,
                   QStringLiteral("entity.create did not land in the armed scene"));

            QJsonArray keys;
            keys.append(QJsonObject{{QStringLiteral("frame"), 0},
                                    {QStringLiteral("value"), 0}});
            keys.append(QJsonObject{{QStringLiteral("frame"), 30},
                                    {QStringLiteral("value"), 120}});
            const QJsonObject keyed = callTool(
                11, QStringLiteral("keyframes.set"),
                QJsonObject{{QStringLiteral("id"), createdId},
                            {QStringLiteral("property"), QStringLiteral("x")},
                            {QStringLiteral("keys"), keys}});
            failIf(!keyed.value(QStringLiteral("ok")).toBool(),
                   QStringLiteral("keyframes.set failed"));
            if (const pvf::Entity* entity = current->entity(createdId))
                failIf(entity->tracks.value(QStringLiteral("x")).keys.size() != 2,
                       QStringLiteral("keyframes.set did not write two keys"));

            QJsonArray operations;
            operations.append(QJsonObject{
                {QStringLiteral("op"), QStringLiteral("entity.update")},
                {QStringLiteral("args"),
                 QJsonObject{{QStringLiteral("id"), createdId},
                             {QStringLiteral("name"),
                              QStringLiteral("批量改名")}}}});
            operations.append(QJsonObject{
                {QStringLiteral("op"), QStringLiteral("keyframes.set")},
                {QStringLiteral("args"),
                 QJsonObject{
                     {QStringLiteral("id"), createdId},
                     {QStringLiteral("property"), QStringLiteral("y")},
                     {QStringLiteral("keys"),
                      QJsonArray{
                          QJsonObject{{QStringLiteral("frame"), 0},
                                      {QStringLiteral("value"), 0}},
                          QJsonObject{{QStringLiteral("frame"), 15},
                                      {QStringLiteral("value"), 60}}}}}}});
            const QJsonObject batch = callTool(
                12, QStringLiteral("batch.run"),
                QJsonObject{{QStringLiteral("operations"), operations}});
            failIf(!batch.value(QStringLiteral("ok")).toBool(),
                   QStringLiteral("batch.run failed"));
            failIf(batch.value(QStringLiteral("data"))
                           .toObject()
                           .value(QStringLiteral("count"))
                           .toInt()
                       != 2,
                   QStringLiteral("batch.run count mismatch"));
            if (const pvf::Entity* entity = current->entity(createdId))
            {
                failIf(entity->name != QStringLiteral("批量改名"),
                       QStringLiteral("batch.run did not rename the entity"));
                failIf(entity->tracks.value(QStringLiteral("y")).keys.size() != 2,
                       QStringLiteral("batch.run did not set y keys"));
            }

            // Notifications are recorded without interrupting the call.
            int notifications = 0;
            QString lastNotification;
            QObject::connect(server, &pvf::McpServer::notificationReceived,
                             [&](const QString& method, const QString&) {
                                 ++notifications;
                                 lastNotification = method;
                             });
            QJsonObject notification;
            notification[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            notification[QStringLiteral("method")] =
                QStringLiteral("notifications/cancelled");
            notification[QStringLiteral("params")] =
                QJsonObject{{QStringLiteral("requestId"), 11},
                            {QStringLiteral("reason"), QStringLiteral("test")}};
            const McpHttpResponse notificationResponse = mcpHttpPost(
                port,
                QJsonDocument(notification).toJson(QJsonDocument::Compact),
                token);
            failIf(notificationResponse.status != 202,
                   QStringLiteral("notification HTTP %1 != 202")
                       .arg(notificationResponse.status));
            failIf(notifications != 1
                       || lastNotification
                              != QStringLiteral("notifications/cancelled"),
                   QStringLiteral("cancellation notification was not forwarded"));

            // Token never reaches logs, status text or errors.
            failIf(logText().contains(QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the log"));
            failIf(statusButton->text().contains(QString::fromLatin1(tokenText))
                       || statusButton->toolTip().contains(
                           QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the status entry"));
            failIf(statusLabel->text().contains(QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the settings status"));
            failIf(errorLabel->text().contains(QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the settings error"));

            // Emergency stop: writes answer stopped_by_human and change nothing.
            const int entityCountBeforeStop = int(current->entities().size());
            auto* stopButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogStopButton"));
            failIf(!stopButton || !stopButton->isEnabled(),
                   QStringLiteral("stop AI button is missing or disabled"));
            if (stopButton)
                stopButton->click();
            QApplication::processEvents();
            failIf(!commands || !commands->isStopped() || commands->isArmed(),
                   QStringLiteral("emergency stop did not mark the service "
                                  "stopped"));
            auto* logStatus = panel->findChild<QLabel*>(
                QStringLiteral("mcpLogStatusLabel"));
            failIf(!logStatus
                       || !logStatus->text().contains(QStringLiteral("急停")),
                   QStringLiteral("log header does not show the emergency stop"));
            failIf(!statusButton->text().contains(QStringLiteral("急停")),
                   QStringLiteral("status bar does not show the emergency stop"));
            const QJsonObject stopped = callTool(
                20, QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("name"), QStringLiteral("急停拒绝")}});
            failIf(errorCode(stopped) != QStringLiteral("stopped_by_human"),
                   QStringLiteral("post-stop write did not return "
                                  "stopped_by_human"));
            failIf(current->entities().size() != entityCountBeforeStop,
                   QStringLiteral("post-stop write changed the project"));
            names = toolNames();
            for (const QString& write : pvf::McpServer::writeToolNames())
                failIf(names.contains(write),
                       QStringLiteral("stopped tools/list advertises %1")
                           .arg(write));

            // Re-arm for the same scene; each arm creates a new checkpoint.
            failIf(permission->currentIndex() != 0,
                   QStringLiteral("emergency stop did not reset the permission "
                                  "combo"));
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            failIf(!window.mcpCommandsForTest()
                       || !window.mcpCommandsForTest()->isArmed(),
                   QStringLiteral("re-arm after stop failed"));
            failIf(window.mcpCommandsForTest() == commands,
                   QStringLiteral("re-arm reused the stopped service"));
            failIf(checkpoints.listCheckpoints(projectPath).size()
                       <= firstCheckpoints.size(),
                   QStringLiteral("re-arm did not create another checkpoint"));

            // Stopping MCP removes the endpoint file and the write service.
            enable->setChecked(false);
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("disabling MCP did not stop the server"));
            failIf(QFileInfo::exists(endpointPath),
                   QStringLiteral("endpoint file survived MCP shutdown"));
            failIf(window.mcpCommandsForTest() != nullptr,
                   QStringLiteral("write service survived MCP shutdown"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status is not stopped"));

            // The stdio configuration references the installed bridge.
            stdioConfigButton->click();
            QApplication::processEvents();
            const QJsonObject stdioConfig =
                QJsonDocument::fromJson(
                    QApplication::clipboard()->text().toUtf8())
                    .object();
            const QJsonObject stdioServer =
                stdioConfig.value(QStringLiteral("mcpServers"))
                    .toObject()
                    .value(QStringLiteral("pvforge"))
                    .toObject();
            failIf(!stdioServer.value(QStringLiteral("command"))
                        .toString()
                        .endsWith(QStringLiteral("pvforge-mcp")),
                   QStringLiteral("stdio config has no pvforge-mcp command"));
            failIf(!stdioServer.value(QStringLiteral("args"))
                        .toArray()
                        .contains(QJsonValue(QStringLiteral("--endpoint-file"))),
                   QStringLiteral("stdio config has no --endpoint-file"));

            dialog->close();
            QApplication::processEvents();

            qInfo() << "mcp-integration-test failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
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

    // Keep self-tests independent from the user's saved layout, monitor
    // geometry and last project list. A fresh config per test process makes
    // pixel/geometry checks reproducible.
    bool testMode = false;
    for (const QString& argument : app.arguments())
        if (argument.endsWith(QStringLiteral("-test")))
            testMode = true;
    if (testMode)
    {
        // Per-process config so parallel test runs in different worktrees do
        // not delete each other's settings mid-test.
        const QString configDir =
            QDir::tempPath() + QStringLiteral("/pvforge-test-config-")
            + QString::number(QCoreApplication::applicationPid());
        QDir(configDir).removeRecursively();
        QDir().mkpath(configDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, configDir);
    }

    pvf::Lang::load(pvf::Settings::language());

    if (app.arguments().contains(QStringLiteral("--self-test")))
        return selfTest();
    if (app.arguments().contains(QStringLiteral("--save-test")))
        return saveTest();
    if (app.arguments().contains(QStringLiteral("--scene-test")))
        return sceneTest();
    if (app.arguments().contains(QStringLiteral("--annotation-test")))
        return annotationTest();
    if (app.arguments().contains(QStringLiteral("--checkpoint-test")))
        return checkpointTest();
    if (app.arguments().contains(QStringLiteral("--scene-ui-test")))
        return sceneUiTest(app);
    if (app.arguments().contains(QStringLiteral("--annotation-ui-test")))
        return annotationUiTest(app);
    if (app.arguments().contains(QStringLiteral("--autosave-test")))
        return autoSaveTest();
    if (app.arguments().contains(QStringLiteral("--special-test")))
        return specialTest(app);
    if (app.arguments().contains(QStringLiteral("--export-test")))
        return exportTest(app);
    if (app.arguments().contains(QStringLiteral("--async-export-test")))
        return asyncExportTest(app);
    if (app.arguments().contains(QStringLiteral("--mcp-readonly-test")))
        return mcpReadonlyTest();
    if (app.arguments().contains(QStringLiteral("--mcp-http-test")))
        return mcpHttpTest();
    if (app.arguments().contains(QStringLiteral("--mcp-ui-test")))
        return mcpUiTest(app);
    if (app.arguments().contains(QStringLiteral("--mcp-bridge-test")))
        return pvf::mcpBridgeTest();
    if (app.arguments().contains(QStringLiteral("--mcp-write-test")))
        return mcpWriteTest();
    if (app.arguments().contains(QStringLiteral("--mcp-integration-test")))
        return mcpIntegrationTest(app);
    if (app.arguments().contains(QStringLiteral("--keyframe-edit-test")))
        return keyframeEditTest(app);
    if (app.arguments().contains(QStringLiteral("--frame-restore-test")))
        return frameRestoreTest(app);
    if (app.arguments().contains(QStringLiteral("--layer-test")))
        return layerTest();
    if (app.arguments().contains(QStringLiteral("--replace-test")))
        return replaceSourceTest();
    if (app.arguments().contains(QStringLiteral("--library-picker-test")))
        return libraryPickerTest();
    if (app.arguments().contains(QStringLiteral("--clipboard-test")))
        return clipboardTest(app);
    if (app.arguments().contains(QStringLiteral("--group-test")))
        return groupTest(app);
    if (app.arguments().contains(QStringLiteral("--opacity-test")))
        return opacityTest(app);
    if (app.arguments().contains(QStringLiteral("--multidrag-test")))
        return multiDragTest(app);
    if (app.arguments().contains(QStringLiteral("--keyframe-drag-test")))
        return keyframeDragTest(app);
    if (app.arguments().contains(QStringLiteral("--curve-editor-test")))
        return curveEditorTest(app);
    if (app.arguments().contains(QStringLiteral("--audio-undo-test")))
        return audioUndoTest(app);
    if (app.arguments().contains(QStringLiteral("--special-demo")))
        return specialDemo(app);
    if (app.arguments().contains(QStringLiteral("--welcome-test")))
        return welcomeTest(app);
    if (app.arguments().contains(QStringLiteral("--library-test")))
        return libraryTest(app);
    if (app.arguments().contains(QStringLiteral("--open-timeline-test")))
        return openTimelineTest(app);
    if (app.arguments().contains(QStringLiteral("--clip-timeline-test")))
        return clipTimelineTest(app);
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
    if (app.arguments().contains(QStringLiteral("--project-limit-test")))
        return projectLimitTest(app);
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
    if (app.arguments().contains(QStringLiteral("--camera-parent-test")))
        return cameraParentTest(app);
    if (app.arguments().contains(QStringLiteral("--monitor-feature-test")))
        return monitorFeatureTest(app);
    if (app.arguments().contains(QStringLiteral("--easing-test")))
        return easingTest(app);
    if (app.arguments().contains(QStringLiteral("--folder-test")))
        return folderTest(app);
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
    if (app.arguments().contains(QStringLiteral("--html-capture-test")))
        return pvf::html::htmlCaptureTest();
    if (app.arguments().contains(QStringLiteral("--html-layer-test")))
        return pvf::html::htmlLayerTest();
    if (app.arguments().contains(QStringLiteral("--html-export-test")))
        return pvf::html::htmlExportTest();

    pvf::WelcomeWindow welcome;
    welcome.show();
    return app.exec();
}
