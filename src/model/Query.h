#pragma once

#include "Project.h"

#include <QVector>

namespace pvf::model {

// Returns the sorted UUIDs of the direct children of parentUuid. An empty
// parentUuid selects root entities. Sorting is row, start, then UUID.
QVector<Uuid> childrenOf(const Project& project, const Uuid& parentUuid);

// Sorted UUIDs of all root entities.
QVector<Uuid> rootEntities(const Project& project);

// Highest row occupied by a direct child of parentUuid whose range overlaps
// [start, end), plus one. Empty sibling sets return 0. This is the model's
// default "auto row" for a newly created or dropped entity.
int nextAutoRow(const Project& project, const Uuid& parentUuid, int start,
                int end);

} // namespace pvf::model
