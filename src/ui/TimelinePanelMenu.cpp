#include "TimelinePanel.h"

#include "app/EditorSession.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "next/core/EntityLock.h"

#include <QContextMenuEvent>
#include <QMenu>

#include <algorithm>

namespace pvf {

namespace {

QString displayName(const model::Entity& entity)
{
    return entity.name.isEmpty() ? entity.uuid : entity.name;
}

} // namespace

void TimelinePanel::contextMenuEvent(QContextMenuEvent* event)
{
    if (!sessionMode())
        return;

    if (event->pos().y() < kRulerHeight)
    {
        const QString markerId = markerAt(event->pos());
        QMenu menu(this);
        QAction* add = menu.addAction(QStringLiteral("在播放头添加标记"));
        QAction* edit = nullptr;
        QAction* remove = nullptr;
        if (!markerId.isEmpty())
        {
            menu.addSeparator();
            edit = menu.addAction(QStringLiteral("编辑标记…"));
            remove = menu.addAction(QStringLiteral("删除标记"));
        }
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == add)
            addMarkerAtPlayhead();
        else if (edit && chosen == edit)
            editMarker(markerId);
        else if (remove && chosen == remove)
            deleteMarkerWithConfirm(markerId);
        event->accept();
        return;
    }

    const int row = rowAt(event->pos().y());
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity)
        return;

    const int hitX = event->pos().x();
    const int keyFrame = hitKeyframeRow(row, hitX, event->pos().y());
    if (keyFrame >= 0)
    {
        selectEntity(id, true);
        m_selectedKeyframes.clear();
        m_selectedKeyframes.append(KeyframeSelection{id, double(keyFrame), QString()});
        const QStringList frameTracks = tracksAtFrame(id, keyFrame);
        if (!frameTracks.isEmpty())
            m_selectedTrack = frameTracks.first();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        update();

        // Easing is edited in the Properties keyframe section; the clip menu
        // only manages keys.
        QMenu menu(this);
        QAction* copy = menu.addAction(QStringLiteral("复制关键帧"));
        QAction* remove = menu.addAction(QStringLiteral("删除关键帧"));
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == copy)
            copySelectedKeyframes();
        else if (chosen == remove)
            deleteSelectedKeyframes();
        event->accept();
        return;
    }

    const QRect clip = clipRect(*entity);
    if (!clip.contains(event->pos()))
        return;

    selectEntity(id, true);
    QMenu menu(this);
    const bool hiddenNow = entity->hidden;
    const bool lockedNow = entity->locked;
    QAction* toggleHidden = menu.addAction(hiddenNow
                                               ? QStringLiteral("显示片段")
                                               : QStringLiteral("隐藏片段"));
    QAction* toggleLocked = menu.addAction(lockedNow
                                               ? QStringLiteral("解锁片段")
                                               : QStringLiteral("锁定片段"));
    menu.addSeparator();
    QAction* open = menu.addAction(QStringLiteral("打开私有时间线"));
    QAction* remove = menu.addAction(QStringLiteral("删除片段"));
    remove->setEnabled(!isEntityLocked(id));
    QAction* split = menu.addAction(QStringLiteral("在播放头切分"));
    const int playhead = m_session ? m_session->playhead() : 0;
    const model::Entity* splitEntity = sessionEntity(uuidForEntityId(id));
    split->setEnabled(splitEntity && playhead > splitEntity->start
                      && playhead < splitEntity->end
                      && !isEntityLocked(id));

    QHash<QAction*, QPair<Uuid, bool>> reparents;
    const Uuid source = uuidForEntityId(id);
    const model::Project& project = m_session->project();
    QMenu* parentMenu = menu.addMenu(QStringLiteral("更换父级"));
    auto addTarget = [&](const Uuid& target, const QString& label,
                         bool enabled) {
        QMenu* sub = parentMenu->addMenu(label);
        sub->setEnabled(enabled);
        QAction* world = sub->addAction(QStringLiteral("保持世界位置"));
        QAction* local = sub->addAction(QStringLiteral("保持局部坐标"));
        reparents.insert(world, qMakePair(target, true));
        reparents.insert(local, qMakePair(target, false));
    };

    const model::Entity& sourceEntity = project.entities.value(source);
    QVector<Uuid> candidates;
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        if (it.key() == source || it.key() == sourceEntity.parentUuid)
            continue;
        bool descendant = false;
        Uuid parent = it.key();
        QSet<Uuid> seen;
        while (!parent.isEmpty() && !seen.contains(parent))
        {
            if (parent == source)
            {
                descendant = true;
                break;
            }
            seen.insert(parent);
            parent = project.entities.value(parent).parentUuid;
        }
        if (!descendant)
            candidates.append(it.key());
    }
    std::sort(candidates.begin(), candidates.end(),
              [&project](const Uuid& a, const Uuid& b) {
                  const QString an = displayName(project.entities.value(a));
                  const QString bn = displayName(project.entities.value(b));
                  if (an != bn)
                      return an < bn;
                  return a < b;
              });
    if (!sourceEntity.parentUuid.isEmpty())
        addTarget(Uuid(), QStringLiteral("顶层（无父级）"), true);
    for (const Uuid& candidate : candidates)
    {
        const model::Entity& target = project.entities.value(candidate);
        addTarget(candidate, displayName(target),
                  !next::isEntityLocked(project, candidate));
    }
    if (parentMenu->isEmpty())
        parentMenu->setEnabled(false);

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == toggleHidden)
    {
        toggleSessionFlag(id, true);
    }
    else if (chosen == toggleLocked)
    {
        toggleSessionFlag(id, false);
    }
    else if (chosen == open)
    {
        openPopup(id);
    }
    else if (chosen == remove)
    {
        deleteSelectedEntities();
        refreshFromSession();
    }
    else if (chosen == split)
    {
        splitEntityAtPlayhead(id);
    }
    else if (reparents.contains(chosen))
    {
        const auto target = reparents.value(chosen);
        QString error;
        if (!m_session->editActions()->reparent(uuidForEntityId(id), target.first,
                                                target.second, &error))
            emit editFailed(error);
        else
            emit editCommitted(QStringLiteral("更改片段父级"));
        refreshFromSession();
    }
    event->accept();
}

} // namespace pvf
