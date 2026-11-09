#pragma once

#include "Project.h"

#include <QVector>

namespace pvf::model {

// Returns the sorted UUIDs of the direct children of parentUuid. An empty
// parentUuid selects root entities. Sorting is row, start, then UUID.
QVector<Uuid> childrenOf(const Project& project, const Uuid& parentUuid);

// Sorted UUIDs of all root entities.
QVector<Uuid> rootEntities(const Project& project);

} // namespace pvf::model
