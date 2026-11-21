#include "PropertiesPanel.h"

#include "EasingChooser.h"
#include "EasingPicker.h"
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
#include <QToolButton>

#include <algorithm>

namespace pvf {

using namespace ui::detail;
namespace wholeframe = ui::wholeframe;

void PropertiesPanel::openKeyframeEasingMenu()
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable() || !m_keyEasingButton)
        return;
    const int frame = activeDisplayKeyFrame(*entity);
    const model::Keyframe* key = model::keyframeAt(entity->keyframes, frame);
    if (!key)
    {
        emit sessionStatusMessage(
            QStringLiteral("当前帧无关键帧：先在时间轴创建关键帧"));
        refresh();
        return;
    }

    auto* menu = new EasingPopup(key->easing, this);
    connect(menu, &EasingPopup::picked, this,
            [this](const model::EasingSpec& spec) {
                applyKeyframeEasing(spec);
            });
    connect(menu, &EasingPopup::customRequested, this,
            &PropertiesPanel::promptGroupBezier);
    menu->popup(
        m_keyEasingButton->mapToGlobal(QPoint(0, m_keyEasingButton->height())));
}

void PropertiesPanel::applyKeyframeEasing(const model::EasingSpec& spec)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;

    const QVector<int> targets = activeTargetKeyFrames(*entity);
    if (m_selectedKeys.size() > 1 && targets.isEmpty())
    {
        emit sessionStatusMessage(QStringLiteral(
            "选中的关键帧不属于当前实体：未修改任何缓动"));
        return;
    }

    if (targets.size() > 1)
    {
        applyBatchKeyframeEdit(
            QStringLiteral("pvf: batch set keyframe easing"),
            [spec](model::Keyframe& key) { key.easing = spec; });
        return;
    }

    const int frame =
        targets.isEmpty() ? activeKeyFrame(*entity) : targets.first();
    if (!model::hasKeyframeAt(entity->keyframes, frame))
    {
        emit sessionStatusMessage(
            QStringLiteral("当前帧无关键帧：先在时间轴创建关键帧"));
        refresh();
        return;
    }

    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->setKeyframeEasing(
        {KeyRef{entity->uuid, frame}}, spec, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::promptGroupBezier()
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    if (m_selectedKeys.size() > 1 && activeTargetKeyFrames(*entity).isEmpty())
    {
        emit sessionStatusMessage(QStringLiteral(
            "选中的关键帧不属于当前实体：未修改任何缓动"));
        return;
    }
    const int frame = activeDisplayKeyFrame(*entity);
    const model::Keyframe* key = model::keyframeAt(entity->keyframes, frame);
    if (!key)
    {
        emit sessionStatusMessage(
            QStringLiteral("当前帧无关键帧：先在时间轴创建关键帧"));
        refresh();
        return;
    }

    const model::EasingSpec current = key->easing;
    model::EasingSpec edited;
    edited.type = QStringLiteral("bezier");
    if (model::canonicalEasingName(current.type) == QLatin1String("bezier"))
    {
        edited.inX = current.inX;
        edited.inY = current.inY;
        edited.outX = current.outX;
        edited.outY = current.outY;
    }

#ifdef PVFORGE_TEST_HOOKS
    if (m_easingCustomEditor)
    {
        if (!m_easingCustomEditor(current, &edited))
        {
            refresh();
            return;
        }
    }
    else
#endif
    {
        EasingSpec legacy;
        legacy.easing = Easing::Bezier;
        legacy.inX = edited.inX;
        legacy.inY = edited.inY;
        legacy.outX = edited.outX;
        legacy.outY = edited.outY;
        BezierEditorDialog dialog(legacy, this);
        if (dialog.exec() != QDialog::Accepted)
        {
            refresh();
            return;
        }
        const EasingSpec result = dialog.easing();
        edited.type = QStringLiteral("bezier");
        edited.inX = result.inX;
        edited.inY = result.inY;
        edited.outX = result.outX;
        edited.outY = result.outY;
    }

    applyKeyframeEasing(edited);
}

#ifdef PVFORGE_TEST_HOOKS
void PropertiesPanel::setEasingCustomEditorForTest(EasingCustomEditor editor)
{
    m_easingCustomEditor = editor;
}
#endif

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
    if (frame < 0 || frame > entity->duration())
    {
        reportSessionError(QStringLiteral(
            "播放针不在实体范围内：不能粘贴整帧参数"));
        return;
    }
    const int target = activeKeyFrame(*entity);
    QString blockedReason;
    if (!wholeframe::canWriteKeyAt(*entity, target, &blockedReason))
    {
        reportSessionError(
            blockedReason.isEmpty()
                ? QStringLiteral("该帧由求解段驱动，不能粘贴整帧参数")
                : blockedReason);
        refresh();
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
            "播放针不在实体范围内：不能重置整帧参数"));
        return;
    }
    const int target = activeKeyFrame(*entity);
    QString blockedReason;
    if (!wholeframe::canWriteKeyAt(*entity, target, &blockedReason))
    {
        reportSessionError(
            blockedReason.isEmpty()
                ? QStringLiteral("该帧由求解段驱动，不能重置整帧参数")
                : blockedReason);
        refresh();
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
