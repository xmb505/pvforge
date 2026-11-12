#pragma once

// Entity identifiers and the two view sentinels the editor UI passes around.
//
// They used to live in core/Project.h.  The delivery shell (EditorSession) only
// needs these aliases, so they get their own header and the retired Project
// model can stay out of the delivery build.
namespace pvf {

using EntityId = int;
constexpr EntityId kInvalidEntity = -1;

// Sentinels the timeline/viewport use for the work camera and the whole-canvas
// overview rows.  They are negative so they never collide with entity ids.
constexpr int kWorkCameraView = -2;
constexpr int kWholeCanvasView = -3;

} // namespace pvf
