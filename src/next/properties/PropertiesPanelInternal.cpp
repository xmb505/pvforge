#include "PropertiesPanelInternal.h"

namespace pvf::next::detail {

QString contentKindLabel(ContentKind kind)
{
    switch (kind)
    {
    case ContentKind::Empty:
        return QStringLiteral("空");
    case ContentKind::Text:
        return QStringLiteral("文字");
    case ContentKind::Vector:
        return QStringLiteral("矢量");
    case ContentKind::Camera:
        return QStringLiteral("摄像机");
    case ContentKind::Asset:
        return QStringLiteral("素材");
    case ContentKind::ComicText:
        return QStringLiteral("漫画文字");
    case ContentKind::RadialLines:
        return QStringLiteral("径向线");
    case ContentKind::FlowLines:
        return QStringLiteral("流线");
    case ContentKind::HalftoneDots:
        return QStringLiteral("网点");
    case ContentKind::BrandWall:
        return QStringLiteral("品牌墙");
    }
    return QStringLiteral("空");
}

bool hasKeyAt(const model::Entity& entity, const QString& trackName, int frame)
{
    const auto track = entity.tracks.constFind(trackName);
    if (track == entity.tracks.constEnd())
        return false;
    for (const model::Keyframe& keyframe : track.value().keys)
    {
        if (keyframe.frame == frame)
            return true;
    }
    return false;
}

QStringList solverDrivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

bool isSolverStartKey(const model::Entity& entity, const QString& trackName,
                      int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (solver.startFrame == frame
            && solverDrivenTracks(solver.effect).contains(trackName))
            return true;
    }
    return false;
}

} // namespace pvf::next::detail
