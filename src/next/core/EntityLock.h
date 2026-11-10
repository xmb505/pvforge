#pragma once

#include "model/Project.h"

namespace pvf::next {

// True when any ancestor of `uuid` is locked.  The entity itself is ignored so
// the properties panel can still unlock a locked entity.
bool hasLockedAncestor(const model::Project& project, const Uuid& uuid);

// True when `uuid` or any ancestor is locked.
bool isEntityLocked(const model::Project& project, const Uuid& uuid);

} // namespace pvf::next
