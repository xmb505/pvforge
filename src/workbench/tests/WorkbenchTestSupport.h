#pragma once

// Helpers shared by the workbench cache test translation units.  They build
// small animated entities and compare cache files without pulling in the
// production headers from a test main.

#include "workbench/Cache.h"
#include "workbench/MetaBuilder.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTest>

#include <cstring>

namespace pvf::workbench::testsupport {

inline model::Entity textEntity(const Uuid& uuid, int start, int end)
{
    model::Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.start = start;
    entity.end = end;
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("hello");
    entity.content.text.pixelSize = 64;
    return entity;
}

// Whole-frame key at one frame: the existing key when present, otherwise the
// entity's static state (including the content animation defaults).
inline model::Keyframe baseKey(const model::Entity& entity, int frame)
{
    if (const model::Keyframe* existing = model::keyframeAt(entity.keyframes,
                                                           frame))
        return *existing;
    const model::KeyframeSample sample = model::staticEntitySample(entity);
    model::Keyframe key;
    key.frame = frame;
    key.x = sample.x;
    key.y = sample.y;
    key.rotation = sample.rotation;
    key.scaleX = sample.scaleX;
    key.scaleY = sample.scaleY;
    key.opacity = sample.opacity;
    key.visible = sample.visible;
    key.physicsInteract = sample.physicsInteract;
    key.content = sample.content;
    key.htmlVars = sample.htmlVars;
    return key;
}

// Sets one public animation parameter at an entity-relative frame.
inline void setWholeValue(model::Entity& entity, const QString& property,
                          int frame, double value)
{
    model::Keyframe key = baseKey(entity, frame);
    if (property == QLatin1String("x"))
        key.x = value;
    else if (property == QLatin1String("y"))
        key.y = value;
    else if (property == QLatin1String("rotation"))
        key.rotation = value;
    else if (property == QLatin1String("visible"))
        key.visible = value != 0.0;
    model::upsertKeyframe(entity.keyframes, key);
}

inline void setContentValue(model::Entity& entity, const QString& property,
                            int frame, double value)
{
    model::Keyframe key = baseKey(entity, frame);
    key.content.insert(property, value);
    model::upsertKeyframe(entity.keyframes, key);
}

inline void setHtmlVar(model::Entity& entity, const QString& name, int frame,
                       double value)
{
    model::Keyframe key = baseKey(entity, frame);
    key.htmlVars.insert(name, value);
    model::upsertKeyframe(entity.keyframes, key);
}

inline Options metaOptions(const model::Project& project, int scalePercent = 50,
                           int maxWidth = 1280, int expandPercent = 0)
{
    Options options;
    options.scalePercent = scalePercent;
    options.maxWidth = maxWidth;
    options.expandPercent = expandPercent;
    options.targetSize =
        cacheSizeFor(project, scalePercent, maxWidth, expandPercent);
    return options;
}

inline QImage testImage(const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(32, 120, 200));
    return image;
}

inline model::Entity assetEntity(const Uuid& uuid, int start, int end,
                                 AssetType type = AssetType::Image)
{
    model::Entity entity = textEntity(uuid, start, end);
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = type;
    entity.content.asset.hash = QStringLiteral("missing-asset");
    return entity;
}

// QOI is lossless; compare the stored straight RGBA bytes (not the container
// format, which may legitimately differ from the input).
inline bool samePixels(const QImage& first, const QImage& second)
{
    if (first.isNull() || second.isNull() || first.size() != second.size())
        return false;
    const QImage left = first.convertToFormat(QImage::Format_RGBA8888);
    const QImage right = second.convertToFormat(QImage::Format_RGBA8888);
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return false;
    for (int y = 0; y < left.height(); ++y)
    {
        if (std::memcmp(left.constScanLine(y), right.constScanLine(y),
                        size_t(left.width()) * 4)
            != 0)
            return false;
    }
    return true;
}

inline void writeTextFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
}

// <content sha1>.<extension> inside the cache directory.
inline QString contentPath(const Cache& cache, const FrameMeta& meta,
                           const QString& extension)
{
    return QDir(cache.directory())
        .filePath(frameContentKey(meta) + QLatin1Char('.') + extension);
}

// Number of cached frame files (QOI + WebP) in the directory.
inline int frameFileCount(const QString& directory)
{
    return QDir(directory)
        .entryList({QStringLiteral("*.qoi"), QStringLiteral("*.webp")},
                   QDir::Files)
        .size();
}

} // namespace pvf::workbench::testsupport
