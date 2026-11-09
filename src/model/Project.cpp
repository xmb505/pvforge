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
    return object;
}

std::optional<Marker> markerFromJson(const QJsonObject& object, QString* error)
{
    Q_UNUSED(error);
    Marker marker;
    marker.id = detail::jsonString(object, QStringLiteral("id"));
    marker.frame = detail::jsonInt(object, QStringLiteral("frame"), marker.frame);
    marker.title = detail::jsonString(object, QStringLiteral("title"));
    marker.note = detail::jsonString(object, QStringLiteral("note"));
    marker.tags = detail::jsonStringList(object, QStringLiteral("tags"));
    marker.color = detail::jsonString(
        object, QStringLiteral("color"), marker.color);
    return marker;
}

QJsonObject toJson(const ProjectSettings& settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), settings.name);
    object.insert(QStringLiteral("width"), settings.width);
    object.insert(QStringLiteral("height"), settings.height);
    object.insert(QStringLiteral("fps"), settings.fps);
    object.insert(QStringLiteral("background"), settings.background);
    return object;
}

std::optional<ProjectSettings> projectSettingsFromJson(const QJsonObject& object,
                                                       QString* error)
{
    Q_UNUSED(error);
    ProjectSettings settings;
    settings.name = detail::jsonString(
        object, QStringLiteral("name"), settings.name);
    settings.width = detail::jsonInt(
        object, QStringLiteral("width"), settings.width);
    settings.height = detail::jsonInt(
        object, QStringLiteral("height"), settings.height);
    settings.fps = detail::jsonInt(object, QStringLiteral("fps"), settings.fps);
    settings.background = detail::jsonString(
        object, QStringLiteral("background"), settings.background);
    return settings;
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
    return object;
}

std::optional<Project> projectFromJson(const QJsonObject& object, QString* error)
{
    const QJsonValue formatValue = object.value(QStringLiteral("format"));
    if (!formatValue.isUndefined() && !formatValue.isNull()
        && formatValue.toString() != QLatin1String(kProjectFormat))
    {
        detail::setError(
            error,
            QStringLiteral("工程格式不匹配：%1").arg(formatValue.toString()));
        return std::nullopt;
    }

    const QJsonValue versionValue = object.value(QStringLiteral("version"));
    if (!versionValue.isUndefined() && !versionValue.isNull()
        && detail::jsonInt(object, QStringLiteral("version"), -1)
               != kProjectFormatVersion)
    {
        detail::setError(
            error,
            QStringLiteral("不支持的工程版本：%1")
                .arg(detail::jsonInt(object, QStringLiteral("version"), -1)));
        return std::nullopt;
    }

    Project project;
    project.settings = *projectSettingsFromJson(object, error);

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
            const auto marker = markerFromJson(markers.at(i).toObject(), error);
            if (!marker)
                return std::nullopt;
            project.markers.append(*marker);
        }
    }

    if (!validateProject(project, error))
        return std::nullopt;
    return project;
}

} // namespace pvf::model
