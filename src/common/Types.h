#pragma once

#include <QPointF>
#include <QString>

namespace pvf {

using Uuid = QString;
using AssetHash = QString;

enum class AssetType
{
    Image,
    Video,
    Audio,
    Html
};

enum class ContentKind
{
    Empty,
    Text,
    Vector,
    Camera,
    Asset,
    ComicText,
    RadialLines,
    FlowLines,
    HalftoneDots
};

enum class SolverEffect
{
    Gravity,
    Orbit,
    Spin,
    BlackHole
};

inline QString assetTypeName(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("image");
    case AssetType::Video:
        return QStringLiteral("video");
    case AssetType::Audio:
        return QStringLiteral("audio");
    case AssetType::Html:
        return QStringLiteral("html");
    }
    return QStringLiteral("image");
}

inline QString contentKindName(ContentKind kind)
{
    switch (kind)
    {
    case ContentKind::Empty:
        return QStringLiteral("empty");
    case ContentKind::Text:
        return QStringLiteral("text");
    case ContentKind::Vector:
        return QStringLiteral("vector");
    case ContentKind::Camera:
        return QStringLiteral("camera");
    case ContentKind::Asset:
        return QStringLiteral("asset");
    case ContentKind::ComicText:
        return QStringLiteral("comicText");
    case ContentKind::RadialLines:
        return QStringLiteral("radialLines");
    case ContentKind::FlowLines:
        return QStringLiteral("flowLines");
    case ContentKind::HalftoneDots:
        return QStringLiteral("halftoneDots");
    }
    return QStringLiteral("empty");
}

inline QString solverEffectName(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return QStringLiteral("gravity");
    case SolverEffect::Orbit:
        return QStringLiteral("orbit");
    case SolverEffect::Spin:
        return QStringLiteral("spin");
    case SolverEffect::BlackHole:
        return QStringLiteral("blackhole");
    }
    return QStringLiteral("gravity");
}

} // namespace pvf
