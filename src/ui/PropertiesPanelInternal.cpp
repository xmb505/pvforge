#include "PropertiesPanelInternal.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>

#include <algorithm>
#include <cmath>

namespace pvf::ui::detail {

QString contentKindLabel(ContentKind kind)
{
    switch (kind)
    {
    case ContentKind::Empty:
        return QStringLiteral("空实体");
    case ContentKind::Text:
        return QStringLiteral("文字");
    case ContentKind::Vector:
        return QStringLiteral("矢量");
    case ContentKind::Camera:
        return QStringLiteral("摄像机");
    case ContentKind::Asset:
        return QStringLiteral("素材");
    case ContentKind::ComicText:
        return QStringLiteral("漫画文本框");
    case ContentKind::RadialLines:
        return QStringLiteral("放射状线条");
    case ContentKind::FlowLines:
        return QStringLiteral("快速流过");
    case ContentKind::HalftoneDots:
        return QStringLiteral("漫画点阵");
    case ContentKind::BrandWall:
        return QStringLiteral("品牌瓷砖墙");
    }
    return QStringLiteral("空实体");
}

QString assetTypeLabel(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("图片");
    case AssetType::Video:
        return QStringLiteral("视频");
    case AssetType::Audio:
        return QStringLiteral("音频");
    case AssetType::Html:
        return QStringLiteral("HTML");
    }
    return QStringLiteral("素材");
}

QString colorString(const QColor& color)
{
    return color.alpha() == 255 ? color.name(QColor::HexRgb)
                                : color.name(QColor::HexArgb);
}

TrackSpec numericTrack(const QString& name, const QString& label, double minimum,
                       double maximum, double singleStep, int decimals,
                       const QString& suffix)
{
    TrackSpec spec;
    spec.name = name;
    spec.label = label;
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.singleStep = singleStep;
    spec.decimals = decimals;
    spec.suffix = suffix;
    return spec;
}

TrackSpec stepTrackSpec(const QString& name, const QString& label)
{
    TrackSpec spec = numericTrack(name, label, 0.0, 1.0, 1.0, 0);
    spec.step = true;
    return spec;
}

QVector<TrackSpec> commonTrackSpecs(bool camera)
{
    QVector<TrackSpec> specs;
    specs.append(numericTrack(QStringLiteral("x"), QStringLiteral("X"),
                              -1000000000.0, 1000000000.0, 1.0, 2,
                              QStringLiteral(" px")));
    specs.append(numericTrack(QStringLiteral("y"), QStringLiteral("Y"),
                              -1000000000.0, 1000000000.0, 1.0, 2,
                              QStringLiteral(" px")));
    specs.append(numericTrack(QStringLiteral("rotation"), QStringLiteral("旋转"),
                              -36000.0, 36000.0, 1.0, 2,
                              QStringLiteral(" °")));
    if (camera)
    {
        specs.append(numericTrack(QStringLiteral("scaleX"),
                                  QStringLiteral("取景比例"), 0.05, 1.0, 0.01,
                                  3));
        return specs;
    }

    specs.append(numericTrack(QStringLiteral("scaleX"), QStringLiteral("缩放 X"),
                              -1000.0, 1000.0, 0.01, 4));
    specs.append(numericTrack(QStringLiteral("scaleY"), QStringLiteral("缩放 Y"),
                              -1000.0, 1000.0, 0.01, 4));
    specs.append(numericTrack(QStringLiteral("opacity"), QStringLiteral("不透明度"),
                              0.0, 1.0, 0.01, 3));
    specs.append(numericTrack(QStringLiteral("pivotX"), QStringLiteral("轴心 X"),
                              -1000000000.0, 1000000000.0, 1.0, 2,
                              QStringLiteral(" px")));
    specs.append(numericTrack(QStringLiteral("pivotY"), QStringLiteral("轴心 Y"),
                              -1000000000.0, 1000000000.0, 1.0, 2,
                              QStringLiteral(" px")));
    specs.append(stepTrackSpec(QStringLiteral("visible"), QStringLiteral("可见")));
    specs.append(stepTrackSpec(QStringLiteral("physicsInteract"),
                               QStringLiteral("物理干涉")));
    return specs;
}

QVector<TrackSpec> trackSpecsFor(const model::Entity& entity)
{
    const ContentKind kind = entity.content.kind;
    QVector<TrackSpec> specs = commonTrackSpecs(kind == ContentKind::Camera);

    switch (kind)
    {
    case ContentKind::Text:
        specs.append(numericTrack(QStringLiteral("fontSize"), QStringLiteral("字号"),
                                  1.0, 100000.0, 1.0, 0,
                                  QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("lineHeight"), QStringLiteral("行高"),
                                  0.01, 100.0, 0.1, 3));
        specs.append(numericTrack(QStringLiteral("letterSpacing"), QStringLiteral("字距"),
                                  -10000.0, 10000.0, 1.0, 2,
                                  QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("strokeWidth"),
                                  QStringLiteral("描边宽度"), 0.0, 1000000000.0,
                                  0.5, 2, QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("padding"), QStringLiteral("内边距"),
                                  0.0, 1000000000.0, 1.0, 2,
                                  QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("boxWidth"), QStringLiteral("框宽"),
                                  0.0, 1000000000.0, 1.0, 2,
                                  QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("boxHeight"), QStringLiteral("框高"),
                                  0.0, 1000000000.0, 1.0, 2,
                                  QStringLiteral(" px")));
        break;
    case ContentKind::Vector:
        specs.append(numericTrack(QStringLiteral("width"), QStringLiteral("宽度"),
                                  0.0, 1000000000.0, 1.0, 2,
                                  QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("height"), QStringLiteral("高度"),
                                  0.0, 1000000000.0, 1.0, 2,
                                  QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("cornerRadius"), QStringLiteral("圆角"),
                                  0.0, 1000000000.0, 1.0, 2,
                                  QStringLiteral(" px")));
        specs.append(numericTrack(QStringLiteral("strokeWidth"),
                                  QStringLiteral("描边宽度"), 0.0, 1000000000.0,
                                  0.5, 2, QStringLiteral(" px")));
        break;
    case ContentKind::Asset:
        if (entity.content.asset.type == AssetType::Audio)
        {
            specs.append(numericTrack(QStringLiteral("volume"), QStringLiteral("音量"),
                                      0.0, 2.0, 0.05, 3));
        }
        break;
    case ContentKind::Empty:
    case ContentKind::Camera:
        break;
    case ContentKind::ComicText:
    case ContentKind::RadialLines:
    case ContentKind::FlowLines:
    case ContentKind::HalftoneDots:
    case ContentKind::BrandWall:
        // Special content editing is P4b.
        break;
    }
    return specs;
}

bool isStepTrack(const QString& track)
{
    return track == QLatin1String("visible")
        || track == QLatin1String("physicsInteract");
}

std::optional<double> staticTrackValue(const model::Entity& entity,
                                       const QString& track)
{
    const model::Transform& transform = entity.transform;
    if (track == QLatin1String("x"))
        return transform.x;
    if (track == QLatin1String("y"))
        return transform.y;
    if (track == QLatin1String("rotation"))
        return transform.rotation;
    if (track == QLatin1String("scaleX"))
        return transform.scaleX;
    if (track == QLatin1String("scaleY"))
        return transform.scaleY;
    if (track == QLatin1String("pivotX"))
        return transform.pivotX;
    if (track == QLatin1String("pivotY"))
        return transform.pivotY;
    if (track == QLatin1String("opacity"))
        return transform.opacity;
    if (track == QLatin1String("visible"))
        return entity.visible ? 1.0 : 0.0;
    if (track == QLatin1String("physicsInteract"))
        return entity.physicsInteract ? 1.0 : 0.0;

    const model::Content& content = entity.content;
    if (content.kind == ContentKind::Text)
    {
        const model::TextContent& text = content.text;
        if (track == QLatin1String("fontSize"))
            return text.pixelSize;
        if (track == QLatin1String("lineHeight"))
            return text.lineHeight;
        if (track == QLatin1String("letterSpacing"))
            return text.letterSpacing;
        if (track == QLatin1String("strokeWidth"))
            return text.strokeWidth;
        if (track == QLatin1String("padding"))
            return text.padding;
        if (track == QLatin1String("boxWidth"))
            return text.boxWidth;
        if (track == QLatin1String("boxHeight"))
            return text.boxHeight;
    }
    else if (content.kind == ContentKind::Vector)
    {
        const model::VectorContent& vector = content.vector;
        if (track == QLatin1String("width"))
            return vector.width;
        if (track == QLatin1String("height"))
            return vector.height;
        if (track == QLatin1String("cornerRadius"))
            return vector.cornerRadius;
        if (track == QLatin1String("strokeWidth"))
            return vector.strokeWidth;
    }
    else if (content.kind == ContentKind::Asset
             && content.asset.type == AssetType::Audio
             && track == QLatin1String("volume"))
    {
        return content.asset.volume;
    }
    return std::nullopt;
}

bool setStaticTrackValue(model::Entity& entity, const QString& track, double value)
{
    model::Transform& transform = entity.transform;
    if (track == QLatin1String("x"))
    {
        transform.x = value;
        return true;
    }
    if (track == QLatin1String("y"))
    {
        transform.y = value;
        return true;
    }
    if (track == QLatin1String("rotation"))
    {
        transform.rotation = value;
        return true;
    }
    if (track == QLatin1String("scaleX"))
    {
        transform.scaleX = value;
        return true;
    }
    if (track == QLatin1String("scaleY"))
    {
        transform.scaleY = value;
        return true;
    }
    if (track == QLatin1String("pivotX"))
    {
        transform.pivotX = value;
        return true;
    }
    if (track == QLatin1String("pivotY"))
    {
        transform.pivotY = value;
        return true;
    }
    if (track == QLatin1String("opacity"))
    {
        transform.opacity = value;
        return true;
    }
    if (track == QLatin1String("visible"))
    {
        entity.visible = value != 0.0;
        return true;
    }
    if (track == QLatin1String("physicsInteract"))
    {
        entity.physicsInteract = value != 0.0;
        return true;
    }

    model::Content& content = entity.content;
    if (content.kind == ContentKind::Text)
    {
        model::TextContent& text = content.text;
        if (track == QLatin1String("fontSize"))
        {
            text.pixelSize = qRound(value);
            return true;
        }
        if (track == QLatin1String("lineHeight"))
        {
            text.lineHeight = value;
            return true;
        }
        if (track == QLatin1String("letterSpacing"))
        {
            text.letterSpacing = value;
            return true;
        }
        if (track == QLatin1String("strokeWidth"))
        {
            text.strokeWidth = value;
            return true;
        }
        if (track == QLatin1String("padding"))
        {
            text.padding = value;
            return true;
        }
        if (track == QLatin1String("boxWidth"))
        {
            text.boxWidth = value;
            return true;
        }
        if (track == QLatin1String("boxHeight"))
        {
            text.boxHeight = value;
            return true;
        }
    }
    else if (content.kind == ContentKind::Vector)
    {
        model::VectorContent& vector = content.vector;
        if (track == QLatin1String("width"))
        {
            vector.width = value;
            return true;
        }
        if (track == QLatin1String("height"))
        {
            vector.height = value;
            return true;
        }
        if (track == QLatin1String("cornerRadius"))
        {
            vector.cornerRadius = value;
            return true;
        }
        if (track == QLatin1String("strokeWidth"))
        {
            vector.strokeWidth = value;
            return true;
        }
    }
    else if (content.kind == ContentKind::Asset
             && content.asset.type == AssetType::Audio
             && track == QLatin1String("volume"))
    {
        content.asset.volume = value;
        return true;
    }
    return false;
}

bool hasKeyAt(const model::Entity& entity, const QString& trackName, int frame)
{
    const auto track = entity.tracks.constFind(trackName);
    if (track == entity.tracks.constEnd())
        return false;
    for (const model::Keyframe& keyframe : track.value().keys)
        if (keyframe.frame == frame)
            return true;
    return false;
}

bool hasAnyKeyAt(const model::Entity& entity, int frame)
{
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
        for (const model::Keyframe& keyframe : it.value().keys)
            if (keyframe.frame == frame)
                return true;
    return false;
}

// The legacy easing button read the first key it found at the frame and
// applied the choice to every track; keep the same "merged keyframe group"
// rule for the group combo.
bool frameEasing(const model::Entity& entity, int frame,
                 model::EasingSpec* easing, bool* mixed)
{
    bool found = false;
    bool differs = false;
    model::EasingSpec first;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
    {
        for (const model::Keyframe& keyframe : it.value().keys)
        {
            if (keyframe.frame != frame)
                continue;
            if (!found)
            {
                first = keyframe.easing;
                found = true;
            }
            else if (keyframe.easing != first)
            {
                differs = true;
            }
        }
    }
    if (easing)
        *easing = first;
    if (mixed)
        *mixed = differs;
    return found;
}

int previousGroupKeyframeFrame(const model::Entity& entity, int frame)
{
    int previous = -1;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
        for (const model::Keyframe& keyframe : it.value().keys)
            if (keyframe.frame < frame && keyframe.frame > previous)
                previous = keyframe.frame;
    return previous;
}

void setKeyframeValue(model::Entity& entity, const QString& track, int frame,
                      double value)
{
    model::Track& target = entity.tracks[track];
    for (model::Keyframe& keyframe : target.keys)
    {
        if (keyframe.frame == frame)
        {
            keyframe.value = value;
            return;
        }
    }
    model::Keyframe keyframe;
    keyframe.frame = frame;
    keyframe.value = value;
    target.keys.append(keyframe);
}

void removeKeyframeAt(model::Entity& entity, const QString& track, int frame)
{
    auto it = entity.tracks.find(track);
    if (it == entity.tracks.end())
        return;
    QVector<model::Keyframe>& keys = it.value().keys;
    keys.erase(std::remove_if(keys.begin(), keys.end(),
                              [frame](const model::Keyframe& keyframe) {
                                  return keyframe.frame == frame;
                              }),
               keys.end());
    if (keys.isEmpty())
        entity.tracks.erase(it);
}

double currentTrackValue(const model::Entity& entity, const QString& track,
                         int relativeFrame, bool step,
                         std::optional<double> fallback)
{
    if (relativeFrame >= 0 && relativeFrame < entity.duration())
    {
        const auto it = entity.tracks.constFind(track);
        if (it != entity.tracks.constEnd())
        {
            if (const auto value =
                    model::evaluateTrack(it.value(), relativeFrame, step))
                return *value;
        }
    }
    if (fallback)
        return *fallback;
    return staticTrackValue(entity, track).value_or(0.0);
}

QStringList solverDrivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

bool trackBlockedAt(const model::Entity& entity, const QString& track, int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (!solverDrivenTracks(solver.effect).contains(track))
            continue;
        // Automatic segments end before the first new key, so a new key is
        // the end marker and must stay writable.  Only an explicit advanced
        // end reserves its frames.
        if (solver.endFrame < 0)
            continue;
        if (frame > solver.startFrame && frame <= solver.endFrame)
            return true;
    }
    return false;
}

bool isDescendant(const model::Project& project, const Uuid& candidate,
                  const Uuid& ancestor)
{
    if (candidate == ancestor)
        return true;
    Uuid parent = project.entities.value(candidate).parentUuid;
    QSet<Uuid> seen;
    while (!parent.isEmpty() && !seen.contains(parent))
    {
        if (parent == ancestor)
            return true;
        seen.insert(parent);
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        parent = it.value().parentUuid;
    }
    return false;
}

void selectComboData(QComboBox* combo, int value)
{
    int index = combo->findData(value);
    if (index < 0)
    {
        combo->addItem(QStringLiteral("自定义 (%1)").arg(value), value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

void setSpinIfIdle(QDoubleSpinBox* spin, double value)
{
    if (!spin || spin->hasFocus())
        return;
    const QSignalBlocker blocker(spin);
    spin->setValue(value);
}

void setSpinIfIdle(QSpinBox* spin, int value)
{
    if (!spin || spin->hasFocus())
        return;
    const QSignalBlocker blocker(spin);
    spin->setValue(value);
}

} // namespace pvf::ui::detail
