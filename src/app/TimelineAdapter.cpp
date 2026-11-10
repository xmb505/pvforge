#include "TimelineAdapter.h"

#include "EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Query.h"

#include <QVariantMap>

#include <algorithm>
#include <functional>

namespace pvf {

namespace {

EntityType legacyType(const model::Content& content)
{
    switch (content.kind)
    {
    case ContentKind::Empty:
        return EntityType::Folder;
    case ContentKind::Text:
        return EntityType::Text;
    case ContentKind::Vector:
        return EntityType::Vector;
    case ContentKind::Camera:
        return EntityType::Camera;
    case ContentKind::ComicText:
        return EntityType::ComicText;
    case ContentKind::RadialLines:
        return EntityType::RadialLines;
    case ContentKind::FlowLines:
        return EntityType::FlowLines;
    case ContentKind::HalftoneDots:
        return EntityType::HalftoneDots;
    case ContentKind::BrandWall:
        return EntityType::BrandWall;
    case ContentKind::Asset:
        switch (content.asset.type)
        {
        case AssetType::Image:
            return EntityType::Image;
        case AssetType::Video:
            return EntityType::Video;
        case AssetType::Audio:
            return EntityType::Audio;
        case AssetType::Html:
            return EntityType::HtmlLayer;
        }
        break;
    }
    return EntityType::Folder;
}

bool parentHidden(const model::Project& project, const model::Entity& entity)
{
    if (entity.hidden)
        return true;
    if (!entity.inheritVisibility)
        return false;
    Uuid parent = entity.parentUuid;
    while (!parent.isEmpty())
    {
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        if (it->hidden)
            return true;
        if (!it->inheritVisibility)
            break;
        parent = it->parentUuid;
    }
    return false;
}

bool parentLocked(const model::Project& project, const model::Entity& entity)
{
    if (entity.locked)
        return true;
    Uuid parent = entity.parentUuid;
    while (!parent.isEmpty())
    {
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        if (it->locked)
            return true;
        parent = it->parentUuid;
    }
    return false;
}

} // namespace

TimelineAdapter::TimelineAdapter(EditorSession* session)
    : m_session(session)
{
}

int TimelineAdapter::idFor(const Uuid& uuid)
{
    const auto it = m_idByUuid.constFind(uuid);
    if (it != m_idByUuid.constEnd())
        return it.value();
    const int id = m_nextId++;
    m_idByUuid.insert(uuid, id);
    m_uuidById.insert(id, uuid);
    return id;
}

int TimelineAdapter::idForUuid(const Uuid& uuid) const
{
    return m_idByUuid.value(uuid, kInvalidEntity);
}

Uuid TimelineAdapter::uuidForId(int id) const
{
    return m_uuidById.value(id);
}

void TimelineAdapter::refresh()
{
    if (!m_session)
    {
        m_snapshot = TimelineSnapshot();
        return;
    }
    m_snapshot = buildSnapshot(m_session->project(), m_session->playhead());
}

TimelineSnapshot TimelineAdapter::buildSnapshot(const model::Project& project,
                                                int playhead)
{
    TimelineSnapshot snapshot;
    if (!m_session)
        return snapshot;

    snapshot.fps = std::max(1, project.settings.fps);
    snapshot.playhead = playhead;

    // Parent before children; each sibling group is already sorted by
    // row, start then uuid (model::childrenOf).
    QVector<Uuid> order;
    std::function<void(const Uuid&)> appendChildren = [&](const Uuid& parent) {
        for (const Uuid& uuid : model::childrenOf(project, parent))
        {
            const auto found = project.entities.constFind(uuid);
            if (found == project.entities.constEnd())
                continue;
            order.append(uuid);
            appendChildren(uuid);
        }
    };
    appendChildren(Uuid{});

    int contentEnd = 0;
    for (const Uuid& uuid : order)
    {
        const model::Entity& source = project.entities[uuid];
        Entity view;
        view.id = idFor(uuid);
        view.name = source.name.isEmpty() ? uuid : source.name;
        view.parent = source.parentUuid.isEmpty()
                          ? kInvalidEntity
                          : idFor(source.parentUuid);
        view.type = legacyType(source.content);
        view.hidden = parentHidden(project, source);
        view.locked = parentLocked(project, source);

        QVariantMap data;
        data.insert(QStringLiteral("timelineRow"), source.row);
        data.insert(QStringLiteral("clipStart"), source.start);
        data.insert(QStringLiteral("clipEnd"), source.end);
        data.insert(QStringLiteral("modelStart"), source.start);
        data.insert(QStringLiteral("modelEnd"), source.end);
        data.insert(QStringLiteral("modelDuration"), source.duration());
        data.insert(QStringLiteral("contentKind"),
                    contentKindName(source.content.kind));

        if (source.content.kind == ContentKind::Asset)
        {
            const model::AssetContent& asset = source.content.asset;
            data.insert(QStringLiteral("assetHash"), asset.hash);
            data.insert(QStringLiteral("assetType"),
                        assetTypeName(asset.type));
            data.insert(QStringLiteral("assetSourceIn"), asset.sourceIn);

            const auto stored = m_session->assets().getIncludingMissing(asset.hash);
            if (stored)
            {
                const QJsonObject metadata = stored->metadata;
                const double seconds =
                    metadata.value(QStringLiteral("durationSeconds")).toDouble();
                const int frames =
                    metadata.value(QStringLiteral("durationFrames"))
                        .toInt();
                data.insert(QStringLiteral("assetName"), stored->name);
                data.insert(QStringLiteral("assetDurationSeconds"), seconds);
                data.insert(QStringLiteral("assetDurationFrames"), frames);
            }
        }
        view.data = data;

        // Keyframes are stored relative to entity.start in the new model;
        // the legacy painter expects absolute project frames.
        for (auto it = source.tracks.constBegin();
             it != source.tracks.constEnd(); ++it)
        {
            Track track;
            track.keys.reserve(it->keys.size());
            for (const model::Keyframe& key : it->keys)
            {
                Keyframe viewKey;
                viewKey.frame = double(source.start + key.frame);
                viewKey.value = key.value;
                viewKey.note = key.note;
                track.keys.append(viewKey);
            }
            view.tracks.insert(it.key(), track);
        }

        if (source.parentUuid.isEmpty())
            contentEnd = std::max(contentEnd, source.end);
        snapshot.entities.append(view);
    }
    snapshot.contentEnd = contentEnd;

    for (const model::Marker& marker : project.markers)
    {
        TimelineMarkerView view;
        view.id = marker.id;
        view.frame = marker.frame;
        view.title = marker.title;
        view.note = marker.note;
        const QColor color(marker.color);
        view.color = color.isValid() ? color : QColor(255, 190, 60);
        snapshot.markers.append(view);
    }
    std::sort(snapshot.markers.begin(), snapshot.markers.end(),
              [](const TimelineMarkerView& a, const TimelineMarkerView& b) {
                  if (a.frame != b.frame)
                      return a.frame < b.frame;
                  if (a.title != b.title)
                      return a.title < b.title;
                  return a.id < b.id;
              });

    return snapshot;
}

} // namespace pvf
