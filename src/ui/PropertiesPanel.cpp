#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "WholeFrameEdit.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Content.h"
#include "model/Easing.h"
#include "model/Entity.h"
#include "model/Keyframe.h"
#include "model/Query.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"
#include "next/properties/SolverPreviewWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace pvf {

using namespace ui::detail;

bool PropertiesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::FocusIn)
    {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        for (const SolverCard& card : m_solverCards)
        {
            if (card.box == watched
                || (widget && card.box && card.box->isAncestorOf(widget)))
            {
                selectSolver(card.id);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PropertiesPanel::setColorButton(QPushButton* button, const QColor& color)
{
    if (!button)
        return;
    button->setText(color.name().toUpper());
    // A small native swatch icon replaces the old hardcoded background QSS,
    // so the text keeps the palette's normal contrast.
    QPixmap swatch(14, 14);
    swatch.fill(button->palette().color(QPalette::Base));
    QPainter painter(&swatch);
    painter.fillRect(QRect(0, 0, 14, 14), color);
    painter.setPen(QPen(button->palette().color(QPalette::Shadow), 1));
    painter.drawRect(QRect(0, 0, 13, 13));
    painter.end();
    button->setIcon(QIcon(swatch));
    button->setIconSize(QSize(14, 14));
    stylePropertyField(button);
}

QColor PropertiesPanel::pickColor(const QColor& initial, const QString& title)
{
    return QColorDialog::getColor(initial, this, title,
                                  QColorDialog::ShowAlphaChannel);
}
namespace wholeframe = ui::wholeframe;

PropertiesPanel::PropertiesPanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    buildUi();
    buildSessionUi();

    if (m_session && m_session->document())
    {
        auto* document = m_session->document();
        connect(document, &next::Document::selectionChanged, this, [this] {
            const Uuid selected = m_session->document()->selection();
            if (selected != m_sessionUuid)
            {
                m_sessionUuid = selected;
                m_selectedKeys.clear();
                refresh();
            }
        });
        connect(document, &next::Document::playheadChanged, this, [this] {
            m_frame = m_session->playhead();
            refreshPlayheadOnly();
        });
        connect(document, &next::Document::changed, this, [this] {
            if (!m_editing)
                refresh();
        });
        if (auto* playback = m_session->playback())
        {
            connect(playback, &next::PlaybackController::playbackStateChanged,
                    this, [this](bool playing) { setPlaybackActive(playing); });
            m_playbackActive = playback->isPlaying();
            if (m_solverPreview)
                m_solverPreview->setPlaybackActive(m_playbackActive);
        }
    }
    refresh();
}

const model::Entity* PropertiesPanel::sessionEntity() const
{
    if (!m_session || m_sessionUuid.isEmpty())
        return nullptr;
    const auto& entities = m_session->project().entities;
    const auto it = entities.constFind(m_sessionUuid);
    return it == entities.constEnd() ? nullptr : &it.value();
}

bool PropertiesPanel::sessionEditable() const
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !m_session)
        return false;
    return !next::isEntityLocked(m_session->project(), entity->uuid);
}

int PropertiesPanel::playheadRelative(const model::Entity& entity) const
{
    const int playhead = m_session ? m_session->playhead() : m_frame;
    return playhead - entity.start;
}

QVector<int> PropertiesPanel::activeTargetKeyFrames(
    const model::Entity& entity) const
{
    if (m_selectedKeys.isEmpty())
        return {playheadRelative(entity)};

    if (m_selectedKeys.size() == 1)
    {
        const KeyRef& ref = m_selectedKeys.first();
        if (ref.entity == entity.uuid
            && model::hasKeyframeAt(entity.keyframes, ref.frame))
            return {ref.frame};
        // A single selected key on another entity must not hijack this
        // entity's playhead.
        return {playheadRelative(entity)};
    }

    QVector<int> frames;
    for (const KeyRef& ref : m_selectedKeys)
    {
        if (ref.entity == entity.uuid
            && model::hasKeyframeAt(entity.keyframes, ref.frame))
            frames.append(ref.frame);
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

int PropertiesPanel::activeDisplayKeyFrame(const model::Entity& entity) const
{
    const QVector<int> targets = activeTargetKeyFrames(entity);
    if (targets.isEmpty())
        return playheadRelative(entity);
    if (targets.size() == 1)
        return targets.first();

    const int playhead = playheadRelative(entity);
    if (targets.contains(playhead))
        return playhead;
    return targets.first();
}

int PropertiesPanel::activeKeyFrame(const model::Entity& entity) const
{
    // Legacy single-target callers (solver conversion, copy/paste transform):
    // one selected key wins.  Multi-selection keeps the old playhead target;
    // the batch writers use activeTargetKeyFrames() instead.
    const QVector<int> targets = activeTargetKeyFrames(entity);
    if (!m_selectedKeys.isEmpty() && targets.size() == 1)
        return targets.first();
    return playheadRelative(entity);
}

#ifdef PVFORGE_TEST_HOOKS
int PropertiesPanel::activeEditFrameForTest() const
{
    const model::Entity* entity = sessionEntity();
    return entity ? activeKeyFrame(*entity) : -1;
}
#endif

void PropertiesPanel::setSelectedKeyframes(const QVector<KeyRef>& selection)
{
    m_selectedKeys = selection;
    const model::Entity* entity = sessionEntity();
    if (entity && m_selectedKeys.size() == 1)
    {
        const KeyRef& ref = m_selectedKeys.first();
        if (ref.entity == entity->uuid && m_session)
            m_session->setPlayhead(ref.frame + entity->start);
    }
    refreshPlayheadOnly();
}

void PropertiesPanel::setSelectedUuid(const Uuid& uuid)
{
    if (m_sessionUuid == uuid)
    {
        refresh();
        return;
    }
    if (m_solverPreview)
        m_solverPreview->clearSolver();
    m_selectedSolverId.clear();
    m_selectedKeys.clear();
    m_sessionUuid = uuid;
    refresh();
}

void PropertiesPanel::setPlaybackActive(bool active)
{
    if (m_playbackActive == active)
        return;
    m_playbackActive = active;
    if (!m_solverPreview)
        return;
    m_solverPreview->setPlaybackActive(active);
    if (active)
        return;
    QTimer::singleShot(0, m_solverPreview, [preview = m_solverPreview] {
        preview->refreshPreview();
    });
}

void PropertiesPanel::reportSessionError(const QString& message)
{
    emit sessionEditFailed(message.isEmpty() ? QStringLiteral("未知错误") : message);
}

bool PropertiesPanel::runSessionEdit(
    const QString& message, const std::function<bool(model::Entity&)>& mutate,
    bool ignoreLock)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return false;
    if (m_session->playback() && m_session->playback()->isPlaying())
        m_session->playback()->pause();
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateEntity(
        m_sessionUuid, message, mutate, &error, ignoreLock);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
    return ok;
}

bool PropertiesPanel::runSessionContentEdit(
    const QString& message, const std::function<bool(model::Content&)>& mutate)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return false;
    if (m_session->playback() && m_session->playback()->isPlaying())
        m_session->playback()->pause();
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateContent(
        m_sessionUuid, message, mutate, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
    return ok;
}

int PropertiesPanel::prepareAnimationEdit(const model::Entity& entity)
{
    if (m_session && m_session->playback()
        && m_session->playback()->isPlaying())
        m_session->playback()->pause();
    if (m_session)
        m_session->setPlayhead(std::lround(double(m_session->playhead())));
    return activeKeyFrame(entity);
}

bool PropertiesPanel::applyBatchKeyframeEdit(
    const QString& message,
    const std::function<void(model::Keyframe&)>& mutate)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return false;
    prepareAnimationEdit(*entity);
    entity = sessionEntity();
    if (!entity)
        return false;

    const QVector<int> targets = activeTargetKeyFrames(*entity);
    if (targets.size() < 2)
        return false;

    QVector<int> writable;
    QVector<int> blocked;
    for (int frame : targets)
    {
        if (wholeframe::canWriteKeyAt(*entity, frame))
            writable.append(frame);
        else
            blocked.append(frame);
    }
    if (writable.isEmpty())
    {
        QString reason;
        if (!blocked.isEmpty())
            wholeframe::canWriteKeyAt(*entity, blocked.first(), &reason);
        reportSessionError(
            reason.isEmpty()
                ? QStringLiteral("选中的关键帧均位于显式求解段内，不能批量修改")
                : reason);
        refresh();
        return false;
    }
    if (!blocked.isEmpty())
        emit sessionStatusMessage(
            QStringLiteral("已跳过 %1 个由显式求解段驱动的关键帧")
                .arg(blocked.size()));

    return runSessionEdit(
        message, [this, writable, mutate](model::Entity& e) {
            bool changed = false;
            for (int frame : writable)
            {
                if (!wholeframe::canWriteKeyAt(e, frame))
                    continue;
                model::Keyframe key = wholeframe::keyAt(e, frame, m_session);
                mutate(key);
                model::upsertKeyframe(e.keyframes, key);
                changed = true;
            }
            return changed;
        });
}

bool PropertiesPanel::writeAnimationValue(const QString& property, double value,
                                          bool boolean)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return false;

    const int frame = prepareAnimationEdit(*entity);
    entity = sessionEntity();
    if (!entity)
        return false;
    const QVector<int> targets = activeTargetKeyFrames(*entity);
    if (m_selectedKeys.size() > 1 && targets.isEmpty())
    {
        emit sessionStatusMessage(QStringLiteral(
            "选中的关键帧不属于当前实体：未修改任何参数"));
        return false;
    }
    const bool camera = entity->content.kind == ContentKind::Camera;
    if (camera && (property == QLatin1String("scaleX")
                   || property == QLatin1String("scaleY")))
        value = std::clamp(std::abs(value), 0.05, 1.0);

    if (targets.size() > 1)
    {
        QVector<int> writable;
        QVector<int> blocked;
        for (int target : targets)
        {
            if (wholeframe::canWriteKeyAt(*entity, target))
                writable.append(target);
            else
                blocked.append(target);
        }
        if (writable.isEmpty())
        {
            QString reason;
            if (!blocked.isEmpty())
                wholeframe::canWriteKeyAt(*entity, blocked.first(), &reason);
            reportSessionError(
                reason.isEmpty()
                    ? QStringLiteral("选中的关键帧均位于显式求解段内，不能批量修改")
                    : reason);
            refresh();
            return false;
        }
        if (!blocked.isEmpty())
            emit sessionStatusMessage(
                QStringLiteral("已跳过 %1 个由显式求解段驱动的关键帧")
                    .arg(blocked.size()));

        return runSessionEdit(
            QStringLiteral("pvf: batch edit %1").arg(property),
            [this, property, value, boolean, camera,
             writable](model::Entity& e) {
                bool changed = false;
                for (int target : writable)
                {
                    if (!wholeframe::canWriteKeyAt(e, target))
                        continue;
                    model::Keyframe key =
                        wholeframe::keyAt(e, target, m_session);
                    if (boolean)
                        wholeframe::setBoolValue(key, property, value != 0.0);
                    else
                        wholeframe::setValue(key, property, value);
                    if (camera && !boolean)
                    {
                        if (property == QLatin1String("scaleX"))
                            key.scaleY =
                                std::abs(value) > 1e-9 ? value : key.scaleY;
                        else if (property == QLatin1String("scaleY"))
                            key.scaleX =
                                std::abs(value) > 1e-9 ? value : key.scaleX;
                    }
                    model::upsertKeyframe(e.keyframes, key);
                    changed = true;
                }
                return changed;
            });
    }

    const bool inRange = frame >= 0 && frame <= entity->duration();

    // A playhead inside an explicit solver segment is in range, but the frame
    // is driven and cannot accept a normal keyframe.  This is not the
    // out-of-range/static fallback path: refuse the edit instead of silently
    // changing static defaults that the solver overrides.
    if (inRange)
    {
        const int target = activeKeyFrame(*entity);
        QString blockedReason;
        if (!wholeframe::canWriteKeyAt(*entity, target, &blockedReason))
        {
            reportSessionError(
                blockedReason.isEmpty()
                    ? QStringLiteral("该帧由求解段驱动，不能写入普通关键帧")
                    : blockedReason);
            refresh();
            return false;
        }
    }

    bool outOfRange = false;
    const bool ok = runSessionEdit(
        QStringLiteral("pvf: edit %1").arg(property),
        [this, property, value, boolean, camera, inRange,
         &outOfRange](model::Entity& e) {
            if (inRange)
            {
                const int target = activeKeyFrame(e);
                model::Keyframe key = wholeframe::keyAt(e, target, m_session);
                if (boolean)
                    wholeframe::setBoolValue(key, property, value != 0.0);
                else
                    wholeframe::setValue(key, property, value);
                if (camera && !boolean)
                {
                    if (property == QLatin1String("scaleX"))
                        key.scaleY = std::abs(value) > 1e-9 ? value : key.scaleY;
                    else if (property == QLatin1String("scaleY"))
                        key.scaleX = std::abs(value) > 1e-9 ? value : key.scaleX;
                }
                model::upsertKeyframe(e.keyframes, key);
                return true;
            }
            outOfRange = true;
            if (boolean)
                return wholeframe::setStaticBoolValue(e, property, value != 0.0);
            if (camera && !boolean)
            {
                if (property == QLatin1String("scaleX")
                    || property == QLatin1String("scaleY"))
                {
                    e.transform.scaleX = value;
                    e.transform.scaleY = value;
                    return true;
                }
            }
            return wholeframe::setStaticValue(e, property, value);
        });
    if (ok && outOfRange)
        emit sessionStatusMessage(QStringLiteral(
            "播放针不在实体范围内：只修改了静态参数，没有创建关键帧"));
    return ok;
}

bool PropertiesPanel::writeHtmlVariable(const QString& name, double value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return false;
    if (entity->content.kind != ContentKind::Asset
        || entity->content.asset.type != AssetType::Html)
        return false;

    prepareAnimationEdit(*entity);
    entity = sessionEntity();
    if (!entity)
        return false;
    const QVector<int> targets = activeTargetKeyFrames(*entity);
    if (m_selectedKeys.size() > 1 && targets.isEmpty())
    {
        emit sessionStatusMessage(QStringLiteral(
            "选中的关键帧不属于当前实体：未修改任何 HTML 变量"));
        return false;
    }

    if (targets.size() > 1)
    {
        QVector<int> writable;
        QVector<int> blocked;
        for (int target : targets)
        {
            if (target >= 0 && target <= entity->duration()
                && wholeframe::canWriteKeyAt(*entity, target))
                writable.append(target);
            else
                blocked.append(target);
        }
        if (writable.isEmpty())
        {
            reportSessionError(QStringLiteral(
                "选中的关键帧均位于显式求解段内，不能批量写入 HTML 变量"));
            refresh();
            return false;
        }
        if (!blocked.isEmpty())
            emit sessionStatusMessage(
                QStringLiteral("已跳过 %1 个由显式求解段驱动的关键帧")
                    .arg(blocked.size()));
        return runSessionEdit(
            QStringLiteral("pvf: batch edit HTML variable"),
            [this, name, value, writable](model::Entity& e) {
                bool changed = false;
                for (int target : writable)
                {
                    if (!wholeframe::canWriteKeyAt(e, target))
                        continue;
                    model::Keyframe key =
                        wholeframe::keyAt(e, target, m_session);
                    wholeframe::setHtmlVar(key, name, value);
                    model::upsertKeyframe(e.keyframes, key);
                    changed = true;
                }
                return changed;
            });
    }

    const int frame = activeKeyFrame(*entity);
    if (frame < 0 || frame > entity->duration())
    {
        emit sessionStatusMessage(QStringLiteral(
            "播放针不在实体范围内：HTML 变量只能存进整帧关键帧"));
        refresh();
        return false;
    }
    bool blocked = false;
    const bool ok = runSessionEdit(
        QStringLiteral("pvf: edit HTML variable"),
        [this, name, value, &blocked](model::Entity& e) {
            const int target = activeKeyFrame(e);
            if (!wholeframe::canWriteKeyAt(e, target))
            {
                blocked = true;
                return false;
            }
            model::Keyframe key = wholeframe::keyAt(e, target, m_session);
            wholeframe::setHtmlVar(key, name, value);
            model::upsertKeyframe(e.keyframes, key);
            return true;
        });
    if (!ok && blocked)
        reportSessionError(QStringLiteral("该帧由求解段驱动，不能写入普通关键帧"));
    return ok;
}

void PropertiesPanel::deleteActiveKeyframe()
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;

    QVector<KeyRef> refs;
    for (int frame : activeTargetKeyFrames(*entity))
    {
        if (model::hasKeyframeAt(entity->keyframes, frame))
            refs.append(KeyRef{entity->uuid, frame});
    }
    if (refs.isEmpty())
        return;

    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->deleteKeyframes(refs, &error);
    m_editing = false;
    if (!ok)
    {
        reportSessionError(error);
    }
    else
    {
        // Drop only this entity's deleted keys; a cross-entity selection keeps
        // the other entities' entries.
        QVector<KeyRef> remaining;
        for (const KeyRef& ref : m_selectedKeys)
        {
            bool removed = false;
            for (const KeyRef& deleted : refs)
                if (ref.entity == deleted.entity && ref.frame == deleted.frame)
                    removed = true;
            if (!removed)
                remaining.append(ref);
        }
        m_selectedKeys = remaining;
    }
    refresh();
}

void PropertiesPanel::refreshPlayheadOnly()
{
    if (m_editing || !m_session)
        return;
    const model::Entity* entity = sessionEntity();
    if (!entity)
        return;

    m_frame = m_session->playhead();
    const int relative = activeDisplayKeyFrame(*entity);
    const bool inRange = relative >= 0 && relative <= entity->duration();
    const bool editable = sessionEditable();

    for (Row& row : m_rows)
    {
        const bool camera = entity->content.kind == ContentKind::Camera;
        if (!inRange)
            continue;
        model::Keyframe key = wholeframe::keyAt(*entity, relative, m_session);
        std::optional<double> value = wholeframe::value(key, row.prop);
        if (!value)
            continue;
        double display = *value / row.factor;
        if (camera && row.prop == QLatin1String("scaleX"))
            display = std::abs(display);
        if (!row.spin->hasFocus())
        {
            const QSignalBlocker blocker(row.spin);
            row.spin->setValue(display);
        }
        row.spin->setEnabled(editable);
    }

    setSpinIfIdle(m_pivotX, entity->transform.pivotX);
    setSpinIfIdle(m_pivotY, entity->transform.pivotY);

    // Content animation parameters are frame-dependent too; keep their
    // controls in sync while scrubbing without rebuilding the solver cards.
    if (inRange)
    {
        const model::Keyframe key = wholeframe::keyAt(*entity, relative,
                                                      m_session);
        auto keyOr = [&key](const QString& name, double fallback) {
            return wholeframe::value(key, name).value_or(fallback);
        };
        if (entity->content.kind == ContentKind::Text)
        {
            const model::TextContent& text = entity->content.text;
            setSpinIfIdle(m_textSize, keyOr(QStringLiteral("pixelSize"),
                                            text.pixelSize));
            setSpinIfIdle(m_textStrokeWidth,
                          keyOr(QStringLiteral("strokeWidth"),
                                text.strokeWidth));
            setSpinIfIdle(m_textLineHeight,
                          keyOr(QStringLiteral("lineHeight"), text.lineHeight));
            setSpinIfIdle(m_textLetterSpacing,
                          keyOr(QStringLiteral("letterSpacing"),
                                text.letterSpacing));
            setSpinIfIdle(m_textPadding,
                          keyOr(QStringLiteral("padding"), text.padding));
            setSpinIfIdle(m_textBoxWidth,
                          keyOr(QStringLiteral("boxWidth"), text.boxWidth));
            setSpinIfIdle(m_textBoxHeight,
                          keyOr(QStringLiteral("boxHeight"), text.boxHeight));
        }
        else if (entity->content.kind == ContentKind::Vector)
        {
            const model::VectorContent& vector = entity->content.vector;
            setSpinIfIdle(m_vectorWidth,
                          keyOr(QStringLiteral("width"), vector.width));
            setSpinIfIdle(m_vectorHeight,
                          keyOr(QStringLiteral("height"), vector.height));
            setSpinIfIdle(m_vectorCorner,
                          keyOr(QStringLiteral("cornerRadius"),
                                vector.cornerRadius));
            setSpinIfIdle(m_strokeWidth,
                          keyOr(QStringLiteral("strokeWidth"),
                                vector.strokeWidth));
        }
        else if (entity->content.kind == ContentKind::Asset)
        {
            const model::AssetContent& asset = entity->content.asset;
            const double volume = keyOr(QStringLiteral("volume"),
                                        asset.volume);
            if (asset.type == AssetType::Audio)
            {
                const QSignalBlocker blocker(m_audioVolume);
                m_audioVolume->setValue(int(std::lround(volume * 100.0)));
            }
            else
            {
                setSpinIfIdle(m_assetVolume, volume);
            }
        }
    }

    reloadSessionHtmlValues(*entity);
    reloadSolverConvert(*entity);
    refreshKeyframeStatus(*entity);

    if (!m_playbackActive && m_solverPreview)
        m_solverPreview->update();
}

void PropertiesPanel::refreshKeyframeStatus(const model::Entity& entity)
{
    if (!m_keyStatus || !m_keyDelete)
        return;
    const QVector<int> targets = activeTargetKeyFrames(entity);
    const bool batch = targets.size() > 1;
    const bool foreignSelection =
        m_selectedKeys.size() > 1 && targets.isEmpty();
    const int frame = activeDisplayKeyFrame(entity);
    const bool inRange = frame >= 0 && frame <= entity.duration();
    const bool hasKey =
        !foreignSelection && inRange
        && model::hasKeyframeAt(entity.keyframes, frame);
    const bool editable = sessionEditable();
    const int displayFrame = entity.start + frame;

    QFont statusFont = m_keyStatus->font();
    statusFont.setBold(hasKey || batch);
    m_keyStatus->setFont(statusFont);
    QPalette statusPalette = m_keyStatus->palette();
    // Use the untouched Text role as the normal colour: WindowText itself is
    // rewritten below on every refresh.
    statusPalette.setColor(
        QPalette::WindowText,
        (hasKey || batch)
            ? statusPalette.color(QPalette::Text)
            : statusPalette.color(QPalette::Disabled, QPalette::WindowText));
    m_keyStatus->setPalette(statusPalette);
    if (batch)
    {
        m_keyStatus->setText(
            QStringLiteral("◆ 正在批量编辑：已选中 %1 个关键帧"
                           "（修改任意参数将同步更新这 %1 帧）")
                .arg(targets.size()));
    }
    else if (foreignSelection)
    {
        m_keyStatus->setText(QStringLiteral(
            "◆ 已选中其他实体的关键帧（当前实体没有可批量编辑的帧）"));
    }
    else if (hasKey)
    {
        m_keyStatus->setText(
            QStringLiteral("◆ 正在编辑：第 %1 帧（整帧关键帧）").arg(displayFrame));
    }
    else
    {
        const int displayPlayhead =
            m_session ? m_session->playhead() : entity.start;
        m_keyStatus->setText(
            QStringLiteral("▶ 播放针第 %1 帧（当前无关键帧，改任意参数自动新建）")
                .arg(displayPlayhead));
    }
    m_keyDelete->setEnabled(editable && (hasKey || batch));
    m_keyDelete->setToolTip(
        batch ? QStringLiteral("整批删除选中的 %1 个关键帧").arg(targets.size())
            : hasKey ? QStringLiteral("整帧删除当前关键帧")
                     : QStringLiteral("当前帧没有关键帧"));

    const model::Keyframe* key =
        hasKey ? model::keyframeAt(entity.keyframes, frame) : nullptr;
    if (m_groupEasing)
    {
        const QSignalBlocker blocker(m_groupEasing);
        if (key)
        {
            const QString type = model::canonicalEasingName(key->easing.type);
            int index = m_groupEasing->findData(type);
            if (index < 0)
            {
                m_groupEasing->addItem(type, type);
                index = m_groupEasing->count() - 1;
            }
            m_groupEasing->setCurrentIndex(index);
        }
        else
        {
            m_groupEasing->setCurrentIndex(-1);
        }
        m_groupEasing->setPlaceholderText(
            key ? QStringLiteral("整帧缓动（本帧 → 下一帧）")
                : QStringLiteral("当前帧无关键帧"));
        m_groupEasing->setEnabled(editable && key);
    }
    if (m_keyNote)
    {
        if (!m_keyNote->hasFocus())
        {
            const QSignalBlocker blocker(m_keyNote);
            m_keyNote->setText(key ? key->note : QString());
        }
        m_keyNote->setEnabled(editable && key);
        m_keyNote->setPlaceholderText(
            key ? QStringLiteral("注释（给 AI 看）")
                : QStringLiteral("当前帧无关键帧，注释不可编辑"));
    }
    if (m_cancelSolverButton)
    {
        bool isSolverStart = false;
        if (key)
            for (const model::SolverSegment& solver : entity.solvers)
                if (solver.startFrame == frame)
                    isSolverStart = true;
        m_cancelSolverButton->setVisible(isSolverStart);
        m_cancelSolverButton->setEnabled(editable && isSolverStart);
    }
}

void PropertiesPanel::refreshSession()
{
    if (m_editing)
        return;
    const model::Entity* entity = sessionEntity();
    if (!entity)
    {
        m_title->setText(QStringLiteral("未选择对象"));
        for (CollapsibleSection* section :
             {m_basicSection, m_transformSection, m_keySection, m_textSection,
              m_sessionHtmlSection, m_vectorSection, m_mediaSection,
              m_audioSection, m_pivotSection, m_cameraSection, m_solverSection})
        {
            if (section)
                section->hide();
        }
        if (m_solverPreview)
        {
            m_solverPreview->clearSolver();
            m_solverPreview->hide();
        }
        if (m_keyStatusRow)
            m_keyStatusRow->hide();
        return;
    }

    const bool editable = sessionEditable();
    const ContentKind kind = entity->content.kind;
    const bool camera = kind == ContentKind::Camera;
    const bool text = kind == ContentKind::Text;
    const bool vector = kind == ContentKind::Vector;
    const bool asset = kind == ContentKind::Asset;
    const bool audio = asset && entity->content.asset.type == AssetType::Audio;

    m_title->setText(entity->name + QStringLiteral(" · ")
                     + contentKindLabel(kind));

    m_keyStatusRow->show();
    m_basicSection->show();
    m_transformSection->show();
    m_keySection->show();
    m_pivotSection->setVisible(!audio);
    m_textSection->setVisible(text);
    m_vectorSection->setVisible(vector);
    m_mediaSection->setVisible(asset);
    m_audioSection->setVisible(audio);
    m_audioSection->setEnabled(editable);
    m_cameraSection->setVisible(camera);
    m_solverSection->show();

    m_basicSection->setEnabled(true);
    m_transformSection->setEnabled(editable);
    m_keySection->setEnabled(true);
    m_pivotSection->setEnabled(editable);
    m_textSection->setEnabled(editable);
    m_vectorSection->setEnabled(editable);
    m_mediaSection->setEnabled(editable);
    m_sessionHtmlSection->setEnabled(editable);
    m_cameraSection->setEnabled(true);
    m_solverSection->setEnabled(editable);

    if (!m_basicName->hasFocus())
    {
        const QSignalBlocker blocker(m_basicName);
        m_basicName->setText(entity->name);
    }
    setSpinIfIdle(m_basicStart, entity->start);
    setSpinIfIdle(m_basicEnd, entity->end);
    m_basicRow->setMinimum(entity->parentUuid.isEmpty() ? 0 : -1000000);
    setSpinIfIdle(m_basicRow, entity->row);
    {
        const QSignalBlocker blocker(m_basicHidden);
        m_basicHidden->setChecked(entity->hidden);
    }
    {
        const QSignalBlocker blocker(m_basicLocked);
        m_basicLocked->setChecked(entity->locked);
    }
    {
        const QSignalBlocker blocker(m_basicVisible);
        m_basicVisible->setChecked(entity->visible);
    }
    {
        const QSignalBlocker blocker(m_basicInherit);
        m_basicInherit->setChecked(entity->inheritVisibility);
    }
    {
        const QSignalBlocker blocker(m_basicPhysics);
        m_basicPhysics->setChecked(entity->physicsInteract);
    }
    {
        const QSignalBlocker blocker(m_basicFollow);
        m_basicFollow->setChecked(entity->followParentRange);
    }
    const bool hasParent = !entity->parentUuid.isEmpty();
    m_basicName->setEnabled(editable);
    m_basicStart->setEnabled(editable && !entity->followParentRange);
    m_basicEnd->setEnabled(editable && !entity->followParentRange);
    m_basicRow->setMinimum(hasParent ? -1000000 : 0);
    m_basicRow->setEnabled(editable);
    m_basicHidden->setEnabled(editable);
    m_basicLocked->setEnabled(true);
    m_basicVisible->setEnabled(editable);
    m_basicInherit->setEnabled(editable);
    m_basicPhysics->setEnabled(editable);
    m_basicFollow->setEnabled(editable && hasParent);
    m_basicFollow->setToolTip(
        hasParent
            ? QStringLiteral("开启后，子实体的时间范围始终等于直接父实体；"
                             "父实体移动或拉伸时子实体一起变化")
            : QStringLiteral("根实体没有父级，不能跟随父区间"));

    if (audio)
    {
        const int fps = std::max(1, m_session->project().settings.fps);
        const model::AssetContent& assetContent = entity->content.asset;
        int durationFrames = 0;
        const auto stored =
            m_session->assets().getIncludingMissing(assetContent.hash);
        if (stored)
            durationFrames = stored->metadata
                                 .value(QStringLiteral("durationFrames"))
                                 .toInt(0);
        const double trimStart = double(assetContent.sourceIn) / fps;
        const double clipSeconds = double(entity->end - entity->start) / fps;
        const double mediaSeconds =
            durationFrames > 0 ? double(durationFrames) / fps : clipSeconds;
        const double trimEnd =
            std::max(0.0, mediaSeconds - trimStart - clipSeconds);
        const double maximum = std::max(36000.0, mediaSeconds);

        auto setAudioSpin = [](auto* spin, double value, double hi) {
            const QSignalBlocker blocker(spin);
            spin->setMaximum(hi);
            spin->setValue(value);
        };
        setAudioSpin(m_audioStart, double(entity->start), 1000000.0);
        setAudioSpin(m_audioTrimStart, trimStart, maximum);
        setAudioSpin(m_audioTrimEnd, trimEnd, maximum);
        {
            const int audioFrame = activeDisplayKeyFrame(*entity);
            double volume = assetContent.volume;
            if (audioFrame >= 0 && audioFrame <= entity->duration())
            {
                const model::Keyframe key =
                    wholeframe::keyAt(*entity, audioFrame, m_session);
                volume = wholeframe::value(key, QStringLiteral("volume"))
                             .value_or(volume);
            }
            const QSignalBlocker blocker(m_audioVolume);
            m_audioVolume->setValue(int(std::lround(volume * 100.0)));
        }
        setAudioSpin(m_audioFadeIn, assetContent.fadeIn, maximum);
        setAudioSpin(m_audioFadeOut, assetContent.fadeOut, maximum);
        const bool lockedRange = !editable || entity->followParentRange;
        m_audioStart->setEnabled(!lockedRange);
        m_audioTrimStart->setEnabled(!lockedRange);
        m_audioTrimEnd->setEnabled(!lockedRange);
        m_audioVolume->setEnabled(editable);
        m_audioFadeIn->setEnabled(editable);
        m_audioFadeOut->setEnabled(editable);
    }

    {
        const QSignalBlocker blocker(m_parent);
        m_parent->clear();
        m_parent->addItem(QStringLiteral("工作台（世界坐标）"), QString());
        const model::Project& project = m_session->project();
        std::function<void(const Uuid&, int)> appendChildren =
            [&](const Uuid& parent, int depth) {
                for (const Uuid& uuid : model::childrenOf(project, parent))
                {
                    if (uuid == entity->uuid
                        || isDescendant(project, uuid, entity->uuid))
                        continue;
                    const model::Entity& candidate = project.entities[uuid];
                    if (camera
                        && candidate.content.kind == ContentKind::Camera)
                        continue;
                    if (candidate.content.kind == ContentKind::Asset
                        && candidate.content.asset.type == AssetType::Audio)
                        continue;
                    const QString prefix = QStringLiteral("  ").repeated(depth);
                    const QString typePrefix =
                        candidate.content.kind == ContentKind::Empty
                            ? QStringLiteral("[空] ")
                        : candidate.content.kind == ContentKind::Camera
                            ? QStringLiteral("[摄像机] ")
                            : QString();
                    m_parent->addItem(prefix + typePrefix + candidate.name,
                                      uuid);
                    appendChildren(uuid, depth + 1);
                }
            };
        appendChildren(Uuid(), 0);
        const int index = m_parent->findData(entity->parentUuid);
        m_parent->setCurrentIndex(index >= 0 ? index : 0);
    }
    m_parent->setEnabled(editable);
    m_parentRow->show();
    if (m_parentLabel)
        m_parentLabel->show();

    const int relative = activeDisplayKeyFrame(*entity);
    const bool inRange = relative >= 0 && relative <= entity->duration();

    for (Row& row : m_rows)
    {
        const bool hiddenRow =
            camera && (row.prop == QLatin1String("scaleY")
                       || row.prop == QLatin1String("opacity"));
        if (row.label)
            row.label->setVisible(!hiddenRow);
        row.spin->setVisible(!hiddenRow);
        {
            const QSignalBlocker blocker(row.spin);
            if (camera && row.prop == QLatin1String("scaleX"))
            {
                row.label->setText(QStringLiteral("取景比例"));
                row.spin->setRange(5.0, 100.0);
                row.spin->setDecimals(1);
                row.spin->setSingleStep(1.0);
                row.spin->setSuffix(QStringLiteral("%"));
                row.spin->setToolTip(
                    QStringLiteral("摄像机取景比例，绝对值限制 0.05–1.0"
                                   "（5%–100%）"));
                row.factor = 0.01;
            }
            else if (row.prop == QLatin1String("scaleX"))
            {
                row.label->setText(row.baseLabel);
                row.spin->setRange(-100.0, 100.0);
                row.spin->setDecimals(3);
                row.spin->setSingleStep(0.01);
                row.spin->setSuffix(QString());
                row.spin->setToolTip(QString());
                row.factor = 1.0;
            }
            if (inRange)
            {
                const model::Keyframe key =
                    wholeframe::keyAt(*entity, relative, m_session);
                if (const auto value = wholeframe::value(key, row.prop))
                {
                    double display = *value / row.factor;
                    if (camera && row.prop == QLatin1String("scaleX"))
                        display = std::abs(display);
                    if (!row.spin->hasFocus())
                        row.spin->setValue(display);
                }
            }
        }
        row.spin->setEnabled(editable && !hiddenRow);
    }

    m_usePivot->hide();
    setSpinIfIdle(m_pivotX, entity->transform.pivotX);
    setSpinIfIdle(m_pivotY, entity->transform.pivotY);
    m_pivotX->setEnabled(editable);
    m_pivotY->setEnabled(editable);

    if (text)
    {
        const model::TextContent& content = entity->content.text;
        const model::Keyframe key = inRange
                                        ? wholeframe::keyAt(*entity, relative,
                                                            m_session)
                                        : model::Keyframe();
        auto keyValue = [&key, &content](const QString& name, double fallback) {
            if (const auto value = wholeframe::value(key, name))
                return *value;
            return fallback;
        };
        if (!m_textContent->hasFocus())
        {
            const QSignalBlocker blocker(m_textContent);
            m_textContent->setText(content.text);
        }
        if (!m_textFont->hasFocus())
        {
            const QSignalBlocker blocker(m_textFont);
            m_textFont->setCurrentFont(content.fontFamily.isEmpty()
                                           ? QFont()
                                           : QFont(content.fontFamily));
        }
        setSpinIfIdle(m_textSize,
                      keyValue(QStringLiteral("pixelSize"), content.pixelSize));
        m_textColorValue = QColor(content.color);
        setColorButton(m_textColorButton, m_textColorValue);
        setSpinIfIdle(m_textStrokeWidth,
                      keyValue(QStringLiteral("strokeWidth"),
                               content.strokeWidth));
        m_textStrokeColorValue = QColor(content.strokeColor);
        setColorButton(m_textStrokeColorButton, m_textStrokeColorValue);
        {
            const QSignalBlocker blocker(m_textAlign);
            selectComboData(m_textAlign, content.align);
        }
        {
            const QSignalBlocker blocker(m_textDirection);
            m_textDirection->setCurrentIndex(
                content.vertical ? (content.verticalRtl ? 1 : 2) : 0);
        }
        {
            const QSignalBlocker blocker(m_textBold);
            m_textBold->setChecked(content.bold);
        }
        {
            const QSignalBlocker blocker(m_textItalic);
            m_textItalic->setChecked(content.italic);
        }
        {
            const QSignalBlocker blocker(m_textUnderline);
            m_textUnderline->setChecked(content.underline);
        }
        setSpinIfIdle(m_textLineHeight,
                      keyValue(QStringLiteral("lineHeight"),
                               content.lineHeight));
        setSpinIfIdle(m_textLetterSpacing,
                      keyValue(QStringLiteral("letterSpacing"),
                               content.letterSpacing));
        setSpinIfIdle(m_textPadding,
                      keyValue(QStringLiteral("padding"), content.padding));
        setSpinIfIdle(m_textBoxWidth,
                      keyValue(QStringLiteral("boxWidth"), content.boxWidth));
        setSpinIfIdle(m_textBoxHeight,
                      keyValue(QStringLiteral("boxHeight"), content.boxHeight));
    }

    if (vector)
    {
        const model::VectorContent& content = entity->content.vector;
        const model::Keyframe key = inRange
                                        ? wholeframe::keyAt(*entity, relative,
                                                            m_session)
                                        : model::Keyframe();
        auto keyValue = [&key, &content](const QString& name, double fallback) {
            if (const auto value = wholeframe::value(key, name))
                return *value;
            return fallback;
        };
        {
            const QSignalBlocker blocker(m_vectorShape);
            selectComboData(m_vectorShape, content.shape);
        }
        setSpinIfIdle(m_vectorCorner,
                      keyValue(QStringLiteral("cornerRadius"),
                               content.cornerRadius));
        setSpinIfIdle(m_vectorSides, content.sides);
        setSpinIfIdle(m_vectorInner, content.innerRatio);
        setSpinIfIdle(m_vectorWidth,
                      keyValue(QStringLiteral("width"), content.width));
        setSpinIfIdle(m_vectorHeight,
                      keyValue(QStringLiteral("height"), content.height));
        m_fillValue = QColor(content.fillColor);
        setColorButton(m_fillButton, m_fillValue);
        m_strokeValue = QColor(content.strokeColor);
        setColorButton(m_strokeButton, m_strokeValue);
        setSpinIfIdle(m_strokeWidth,
                      keyValue(QStringLiteral("strokeWidth"),
                               content.strokeWidth));
        m_vectorCorner->setEnabled(editable && content.shape == 3);
        m_vectorSides->setEnabled(editable
                                  && (content.shape == 6 || content.shape == 7));
        m_vectorInner->setEnabled(editable && content.shape == 6);
    }

    if (asset)
    {
        const model::AssetContent& content = entity->content.asset;
        m_assetHash->setText(content.hash);
        m_assetType->setText(assetTypeLabel(content.type));

        int frames = 0;
        const auto stored = m_session->assets().getIncludingMissing(content.hash);
        if (stored)
            frames = stored->metadata.value(QStringLiteral("durationFrames")).toInt(0);
        m_assetSourceIn->setMaximum(frames > 0 ? std::max(0, frames - 1)
                                               : 1000000);
        setSpinIfIdle(m_assetSourceIn, content.sourceIn);
        if (inRange)
        {
            const model::Keyframe key =
                wholeframe::keyAt(*entity, relative, m_session);
            if (const auto volume = wholeframe::value(key, QStringLiteral("volume")))
                setSpinIfIdle(m_assetVolume, *volume);
            else
                setSpinIfIdle(m_assetVolume, content.volume);
        }
        else
        {
            setSpinIfIdle(m_assetVolume, content.volume);
        }
        // Volume is an animated parameter only for audio; image/video have no
        // volume animation key, so their static volume field stays hidden.
        const bool audioAsset = content.type == AssetType::Audio;
        m_assetVolume->setVisible(audioAsset);
        m_assetSourceIn->setEnabled(editable);
        m_assetVolume->setEnabled(editable);
        m_replaceSource->show();
        m_replaceSource->setEnabled(editable);

        QString info = assetTypeLabel(content.type);
        if (!stored)
            info += QStringLiteral(" · 素材缺失");
        else if (!stored->name.isEmpty())
            info += QStringLiteral(" · ") + stored->name;
        if (frames > 0)
            info += QStringLiteral(" · %1 帧").arg(frames);
        m_mediaInfo->setText(info);
        m_mediaInfo->setToolTip(content.hash);
    }

    reloadSessionHtml(*entity);

    updateSolverCards(*entity);
    for (SolverCard& card : m_solverCards)
        card.box->setEnabled(editable);
    reloadSolverConvert(*entity);
    m_solverEffectCombo->setEnabled(editable);
    m_solverTargetCombo->setEnabled(editable);
    m_solverEndSpin->setEnabled(editable);
    refreshKeyframeStatus(*entity);
}

void PropertiesPanel::refresh()
{
    refreshSession();
}

} // namespace pvf
