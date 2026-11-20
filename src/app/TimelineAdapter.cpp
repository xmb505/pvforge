#include "TimelineAdapter.h"

#include "EditorSession.h"
#include "assets/AssetStore.h"
#include "audio2/Peaks.h"
#include "model/Query.h"

#include <QDir>
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

QString audioContentFile(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

// Range, row, parent and keyframe fields derived from the model entity.  The
// drag preview refreshes exactly these and keeps the expensive asset metadata
// (name, duration, decoded peaks) from the snapshot it was built with.
void fillViewFields(Entity& view, const model::Entity& source,
                    const model::Project& project, int fps)
{
    view.name = source.name.isEmpty() ? source.uuid : source.name;
    view.hidden = parentHidden(project, source);
    view.locked = parentLocked(project, source);

    QVariantMap& data = view.data;
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
        data.insert(QStringLiteral("assetType"), assetTypeName(asset.type));
        data.insert(QStringLiteral("assetSourceIn"), asset.sourceIn);

        if (asset.type == AssetType::Audio)
        {
            const int safeFps = std::max(1, fps);
            const double trimStart = double(asset.sourceIn) / safeFps;
            const int frames = data.value(QStringLiteral("assetDurationFrames"))
                                   .toInt();
            const int clipFrames = std::max(0, source.end - source.start);
            const int left = frames > 0
                                 ? std::max(0, frames - asset.sourceIn
                                                    - clipFrames)
                                 : 0;
            data.insert(QStringLiteral("trimStart"), trimStart);
            data.insert(QStringLiteral("trimEnd"), double(left) / safeFps);
        }
    }

    // Keyframes are stored relative to entity.start in the model; the legacy
    // painter expects absolute project frames and one diamond per frame.
    view.keyframes.clear();
    view.keyframes.reserve(source.keyframes.size());
    for (const model::Keyframe& key : source.keyframes)
    {
        Keyframe viewKey;
        viewKey.frame = source.start + key.frame;
        viewKey.ease = key.easing;
        viewKey.note = key.note;
        view.keyframes.append(viewKey);
    }
    for (const model::SolverSegment& solver : source.solvers)
    {
        const int startFrame = source.start + solver.startFrame;
        const int endFrame = source.start + model::solverStructuralEnd(source, solver);
        for (Keyframe& viewKey : view.keyframes)
        {
            if (viewKey.frame == startFrame)
                viewKey.solverStart = true;
            if (viewKey.frame == endFrame)
                viewKey.solverEnd = true;
        }
    }
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
    // row, start then uuid.  One index build keeps the walk linear (#132).
    const model::ChildrenIndex index(project);
    QVector<Uuid> order;
    std::function<void(const Uuid&)> appendChildren = [&](const Uuid& parent) {
        for (const Uuid& uuid : index.children(parent))
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
        view.parent = source.parentUuid.isEmpty()
                          ? kInvalidEntity
                          : idFor(source.parentUuid);
        view.type = legacyType(source.content);
        fillViewFields(view, source, project, project.settings.fps);

        if (source.content.kind == ContentKind::Asset)
        {
            const model::AssetContent& asset = source.content.asset;
            const auto stored = m_session->assets().getIncludingMissing(asset.hash);
            if (stored)
            {
                const QJsonObject metadata = stored->metadata;
                const double seconds =
                    metadata.value(QStringLiteral("durationSeconds")).toDouble();
                const int frames =
                    metadata.value(QStringLiteral("durationFrames"))
                        .toInt();
                view.data.insert(QStringLiteral("assetName"), stored->name);
                view.data.insert(QStringLiteral("assetDurationSeconds"), seconds);
                view.data.insert(QStringLiteral("assetDurationFrames"), frames);

                if (asset.type == AssetType::Audio)
                {
                    auto cached = m_audioPeaks.constFind(asset.hash);
                    if (cached == m_audioPeaks.constEnd())
                    {
                        auto clip = std::make_shared<AudioClip>();
                        clip->duration = seconds;
                        const QString path = audioContentFile(*stored);
                        if (!path.isEmpty() && seconds > 0.0)
                        {
                            const int buckets = std::clamp(
                                int(std::lround(seconds * 1000.0)), 1, 600000);
                            clip->peaks =
                                audio2::audioPeaks(path, buckets, nullptr);
                        }
                        cached = m_audioPeaks.insert(asset.hash, clip);
                    }
                    view.audio = cached.value();
                }
            }
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

bool TimelineAdapter::patchSnapshot(TimelineSnapshot& snapshot,
                                    const model::Project& project,
                                    int playhead) const
{
    if (!m_session)
        return false;
    if (snapshot.entities.size() != project.entities.size())
        return false;

    int contentEnd = 0;
    for (Entity& view : snapshot.entities)
    {
        const Uuid uuid = uuidForId(view.id);
        const auto source = project.entities.constFind(uuid);
        if (uuid.isEmpty() || source == project.entities.constEnd())
            return false;

        view.type = legacyType(source->content);
        view.parent = source->parentUuid.isEmpty()
                          ? kInvalidEntity
                          : idForUuid(source->parentUuid);
        fillViewFields(view, *source, project, project.settings.fps);
        if (source->parentUuid.isEmpty())
            contentEnd = std::max(contentEnd, source->end);
    }

    snapshot.playhead = playhead;
    snapshot.fps = std::max(1, project.settings.fps);
    snapshot.contentEnd = contentEnd;
    return true;
}

} // namespace pvf
