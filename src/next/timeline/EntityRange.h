#pragma once

#include "assets/AssetStore.h"
#include "model/Project.h"
#include "next/core/EntityLock.h"

#include <QString>
#include <QVector>

namespace pvf::next {

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
//   - followParentRange children always take the full new range, recursively.
// `allowDisjointRoot` keeps the entity even when its old range and the new
// range do not overlap; keys/solvers are rebased and out-of-range ones are
// dropped.  It is used when a following entity is reparented and jumps to the
// new parent's range, where a normal trim would delete the whole subtree.
bool applyEntityRange(const model::Project& project,
                      const assets::AssetStore& assets, const Uuid& uuid,
                      int newStart, int newEnd, model::Project* result,
                      QVector<Uuid>* upserts, QVector<Uuid>* deletes,
                      QString* error = nullptr,
                      bool allowDisjointRoot = false);

// Turns the direct parent-range following flag on or off.  Enabling rewrites
// the entity range to its direct parent's range and rebases keys/solvers while
// keeping their absolute frames; disabling only clears the flag, so the range
// stays where it is.  The whole project is validated on success.
bool applyFollowParentRange(const model::Project& project,
                            const assets::AssetStore& assets,
                            const Uuid& uuid, bool follow,
                            model::Project* result, QVector<Uuid>* upserts,
                            QVector<Uuid>* deletes,
                            QString* error = nullptr);

// Splits `uuid` at the absolute frame `frame` into two sibling entities:
//   - the original entity keeps the left range [start, frame);
//   - a new entity with a fresh UUID takes the right range [frame, end).
// Video/audio sourceIn is shifted on the right half, keyframes are cropped and
// shifted, solver segments are cut, and children are split recursively.  The
// cut must be strictly inside the entity.  `result` is only written on success;
// `upserts` contains both halves and every surviving descendant, `deletes` the
// original descendants that fall entirely inside the left half's discarded
// right-hand region.
bool splitEntityAt(const model::Project& project,
                   const assets::AssetStore& assets, const Uuid& uuid,
                   int frame, model::Project* result, QVector<Uuid>* upserts,
                   QVector<Uuid>* deletes, Uuid* rightUuid,
                   QString* error = nullptr);

} // namespace pvf::next
