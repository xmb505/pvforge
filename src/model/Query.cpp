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

} // namespace

ChildrenIndex::ChildrenIndex(const Project& project)
{
    QHash<Uuid, QVector<const Entity*>> byParent;
    byParent.reserve(project.entities.size());
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        byParent[it.value().parentUuid].append(&it.value());
    }

    m_children.reserve(byParent.size());
    for (auto it = byParent.begin(); it != byParent.end(); ++it)
    {
        QVector<const Entity*> matches = it.value();
        std::sort(matches.begin(), matches.end(),
                  [](const Entity* a, const Entity* b) {
                      return entityComesBefore(*a, *b);
                  });
        QVector<Uuid> sorted;
        sorted.reserve(matches.size());
        for (const Entity* entity : matches)
            sorted.append(entity->uuid);
        m_children.insert(it.key(), std::move(sorted));
    }
}

const QVector<Uuid>& ChildrenIndex::children(const Uuid& parentUuid) const
{
    const auto it = m_children.constFind(parentUuid);
    return it == m_children.constEnd() ? m_empty : it.value();
}

QVector<Uuid> childrenOf(const Project& project, const Uuid& parentUuid)
{
    return ChildrenIndex(project).children(parentUuid);
}

QVector<Uuid> rootEntities(const Project& project)
{
    return ChildrenIndex(project).roots();
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
        if (entity.start <= end && start <= entity.end)
            highest = std::max(highest, entity.row);
    }
    return std::max(0, highest + 1);
}

} // namespace pvf::model
