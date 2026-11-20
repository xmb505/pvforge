#pragma once

// Whole-frame keyframe payload rules shared by keyframes.* and entity.create.
//
// A keyframe always carries every animation parameter of its entity type.
// There are no per-property tracks: `track` is not an accepted field anywhere,
// and definition parameters (text/fontFamily, vector/shape, asset/hash, ...)
// can never be written into a keyframe.

#include "mcp2/Internal.h"

namespace pvf::mcp2::detail {

// Parses one strictly complete whole-frame keyframe.  Missing public or
// content animation parameters, unknown fields and definition parameters are
// rejected with the JSON path in `label`.
Result parseWholeFrameKeyframe(const model::Content& content, int duration,
                               const QJsonObject& object,
                               const QString& label,
                               model::Keyframe* out);

// Parses a "keyframes" array.  `value` may be undefined/null for an empty
// array (entity.create); any non-array value is rejected with the legacy
// split-track message.  Output is sorted by frame and duplicate frames fail.
Result parseWholeFrameKeyframes(const model::Content& content, int duration,
                                const QJsonValue& value,
                                QVector<model::Keyframe>* out);

// True for a frame inside an explicitly ended solver segment
// (startFrame < frame <= endFrame).  Ordinary whole-frame keys cannot live
// there.  Automatic segments (endFrame == -1) end at the first key after the
// start, so any such key is legal and becomes the end marker.
bool isSolvedFrame(const model::Entity& entity, int frame);

// Every solver starts from an ordinary whole-frame keyframe at startFrame; an
// explicit solver band also forbids ordinary keys through its inclusive end.
Result checkKeyframesAgainstSolvers(const model::Entity& entity,
                                    const QVector<model::Keyframe>& keyframes);

// Convenience: the solver whose explicit band blocks `frame`, or nullptr.
const model::SolverSegment* blockingSolver(const model::Entity& entity,
                                           int frame);

} // namespace pvf::mcp2::detail
