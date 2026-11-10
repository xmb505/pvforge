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

#include <algorithm>
#include <cmath>
#include <optional>

namespace pvf::workbench {
namespace {

constexpr quint32 kMetaFormat = 2;

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
    return frame >= entity.start && frame < entity.end;
}

// Every entity visible at this frame, plus the chain of parents whose
// transforms/visibility feed into those entities.  An inactive parent is still
// included when an active child inherits from it, but inactive siblings are
// not: editing an entity in another clip cannot invalidate this frame.
QSet<Uuid> collectEntities(const model::Project& project, int frame)
{
    QSet<Uuid> result;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd(); ++it)
    {
        if (!activeAt(it.value(), frame))
            continue;

        QSet<Uuid> visited;
        Uuid current = it.key();
        while (!current.isEmpty() && !visited.contains(current))
        {
            visited.insert(current);
            const auto found = project.entities.constFind(current);
            if (found == project.entities.constEnd())
                break;
            result.insert(current);
            current = found->parentUuid;
        }
    }
    return result;
}

void writeContent(QDataStream& out, const model::Content& content)
{
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
    case ContentKind::ComicText: {
        const model::ComicTextContent& comic = content.comic;
        out << comic.text.toUtf8() << comic.fontFamily.toUtf8()
            << qint32(comic.pixelSize) << comic.color.toUtf8()
            << comic.strokeColor.toUtf8() << comic.strokeWidth
            << qint32(comic.align) << comic.lineHeight << comic.bold
            << comic.italic << comic.shape.toUtf8() << comic.boxWidth
            << comic.boxHeight << comic.fillColor.toUtf8()
            << comic.bubbleStrokeColor.toUtf8() << comic.bubbleStrokeWidth
            << comic.cornerRadius << comic.tail << comic.tailX << comic.tailY
            << comic.rotation << comic.padding << qint32(comic.seed);
        break;
    }
    case ContentKind::RadialLines: {
        const model::RadialLinesContent& radial = content.radial;
        out << qint32(radial.count) << radial.radius << radial.innerRatio
            << radial.width << radial.angle << radial.color.toUtf8()
            << qint32(radial.seed);
        break;
    }
    case ContentKind::FlowLines: {
        const model::FlowLinesContent& flow = content.flow;
        out << qint32(flow.count) << flow.length << flow.spacing
            << flow.thickness << flow.area << flow.bend << flow.angle
            << flow.phase << flow.color.toUtf8() << qint32(flow.seed);
        break;
    }
    case ContentKind::HalftoneDots: {
        const model::HalftoneDotsContent& halftone = content.halftone;
        out << halftone.layout.toUtf8() << halftone.area << halftone.radius
            << halftone.spacing << halftone.dotSize << halftone.angle
            << halftone.gradientAngle << halftone.minRatio << halftone.jitter
            << halftone.color.toUtf8() << qint32(halftone.seed);
        break;
    }
    }
}

void writeAssetDetails(QDataStream& out, const model::Entity& entity, int frame,
                       const assets::StoredAsset* stored)
{
    const model::AssetContent& asset = entity.content.asset;
    out << asset.sourceIn << asset.volume;
    out << bool(stored != nullptr);

    // Actual source frame requested from a video asset.  Audio has no picture
    // and images ignore sourceIn, so they get -1.
    qint64 sourceFrame = -1;
    if (asset.type == AssetType::Video)
        sourceFrame = qint64(asset.sourceIn) + (frame - entity.start);
    out << sourceFrame;

    QVariantMap defaults;
    QVariantMap overrides;
    if (asset.type == AssetType::Html)
    {
        if (stored)
        {
            defaults = stored->metadata.value(QStringLiteral("variables"))
                           .toObject()
                           .toVariantMap();
        }

        const double relativeFrame = double(frame - entity.start);
        for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
        {
            if (!it.key().startsWith(QStringLiteral("var."), Qt::CaseInsensitive))
                continue;
            const std::optional<double> value =
                model::evaluateTrack(it.value(), relativeFrame);
            if (value)
                overrides.insert(it.key().mid(4), *value);
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
                         const assets::AssetStore& assets)
    : m_project(project)
    , m_assets(assets)
{
}

FrameMeta MetaBuilder::build(int frame, const Uuid& cameraUuid,
                             const Options& options) const
{
    render2::SceneWorld world(m_project, m_assets);
    anim::Cache cache;

    const anim::Pose cameraPose =
        cameraUuid.isEmpty()
            ? anim::Pose{}
            : anim::evaluateEntity(m_project, cameraUuid, frame, world, cache);

    QByteArray buffer;
    QDataStream out(&buffer, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.setFloatingPointPrecision(QDataStream::DoublePrecision);

    out << kMetaFormat;
    out << qint32(frame) << qint32(m_project.settings.fps)
        << qint32(m_project.settings.width) << qint32(m_project.settings.height)
        << m_project.settings.background << options.targetSize
        << qint32(options.scalePercent) << qint32(options.expandPercent)
        << quint8(options.transparentOutsideCanvas ? 1 : 0)
        << options.rendererVersion << options.chromeVersion << cameraUuid;

    const QRectF worldRect = renderRect(m_project, options.expandPercent);
    out << worldRect.left() << worldRect.top() << worldRect.width()
        << worldRect.height();

    out << cameraPose.world << cameraPose.opacity << cameraPose.visible;

    QSet<Uuid> collected = collectEntities(m_project, frame);
    QStringList uuids = collected.values();
    std::sort(uuids.begin(), uuids.end());

    out << quint32(uuids.size());
    for (const Uuid& uuid : uuids)
    {
        const auto found = m_project.entities.constFind(uuid);
        if (found == m_project.entities.constEnd())
            continue;
        const model::Entity& entity = found.value();
        const anim::Pose pose =
            anim::evaluateEntity(m_project, uuid, frame, world, cache);

        out << entity.uuid << entity.parentUuid << qint32(entity.row)
            << entity.hidden << entity.visible << entity.inheritVisibility;
        out << pose.world << pose.opacity << pose.visible;

        writeContent(out, entity.content);
        if (entity.content.kind == ContentKind::Asset)
        {
            const auto stored = m_assets.get(entity.content.asset.hash);
            writeAssetDetails(out, entity, frame, stored ? &*stored : nullptr);
        }

        writeSolvers(out, entity);
    }

    FrameMeta meta;
    meta.bytes = buffer;
    meta.sha1 = QCryptographicHash::hash(buffer, QCryptographicHash::Sha1);
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
    return QRectF(-expandX, -expandY, width + 2.0 * expandX,
                  height + 2.0 * expandY);
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
