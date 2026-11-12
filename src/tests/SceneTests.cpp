#include "TestSupport.h"
#include "TestRegistry.h"

// Historically this translation unit covered scenes and annotations in the
// legacy `pvf::Project` model, plus their scene band / annotation lane in the
// old `TimelinePanel`. The timeline no longer draws either band (the accessors
// are documented stubs in `TimelinePanel.h`), and scenes are not part of the
// next model, so `--scene-test`, `--scene-ui-test` and `--annotation-ui-test`
// were retired with issue #72. What is left is the legacy-model annotation and
// keyframe-note roundtrip, which still runs against live `core/Project` code.

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

} // namespace

namespace pvf::tests {

void registerSceneTests(Registry& registry)
{
    registry.add(QStringLiteral("--annotation-test"),
                 [](QApplication&) { return annotationTest(); });
}

} // namespace pvf::tests
