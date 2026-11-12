#include "Project.h"

#include "JsonUtil.h"
#include "Uuid.h"

#include <QJsonArray>
#include <QMetaType>
#include <QSet>
#include <algorithm>
#include <functional>

namespace pvf::model {

QJsonObject toJson(const Marker& marker)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), marker.id);
    object.insert(QStringLiteral("frame"), marker.frame);
    object.insert(QStringLiteral("title"), marker.title);
    object.insert(QStringLiteral("note"), marker.note);

    QJsonArray tags;
    for (const QString& tag : marker.tags)
        tags.append(tag);
    object.insert(QStringLiteral("tags"), tags);
    object.insert(QStringLiteral("color"), marker.color);
    detail::mergeExtras(object, marker.extras);
    return object;
}

std::optional<Marker> markerFromJson(const QJsonObject& object,
                                     const ParseOptions& options,
                                     QString* error)
{
    if (!detail::requireField(object, QStringLiteral("id"), options, error)
        || !detail::requireField(object, QStringLiteral("frame"), options, error))
    {
        return std::nullopt;
    }

    Marker marker;
    marker.id = detail::jsonString(object, QStringLiteral("id"));
    marker.frame = detail::optionalInt(
        object, QStringLiteral("frame"), marker.frame, options);
    marker.title = detail::optionalString(
        object, QStringLiteral("title"), marker.title, options);
    marker.note = detail::optionalString(
        object, QStringLiteral("note"), marker.note, options);
    detail::warnDefaulted(object, QStringLiteral("tags"), options);
    marker.tags = detail::jsonStringList(object, QStringLiteral("tags"));
    marker.color = detail::optionalString(
        object, QStringLiteral("color"), marker.color, options);

    marker.extras = detail::extrasFrom(
        object, {QStringLiteral("id"), QStringLiteral("frame"),
                 QStringLiteral("title"), QStringLiteral("note"),
                 QStringLiteral("tags"), QStringLiteral("color")});
    return marker;
}

std::optional<Marker> markerFromJson(const QJsonObject& object, QString* error)
{
    return markerFromJson(object, ParseOptions{}, error);
}

QJsonObject toJson(const ProjectSettings& settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), settings.name);
    object.insert(QStringLiteral("width"), settings.width);
    object.insert(QStringLiteral("height"), settings.height);
    object.insert(QStringLiteral("fps"), settings.fps);
    object.insert(QStringLiteral("background"), settings.background);
    detail::mergeExtras(object, settings.extras);
    return object;
}

std::optional<ProjectSettings> projectSettingsFromJson(
    const QJsonObject& object, const ParseOptions& options, QString* error)
{
    if (!detail::requireField(object, QStringLiteral("width"), options, error)
        || !detail::requireField(object, QStringLiteral("height"), options, error)
        || !detail::requireField(object, QStringLiteral("fps"), options, error))
    {
        return std::nullopt;
    }

    ProjectSettings settings;
    settings.name = detail::optionalString(
        object, QStringLiteral("name"), settings.name, options);
    settings.width = detail::optionalInt(
        object, QStringLiteral("width"), settings.width, options);
    settings.height = detail::optionalInt(
        object, QStringLiteral("height"), settings.height, options);
    settings.fps = detail::optionalInt(
        object, QStringLiteral("fps"), settings.fps, options);
    settings.background = detail::optionalString(
        object, QStringLiteral("background"), settings.background, options);

    settings.extras = detail::extrasFrom(
        object, {QStringLiteral("name"), QStringLiteral("width"),
                 QStringLiteral("height"), QStringLiteral("fps"),
                 QStringLiteral("background")});
    return settings;
}

std::optional<ProjectSettings> projectSettingsFromJson(const QJsonObject& object,
                                                       QString* error)
{
    return projectSettingsFromJson(object, ParseOptions{}, error);
}

namespace {

bool validateSolverTargets(const Project& project, QString* error,
                           QHash<Uuid, QSet<Uuid>>* edges)
{
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd(); ++it)
    {
        const Entity& entity = it.value();
        for (const SolverSegment& segment : entity.solvers)
        {
            if (segment.effect == SolverEffect::Spin)
                continue;

            const QString label =
                QStringLiteral("实体 %1 的求解段 %2（%3）")
                    .arg(entity.uuid, segment.id,
                         solverEffectName(segment.effect));

            const QVariant targetValue = segment.params.value(
                QStringLiteral("targetUuid"));
            if (!targetValue.isValid())
            {
                detail::setError(
                    error, label + QStringLiteral("缺少 targetUuid"));
                return false;
            }
            if (targetValue.typeId() != QMetaType::QString)
            {
                detail::setError(
                    error,
                    label + QStringLiteral("的 targetUuid 必须是字符串"));
                return false;
            }

            const QString target = targetValue.toString();
            if (target.isEmpty())
            {
                detail::setError(
                    error,
                    label + QStringLiteral("的 targetUuid 不能为空"));
                return false;
            }
            if (!isValidUuid(target))
            {
                detail::setError(
                    error,
                    label
                        + QStringLiteral("的 targetUuid 不是合法 UUID：%1")
                              .arg(target));
                return false;
            }
            if (!project.entities.contains(target))
            {
                detail::setError(
                    error,
                    label
                        + QStringLiteral("的 targetUuid 指向的实体 %1 不存在")
                              .arg(target));
                return false;
            }
            if (target == entity.uuid)
            {
                detail::setError(
                    error,
                    label + QStringLiteral("的 targetUuid 不能指向实体自己"));
                return false;
            }

            if (edges)
                (*edges)[entity.uuid].insert(target);
        }
    }
    return true;
}

bool detectSolverTargetCycles(const QHash<Uuid, QSet<Uuid>>& edges,
                              QString* error)
{
    QHash<Uuid, int> state; // 1 = visiting, 2 = done
    QVector<Uuid> stack;

    std::function<bool(const Uuid&)> visit = [&](const Uuid& node) -> bool {
        state.insert(node, 1);
        stack.append(node);

        const auto outgoing = edges.constFind(node);
        if (outgoing != edges.constEnd())
        {
            QVector<Uuid> targets(outgoing.value().begin(),
                                  outgoing.value().end());
            std::sort(targets.begin(), targets.end());
            for (const Uuid& target : targets)
            {
                const int targetState = state.value(target, 0);
                if (targetState == 1)
                {
                    const int start = stack.indexOf(target);
                    QStringList path;
                    for (int i = start; i < stack.size(); ++i)
                        path.append(stack.at(i));
                    path.append(target);
                    detail::setError(
                        error,
                        QStringLiteral("求解段目标存在循环依赖：%1")
                            .arg(path.join(QStringLiteral(" -> "))));
                    return true;
                }
                if (targetState == 0 && visit(target))
                    return true;
            }
        }

        stack.removeLast();
        state.insert(node, 2);
        return false;
    };

    QVector<Uuid> nodes = edges.keys();
    std::sort(nodes.begin(), nodes.end());
    for (const Uuid& node : nodes)
    {
        if (state.value(node, 0) == 0 && visit(node))
            return false;
    }
    return true;
}

} // namespace

bool validateProject(const Project& project, QString* error)
{
    if (project.settings.width <= 0 || project.settings.height <= 0)
    {
        detail::setError(error, QStringLiteral("工程分辨率必须大于 0"));
        return false;
    }
    if (project.settings.fps <= 0)
    {
        detail::setError(error, QStringLiteral("工程 fps 必须大于 0"));
        return false;
    }

    for (auto it = project.entities.constBegin(); it != project.entities.constEnd(); ++it)
    {
        if (!validateEntity(it.value(), error))
            return false;
        if (it.key() != it.value().uuid)
        {
            detail::setError(
                error,
                QStringLiteral("实体索引键 %1 与实体 UUID %2 不一致")
                    .arg(it.key(), it.value().uuid));
            return false;
        }
    }

    for (auto it = project.entities.constBegin(); it != project.entities.constEnd(); ++it)
    {
        const Entity& entity = it.value();
        if (entity.parentUuid.isEmpty())
            continue;

        const auto parent = project.entities.constFind(entity.parentUuid);
        if (parent == project.entities.constEnd())
        {
            detail::setError(
                error,
                QStringLiteral("实体 %1 的父实体 %2 不存在")
                    .arg(entity.uuid, entity.parentUuid));
            return false;
        }

        if (entity.start < parent.value().start
            || entity.end > parent.value().end)
        {
            detail::setError(
                error,
                QStringLiteral("实体 %1 的时间范围 [%2, %3) 超出父实体 %4 的 [%5, %6)")
                    .arg(entity.uuid)
                    .arg(entity.start)
                    .arg(entity.end)
                    .arg(parent.value().uuid)
                    .arg(parent.value().start)
                    .arg(parent.value().end));
            return false;
        }
    }

    // Walk every parent chain to reject cycles. Entities validated above make
    // sure a non-empty parentUuid always resolves.
    QSet<Uuid> resolved;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd(); ++it)
    {
        QSet<Uuid> path;
        Uuid current = it.key();
        while (!current.isEmpty() && !resolved.contains(current))
        {
            if (path.contains(current))
            {
                detail::setError(
                    error,
                    QStringLiteral("实体父子关系存在循环：%1").arg(current));
                return false;
            }
            path.insert(current);

            const auto entity = project.entities.constFind(current);
            if (entity == project.entities.constEnd())
                break;
            current = entity.value().parentUuid;
        }
        resolved.unite(path);
    }

    QHash<Uuid, QSet<Uuid>> solverEdges;
    if (!validateSolverTargets(project, error, &solverEdges))
        return false;
    if (!detectSolverTargetCycles(solverEdges, error))
        return false;

    for (const Marker& marker : project.markers)
    {
        if (marker.id.isEmpty())
        {
            detail::setError(error, QStringLiteral("marker id 不能为空"));
            return false;
        }
        if (marker.frame < 0)
        {
            detail::setError(
                error,
                QStringLiteral("marker %1 的 frame 不能小于 0").arg(marker.id));
            return false;
        }
    }

    return true;
}

QJsonObject toJson(const Project& project)
{
    QJsonObject object;
    object.insert(QStringLiteral("format"),
                  QString::fromLatin1(kProjectFormat));
    object.insert(QStringLiteral("version"), kProjectFormatVersion);
    object.insert(QStringLiteral("name"), project.settings.name);
    object.insert(QStringLiteral("width"), project.settings.width);
    object.insert(QStringLiteral("height"), project.settings.height);
    object.insert(QStringLiteral("fps"), project.settings.fps);
    object.insert(QStringLiteral("background"), project.settings.background);

    QJsonArray markers;
    for (const Marker& marker : project.markers)
        markers.append(toJson(marker));
    object.insert(QStringLiteral("markers"), markers);
    detail::mergeExtras(object, project.extras);
    return object;
}

std::optional<Project> projectFromJson(const QJsonObject& object,
                                       const ParseOptions& options,
                                       QString* error)
{
    if (detail::hasField(object, QStringLiteral("format")))
    {
        const QString format =
            detail::jsonString(object, QStringLiteral("format"));
        if (format != QLatin1String(kProjectFormat))
        {
            detail::setError(
                error, QStringLiteral("工程格式不匹配：%1").arg(format));
            return std::nullopt;
        }
    }
    else if (!detail::requireField(object, QStringLiteral("format"), options,
                                   error))
    {
        return std::nullopt;
    }

    if (!detail::requireField(object, QStringLiteral("version"), options, error))
        return std::nullopt;

    int version = kProjectFormatVersion;
    if (detail::hasField(object, QStringLiteral("version")))
    {
        const QJsonValue versionValue = object.value(QStringLiteral("version"));
        if (!versionValue.isDouble())
        {
            detail::setError(
                error, QStringLiteral("字段 version 必须是整数"));
            return std::nullopt;
        }
        version = versionValue.toInt();
    }

    if (version > kProjectFormatVersion)
    {
        detail::setError(
            error,
            QStringLiteral("工程版本 %1 高于本程序支持的版本 %2：请升级 PVForge "
                           "后再打开（原文件不会被修改）")
                .arg(version)
                .arg(kProjectFormatVersion));
        return std::nullopt;
    }
    if (version < kProjectFormatVersion)
    {
        detail::setError(
            error,
            QStringLiteral("工程版本 %1 低于本程序支持的版本 %2：需要先迁移到版本 "
                           "%2（ProjectStore 打开时会自动升级）")
                .arg(version)
                .arg(kProjectFormatVersion));
        return std::nullopt;
    }

    Project project;
    // Settings live flattened at the top level of project.json; their extras
    // mirror the document's unknown keys and are superseded by project.extras.
    const auto settings = projectSettingsFromJson(object, options, error);
    if (!settings)
        return std::nullopt;
    project.settings = *settings;

    const QJsonValue markersValue = object.value(QStringLiteral("markers"));
    if (!markersValue.isUndefined() && !markersValue.isNull())
    {
        if (!markersValue.isArray())
        {
            detail::setError(error, QStringLiteral("markers 必须是数组"));
            return std::nullopt;
        }
        const QJsonArray markers = markersValue.toArray();
        for (int i = 0; i < markers.size(); ++i)
        {
            if (!markers.at(i).isObject())
            {
                detail::setError(error,
                                 QStringLiteral("第 %1 个 marker 不是对象").arg(i));
                return std::nullopt;
            }
            ParseOptions markerOptions = detail::childOptions(
                options, QStringLiteral("markers[%1]").arg(i));
            const auto marker =
                markerFromJson(markers.at(i).toObject(), markerOptions, error);
            if (!marker)
                return std::nullopt;
            project.markers.append(*marker);
        }
    }

    project.extras = detail::extrasFrom(
        object,
        {QStringLiteral("format"), QStringLiteral("version"),
         QStringLiteral("name"), QStringLiteral("width"),
         QStringLiteral("height"), QStringLiteral("fps"),
         QStringLiteral("background"), QStringLiteral("markers")});

    if (!validateProject(project, error))
        return std::nullopt;
    return project;
}

std::optional<Project> projectFromJson(const QJsonObject& object, QString* error)
{
    return projectFromJson(object, ParseOptions{}, error);
}

} // namespace pvf::model
