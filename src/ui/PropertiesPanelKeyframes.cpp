#include "PropertiesPanel.h"

#include "PropertiesPanelInternal.h"
#include "WholeFrameEdit.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "model/Easing.h"
#include "model/Entity.h"
#include "model/Keyframe.h"
#include "ui/BezierEditorDialog.h"

#include <QJsonObject>
#include <QStringList>

#include <algorithm>

namespace pvf {

using namespace ui::detail;
namespace wholeframe = ui::wholeframe;

void PropertiesPanel::promptGroupBezier()
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int frame = activeKeyFrame(*entity);
    const model::Keyframe* key = model::keyframeAt(entity->keyframes, frame);
    if (!key)
        return;

    const model::EasingSpec& current = key->easing;
    EasingSpec legacy;
    legacy.easing = easingFromName(model::canonicalEasingName(current.type));
    legacy.inX = current.inX;
    legacy.inY = current.inY;
    legacy.outX = current.outX;
    legacy.outY = current.outY;
    if (legacy.easing != Easing::Bezier)
    {
        legacy.easing = Easing::Bezier;
        legacy.inX = 0.42;
        legacy.inY = 0.0;
        legacy.outX = 0.58;
        legacy.outY = 1.0;
    }

    BezierEditorDialog dialog(legacy, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        refresh();
        return;
    }
    const EasingSpec edited = dialog.easing();
    model::EasingSpec spec;
    spec.type = easingName(edited.easing);
    spec.inX = edited.inX;
    spec.inY = edited.inY;
    spec.outX = edited.outX;
    spec.outY = edited.outY;

    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->setKeyframeEasing(
        {KeyRef{entity->uuid, frame}}, spec, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

namespace {

QJsonObject transformClipboardPayload(const model::Keyframe& key)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("x"), key.x);
    payload.insert(QStringLiteral("y"), key.y);
    payload.insert(QStringLiteral("rotation"), key.rotation);
    payload.insert(QStringLiteral("scaleX"), key.scaleX);
    payload.insert(QStringLiteral("scaleY"), key.scaleY);
    payload.insert(QStringLiteral("opacity"), key.opacity);
    payload.insert(QStringLiteral("visible"), key.visible);
    payload.insert(QStringLiteral("physicsInteract"), key.physicsInteract);
    payload.insert(QStringLiteral("content"), key.content);
    payload.insert(QStringLiteral("htmlVars"), key.htmlVars);
    return payload;
}

void applyTransformClipboard(const QJsonObject& payload, model::Keyframe* key)
{
    key->x = payload.value(QStringLiteral("x")).toDouble(key->x);
    key->y = payload.value(QStringLiteral("y")).toDouble(key->y);
    key->rotation =
        payload.value(QStringLiteral("rotation")).toDouble(key->rotation);
    key->scaleX = payload.value(QStringLiteral("scaleX")).toDouble(key->scaleX);
    key->scaleY = payload.value(QStringLiteral("scaleY")).toDouble(key->scaleY);
    key->opacity =
        payload.value(QStringLiteral("opacity")).toDouble(key->opacity);
    key->visible = payload.value(QStringLiteral("visible")).toBool(key->visible);
    key->physicsInteract = payload.value(QStringLiteral("physicsInteract"))
                               .toBool(key->physicsInteract);
    key->content = payload.value(QStringLiteral("content")).toObject();
    key->htmlVars = payload.value(QStringLiteral("htmlVars")).toObject();
}

} // namespace

void PropertiesPanel::copyTransformParams()
{
    const model::Entity* entity = sessionEntity();
    if (!entity)
        return;
    const int frame = activeKeyFrame(*entity);
    if (frame < 0 || frame > entity->duration())
        return;
    m_transformClipboard =
        transformClipboardPayload(wholeframe::keyAt(*entity, frame, m_session));
    m_hasTransformClipboard = true;
}

void PropertiesPanel::pasteTransformParams()
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable() || !m_hasTransformClipboard)
        return;
    const int frame = prepareAnimationEdit(*entity);
    const bool inRange = frame >= 0 && frame <= entity->duration();
    if (!inRange)
    {
        reportSessionError(QStringLiteral(
            "播放针不在实体范围内：不能粘贴整帧参数"));
        return;
    }
    const QJsonObject payload = m_transformClipboard;
    runSessionEdit(QStringLiteral("pvf: paste whole-frame parameters"),
                   [this, payload](model::Entity& e) {
                       const int target = activeKeyFrame(e);
                       if (!wholeframe::canWriteKeyAt(e, target))
                           return false;
                       model::Keyframe key =
                           wholeframe::keyAt(e, target, m_session);
                       applyTransformClipboard(payload, &key);
                       model::upsertKeyframe(e.keyframes, key);
                       return true;
                   });
}

void PropertiesPanel::resetTransformParams()
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int frame = prepareAnimationEdit(*entity);
    if (frame < 0 || frame > entity->duration())
    {
        reportSessionError(QStringLiteral(
            "播放针不在实体范围内：只修改静态参数"));
        return;
    }
    runSessionEdit(QStringLiteral("pvf: reset animation parameters"),
                   [this](model::Entity& e) {
                       const int target = activeKeyFrame(e);
                       if (!wholeframe::canWriteKeyAt(e, target))
                           return false;
                       model::Keyframe key =
                           wholeframe::keyAt(e, target, m_session);
                       key.x = 0.0;
                       key.y = 0.0;
                       key.rotation = 0.0;
                       key.scaleX = 1.0;
                       key.scaleY = 1.0;
                       key.opacity = 1.0;
                       key.visible = true;
                       key.physicsInteract = false;
                       model::upsertKeyframe(e.keyframes, key);
                       return true;
                   });
}

} // namespace pvf
