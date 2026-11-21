#pragma once

#include "Project.h"

#include <QHash>
#include <QVector>

namespace pvf::model {

// One parent -> children index for a project snapshot (issue #132).
//
// Building is a single pass over the entity table plus one sort per sibling
// group, so a subtree walk that visits every node is O(E log E) total instead
// of O(E^2).  children() returns the cached, already sorted list by const
// reference: callers must never copy the entities or re-scan the project just
// to look up siblings.  Entries are UUIDs, not Entity copies.
//
// The index is a snapshot.  Rebuild it after the project changes; a
// long-lived index must be rebuilt whenever the owning code re-reads the
// project (paint frame, timeline snapshot, export frame batch).
class ChildrenIndex
{
public:
    explicit ChildrenIndex(const Project& project);

    // Sorted by row, start, then UUID.  An empty parentUuid selects root
    // entities.  Unknown parents return a shared empty list.
    const QVector<Uuid>& children(const Uuid& parentUuid) const;
    const QVector<Uuid>& roots() const { return children(Uuid{}); }

private:
    QHash<Uuid, QVector<Uuid>> m_children;
    QVector<Uuid> m_empty;
};

// Returns the sorted UUIDs of the direct children of parentUuid. An empty
// parentUuid selects root entities. Sorting is row, start, then UUID.  One-off
// helper; hot paths should build a ChildrenIndex once and reuse it.
QVector<Uuid> childrenOf(const Project& project, const Uuid& parentUuid);

// Sorted UUIDs of all root entities.
QVector<Uuid> rootEntities(const Project& project);

// Highest row occupied by a direct child of parentUuid whose closed range
// [start, end] overlaps, plus one. Empty sibling sets return 0. This is the
// model's default "auto row" for a newly created or dropped entity.
int nextAutoRow(const Project& project, const Uuid& parentUuid, int start,
                int end);

} // namespace pvf::model
