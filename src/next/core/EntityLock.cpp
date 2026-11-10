#include "next/core/EntityLock.h"

#include <QSet>

namespace pvf::next {

bool hasLockedAncestor(const model::Project& project, const Uuid& uuid)
{
    QSet<Uuid> visited;
    visited.insert(uuid);
    auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return false;

    Uuid parent = it.value().parentUuid;
    while (!parent.isEmpty() && !visited.contains(parent))
    {
        visited.insert(parent);
        const auto parentIt = project.entities.constFind(parent);
        if (parentIt == project.entities.constEnd())
            return false;
        if (parentIt.value().locked)
            return true;
        parent = parentIt.value().parentUuid;
    }
    return false;
}

bool isEntityLocked(const model::Project& project, const Uuid& uuid)
{
    const auto it = project.entities.constFind(uuid);
    if (it != project.entities.constEnd() && it.value().locked)
        return true;
    return hasLockedAncestor(project, uuid);
}

} // namespace pvf::next
