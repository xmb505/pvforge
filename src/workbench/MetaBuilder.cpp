#include "workbench/MetaBuilder.h"

#include "render2/Render.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QMetaType>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pvf::workbench {
namespace {

// Bumped from 3 when the absolute project frame number and per-frame audio
// playback state were removed: a cached frame now describes only the picture,
// so replacing the format keeps pre-existing rows from ever comparing equal.
constexpr quint32 kMetaFormat = 4;

// Explicit, ordered QVariant serialisation.  QVariant's own stream operator is
// stable for a given Qt build but depends on the exact stored type, while
// solver params and HTML variables can arrive as int or double depending on
// where they were parsed.  The tags below make the bytes deterministic.
void writeVariant(QDataStream& out, const QVariant& value)
{
    switch (value.typeId())
    {
    case QMetaType::UnknownType:
        out << quint8(0);
        return;
    case QMetaType::Bool:
        out << quint8(1) << quint8(value.toBool() ? 1 : 0);
        return;
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        out << quint8(2) << qint64(value.toLongLong());
        return;
    case QMetaType::Float:
    case QMetaType::Double:
        out << quint8(3) << double(value.toDouble());
        return;
    case QMetaType::QString:
        out << quint8(4) << value.toString();
        return;
    case QMetaType::QByteArray:
        out << quint8(5) << value.toByteArray();
        return;
    case QMetaType::QStringList: {
        const QStringList list = value.toStringList();
        out << quint8(6) << quint32(list.size());
        for (const QString& item : list)
            out << item;
        return;
    }
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        out << quint8(7) << quint32(list.size());
        for (const QVariant& item : list)
            writeVariant(out, item);
        return;
    }
    case QMetaType::QVariantMap:
    case QMetaType::QVariantHash: {
        const QVariantMap map = value.toMap();
        QStringList keys = map.keys();
        std::sort(keys.begin(), keys.end());
        out << quint8(8) << quint32(keys.size());
        for (const QString& key : keys)
        {
            out << key;
            writeVariant(out, map.value(key));
        }
        return;
    }
    default:
        break;
    }

    // Unknown types are compared by their printable representation.  None of
    // the fields entering a frame meta use them today, but keeping the
    // fallback deterministic means a future caller cannot silently make the
    // cache non-reproducible.
    out << quint8(9) << QString::fromLatin1(value.typeName()) << value.toString();
}

void writeVariantMap(QDataStream& out, const QVariantMap& map)
{
    QStringList keys = map.keys();
    std::sort(keys.begin(), keys.end());
    out << quint32(keys.size());
    for (const QString& key : keys)
    {
        out << key;
        writeVariant(out, map.value(key));
    }
}

bool activeAt(const model::Entity& entity, int frame)
{
    return frame >= entity.start && frame <= entity.end;
}

bool isAudio(const model::Entity& entity)
{
    return entity.content.kind == ContentKind::Asset
           && entity.content.asset.type == AssetType::Audio;
}

// Content kinds that put pixels on the canvas.  Camera and Empty entities only
// contribute a transform (to their children) and audio never contributes at
// all, so a standalone one must not enter the frame meta.
bool paintsPixels(const model::Entity& entity)
{
    switch (entity.content.kind)
    {
    case ContentKind::Text:
    case ContentKind::Vector:
        return true;
    case ContentKind::Asset: {
        const AssetType type = entity.content.asset.type;
        return type == AssetType::Image || type == AssetType::Video
               || type == AssetType::Html;
    }
    case ContentKind::Camera:
    case ContentKind::Empty:
        return false;
    }
    return false;
}

// Mirrors RenderScene's editor-hidden propagation: `hidden` also hides a child
// that inherits visibility, and the flag follows the same chain down.
bool editorHiddenAt(const model::Project& project, const Uuid& uuid)
{
    QVector<const model::Entity*> chain;
    QSet<Uuid> visited;
    Uuid current = uuid;
    while (!current.isEmpty() && !visited.contains(current))
    {
        visited.insert(current);
        const auto found = project.entities.constFind(current);
        if (found == project.entities.constEnd())
            break;
        chain.prepend(&found.value());
        current = found->parentUuid;
    }

    bool hidden = false;
    for (const model::Entity* entity : chain)
        hidden = entity->hidden || (hidden && entity->inheritVisibility);
    return hidden;
}

struct FrameEntities
{
    QSet<Uuid> uuids;
    QHash<Uuid, anim::Pose> poses;
    bool hasRaster = false;
};

// Every entity that can paint at this frame, plus the parent chain whose
// transforms/visibility feed into it.  An inactive parent is still included
// when a drawing child inherits from it, but inactive siblings are not:
// editing an entity in another clip cannot invalidate this frame.
//
// Entities that cannot change the picture are deliberately left out:
//   - audio assets (no pixels, and their sourceFrame advances every frame);
//   - invisible/editor-hidden leaves (opacity <= 0, !visible, hidden) when no
//     visible child depends on them (an invisible physics collider is kept
//     because it can still move the visible entities that hit it);
//   - standalone Camera/Empty entities (only their children can paint).
// Audio or invisible ancestors of a visible child are still serialised to keep
// transform inheritance, but writeAssetDetails() omits their playback state.
FrameEntities collectEntities(const model::Project& project, int frame,
                              render2::SceneWorld& world, anim::Cache& cache)
{
    FrameEntities result;

    // Evaluate in UUID order instead of QHash order: pose evaluation may fill
    // shared caches, and the serialised bytes must not depend on bucket order.
    QStringList activeUuids;
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        if (activeAt(it.value(), frame))
            activeUuids.append(it.key());
    }
    std::sort(activeUuids.begin(), activeUuids.end());

    for (const Uuid& uuid : activeUuids)
    {
        const auto found = project.entities.constFind(uuid);
        if (found == project.entities.constEnd())
            continue;
        const model::Entity& entity = found.value();

        const anim::Pose pose =
            anim::evaluateEntity(project, uuid, frame, world, cache);
        result.poses.insert(uuid, pose);

        const bool draws = paintsPixels(entity) && pose.visible
                           && pose.opacity > 0.0
                           && !editorHiddenAt(project, uuid);
        // An invisible entity normally cannot change the picture, but a
        // physics collider still moves the visible entities that hit it.
        if (!draws && !entity.physicsInteract)
            continue;

        result.uuids.insert(uuid);
        if (draws && entity.content.kind == ContentKind::Asset)
        {
            const AssetType type = entity.content.asset.type;
            if (type == AssetType::Image || type == AssetType::Video
                || type == AssetType::Html)
                result.hasRaster = true;
        }

        QSet<Uuid> visited;
        Uuid current = entity.parentUuid;
        while (!current.isEmpty() && !visited.contains(current))
        {
            visited.insert(current);
            const auto found = project.entities.constFind(current);
            if (found == project.entities.constEnd())
                break;
            result.uuids.insert(current);
            if (!result.poses.contains(current))
            {
                result.poses.insert(
                    current,
                    anim::evaluateEntity(project, current, frame, world, cache));
            }
            current = found->parentUuid;
        }
    }
    return result;
}

// Serialises the content as it looks at this project frame: whole-frame
// content animation values (pixelSize, box sizes, vector size, ...) are written
// as their evaluated values, so keying them changes the frame's cache metadata
// exactly when it changes the picture.
void writeContent(QDataStream& out, const model::Entity& entity,
                  double projectFrame)
{
    const model::Content content = render2::animatedContent(entity, projectFrame);
    out << qint32(static_cast<int>(content.kind));
    switch (content.kind)
    {
    case ContentKind::Text: {
        const model::TextContent& text = content.text;
        out << text.text << text.fontFamily << qint32(text.pixelSize) << text.color
            << text.strokeColor << text.strokeWidth << qint32(text.align)
            << text.lineHeight << text.letterSpacing << text.padding
            << text.boxWidth << text.boxHeight << text.bold << text.italic
            << text.underline << text.vertical << text.verticalRtl;
        break;
    }
    case ContentKind::Vector: {
        const model::VectorContent& vector = content.vector;
        out << qint32(vector.shape) << vector.width << vector.height
            << vector.cornerRadius << qint32(vector.sides) << vector.innerRatio
            << vector.strokeWidth << vector.fillColor << vector.strokeColor;
        break;
    }
    case ContentKind::Camera:
    case ContentKind::Empty:
        break;
    case ContentKind::Asset:
        out << content.asset.hash
            << qint32(static_cast<int>(content.asset.type));
        break;
    }
}

void writeAssetDetails(QDataStream& out, const model::Entity& entity, int frame,
                       int fps, const assets::StoredAsset* stored)
{
    const model::AssetContent& asset = entity.content.asset;

    // Audio never paints.  It can only reach this function as an ancestor of a
    // visible entity that inherits its transform, and its per-frame playback
    // state (sourceFrame) and volume must not make the picture key miss.
    if (isAudio(entity))
        return;

    out << bool(stored != nullptr);

    if (asset.type == AssetType::Video)
    {
        // Actual source frame requested from a timed video.  Images ignore
        // sourceIn and HTML has its own request frame/variables below.
        out << asset.sourceIn << asset.volume;
        const qint64 sourceFrame =
            qint64(asset.sourceIn) + (frame - entity.start);
        out << sourceFrame;
    }

    QVariantMap defaults;
    QVariantMap overrides;
    if (asset.type == AssetType::Html)
    {
        const QJsonObject metadata = stored ? stored->metadata : QJsonObject();
        defaults = metadata.value(QStringLiteral("variables"))
                       .toObject()
                       .toVariantMap();

        // Mirrors the request MediaServices builds for HtmlFrameService: the
        // entity-relative frame and time select the page state.  When the
        // manifest marks the page timeDependent=false the service normalises
        // both to 0, so moving the entity must not invalidate it.
        const int localFrame = frame - entity.start;
        const bool timeDependent =
            metadata.value(QStringLiteral("timeDependent")).toBool(true);
        const int requestedFrame = timeDependent ? localFrame : 0;
        const double requestedTime =
            double(requestedFrame) / double(std::max(1, fps));
        out << requestedFrame << requestedTime << timeDependent
            << metadata.value(QStringLiteral("entry")).toString();

        // Whole-frame keyframes drive the declared numeric variables at the
        // entity-relative frame.  A page marked timeDependent=false still lets
        // the timeline drive those variables, so they are evaluated at the real
        // relative frame, not the normalised render request frame.
        const model::KeyframeSample sample = model::entitySampleAt(
            entity, double(localFrame));
        for (auto it = sample.htmlVars.constBegin();
             it != sample.htmlVars.constEnd(); ++it)
        {
            if (it.value().isDouble())
                overrides.insert(it.key(), it.value().toDouble());
        }
    }

    writeVariantMap(out, defaults);
    writeVariantMap(out, overrides);
}

void writeSolvers(QDataStream& out, const model::Entity& entity)
{
    out << quint32(entity.solvers.size());
    for (const model::SolverSegment& segment : entity.solvers)
    {
        out << segment.id << qint32(static_cast<int>(segment.effect))
            << qint32(segment.startFrame) << qint32(segment.endFrame)
            << qint32(segment.version) << segment.initialVelocity;
        writeVariantMap(out, segment.params);
    }
}

bool projectHasHtml(const model::Project& project)
{
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.content.kind == ContentKind::Asset
            && entity.content.asset.type == AssetType::Html)
            return true;
    }
    return false;
}

QString sha1Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

} // namespace

bool FrameMeta::equals(const FrameMeta& other) const
{
    if (sha1 != other.sha1)
        return false;
    return bytes == other.bytes;
}

MetaBuilder::MetaBuilder(const model::Project& project,
                         const assets::AssetStore& assets,
                         QHash<QString, solver::Result>* sharedSolverResults)
    : m_project(project)
    , m_assets(assets)
    , m_sharedSolverResults(sharedSolverResults)
{
}

FrameMeta MetaBuilder::build(int frame, const Uuid& cameraUuid,
                             const Options& options) const
{
    anim::Cache cache;
    cache.sharedSolverResults = m_sharedSolverResults;
    render2::SceneWorld world(m_project, m_assets, &cache);

    const anim::Pose cameraPose =
        cameraUuid.isEmpty()
            ? anim::Pose{}
            : anim::evaluateEntity(m_project, cameraUuid, frame, world, cache);

    QByteArray buffer;
    QDataStream out(&buffer, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.setFloatingPointPrecision(QDataStream::DoublePrecision);

    out << kMetaFormat;
    out << qint32(m_project.settings.fps)
        << qint32(m_project.settings.width) << qint32(m_project.settings.height)
        << m_project.settings.background << options.targetSize
        << qint32(options.scalePercent) << qint32(options.expandPercent)
        << quint8(options.transparentOutsideCanvas ? 1 : 0)
        << options.rendererVersion << options.chromeVersion << cameraUuid;

    const QRectF worldRect = renderRect(m_project, options.expandPercent);
    out << worldRect.left() << worldRect.top() << worldRect.width()
        << worldRect.height();

    out << cameraPose.world << cameraPose.opacity << cameraPose.visible;

    const FrameEntities collected = collectEntities(m_project, frame, world, cache);
    QStringList uuids = collected.uuids.values();
    std::sort(uuids.begin(), uuids.end());

    out << quint32(uuids.size());
    for (const Uuid& uuid : uuids)
    {
        const auto found = m_project.entities.constFind(uuid);
        if (found == m_project.entities.constEnd())
            continue;
        const model::Entity& entity = found.value();
        const anim::Pose pose = collected.poses.value(uuid);

        out << entity.uuid << entity.parentUuid << qint32(entity.row)
            << entity.hidden << entity.visible << entity.inheritVisibility;
        out << pose.world << pose.opacity << pose.visible;

        writeContent(out, entity, double(frame));
        if (entity.content.kind == ContentKind::Asset)
        {
            const auto stored = m_assets.get(entity.content.asset.hash);
            writeAssetDetails(out, entity, frame, m_project.settings.fps,
                              stored ? &*stored : nullptr);
        }

        writeSolvers(out, entity);
    }

    FrameMeta meta;
    meta.bytes = buffer;
    meta.sha1 = QCryptographicHash::hash(buffer, QCryptographicHash::Sha1);
    meta.hasRaster = collected.hasRaster;
    return meta;
}

QString renderSignature(const model::Project& project, const Options& options)
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);

    out << quint32(1) << options.rendererVersion << qint32(options.scalePercent)
        << qint32(options.expandPercent)
        << quint8(options.transparentOutsideCanvas ? 1 : 0)
        << qint32(project.settings.fps) << qint32(project.settings.width)
        << qint32(project.settings.height);

    const QRectF worldRect = renderRect(project, options.expandPercent);
    out << worldRect.left() << worldRect.top() << worldRect.width()
        << worldRect.height();

    const bool html = projectHasHtml(project);
    out << html;
    if (html)
        out << options.chromeVersion;

    return QStringLiteral("wb1-") + sha1Hex(payload);
}

QRectF renderRect(const model::Project& project, int expandPercent)
{
    const double width = double(std::max(1, project.settings.width));
    const double height = double(std::max(1, project.settings.height));
    const double factor = double(std::clamp(expandPercent, 0, 200)) / 100.0;
    const double expandX = width * factor;
    const double expandY = height * factor;
    // Model space is centred on (0,0), including the unexpanded canvas.
    return QRectF(-width / 2.0 - expandX, -height / 2.0 - expandY,
                  width + 2.0 * expandX, height + 2.0 * expandY);
}

QSize cacheSizeFor(const model::Project& project, int scalePercent, int maxWidth,
                   int expandPercent)
{
    const QRectF world = renderRect(project, expandPercent);
    const double scale = double(scalePercent) / 100.0;
    int width = std::max(1, int(std::lround(world.width() * scale)));
    int height = std::max(1, int(std::lround(world.height() * scale)));

    if (maxWidth > 0 && width > maxWidth)
    {
        height = std::max(1, int(std::lround(double(height) * maxWidth / double(width))));
        width = maxWidth;
    }
    return QSize(width, height);
}

} // namespace pvf::workbench
