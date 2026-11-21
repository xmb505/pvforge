#pragma once

// Whole-frame keyframe edit helpers shared by the properties panel, the
// viewport gizmo and the timeline.  Every animation edit follows the same
// rule: write one keyframe that carries every animation parameter of the
// entity at that frame; there are no per-property tracks.

#include "model/Entity.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace pvf {

class EditorSession;

namespace ui::wholeframe {

// True for 0..duration-1 when no explicitly ended solver segment owns the
// frame.  An open solver segment accepts a key (that key becomes its end).
bool canWriteKeyAt(const model::Entity& entity, int frame,
                   QString* reason = nullptr);

// Existing key at `frame` (returned verbatim, keeping easing/note/extras) or a
// fully evaluated whole-frame key.  A new key fills every public animation
// parameter, every content animation parameter and numeric HTML variables
// (manifest defaults included) at that frame.  An open solver segment's
// instantaneous pose is used for x/y/rotation/scale.
model::Keyframe keyAt(const model::Entity& entity, int frame,
                      EditorSession* session);

// Animation parameter access.  `property` uses the model names: x, y,
// rotation, scaleX, scaleY, opacity, visible, physicsInteract, plus content
// animation keys (pixelSize, lineHeight, ..., volume) and HTML variables.
bool setValue(model::Keyframe& key, const QString& property, double value);
bool setBoolValue(model::Keyframe& key, const QString& property, bool value);
// Numeric HTML variable (the name without the legacy "var." prefix).
void setHtmlVar(model::Keyframe& key, const QString& name, double value);
std::optional<double> value(const model::Keyframe& key,
                            const QString& property);

// Static fallback edits used when the playhead is outside the entity range:
// only the entity's static definition parameters change, no key is created.
bool setStaticValue(model::Entity& entity, const QString& property,
                    double value);
bool setStaticBoolValue(model::Entity& entity, const QString& property,
                        bool value);

// Numeric defaults declared by an HTML asset manifest.
QJsonObject htmlVariableDefaults(EditorSession* session,
                                 const model::Entity& entity);
void fillHtmlVariableDefaults(model::Keyframe& key,
                              const QJsonObject& defaults);

} // namespace ui::wholeframe
} // namespace pvf
