#include "TimelinePanel.h"

#include "WholeFrameEdit.h"
#include "ui/TimelinePanelInternal.h"

#include "app/EditorSession.h"
#include "model/Entity.h"

#include <QApplication>
#include <QDateTime>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace pvf {

using ui::detail::plusButtonRect;

namespace {
constexpr qint64 kSlowDoubleClickMs = 1400;
}

void TimelinePanel::mousePressEvent(QMouseEvent* event)
{
    if (!sessionMode() || event->button() != Qt::LeftButton)
        return;
    setFocus(Qt::MouseFocusReason);
    if (m_popup)
        if (QWidget* popup = parentWidget())
            popup->raise();

    const QPointF pos = event->position();
    if (pos.y() < kRulerHeight)
    {
        if (pos.x() < nameWidth())
            return;
        const QString markerId = markerAt(pos.toPoint());
        if (!markerId.isEmpty())
        {
            beginMarkerDrag(markerId, int(std::lround(pos.x())));
            return;
        }
        m_dragMode = DragMode::Scrub;
        m_lastDragPos = pos;
        m_autoScroll->start(30);
        emit scrubStarted();
        setScrubFrame(pos.x());
        return;
    }

    const int row = rowAt(int(pos.y()));
    const int pressX = int(std::lround(pos.x()));
    const EntityId id = hitClipRow(row, pressX);
    const Entity* entity = entityById(id);

    // Inline clip buttons win over trim/keyframe gestures.
    if (entity && clipRect(*entity).width() >= kInlineButtonsMinWidth)
    {
        for (int index = 0; index < 3; ++index)
        {
            if (clipButtonRect(*entity, index).contains(pos.toPoint()))
            {
                handleRowButton(row, index);
                return;
            }
        }
    }
    if (!entity)
    {
        m_dragMode = DragMode::PendingSelect;
        m_pressPos = pos;
        m_boxStart = pos;
        m_boxEnd = pos;
        m_pressFrame = std::max(0, frameFromX(pressX));
        m_pressEntity = kInvalidEntity;
        update();
        return;
    }

    const QRect clip = clipRect(*entity);
    const QRect plus = plusButtonRect(clip, width() - kScrollBarSize);
    if (!plus.isEmpty() && plus.contains(pos.toPoint()))
    {
        openPopup(id);
        return;
    }

    // Slow double-click on an already selected clip starts inline rename.  A
    // fast double-click arrives through mouseDoubleClickEvent and creates a
    // whole-frame key instead.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 interval = now - m_lastClipClickMs;
    const bool alreadySelected =
        id == m_selected || m_entitySelection.contains(id);
    if (alreadySelected && id == m_lastClipClickEntity
        && interval > QApplication::doubleClickInterval()
        && interval <= kSlowDoubleClickMs)
    {
        m_lastClipClickMs = 0;
        m_lastClipClickEntity = kInvalidEntity;
        beginInlineRename(id);
        return;
    }
    m_lastClipClickMs = now;
    m_lastClipClickEntity = id;

    const int x = int(std::lround(pos.x()));
    m_dragEntity = id;
    m_dragRow = row;
    m_dragStartFrame = frameFromX(x);
    m_dragStartRow = row;
    m_dragClipStart = clipStartOf(*entity);
    m_dragClipEnd = clipEndOf(*entity);
    m_lastDragPos = pos;
    m_moved = false;

    if (!isEntityLocked(id))
    {
        const int keyFrame = hitKeyframeRow(row, x, int(std::lround(pos.y())));
        if (keyFrame >= 0)
        {
            selectEntity(id, true);
            const KeyframeSelection selection{id, double(keyFrame)};
            const bool toggle =
                event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            auto it = std::find(m_selectedKeyframes.begin(),
                                m_selectedKeyframes.end(), selection);
            if (toggle)
            {
                if (it != m_selectedKeyframes.end())
                    m_selectedKeyframes.erase(it);
                else
                    m_selectedKeyframes.append(selection);
            }
            else if (it == m_selectedKeyframes.end())
            {
                m_selectedKeyframes.clear();
                m_selectedKeyframes.append(selection);
            }
            m_lastClickedKeyEntity = id;
            m_lastClickedKeyFrame = keyFrame;

            m_dragMode = DragMode::MoveKeyframe;
            m_dragRoots.clear();
            m_keyDragStart = keyFrame;
            m_dragStartFrame = keyFrame;
            m_frame = std::max(0, keyFrame);
            emit frameRequested(m_frame);
            emit keyframeSelectionChanged(m_selectedKeyframes);
            beginSessionDrag();
            update();
            return;
        }
    }

    if (m_editMode == EditMode::Clips && !isEntityLocked(id))
    {
        const bool nearLeft = std::abs(x - clip.left()) <= kTrimGrab;
        const bool nearRight = std::abs(x - clip.right()) <= kTrimGrab;
        if (nearLeft || nearRight)
        {
            selectEntity(id, true);
            m_dragRoots.clear();
            m_dragMode = nearLeft ? DragMode::TrimStart : DragMode::TrimEnd;
            beginSessionDrag();
            update();
            return;
        }
        if (clip.contains(pos.toPoint()))
        {
            const bool multiMod =
                (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
                != 0;
            if (multiMod)
            {
                if (m_entitySelection.contains(id))
                {
                    m_entitySelection.remove(id);
                    if (m_selected == id)
                        m_selected = m_entitySelection.isEmpty()
                                         ? kInvalidEntity
                                         : *m_entitySelection.constBegin();
                    if (m_selected != kInvalidEntity)
                        emit entitySelected(m_selected);
                }
                else
                {
                    if (m_selected != kInvalidEntity)
                        m_entitySelection.insert(m_selected);
                    m_entitySelection.insert(id);
                    m_selected = id;
                    emit entitySelected(id);
                }
                update();
                return;
            }
            if (!m_entitySelection.contains(id))
                selectEntity(id, true);
            m_dragRoots.clear();
            if (m_entitySelection.size() > 1)
                m_dragRoots = selectedUuids();
            m_selectedKeyframes.clear();
            emit keyframeSelectionChanged(m_selectedKeyframes);
            m_dragMode = DragMode::MoveClip;
            beginSessionDrag();
            update();
            return;
        }
    }

    m_dragMode = DragMode::PendingSelect;
    m_pressPos = pos;
    m_boxStart = pos;
    m_boxEnd = pos;
    m_pressFrame = std::max(0, m_dragStartFrame);
    m_pressEntity = id;
    update();
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!sessionMode())
        return;
    if (m_dragMode == DragMode::MoveMarker)
    {
        updateMarkerDrag(int(std::lround(event->position().x())));
        return;
    }
    if (m_dragMode == DragMode::Scrub)
    {
        m_lastDragPos = event->position();
        setScrubFrame(event->position().x());
        if (event->position().x() > visibleWidth() - 12
            || event->position().x() < 12)
            m_autoScroll->start(30);
        return;
    }

    if (m_dragMode == DragMode::PendingSelect)
    {
        const QPointF delta = event->position() - m_pressPos;
        if (std::abs(delta.x()) + std::abs(delta.y()) <= 5.0)
            return;
        m_dragMode = DragMode::BoxSelect;
        m_boxStart = m_pressPos;
    }
    if (m_dragMode == DragMode::BoxSelect)
    {
        m_boxEnd = event->position();
        update();
        return;
    }

    if (m_dragMode == DragMode::None)
    {
        if (event->position().y() < kRulerHeight)
        {
            setCursor(markerAt(event->position().toPoint()).isEmpty()
                          ? Qt::ArrowCursor
                          : Qt::SizeHorCursor);
            return;
        }
        const int row = rowAt(int(event->position().y()));
        const int x = int(std::lround(event->position().x()));
        const Entity* entity = entityById(hitClipRow(row, x));
        m_hoverRow = row;
        m_hoverLeft = false;
        m_hoverRight = false;
        if (entity)
        {
            const QRect clip = clipRect(*entity);
            const int startFrame = clipStartOf(*entity);
            const int endFrame = clipEndOf(*entity);
            setToolTip(QStringLiteral("%1\n%2–%3 帧 · 时长 %4 帧")
                           .arg(entity->name)
                           .arg(startFrame)
                           .arg(endFrame)
                           .arg(endFrame - startFrame));
            QString tip = toolTip();
            if (entity->locked)
                tip += QStringLiteral("\n已锁定（不能拖动或裁剪）");
            if (entity->hidden)
                tip += QStringLiteral("\n已隐藏");
            if (entity->isAudio())
            {
                const QString hash =
                    entity->data.value(QStringLiteral("assetHash")).toString();
                if (!hash.isEmpty())
                    tip += QStringLiteral("\n素材 %1…").arg(hash.left(12));
            }
            setToolTip(tip);
            if (!entity->locked && m_editMode == EditMode::Clips
                && (std::abs(x - clip.left()) <= kTrimGrab
                    || std::abs(x - clip.right()) <= kTrimGrab))
                setCursor(Qt::SizeHorCursor);
            else if (!entity->locked
                     && hitKeyframeRow(row, x,
                                       int(std::lround(event->position().y())))
                            >= 0)
                setCursor(Qt::PointingHandCursor);
            else if (clip.contains(event->position().toPoint()))
                setCursor(entity->locked ? Qt::ArrowCursor
                                         : Qt::OpenHandCursor);
            else
                unsetCursor();
        }
        else
        {
            unsetCursor();
            setToolTip(QString());
        }
        update();
        return;
    }

    m_lastDragPos = event->position();
    if (m_dragMode != DragMode::Scrub
        && (event->position().x() > visibleWidth() - 24
            || event->position().x() < 24
            || event->position().y() > bodyBottom() - 20
            || event->position().y() < kRulerHeight + 20))
    {
        m_autoScroll->start(30);
    }
    updateDragPreview(event->position());
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_dragMode == DragMode::MoveMarker)
    {
        finishMarkerDrag();
        return;
    }
    if (m_dragMode == DragMode::PendingSelect)
    {
        m_dragMode = DragMode::None;
        m_frame = std::max(0, m_pressFrame);
        emit frameRequested(m_frame);
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        if (m_pressEntity != kInvalidEntity
            && (sessionMode() || !isEntityLocked(m_pressEntity)))
            selectEntity(m_pressEntity, true);
        update();
        return;
    }
    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        const bool additive = event->modifiers() & Qt::ShiftModifier;

        if (m_editMode == EditMode::Clips)
        {
            QSet<EntityId> selected =
                additive ? m_entitySelection : QSet<EntityId>();
            EntityId active = additive ? m_selected : kInvalidEntity;
            for (const Entity& entity : activeEntities())
            {
                const bool dangling =
                    entity.parent != kInvalidEntity
                    && entityById(entity.parent) == nullptr;
                if (entity.parent != m_owner
                    && !(m_owner == kInvalidEntity && dangling))
                    continue;
                if (!isValidRow(rowOfEntity(entity)))
                    continue;
                const QRect clip = clipRect(entity);
                if (clip.isEmpty() || !clip.intersects(box.toRect()))
                    continue;
                selected.insert(entity.id);
                active = entity.id;
            }
            m_entitySelection = selected;
            m_selected = selected.contains(active) ? active
                        : selected.isEmpty() ? kInvalidEntity
                                             : *selected.constBegin();
            m_selectedKeyframes.clear();
            emit keyframeSelectionChanged(m_selectedKeyframes);
            emit entitySelected(m_selected);
            m_dragMode = DragMode::None;
            update();
            return;
        }

        const QVector<EntityId> rows = rowIds();
        const int topRow = rowAt(int(box.top()));
        const int bottomRow = rowAt(int(box.bottom()));
        const int firstRow = bottomRow >= 0 ? bottomRow : 0;
        const int lastRow = topRow >= 0 ? topRow : int(rows.size()) - 1;
        const int firstFrame = std::max(0, frameFromX(int(box.left())));
        const int lastFrame = std::max(0, frameFromX(int(box.right())));
        if (!additive)
            m_selectedKeyframes.clear();
        for (int row = std::max(0, firstRow);
             row <= std::min(lastRow, int(rows.size()) - 1); ++row)
        {
            const Entity* entity = entityById(rows[row]);
            if (!entity || isEntityLocked(entity->id))
                continue;
            for (int frame : keyframeFrames(*entity))
            {
                if (frame < firstFrame || frame > lastFrame)
                    continue;
                const KeyframeSelection selection{entity->id, double(frame)};
                if (std::find(m_selectedKeyframes.begin(),
                              m_selectedKeyframes.end(), selection)
                    == m_selectedKeyframes.end())
                    m_selectedKeyframes.append(selection);
            }
        }
        if (firstRow >= 0 && firstRow < rows.size())
        {
            const Entity* last = entityById(rows[firstRow]);
            if (last && !isEntityLocked(last->id))
                selectEntity(last->id, true);
        }
        emit keyframeSelectionChanged(m_selectedKeyframes);
        m_dragMode = DragMode::None;
        return;
    }
    if (m_dragMode == DragMode::Scrub)
    {
        if (m_autoScroll)
            m_autoScroll->stop();
        m_dragMode = DragMode::None;
        return;
    }
    m_lastDragPos = event->position();
    finishSessionDrag();
}

void TimelinePanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!sessionMode() || event->button() != Qt::LeftButton)
        return;
    if (event->position().y() < kRulerHeight)
        return;
    if (event->position().x() < nameWidth())
        return;

    // Inline rename is the slow-double-click gesture; never create a key while
    // the editor is open.
    if (m_renameEdit)
        return;

    const int row = rowAt(int(event->position().y()));
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity || isEntityLocked(id))
        return;

    const int x = int(std::lround(event->position().x()));
    const int keyFrame =
        hitKeyframeRow(row, x, int(std::lround(event->position().y())));
    if (keyFrame >= 0)
    {
        selectEntity(id, true);
        m_selectedKeyframes.clear();
        m_selectedKeyframes.append(KeyframeSelection{id, double(keyFrame)});
        m_lastClickedKeyEntity = id;
        m_lastClickedKeyFrame = keyFrame;
        emit keyframeSelectionChanged(m_selectedKeyframes);
        m_frame = keyFrame;
        emit frameRequested(m_frame);
        update();
        return;
    }

    const QRect clip = clipRect(*entity);
    if (!clip.contains(event->position().toPoint()))
        return;
    const QRect plus = plusButtonRect(clip, width() - kScrollBarSize);
    if (!plus.isEmpty() && plus.contains(event->position().toPoint()))
        return;

    const int frame = std::clamp(frameFromX(x), clipStartOf(*entity),
                                 clipEndOf(*entity));
    m_frame = frame;
    emit frameRequested(m_frame);
    selectEntity(id, true);

    const model::Entity* modelEntity = sessionEntity(uuidForEntityId(id));
    if (!modelEntity)
        return;
    const int relative = frame - modelEntity->start;
    QString reason;
    if (!ui::wholeframe::canWriteKeyAt(*modelEntity, relative, &reason))
    {
        emit editCommitted(reason);
        return;
    }
    addKeyframeAt(id, frame);
    update();
}

void TimelinePanel::beginInlineRename(EntityId id)
{
    finishInlineRename(true);
    const Entity* entity = entityById(id);
    if (!entity || isEntityLocked(id))
        return;

    const QRect clip = clipRect(*entity);
    if (clip.isEmpty())
        return;
    m_renameEntity = id;

    auto* edit = new QLineEdit(this);
    edit->setObjectName(QStringLiteral("clipRenameEdit"));
    edit->setText(entity->name);
    const bool showButtons = clip.width() >= kInlineButtonsMinWidth;
    const int textLeft = clip.left() + 5
                         + (showButtons ? kButtonStrip + 2 : 0);
    const int textRight = clip.right() - 5;
    edit->setGeometry(textLeft, clip.top() + 2,
                      std::max(60, textRight - textLeft), clip.height() - 4);
    edit->selectAll();
    edit->installEventFilter(this);
    connect(edit, &QLineEdit::returnPressed, this,
            [this] { finishInlineRename(true); });
    connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
        if (m_renameEdit == edit)
            finishInlineRename(true);
    });
    edit->show();
    edit->raise();
    edit->setFocus(Qt::OtherFocusReason);
    m_renameEdit = edit;
    update();
}

void TimelinePanel::finishInlineRename(bool commit)
{
    if (!m_renameEdit)
        return;
    QLineEdit* edit = m_renameEdit;
    m_renameEdit = nullptr;
    const EntityId id = m_renameEntity;
    m_renameEntity = kInvalidEntity;
    const QString text = edit->text().trimmed();
    edit->removeEventFilter(this);
    edit->disconnect(this);
    edit->hide();
    edit->deleteLater();

    if (!commit || text.isEmpty() || !m_session || !m_session->editActions())
    {
        update();
        return;
    }
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* entity = sessionEntity(uuid);
    if (!entity || entity->name == text || isEntityLocked(id))
    {
        update();
        return;
    }
    QString error;
    const bool ok = m_session->editActions()->updateEntity(
        uuid, QStringLiteral("pvf: rename clip"),
        [text](model::Entity& e) {
            e.name = text;
            return true;
        },
        &error);
    if (!ok)
        emit editFailed(error);
    else
        emit editCommitted(QStringLiteral("重命名片段"));
    refreshFromSession();
}

} // namespace pvf
