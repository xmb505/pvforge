#pragma once

#include "assets/AssetStore.h"
#include "model/Project.h"

#include <QString>
#include <QVector>

namespace pvf::next {

// True when any ancestor of `uuid` is locked.  The entity itself is ignored so
// the properties panel can still unlock a locked entity.
bool hasLockedAncestor(const model::Project& project, const Uuid& uuid);

// True when `uuid` or any ancestor is locked.
bool isEntityLocked(const model::Project& project, const Uuid& uuid);

// Metadata durationFrames of a video/audio asset.  Returns 0 for other content,
// missing assets and assets without a known duration.
int timedAssetDurationFrames(const assets::AssetStore& assets,
                             const model::Entity& entity);

// Applies a new absolute start/end range to `uuid` and its subtree.  The whole
// project is edited on a copy, validated with model::validateProject(), and
// only then written to `result`; on failure `result` is untouched and `error`
// explains why.  The rules mirror the timeline trim:
//   - the new range must stay inside the parent;
//   - trimming start shifts/crops ordinary keys, updates video/audio sourceIn
//     (never below 0) and cascades into children;
//   - trimming end drops out-of-range keys, clamps/pins solver segments and
//     clips or deletes children;
//   - a solver segment cut by a new start is removed and the driven tracks get
//     an ordinary key at frame 0 holding the pose anim::evaluateEntity() reports
//     at the cut frame;
//   - video/audio ranges must stay within the material left after sourceIn.
bool applyEntityRange(const model::Project& project,
                      const assets::AssetStore& assets, const Uuid& uuid,
                      int newStart, int newEnd, model::Project* result,
                      QVector<Uuid>* upserts, QVector<Uuid>* deletes,
                      QString* error = nullptr);

} // namespace pvf::next
