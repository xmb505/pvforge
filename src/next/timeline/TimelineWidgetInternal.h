#pragma once

#include "next/timeline/TimelineWidget.h"

#include <QString>

namespace pvf::next::detail {

// Helpers shared by the split TimelineWidget sources.

// Locked entity or locked ancestor; a null document is never locked.
bool entityIsLocked(const Document* document, const Uuid& uuid);

// True when `frame` falls inside a solver segment that drives `track`: such a
// frame cannot hold a normal keyframe.
bool trackBlockedAt(const model::Entity& entity, const QString& track, int frame);

} // namespace pvf::next::detail
