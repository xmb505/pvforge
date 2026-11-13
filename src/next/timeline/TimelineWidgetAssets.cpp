#include "next/timeline/TimelineWidget.h"

#include "TimelineWidgetInternal.h"
#include "next/core/Document.h"
#include "next/timeline/EntityRange.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

#include <algorithm>
#include <utility>

namespace pvf::next {

namespace {

constexpr char kAssetMimeType[] = "application/x-pvforge-asset";

bool hasAssetMime(const QMimeData* mime)
{
    return mime && mime->hasFormat(QString::fromLatin1(kAssetMimeType));
}

bool rangesOverlap(int aStart, int aEnd, int bStart, int bEnd)
{
    return aStart < bEnd && bStart < aEnd;
}

} // namespace

bool TimelineWidget::splitSelectedAtPlayhead(QString* error)
{
    if (error)
        error->clear();
    if (!m_document)
    {
        if (error)
            *error = tr("未打开工程");
        return false;
    }

    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
    {
        if (error)
            *error = tr("没有选中的实体");
        emit statusMessage(tr("没有选中的实体，无法切分"));
        return false;
    }

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd())
    {
        if (error)
            *error = tr("实体不存在");
        return false;
    }
    const model::Entity& entity = it.value();
    const QString name = entityDisplayName(entity);
    if (detail::entityIsLocked(m_document, uuid))
    {
        if (error)
            *error = tr("实体已锁定：%1").arg(name);
        emit statusMessage(tr("实体已锁定：%1").arg(name));
        return false;
    }

    const int cut = playhead();
    if (cut <= entity.start || cut >= entity.end)
    {
        const QString message =
            tr("播放头必须位于实体内部才能切分：%1 范围 [%2, %3)，播放头 %4")
                .arg(name)
                .arg(entity.start)
                .arg(entity.end)
                .arg(cut);
        if (error)
            *error = message;
        emit statusMessage(message);
        return false;
    }

    const model::Project before = m_document->project();
    model::Project split;
    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    Uuid rightUuid;
    QString splitError;
    if (!splitEntityAt(before, m_document->assets(), uuid, cut, &split,
                       &upserts, &deletes, &rightUuid, &splitError))
    {
        if (error)
            *error = splitError;
        emit statusMessage(splitError.isEmpty() ? tr("切分失败")
                                                : tr("切分失败：%1")
                                                      .arg(splitError));
        return false;
    }

    m_document->mutableProject() = split;
    QString commitError;
    if (!m_document->commitChanges(
            upserts, deletes,
            tr("pvf: split entity \"%1\"").arg(name), &commitError))
    {
        m_document->mutableProject() = before;
        if (error)
            *error = commitError;
        emit statusMessage(
            commitError.isEmpty() ? tr("切分提交失败")
                                  : tr("切分提交失败：%1").arg(commitError));
        updateScrollBar();
        update();
        return false;
    }

    updateScrollBar();
    update();
    return true;
}

int TimelineWidget::assetDurationFrames(const assets::StoredAsset& asset) const
{
    int frames =
        asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
    if (frames <= 0 && m_document)
        frames = m_document->project().settings.fps;
    return std::max(1, frames);
}

int TimelineWidget::dropRowFor(int y, int startFrame, int endFrame) const
{
    const QRect body = bodyRect();
    int candidate = -1;
    if (!body.isEmpty() && y >= body.top() && y <= body.bottom())
        candidate =
            std::max(0, (body.bottom() - y + m_rowScroll) / kRowHeight);

    int highestInRange = -1;
    const QVector<Uuid> siblings = visibleEntities();
    for (const Uuid& uuid : siblings)
    {
        const auto it = m_document->project().entities.constFind(uuid);
        if (it == m_document->project().entities.constEnd())
            continue;
        const model::Entity& entity = it.value();
        if (rangesOverlap(entity.start, entity.end, startFrame, endFrame))
            highestInRange = std::max(highestInRange, std::max(0, entity.row));
    }

    // Drops outside the body use the model default: highest occupied row in
    // [start, end) + 1.
    const int autoRow = std::max(0, highestInRange + 1);
    if (candidate < 0)
        return autoRow;

    // Fixed slots: the row under the cursor wins even when it is far above the
    // rows currently in use. Only an actual overlap on that row falls back.
    for (const Uuid& uuid : siblings)
    {
        const auto it = m_document->project().entities.constFind(uuid);
        if (it == m_document->project().entities.constEnd())
            continue;
        const model::Entity& entity = it.value();
        if (std::max(0, entity.row) == candidate
            && rangesOverlap(entity.start, entity.end, startFrame, endFrame))
            return autoRow;
    }
    return candidate;
}

QVector<Uuid> TimelineWidget::createEntitiesForAsset(
    const assets::StoredAsset& asset, int startFrame, int row)
{
    QVector<Uuid> created;
    if (!m_document)
        return created;

    const int start = std::max(0, startFrame);
    const int duration = assetDurationFrames(asset);
    const QString name = asset.name.isEmpty() ? asset.hash : asset.name;

    model::Entity main;
    main.uuid = model::newUuid();
    main.name = name;
    main.parentUuid = m_parentUuid;
    main.start = start;
    main.end = start + duration;
    main.row = std::max(0, row);
    main.content.kind = ContentKind::Asset;
    main.content.asset.hash = asset.hash;
    main.content.asset.type = asset.type;
    main.content.asset.sourceIn = 0;
    main.content.asset.volume = 1.0;
    m_document->mutableProject().entities.insert(main.uuid, main);
    created.append(main.uuid);

    // Video imports split their audio into a separate asset. Recreate the
    // paired entity on the row above, exactly like the importer does.
    const QString audioHash =
        asset.metadata.value(QStringLiteral("audioHash")).toString();
    if (asset.type == AssetType::Video && !audioHash.isEmpty())
    {
        model::Entity audio;
        audio.uuid = model::newUuid();
        audio.name = name + QStringLiteral(" 音频");
        audio.parentUuid = m_parentUuid;
        audio.start = main.start;
        audio.end = main.end;
        audio.row = main.row + 1;
        audio.content.kind = ContentKind::Asset;
        audio.content.asset.hash = audioHash;
        audio.content.asset.type = AssetType::Audio;
        audio.content.asset.sourceIn = 0;
        audio.content.asset.volume = 1.0;
        m_document->mutableProject().entities.insert(audio.uuid, audio);
        created.append(audio.uuid);
    }

    return created;
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_document && hasAssetMime(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (m_document && hasAssetMime(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void TimelineWidget::dropEvent(QDropEvent* event)
{
    if (!m_document || !hasAssetMime(event->mimeData()))
    {
        event->ignore();
        return;
    }

    const QString hash =
        QString::fromUtf8(event->mimeData()->data(QString::fromLatin1(kAssetMimeType)))
            .trimmed();
    if (hash.isEmpty())
    {
        event->ignore();
        return;
    }

    const auto asset = m_document->assets().get(hash);
    if (!asset)
    {
        emit statusMessage(tr("素材缺失：%1").arg(hash));
        event->ignore();
        return;
    }

    const QPoint pos = event->position().toPoint();
    const int start = m_parentUuid.isEmpty()
                          ? std::max(0, frameFromX(pos.x()))
                          : frameFromX(pos.x());
    const int end = start + assetDurationFrames(*asset);

    if (!m_parentUuid.isEmpty())
    {
        const auto parent =
            m_document->project().entities.constFind(m_parentUuid);
        if (parent == m_document->project().entities.constEnd())
        {
            emit statusMessage(tr("父实体不存在，不能拖入素材"));
            event->ignore();
            return;
        }
        if (start < parent.value().start || end > parent.value().end)
        {
            emit statusMessage(
                tr("素材范围 [%1, %2) 超出父实体 [%3, %4)")
                    .arg(start)
                    .arg(end)
                    .arg(parent.value().start)
                    .arg(parent.value().end));
            event->ignore();
            return;
        }
    }

    const int row = dropRowFor(pos.y(), start, end);
    const QVector<Uuid> created = createEntitiesForAsset(*asset, start, row);
    if (created.isEmpty())
    {
        event->ignore();
        return;
    }

    m_document->setSelection(created.first());
    const QString label = asset->name.isEmpty() ? hash : asset->name;
    m_document->commitEntities(
        created, QStringLiteral("pvf: add asset \"%1\"").arg(label));
    updateScrollBar();
    update();
    event->acceptProposedAction();
}

bool TimelineWidget::trimEntityTree(const Uuid& uuid, int newStart, int newEnd,
                                    QVector<Uuid>& upserts,
                                    QVector<Uuid>& deletes, QString* error)
{
    if (!m_document)
        return false;

    model::Project trimmed;
    QVector<Uuid> localUpserts;
    QVector<Uuid> localDeletes;
    if (!applyEntityRange(m_document->project(), m_document->assets(), uuid,
                          newStart, newEnd, &trimmed, &localUpserts,
                          &localDeletes, error))
        return false;

    m_document->mutableProject() = std::move(trimmed);
    upserts = localUpserts;
    deletes = localDeletes;
    return true;
}

} // namespace pvf::next
