#pragma once

// Entity identifiers and the two view sentinels the editor UI passes around.
//
// The int entity id is only the display identity the timeline/viewport use to
// talk to their panels; the model's identity is `model::Uuid`.  These aliases
// live on their own so the delivery headers do not pull a project model in.
namespace pvf {

using EntityId = int;
constexpr EntityId kInvalidEntity = -1;

// Sentinels the timeline/viewport use for the work camera and the whole-canvas
// overview rows.  They are negative so they never collide with entity ids.
constexpr int kWorkCameraView = -2;
constexpr int kWholeCanvasView = -3;

} // namespace pvf
