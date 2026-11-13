#pragma once

#include "next/core/Document.h"

#include <QString>
#include <QStringList>

namespace pvf::next::detail {

// Helpers shared by the split PropertiesPanel sources.  They live here instead
// of a per-translation-unit anonymous namespace because the content, keyframe,
// HTML and solver pages of the panel all need them.

// Localised label for a content kind ("文字", "素材", ...).
QString contentKindLabel(ContentKind kind);

// True when `trackName` has a keyframe exactly at `frame` (entity-relative).
bool hasKeyAt(const model::Entity& entity, const QString& trackName, int frame);

// Tracks a solver drives: rotation for spin, x/y for the rest.
QStringList solverDrivenTracks(SolverEffect effect);

// True when a solver segment starts at `frame` and drives `trackName`; such a
// keyframe cannot be deleted without removing the solver first.
bool isSolverStartKey(const model::Entity& entity, const QString& trackName,
                      int frame);

} // namespace pvf::next::detail
