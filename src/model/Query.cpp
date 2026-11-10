#include "Query.h"

#include <algorithm>

namespace pvf::model {

namespace {

bool entityComesBefore(const Entity& a, const Entity& b)
{
    if (a.row != b.row)
        return a.row < b.row;
    if (a.start != b.start)
        return a.start < b.start;
    return a.uuid < b.uuid;
}

QVector<Uuid> sortedSiblings(const Project& project, const Uuid& parentUuid)
{
    QVector<Entity> matches;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd(); ++it)
    {
        if (it.value().parentUuid == parentUuid)
            matches.append(it.value());
    }

    std::sort(matches.begin(), matches.end(), entityComesBefore);

    QVector<Uuid> result;
    result.reserve(matches.size());
    for (const Entity& entity : matches)
        result.append(entity.uuid);
    return result;
}

} // namespace

QVector<Uuid> childrenOf(const Project& project, const Uuid& parentUuid)
{
    return sortedSiblings(project, parentUuid);
}

QVector<Uuid> rootEntities(const Project& project)
{
    return sortedSiblings(project, Uuid{});
}

int nextAutoRow(const Project& project, const Uuid& parentUuid, int start,
                int end)
{
    int highest = -1;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        const Entity& entity = it.value();
        if (entity.parentUuid != parentUuid)
            continue;
        if (entity.start < end && start < entity.end)
            highest = std::max(highest, entity.row);
    }
    return std::max(0, highest + 1);
}

} // namespace pvf::model
