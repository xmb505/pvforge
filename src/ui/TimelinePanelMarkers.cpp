#include "TimelinePanel.h"

#include "app/EditorSession.h"
#include "model/Project.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/timeline/MarkerDialog.h"

#include <QMessageBox>

#include <algorithm>

namespace pvf {

bool TimelinePanel::splitAtPlayhead()
{
    if (!sessionMode() || m_selected == kInvalidEntity)
    {
        emit editFailed(QStringLiteral("没有选中的片段，无法切分"));
        return false;
    }
    return splitEntityAtPlayhead(m_selected);
}

bool TimelinePanel::splitEntityAtPlayhead(EntityId id)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return false;
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* entity = sessionEntity(uuid);
    if (!entity)
        return false;
    const int cut = m_session->playhead();
    if (cut <= entity->start || cut >= entity->end)
    {
        emit editFailed(QStringLiteral("播放头必须位于片段内部才能切分（%1–%2，播放头 %3）")
                            .arg(entity->start)
                            .arg(entity->end)
                            .arg(cut));
        return false;
    }
    QString error;
    if (!m_session->editActions()->splitClip(uuid, cut, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return false;
    }
    emit editCommitted(QStringLiteral("在播放头切分片段"));
    refreshFromSession();
    return true;
}

void TimelinePanel::addMarkerAtPlayhead()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    model::Marker marker;
    marker.id = model::newUuid();
    marker.frame = std::max(0, m_session->playhead());
    marker.title = QStringLiteral("标记 %1").arg(marker.frame);
    marker.color = QStringLiteral("#ffbe3c");
    QString error;
    if (!m_session->editActions()->addMarker(marker, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(QStringLiteral("添加标记"));
    refreshFromSession();
}

QString TimelinePanel::markerAt(const QPoint& pos) const
{
    for (int i = 0; i < m_snapshot.markers.size(); ++i)
    {
        if (markerRect(i).contains(pos))
            return m_snapshot.markers.at(i).id;
    }
    return QString();
}

void TimelinePanel::editMarker(const QString& id)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const model::Project& project = m_session->project();
    for (const model::Marker& marker : project.markers)
    {
        if (marker.id != id)
            continue;
        next::MarkerDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("编辑标记"));
        dialog.setMarker(marker);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QString error;
        if (!m_session->editActions()->updateMarker(dialog.marker(), &error))
        {
            if (!error.isEmpty())
                emit editFailed(error);
            return;
        }
        emit editCommitted(QStringLiteral("编辑标记"));
        refreshFromSession();
        return;
    }
}

void TimelinePanel::deleteMarkerWithConfirm(const QString& id)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    QString title;
    for (const model::Marker& marker : m_session->project().markers)
        if (marker.id == id)
            title = marker.title;
    if (title.isEmpty())
        title = id;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除标记"),
        QStringLiteral("确定删除标记「%1」？").arg(title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!m_session->editActions()->deleteMarker(id, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(QStringLiteral("删除标记"));
    refreshFromSession();
}

void TimelinePanel::beginMarkerDrag(const QString& id, int x)
{
    m_dragMarkerId = id;
    m_dragMode = DragMode::MoveMarker;
    m_markerDragStartFrame = 0;
    for (const TimelineMarkerView& marker : m_snapshot.markers)
        if (marker.id == id)
            m_markerDragStartFrame = marker.frame;
    m_markerDragFrame = m_markerDragStartFrame;
    m_lastDragPos = QPointF(x, kRulerHeight - 8);
    setCursor(Qt::SizeHorCursor);
    update();
}

void TimelinePanel::updateMarkerDrag(int x)
{
    if (m_dragMode != DragMode::MoveMarker || m_dragMarkerId.isEmpty())
        return;
    const int frame = std::max(0, frameFromX(x));
    if (frame == m_markerDragFrame)
        return;
    m_markerDragFrame = frame;
    for (TimelineMarkerView& marker : m_snapshot.markers)
        if (marker.id == m_dragMarkerId)
            marker.frame = frame;
    update();
}

void TimelinePanel::finishMarkerDrag()
{
    const QString id = m_dragMarkerId;
    const int frame = m_markerDragFrame;
    const int start = m_markerDragStartFrame;
    m_dragMarkerId.clear();
    m_dragMode = DragMode::None;
    unsetCursor();
    if (!id.isEmpty() && frame != start && sessionMode() && m_session
        && m_session->editActions())
    {
        QString error;
        if (!m_session->editActions()->moveMarker(id, frame, &error))
        {
            if (!error.isEmpty())
                emit editFailed(error);
        }
        else
        {
            emit editCommitted(QStringLiteral("移动标记"));
        }
    }
    refreshFromSession();
}

} // namespace pvf
