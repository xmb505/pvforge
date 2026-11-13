#include "next/timeline/TimelineWidget.h"

#include "next/core/Document.h"
#include "next/timeline/MarkerDialog.h"

#include <QDialog>
#include <QFont>
#include <QFontMetrics>
#include <QMessageBox>

#include <algorithm>

namespace pvf::next {

QRect TimelineWidget::markerHitRect(const model::Marker& marker) const
{
    const int x = xFromFrame(marker.frame);
    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 2.0));
    const QFontMetrics metrics(small);
    const QString title = marker.title.isEmpty() ? tr("标记") : marker.title;
    const int labelWidth = std::min(160, metrics.horizontalAdvance(title) + 12);
    const QRect lane = markerLaneRect();

    QRect rect(x + 9, lane.top() + 1, labelWidth, lane.height() - 2);
    if (rect.right() > width() - 2)
        rect.translate(width() - 2 - rect.right(), 0);
    if (rect.left() < 2)
        rect.translate(2 - rect.left(), 0);
    return rect.united(QRect(x - 2, lane.top(), 7, lane.height()));
}

QString TimelineWidget::markerIdAt(const QPoint& pos) const
{
    if (!m_document)
        return {};

    const QRect lane = markerLaneRect();
    const QRect body = bodyRect();
    if (!lane.contains(pos) && !body.contains(pos))
        return {};

    // Later markers are painted on top, so hit-test them first.
    const QVector<model::Marker>& markers = m_document->project().markers;
    for (int i = markers.size() - 1; i >= 0; --i)
    {
        const model::Marker& marker = markers.at(i);
        if (marker.id.isEmpty())
            continue;

        const int x = xFromFrame(marker.frame);
        if (lane.contains(pos))
        {
            if (markerHitRect(marker).contains(pos))
                return marker.id;
            if (std::abs(pos.x() - x) <= 4)
                return marker.id;
        }
        else if (std::abs(pos.x() - x) <= 3)
        {
            return marker.id;
        }
    }
    return {};
}

int TimelineWidget::markerIndex(const QString& markerId) const
{
    if (!m_document || markerId.isEmpty())
        return -1;
    const QVector<model::Marker>& markers = m_document->project().markers;
    for (int i = 0; i < markers.size(); ++i)
    {
        if (markers.at(i).id == markerId)
            return i;
    }
    return -1;
}

bool TimelineWidget::commitMarkers(const QVector<model::Marker>& before,
                                   const QString& message, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("未打开工程");
        return false;
    }

    QString commitError;
    if (!m_document->commitProject(message, &commitError))
    {
        m_document->mutableProject().markers = before;
        const QString failure =
            commitError.isEmpty() ? tr("标记保存失败") : commitError;
        if (error)
            *error = failure;
        emit statusMessage(tr("标记保存失败：%1").arg(failure));
        return false;
    }

    if (error)
        error->clear();
    updateScrollBar();
    update();
    return true;
}

bool TimelineWidget::insertMarker(const model::Marker& marker, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("未打开工程");
        return false;
    }

    model::Marker candidate = marker;
    if (candidate.id.isEmpty())
        candidate.id = model::newUuid();
    candidate.frame = std::max(0, candidate.frame);
    if (!normalizeMarker(candidate, error))
        return false;

    const QVector<model::Marker> before = m_document->project().markers;
    m_document->mutableProject().markers.append(candidate);
    return commitMarkers(
        before, tr("pvf: add marker \"%1\"").arg(candidate.title), error);
}

void TimelineWidget::addMarkerAtPlayhead()
{
    model::Marker marker;
    marker.id = model::newUuid();
    marker.frame = std::max(0, playhead());
    marker.title = tr("标记 %1").arg(marker.frame);
    marker.color = QStringLiteral("#ffbe3c");
    insertMarker(marker);
}

bool TimelineWidget::updateMarker(const QString& markerId,
                                  const model::Marker& marker, QString* error)
{
    const int index = markerIndex(markerId);
    if (index < 0)
    {
        if (error)
            *error = tr("标记不存在");
        return false;
    }

    model::Marker candidate = marker;
    candidate.id = markerId; // ids are stable; only fields are editable.
    if (!normalizeMarker(candidate, error))
        return false;

    const QVector<model::Marker> before = m_document->project().markers;
    m_document->mutableProject().markers[index] = candidate;
    return commitMarkers(
        before, tr("pvf: edit marker \"%1\"").arg(candidate.title), error);
}

bool TimelineWidget::deleteMarker(const QString& markerId, QString* error)
{
    const int index = markerIndex(markerId);
    if (index < 0)
    {
        if (error)
            *error = tr("标记不存在");
        return false;
    }

    const QVector<model::Marker> before = m_document->project().markers;
    const QString title = before.at(index).title;
    m_document->mutableProject().markers.removeAt(index);
    return commitMarkers(
        before,
        tr("pvf: delete marker \"%1\"").arg(title.isEmpty() ? markerId : title),
        error);
}

void TimelineWidget::deleteMarkerWithConfirm(const QString& markerId)
{
    const int index = markerIndex(markerId);
    if (index < 0)
        return;

    const QString title = m_document->project().markers.at(index).title;
    const auto answer = QMessageBox::question(
        this, tr("删除标记"),
        tr("确定删除标记 \"%1\"？").arg(title.isEmpty() ? markerId : title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!deleteMarker(markerId, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("标记"), error);
}

void TimelineWidget::openNewMarkerDialog()
{
    if (!m_document)
        return;

    model::Marker marker;
    marker.id = model::newUuid();
    marker.frame = std::max(0, playhead());
    marker.title = tr("标记 %1").arg(marker.frame);
    marker.color = QStringLiteral("#ffbe3c");

    MarkerDialog dialog(this);
    dialog.setWindowTitle(tr("添加标记"));
    dialog.setMarker(marker);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!insertMarker(dialog.marker(), &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("标记"), error);
}

void TimelineWidget::openMarkerEditor(const QString& markerId)
{
    const int index = markerIndex(markerId);
    if (index < 0)
        return;

    MarkerDialog dialog(this);
    dialog.setWindowTitle(tr("编辑标记"));
    dialog.setMarker(m_document->project().markers.at(index));
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!updateMarker(markerId, dialog.marker(), &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("标记"), error);
}

} // namespace pvf::next
