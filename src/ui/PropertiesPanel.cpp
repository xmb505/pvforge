#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Content.h"
#include "model/Easing.h"
#include "model/Entity.h"
#include "model/Query.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"
#include "next/properties/SolverPreviewWidget.h"
#include "next/properties/SpecialContentEditor.h"
#include "next/properties/TextLayoutDialog.h"
#include "ui/BezierEditorDialog.h"
#include "ui/EasingChooser.h"
#include "ui/BrandWallDialog.h"
#include "ui/TextLayoutDialog.h"
#include "ui/Viewport.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace pvf {

namespace {

QString typeName(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QStringLiteral("图片");
    case EntityType::Video:
        return QStringLiteral("视频");
    case EntityType::Text:
        return QStringLiteral("文字");
    case EntityType::Vector:
        return QStringLiteral("矢量");
    case EntityType::Camera:
        return QStringLiteral("摄像机");
    case EntityType::Audio:
        return QStringLiteral("音频");
    case EntityType::Folder:
        return QStringLiteral("文件夹");
    case EntityType::ComicText:
        return QStringLiteral("漫画文本框");
    case EntityType::RadialLines:
        return QStringLiteral("放射状线条");
    case EntityType::FlowLines:
        return QStringLiteral("快速流过");
    case EntityType::BrandWall:
        return QStringLiteral("品牌瓷砖墙");
    case EntityType::HalftoneDots:
        return QStringLiteral("漫画点阵");
    case EntityType::HtmlLayer:
        return QStringLiteral("HTML 层");
    }
    return QString();
}

QString htmlFileStamp(const QString& path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1|%2")
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

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

QString jsonValueText(const QJsonValue& value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 8);
    if (value.isString())
        return value.toString();
    if (value.isNull() || value.isUndefined())
        return QStringLiteral("null");
    return QString::fromUtf8(
        QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

// Manifest variables may be plain defaults ({"speed": 2.5}) or descriptor
// objects ({"speed": {"type": "number", "default": 2.5, "min": 0, "max": 10}}).
// The asset store keeps the manifest's variables object verbatim.
struct HtmlVarSpec
{
    QString name;
    QString track;
    bool numeric = false;
    double defaultValue = 0.0;
    double minimum = -1000000000.0;
    double maximum = 1000000000.0;
    double step = 0.01;
    int decimals = 3;
    QString text;
};

HtmlVarSpec htmlVarSpec(const QString& name, const QJsonValue& value)
{
    HtmlVarSpec spec;
    spec.name = name;
    spec.track = QStringLiteral("var.") + name;

    QJsonValue defaultValue = value;
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("default")))
            defaultValue = object.value(QStringLiteral("default"));
        const double minimum =
            object.value(QStringLiteral("min")).toDouble(spec.minimum);
        const double maximum =
            object.value(QStringLiteral("max")).toDouble(spec.maximum);
        if (maximum > minimum)
        {
            spec.minimum = minimum;
            spec.maximum = maximum;
        }
        const bool integer =
            object.value(QStringLiteral("integer")).toBool(false);
        spec.decimals = integer ? 0 : 3;
        spec.step = integer ? 1.0 : 0.01;
        if (object.value(QStringLiteral("type")).toString()
            == QLatin1String("number"))
            spec.numeric = true;
    }

    if (defaultValue.isDouble())
        spec.numeric = true;
    if (spec.numeric)
        spec.defaultValue = defaultValue.toDouble();
    else
        spec.text = jsonValueText(defaultValue);
    return spec;
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

TextStyle textStyleFromContent(const model::TextContent& content)
{
    TextStyle style;
    style.text = content.text;
    style.fontFamily = content.fontFamily;
    style.pixelSize = content.pixelSize;
    style.color = QColor(content.color);
    if (!style.color.isValid())
        style.color = Qt::white;
    style.strokeColor = QColor(content.strokeColor);
    if (!style.strokeColor.isValid())
        style.strokeColor = Qt::black;
    style.strokeWidth = content.strokeWidth;
    style.align = content.align;
    style.lineHeight = content.lineHeight;
    style.letterSpacing = content.letterSpacing;
    style.padding = content.padding;
    style.boxWidth = content.boxWidth;
    style.boxHeight = content.boxHeight;
    style.bold = content.bold;
    style.italic = content.italic;
    style.underline = content.underline;
    style.vertical = content.vertical;
    style.verticalRtl = content.verticalRtl;
    return style;
}

model::TextContent textContentFromStyle(const model::TextContent& original,
                                        const TextStyle& style)
{
    model::TextContent out = original;
    out.text = style.text;
    out.fontFamily = style.fontFamily;
    out.pixelSize = style.pixelSize;
    out.color = colorString(style.color);
    out.strokeColor = colorString(style.strokeColor);
    out.strokeWidth = style.strokeWidth;
    out.align = style.align;
    out.lineHeight = style.lineHeight;
    out.letterSpacing = style.letterSpacing;
    out.padding = style.padding;
    out.boxWidth = style.boxWidth;
    out.boxHeight = style.boxHeight;
    out.bold = style.bold;
    out.italic = style.italic;
    out.underline = style.underline;
    out.vertical = style.vertical;
    out.verticalRtl = style.verticalRtl;
    return out;
}

struct TrackSpec
{
    QString name;
    QString label;
    bool step = false;
    double minimum = -1000000000.0;
    double maximum = 1000000000.0;
    double singleStep = 1.0;
    int decimals = 2;
    QString suffix;
};

TrackSpec numericTrack(const QString& name, const QString& label, double minimum,
                       double maximum, double singleStep, int decimals,
                       const QString& suffix = {})
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
                         std::optional<double> fallback = std::nullopt)
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

bool solverNeedsTarget(SolverEffect effect)
{
    return effect != SolverEffect::Spin;
}

QStringList solverDrivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

QString solverRangeText(const model::SolverSegment& solver)
{
    if (solver.endFrame < 0)
        return QStringLiteral("%1 帧起 · 自动结束").arg(solver.startFrame);
    return QStringLiteral("%1 – %2 帧").arg(solver.startFrame).arg(solver.endFrame);
}

QString solverStopReasonText(solver::StopReason reason)
{
    switch (reason)
    {
    case solver::StopReason::Collision:
        return QStringLiteral("碰撞/捕获停止");
    case solver::StopReason::StopThreshold:
        return QStringLiteral("速度低于停止阈值");
    case solver::StopReason::ClipEnd:
        return QStringLiteral("片段末尾");
    case solver::StopReason::ExplicitEnd:
        return QStringLiteral("显式覆盖");
    case solver::StopReason::None:
        break;
    }
    return QStringLiteral("无法解算");
}

QString solverStopInfoText(const model::Entity& entity,
                           const model::SolverSegment& solver,
                           const SolverPreviewEval& preview)
{
    if (!preview.error.isEmpty())
        return QStringLiteral("结束：无法解算（%1）").arg(preview.error);
    if (!preview.isValid() || preview.result.stopFrame < 0)
        return QStringLiteral("结束：无法解算");

    if (solver.endFrame >= 0)
        return QStringLiteral("结束：第 %1 帧（%2）")
            .arg(preview.result.stopFrame)
            .arg(solverStopReasonText(preview.result.reason));

    const int structural = model::solverStructuralEnd(entity, solver);
    int end = std::min(preview.result.stopFrame, structural);
    QString reason = solverStopReasonText(preview.result.reason);
    if (structural < preview.result.stopFrame)
    {
        end = structural;
        if (structural >= entity.duration() - 1)
            reason = QStringLiteral("片段末尾");
        else
        {
            bool nextSegment = false;
            for (const model::SolverSegment& other : entity.solvers)
            {
                if (other.id != solver.id
                    && other.startFrame == structural + 1)
                {
                    nextSegment = true;
                    break;
                }
            }
            reason = nextSegment ? QStringLiteral("下一段开始")
                                 : QStringLiteral("后续关键帧");
        }
    }
    return QStringLiteral("结束：自动（第 %1 帧 %2）").arg(end).arg(reason);
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

enum class ParamKind
{
    Number,
    Uuid,
    Direction
};

struct ParamSpec
{
    QString key;
    QString label;
    ParamKind kind = ParamKind::Number;
    double defaultValue = 0.0;
    double minimum = -1000000000.0;
    double maximum = 1000000000.0;
    double step = 1.0;
    int decimals = 2;
    QString suffix;
};

ParamSpec numberParam(const QString& key, const QString& label, double defaultValue,
                      double minimum, double maximum, double step = 1.0,
                      int decimals = 2, const QString& suffix = {})
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
    spec.kind = ParamKind::Number;
    spec.defaultValue = defaultValue;
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.step = step;
    spec.decimals = decimals;
    spec.suffix = suffix;
    return spec;
}

ParamSpec uuidParam(const QString& key, const QString& label)
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
    spec.kind = ParamKind::Uuid;
    return spec;
}

ParamSpec directionParam()
{
    ParamSpec spec;
    spec.key = QStringLiteral("direction");
    spec.label = QStringLiteral("方向");
    spec.kind = ParamKind::Direction;
    spec.defaultValue = 1.0;
    return spec;
}

QVector<ParamSpec> solverParams(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("目标实体")),
            numberParam(QStringLiteral("strength"), QStringLiteral("重力强度"),
                        980.0, 0.0, 1000000.0, 10.0, 2,
                        QStringLiteral("px/s²")),
            numberParam(QStringLiteral("elasticity"), QStringLiteral("弹性"),
                        0.35, 0.0, 1.0, 0.05, 3),
            numberParam(QStringLiteral("friction"), QStringLiteral("摩擦"), 0.2, 0.0,
                        1.0, 0.05, 3),
            numberParam(QStringLiteral("stopThreshold"), QStringLiteral("停止阈值"),
                        2.0, 0.0, 1000.0, 0.5, 2, QStringLiteral("px/s")),
        };
    case SolverEffect::Orbit:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("中心实体")),
            numberParam(QStringLiteral("aspect"), QStringLiteral("扁率"), 1.0, 0.01,
                        1.0, 0.05, 3),
            numberParam(QStringLiteral("angle"), QStringLiteral("椭圆角度"), 0.0,
                        -360.0, 360.0, 1.0, 2, QStringLiteral("°")),
            directionParam(),
            numberParam(QStringLiteral("periodFrames"), QStringLiteral("周期"), 60.0,
                        1.0, 1000000.0, 1.0, 0, QStringLiteral("帧")),
        };
    case SolverEffect::Spin:
        return {
            numberParam(QStringLiteral("angularVelocity"), QStringLiteral("角速度"),
                        6.0, 0.0, 360.0, 0.5, 2, QStringLiteral("°/帧")),
            directionParam(),
        };
    case SolverEffect::BlackHole:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("黑洞实体")),
            numberParam(QStringLiteral("simulationSpeed"), QStringLiteral("模拟速度"),
                        1.0, 0.01, 20.0, 0.1, 2),
            numberParam(QStringLiteral("mass"), QStringLiteral("质量"), 1000.0,
                        0.0001, 1000000000.0, 10.0, 2),
            numberParam(QStringLiteral("angularMomentum"), QStringLiteral("角动量"),
                        0.0, -1000000.0, 1000000.0, 10.0, 2),
            numberParam(QStringLiteral("eventHorizonRadius"),
                        QStringLiteral("视界半径"), 80.0, 0.0, 1000000.0, 1.0, 1,
                        QStringLiteral("px")),
            numberParam(QStringLiteral("objectRadius"), QStringLiteral("物体半径"),
                        40.0, 0.0, 1000000.0, 1.0, 1, QStringLiteral("px")),
            numberParam(QStringLiteral("softening"), QStringLiteral("软化半径"), 60.0,
                        0.0001, 1000000.0, 1.0, 1, QStringLiteral("px")),
            numberParam(QStringLiteral("maxSpeed"), QStringLiteral("最大速度"), 4000.0,
                        0.0001, 1000000.0, 100.0, 1, QStringLiteral("px/s")),
        };
    }
    return {};
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

} // namespace

PropertiesPanel::PropertiesPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    buildUi();
    refresh();
}

PropertiesPanel::PropertiesPanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_sessionMode(true)
{
    buildUi();
    buildSessionUi();

    if (m_session && m_session->document())
    {
        auto* document = m_session->document();
        connect(document, &next::Document::selectionChanged, this, [this] {
            const Uuid selected = m_session->document()->selection();
            if (selected != m_sessionUuid)
            {
                m_sessionUuid = selected;
                refresh();
            }
        });
        connect(document, &next::Document::playheadChanged, this, [this] {
            m_frame = m_session->playhead();
            refresh();
        });
        connect(document, &next::Document::changed, this, [this] {
            if (!m_editing)
                refresh();
        });
    }
    refresh();
}

void PropertiesPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_rootLayout = layout;

    m_title = new QLabel(QStringLiteral("未选择对象"), this);
    m_title->setContentsMargins(10, 8, 10, 8);
    QFont titleFont = m_title->font();
    titleFont.setItalic(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    auto* transformContent = new QWidget(this);
    auto* transformForm = new QFormLayout(transformContent);
    transformForm->setContentsMargins(10, 8, 10, 8);
    transformForm->setVerticalSpacing(6);
    m_parentRow = new QWidget(transformContent);
    auto* parentLayout = new QHBoxLayout(m_parentRow);
    parentLayout->setContentsMargins(0, 0, 0, 0);
    m_parent = new QComboBox(m_parentRow);
    m_parent->setObjectName(QStringLiteral("parentCombo"));
    m_parent->setToolTip(
        QStringLiteral("选择实体的坐标系：工作台或另一个实体；子级的位置/旋转/缩放相对父级"));
    parentLayout->addWidget(m_parent);
    transformForm->addRow(QStringLiteral("父级"), m_parentRow);
    m_parentLabel = qobject_cast<QLabel*>(transformForm->labelForField(m_parentRow));
    connect(m_parent, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (m_sessionMode)
                {
                    if (m_sessionUuid.isEmpty() || m_parent->currentIndex() < 0)
                        return;
                    applySessionParent(m_parent->currentData().toString());
                    return;
                }
                if (m_entity == kInvalidEntity || m_parent->currentIndex() < 0)
                    return;
                emit parentEdited(
                    m_entity, EntityId(m_parent->currentData().toInt()));
            });

    addRow(transformForm, QStringLiteral("X"), QStringLiteral("x"), -10000, 10000, 1, 1);
    addRow(transformForm, QStringLiteral("Y"), QStringLiteral("y"), -10000, 10000, 1, 1);
    addRow(transformForm, QStringLiteral("旋转"), QStringLiteral("rotation"), -3600, 3600, 1, 1);
    addRow(transformForm, QStringLiteral("缩放 X"), QStringLiteral("scaleX"), -100, 100, 3, 0.01);
    addRow(transformForm, QStringLiteral("缩放 Y"), QStringLiteral("scaleY"), -100, 100, 3, 0.01);
    addRow(transformForm, QStringLiteral("透明度"), QStringLiteral("opacity"), 0, 100, 0, 1,
           0.01);
    m_transformSection =
        new CollapsibleSection(QStringLiteral("变换"), transformContent, this);
    m_transformSection->setExpanded(true);
    layout->addWidget(m_transformSection);

    auto* keyContent = new QWidget(this);
    auto* keyLayout = new QVBoxLayout(keyContent);
    keyLayout->setContentsMargins(10, 8, 10, 8);
    m_keyLayout = keyLayout;
    m_keyButton = new QPushButton(QStringLiteral("在当前帧打关键帧"), keyContent);
    m_keyButton->setObjectName(QStringLiteral("keyframeButton"));
    m_keyButton->setEnabled(false);
    keyLayout->addWidget(m_keyButton);

    m_easeButton = new QPushButton(QStringLiteral("缓动：线性"), keyContent);
    m_easeButton->setObjectName(QStringLiteral("keyframeEasingButton"));
    m_easeButton->setEnabled(false);
    m_easeButton->setToolTip(
        QStringLiteral("修改当前帧关键帧的进入段曲线"));
    keyLayout->addWidget(m_easeButton);

    m_keySection = new CollapsibleSection(QStringLiteral("关键帧"), keyContent, this);
    m_keySection->setObjectName(QStringLiteral("keySection"));
    m_keySection->setExpanded(true);
    layout->addWidget(m_keySection);

    buildTextSection(layout);
    buildComicSection(layout);
    buildLinesSection(layout);
    buildBrandWallSection(layout);
    buildHalftoneSection(layout);
    buildHtmlSection(layout);
    buildVectorSection(layout);
    buildMediaSection(layout);
    buildAudioSection(layout);
    buildPivotSection(layout);
    layout->addStretch(1);

    connect(m_keyButton, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
        {
            sessionAddTransformKeys();
            return;
        }
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        const bool camera = entity && entity->isCamera();
        for (const Row& r : m_rows)
        {
            if (camera && r.prop == QStringLiteral("opacity"))
                continue;
            m_project->setValue(m_entity, r.prop, m_frame,
                                m_project->value(m_entity, r.prop, m_frame), true);
        }
        refresh();
    });

    connect(m_easeButton, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
            return;
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        if (!entity || entity->locked)
            return;

        EasingSpec current;
        bool found = false;
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - m_frame) < 1e-6)
                {
                    current = key.ease;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
            return;

        const double previous = m_project->previousKeyframeFrame(m_entity, m_frame);
        if (previous < 0.0)
            return;
        const int fps = std::max(1, m_project->fps);
        const QString title = QStringLiteral("进入本帧：%1s → %2s")
                                  .arg(previous / double(fps), 0, 'f', 2)
                                  .arg(m_frame / double(fps), 0, 'f', 2);

        EasingSpec chosen;
        const QPoint anchor =
            m_easeButton->mapToGlobal(QPoint(0, m_easeButton->height()));
        if (EasingChooser::choose(this, current, anchor, &chosen, title))
            emit keyframeEasingEdited(m_entity, m_frame, chosen);
    });
}

void PropertiesPanel::buildSessionUi()
{
    // Conversion lives in the keyframe section: a normal key that is the last
    // key in the clip can become the solver start.  The effect is chosen from
    // the button's menu; the target stays in the solver section.
    m_convertSolverButton =
        new QPushButton(QStringLiteral("转换为求解关键帧…"), this);
    m_convertSolverButton->setObjectName(QStringLiteral("solverConvertButton"));
    m_convertSolverButton->setEnabled(false);
    m_keyLayout->addWidget(m_convertSolverButton);
    m_convertSolverHint = new QLabel(this);
    m_convertSolverHint->setObjectName(QStringLiteral("solverConvertHint"));
    m_convertSolverHint->setWordWrap(true);
    m_convertSolverHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_keyLayout->addWidget(m_convertSolverHint);
    connect(m_convertSolverButton, &QPushButton::clicked, this,
            &PropertiesPanel::showSolverConvertMenu);

    buildBasicSection(m_rootLayout);

    // Text layout fields that only the new model exposes.
    m_textLineHeight = new QDoubleSpinBox(this);
    m_textLineHeight->setObjectName(QStringLiteral("textLineHeight"));
    m_textLineHeight->setRange(0.1, 10.0);
    m_textLineHeight->setDecimals(3);
    m_textLineHeight->setSingleStep(0.05);
    connect(m_textLineHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("lineHeight"), value); });
    m_textForm->addRow(QStringLiteral("行高"), m_textLineHeight);

    m_textLetterSpacing = new QDoubleSpinBox(this);
    m_textLetterSpacing->setObjectName(QStringLiteral("textLetterSpacing"));
    m_textLetterSpacing->setRange(-1000.0, 1000.0);
    m_textLetterSpacing->setDecimals(2);
    m_textLetterSpacing->setSingleStep(1.0);
    m_textLetterSpacing->setSuffix(QStringLiteral(" px"));
    connect(m_textLetterSpacing, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applyData(QStringLiteral("letterSpacing"), value);
            });
    m_textForm->addRow(QStringLiteral("字距"), m_textLetterSpacing);

    m_textPadding = new QDoubleSpinBox(this);
    m_textPadding->setObjectName(QStringLiteral("textPadding"));
    m_textPadding->setRange(0.0, 100000.0);
    m_textPadding->setDecimals(2);
    m_textPadding->setSingleStep(1.0);
    m_textPadding->setSuffix(QStringLiteral(" px"));
    connect(m_textPadding, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("padding"), value); });
    m_textForm->addRow(QStringLiteral("内边距"), m_textPadding);

    m_textBoxWidth = new QDoubleSpinBox(this);
    m_textBoxWidth->setObjectName(QStringLiteral("textBoxWidth"));
    m_textBoxWidth->setRange(0.0, 100000.0);
    m_textBoxWidth->setDecimals(2);
    m_textBoxWidth->setSingleStep(1.0);
    m_textBoxWidth->setSuffix(QStringLiteral(" px"));
    connect(m_textBoxWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("boxWidth"), value); });
    m_textForm->addRow(QStringLiteral("框宽"), m_textBoxWidth);

    m_textBoxHeight = new QDoubleSpinBox(this);
    m_textBoxHeight->setObjectName(QStringLiteral("textBoxHeight"));
    m_textBoxHeight->setRange(0.0, 100000.0);
    m_textBoxHeight->setDecimals(2);
    m_textBoxHeight->setSingleStep(1.0);
    m_textBoxHeight->setSuffix(QStringLiteral(" px"));
    connect(m_textBoxHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("boxHeight"), value); });
    m_textForm->addRow(QStringLiteral("框高"), m_textBoxHeight);

    // Asset fields. The hash and type stay read-only in P4a; sourceIn and
    // volume are the editable asset values.
    auto* assetForm = new QFormLayout();
    assetForm->setContentsMargins(0, 0, 0, 0);
    assetForm->setVerticalSpacing(6);
    m_assetHash = new QLineEdit(this);
    m_assetHash->setObjectName(QStringLiteral("assetHash"));
    m_assetHash->setReadOnly(true);
    assetForm->addRow(QStringLiteral("素材 Hash"), m_assetHash);
    m_assetType = new QLabel(this);
    m_assetType->setObjectName(QStringLiteral("assetType"));
    assetForm->addRow(QStringLiteral("类型"), m_assetType);
    m_assetSourceIn = new QSpinBox(this);
    m_assetSourceIn->setObjectName(QStringLiteral("assetSourceIn"));
    m_assetSourceIn->setRange(0, 1000000);
    m_assetSourceIn->setSuffix(QStringLiteral(" 帧"));
    m_assetSourceIn->setKeyboardTracking(false);
    m_assetSourceIn->setToolTip(QStringLiteral("从素材的第几帧开始播放；范围受素材长度限制"));
    connect(m_assetSourceIn, &QSpinBox::valueChanged, this, [this](int value) {
        applySessionAsset(QStringLiteral("sourceIn"), value);
    });
    assetForm->addRow(QStringLiteral("素材起点"), m_assetSourceIn);
    m_assetVolume = new QDoubleSpinBox(this);
    m_assetVolume->setObjectName(QStringLiteral("assetVolume"));
    m_assetVolume->setRange(0.0, 2.0);
    m_assetVolume->setDecimals(3);
    m_assetVolume->setSingleStep(0.05);
    m_assetVolume->setSuffix(QStringLiteral(" ×"));
    connect(m_assetVolume, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applySessionAsset(QStringLiteral("volume"), value);
    });
    assetForm->addRow(QStringLiteral("音量"), m_assetVolume);
    m_mediaLayout->addLayout(assetForm);

    buildSessionHtmlSection(m_rootLayout);
    buildSpecialSection(m_rootLayout);
    buildCameraSection(m_rootLayout);
    buildSolverSection(m_rootLayout);

    // One easing control for the merged keyframe group at the current frame.
    // The legacy UI applied the choice to every track, not one track at a time.
    auto* easingRow = new QWidget(this);
    auto* easingLayout = new QHBoxLayout(easingRow);
    easingLayout->setContentsMargins(0, 0, 0, 0);
    easingLayout->setSpacing(6);
    auto* easingLabel = new QLabel(QStringLiteral("缓动"), easingRow);
    easingLayout->addWidget(easingLabel);
    m_groupEasing = new QComboBox(easingRow);
    m_groupEasing->setObjectName(QStringLiteral("keyframeEasingGroup"));
    m_groupEasing->setToolTip(
        QStringLiteral("修改当前帧所有轨道关键帧的进入段曲线；"
                       "自定义贝塞尔仍可选"));
    m_groupEasing->addItem(QStringLiteral("linear"), QStringLiteral("linear"));
    m_groupEasing->addItem(QStringLiteral("instant"), QStringLiteral("instant"));
    for (const QString& preset : model::easingPresetNames())
        m_groupEasing->addItem(preset, preset);
    m_groupEasing->addItem(QStringLiteral("bezier"), QStringLiteral("bezier"));
    easingLayout->addWidget(m_groupEasing, 1);
    m_keyLayout->addWidget(easingRow);
    connect(m_groupEasing, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_editing || !m_groupEasing)
            return;
        const QString type = m_groupEasing->currentData().toString();
        if (type.isEmpty())
            return;
        if (type == QLatin1String("bezier"))
        {
            sessionPromptGroupBezier();
            return;
        }
        model::EasingSpec spec;
        spec.type = model::canonicalEasingName(type);
        sessionSetGroupEasing(spec);
    });

    m_autoKeyCheck = new QCheckBox(QStringLiteral("自动关键帧"), this);
    m_autoKeyCheck->setObjectName(QStringLiteral("autoKeyCheck"));
    m_autoKeyCheck->setChecked(m_autoKey);
    m_autoKeyCheck->setToolTip(
        QStringLiteral("修改数值时自动在播放头处插入或更新关键帧"));
    connect(m_autoKeyCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_autoKey = on;
        emit autoKeyToggled(on);
    });
    m_keyLayout->addWidget(m_autoKeyCheck);

    m_trackHost = new QWidget(this);
    m_trackHost->setObjectName(QStringLiteral("trackHost"));
    m_trackForm = new QFormLayout(m_trackHost);
    m_trackForm->setContentsMargins(0, 2, 0, 0);
    m_trackForm->setVerticalSpacing(2);
    m_trackForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_trackForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_keyLayout->addWidget(m_trackHost);

    // The legacy button is kept for the legacy Project shell; session mode
    // uses the group combo above.
    m_easeButton->hide();

    // #50: keep the content pages on the first screen of an 800px window.
    // The entity and keyframe sections are the two tall blocks in front of
    // them; both stay one click away.  Compact controls match the denser
    // legacy property rows and keep their height stable across pages.
    m_basicSection->setExpanded(false);
    m_keySection->setExpanded(false);
    for (QWidget* widget : findChildren<QWidget*>())
        if (qobject_cast<QDoubleSpinBox*>(widget) || qobject_cast<QSpinBox*>(widget)
            || qobject_cast<QComboBox*>(widget)
            || qobject_cast<QLineEdit*>(widget))
            widget->setFixedHeight(24);
    for (CollapsibleSection* section :
         {m_transformSection, m_textSection, m_vectorSection})
        if (auto* form = section ? section->findChild<QFormLayout*>() : nullptr)
            form->setVerticalSpacing(2);
}

void PropertiesPanel::buildBasicSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_basicName = new QLineEdit(content);
    m_basicName->setObjectName(QStringLiteral("entityName"));
    connect(m_basicName, &QLineEdit::editingFinished, this, [this] {
        if (!m_session || m_editing || m_sessionUuid.isEmpty())
            return;
        const model::Entity* entity = sessionEntity();
        if (!entity || m_basicName->text() == entity->name)
            return;
        const QString name = m_basicName->text().trimmed();
        runSessionEdit(QStringLiteral("pvf: rename entity"),
                       [name](model::Entity& e) {
                           e.name = name;
                           return true;
                       });
    });
    form->addRow(QStringLiteral("名称"), m_basicName);

    m_basicStart = new QSpinBox(content);
    m_basicStart->setObjectName(QStringLiteral("entityStart"));
    m_basicStart->setRange(0, 1000000);
    m_basicStart->setSuffix(QStringLiteral(" 帧"));
    m_basicStart->setKeyboardTracking(false);
    form->addRow(QStringLiteral("开始"), m_basicStart);

    m_basicEnd = new QSpinBox(content);
    m_basicEnd->setObjectName(QStringLiteral("entityEnd"));
    m_basicEnd->setRange(1, 1000000);
    m_basicEnd->setSuffix(QStringLiteral(" 帧"));
    m_basicEnd->setKeyboardTracking(false);
    form->addRow(QStringLiteral("结束"), m_basicEnd);

    auto applyRange = [this] {
        if (!m_session || m_editing || m_sessionUuid.isEmpty())
            return;
        const model::Entity* entity = sessionEntity();
        if (!entity)
            return;
        const int start = m_basicStart->value();
        const int end = m_basicEnd->value();
        if (start == entity->start && end == entity->end)
            return;
        m_editing = true;
        QString error;
        const bool ok = m_session->editActions()->trimClip(
            m_sessionUuid, start, end, &error);
        m_editing = false;
        if (!ok)
            reportSessionError(error);
        refresh();
    };
    connect(m_basicStart, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [applyRange](int) { applyRange(); });
    connect(m_basicEnd, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [applyRange](int) { applyRange(); });

    m_basicRow = new QSpinBox(content);
    m_basicRow->setObjectName(QStringLiteral("entityRow"));
    m_basicRow->setRange(0, 1000000);
    m_basicRow->setKeyboardTracking(false);
    connect(m_basicRow, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                if (!m_session || m_editing)
                    return;
                const model::Entity* entity = sessionEntity();
                if (!entity || entity->row == value)
                    return;
                runSessionEdit(QStringLiteral("pvf: edit entity row"),
                               [value](model::Entity& e) {
                                   e.row = value;
                                   return true;
                               });
            });
    form->addRow(QStringLiteral("行"), m_basicRow);

    auto addFlag = [this, form, content](const QString& label, const QString& name,
                                         bool model::Entity::*member,
                                         bool ignoreLock = false) {
        auto* box = new QCheckBox(label, content);
        box->setObjectName(name);
        connect(box, &QCheckBox::toggled, this, [this, member, ignoreLock](bool on) {
            if (!m_sessionMode || m_editing)
                return;
            runSessionEdit(QStringLiteral("pvf: edit entity flags"),
                           [member, on](model::Entity& e) {
                               e.*member = on;
                               return true;
                           },
                           ignoreLock);
        });
        form->addRow(box);
        return box;
    };
    m_basicHidden = addFlag(QStringLiteral("隐藏"), QStringLiteral("entityHidden"),
                            &model::Entity::hidden);
    m_basicLocked = addFlag(QStringLiteral("锁定"), QStringLiteral("entityLocked"),
                            &model::Entity::locked, true);
    m_basicVisible = addFlag(QStringLiteral("可见"), QStringLiteral("entityVisible"),
                             &model::Entity::visible);
    m_basicInherit =
        addFlag(QStringLiteral("继承父级可见性"),
                QStringLiteral("entityInheritVisibility"),
                &model::Entity::inheritVisibility);
    m_basicPhysics =
        addFlag(QStringLiteral("参与物理干涉"),
                QStringLiteral("entityPhysicsInteract"),
                &model::Entity::physicsInteract);

    m_basicSection = new CollapsibleSection(QStringLiteral("实体"), content, this);
    m_basicSection->setExpanded(true);
    layout->insertWidget(1, m_basicSection);
}

void PropertiesPanel::buildCameraSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    m_cameraInfo = new QLabel(content);
    m_cameraInfo->setObjectName(QStringLiteral("cameraInfo"));
    m_cameraInfo->setWordWrap(true);
    m_cameraInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_cameraInfo->setText(
        QStringLiteral("摄像机内容为只读：位置、旋转和取景比例请在变换区编辑。"
                       "取景比例按绝对值限制在 0.05–1.0（5%–100%）。"));
    box->addWidget(m_cameraInfo);
    m_cameraSection = new CollapsibleSection(QStringLiteral("摄像机"), content, this);
    m_cameraSection->setExpanded(true);
    layout->insertWidget(3, m_cameraSection);
}

void PropertiesPanel::buildSolverSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);

    auto* createBox = new QGroupBox(QStringLiteral("转换为求解段（快捷）"), content);
    createBox->setObjectName(QStringLiteral("solverCreateBox"));
    auto* createForm = new QFormLayout(createBox);
    createForm->setContentsMargins(8, 6, 8, 6);
    createForm->setVerticalSpacing(4);
    createForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_solverEffectCombo = new QComboBox(createBox);
    m_solverEffectCombo->setObjectName(QStringLiteral("solverEffectCombo"));
    m_solverEffectCombo->addItem(QStringLiteral("重力"), int(SolverEffect::Gravity));
    m_solverEffectCombo->addItem(QStringLiteral("公转"), int(SolverEffect::Orbit));
    m_solverEffectCombo->addItem(QStringLiteral("自转"), int(SolverEffect::Spin));
    m_solverEffectCombo->addItem(QStringLiteral("黑洞"), int(SolverEffect::BlackHole));
    createForm->addRow(QStringLiteral("效果"), m_solverEffectCombo);

    m_solverTargetCombo = new QComboBox(createBox);
    m_solverTargetCombo->setObjectName(QStringLiteral("solverTargetCombo"));
    createForm->addRow(QStringLiteral("目标实体"), m_solverTargetCombo);

    m_solverEndAuto = new QLabel(
        QStringLiteral("结束：自动（后续第一个关键帧 / 物理停止 / 片段末尾）"),
        createBox);
    m_solverEndAuto->setObjectName(QStringLiteral("solverCreateEndAuto"));
    m_solverEndAuto->setWordWrap(true);
    createForm->addRow(m_solverEndAuto);

    m_solverManualEnd =
        new QCheckBox(QStringLiteral("手动指定结束帧"), createBox);
    m_solverManualEnd->setObjectName(QStringLiteral("solverCreateManualEnd"));
    m_solverManualEnd->setToolTip(
        QStringLiteral("高级覆盖：只解到指定帧，之后冻结。默认由物理停止决定。"));
    createForm->addRow(m_solverManualEnd);

    m_solverEndRow = new QWidget(createBox);
    auto* endLayout = new QHBoxLayout(m_solverEndRow);
    endLayout->setContentsMargins(0, 0, 0, 0);
    endLayout->addWidget(new QLabel(QStringLiteral("结束帧"), m_solverEndRow));
    m_solverEndSpin = new QSpinBox(m_solverEndRow);
    m_solverEndSpin->setObjectName(QStringLiteral("solverCreateEnd"));
    m_solverEndSpin->setRange(-1, 1000000);
    m_solverEndSpin->setSpecialValueText(QStringLiteral("自动"));
    m_solverEndSpin->setValue(-1);
    m_solverEndSpin->setKeyboardTracking(false);
    endLayout->addWidget(m_solverEndSpin, 1);
    m_solverEndRow->hide();
    createForm->addRow(m_solverEndRow);
    connect(m_solverManualEnd, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_solverEndRow)
            return;
        m_solverEndRow->setVisible(on);
        if (on && m_solverEndSpin->value() < 0)
        {
            const model::Entity* entity = sessionEntity();
            if (entity)
                m_solverEndSpin->setValue(std::max(0, entity->duration() - 1));
        }
    });

    m_solverCreateButton =
        new QPushButton(QStringLiteral("将当前关键帧转换为求解开始帧"), createBox);
    m_solverCreateButton->setObjectName(QStringLiteral("solverCreateButton"));
    createForm->addRow(m_solverCreateButton);
    box->addWidget(createBox);

    m_solverEmpty = new QLabel(QStringLiteral("（没有求解段）"), content);
    m_solverEmpty->setObjectName(QStringLiteral("solverEmptyLabel"));
    box->addWidget(m_solverEmpty);

    // Mini trajectory window: target point, solver point, path and the
    // initial-velocity arrow.  It never draws the entity content, so it stays
    // readable at this size.  Above the cards it is the primary handle.
    m_solverPreview = new next::SolverPreviewWidget(
        m_session ? m_session->document() : nullptr, content);
    m_solverPreview->setObjectName(QStringLiteral("solverPreviewMini"));
    m_solverPreview->setMinimumWidth(260);
    m_solverPreview->setFixedHeight(172);
    m_solverPreview->setToolTip(
        QStringLiteral("拖动红色箭头手柄调整初速度（松手提交，Esc 取消）"));
    m_solverPreview->hide();
    box->addWidget(m_solverPreview);

    m_solverHost = new QWidget(content);
    m_solverCardsLayout = new QVBoxLayout(m_solverHost);
    m_solverCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_solverCardsLayout->setSpacing(6);
    box->addWidget(m_solverHost);

    connect(m_solverCreateButton, &QPushButton::clicked, this,
            &PropertiesPanel::createSolverFromPlayhead);

    m_solverSection = new CollapsibleSection(QStringLiteral("求解段"), content, this);
    m_solverSection->setObjectName(QStringLiteral("solverSection"));
    m_solverSection->setExpanded(false);
    layout->insertWidget(layout->count() - 1, m_solverSection);
}

void PropertiesPanel::addRow(QFormLayout* form, const QString& label, const QString& prop,
                             double lo, double hi, int decimals, double step, double factor)
{
    auto* rowWidget = new QWidget(form->parentWidget());
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);
    auto* spin = new QDoubleSpinBox(rowWidget);
    spin->setObjectName(QStringLiteral("row_") + prop);
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setKeyboardTracking(false);
    spin->setEnabled(false);
    rowLayout->addWidget(spin, 1);
    auto* key = new QToolButton(rowWidget);
    key->setObjectName(QStringLiteral("key_") + prop);
    key->setFixedSize(18, 18);
    key->setAutoRaise(true);
    key->setText(QStringLiteral("◇"));
    key->setToolTip(QStringLiteral("在当前帧打关键帧"));
    key->setCursor(Qt::PointingHandCursor);
    connect(key, &QToolButton::clicked, this, [this, prop] {
        if (m_sessionMode)
        {
            sessionToggleKey(prop);
            return;
        }
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        if (!entity || m_project->isEffectivelyLocked(m_entity))
            return;
        bool hasKey = false;
        const auto track = entity->tracks.constFind(prop);
        if (track != entity->tracks.constEnd())
            for (const Keyframe& frame : track->keys)
                if (std::abs(frame.frame - m_frame) < 1e-6)
                {
                    hasKey = true;
                    break;
                }
        emit keyframeToggled(m_entity, prop, m_frame, hasKey);
    });
    rowLayout->addWidget(key);
    auto* labelWidget = new QLabel(label, form->parentWidget());
    form->addRow(labelWidget, rowWidget);

    Row row;
    row.prop = prop;
    row.baseLabel = label;
    row.label = labelWidget;
    row.spin = spin;
    row.key = key;
    row.factor = factor;
    m_rows.append(row);

    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, prop](double v) {
        if (m_sessionMode)
        {
            double rowFactor = 1.0;
            for (const Row& row : m_rows)
            {
                if (row.prop == prop)
                {
                    rowFactor = row.factor;
                    break;
                }
            }
            sessionSetTrackValue(prop, v * rowFactor);
            return;
        }
        if (m_entity == kInvalidEntity)
            return;
        double rowFactor = 1.0;
        for (const Row& row : m_rows)
        {
            if (row.prop == prop)
            {
                rowFactor = row.factor;
                break;
            }
        }
        emit valueEdited(m_entity, prop, v * rowFactor);
    });
}

void PropertiesPanel::buildTextSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);
    m_textForm = form;

    m_textContent = new QLineEdit(content);
    m_textContent->setObjectName(QStringLiteral("textContent"));
    connect(m_textContent, &QLineEdit::textEdited, this,
            [this](const QString& text) { applyData(QStringLiteral("text"), text); });
    form->addRow(QStringLiteral("内容"), m_textContent);

    m_textFont = new QFontComboBox(content);
    m_textFont->setObjectName(QStringLiteral("textFont"));
    // Keep the property column inside the legacy 300px panel: the font model
    // otherwise asks for the width of the longest installed family.
    m_textFont->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_textFont->setMinimumContentsLength(10);
    connect(m_textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        applyData(QStringLiteral("font"), font.family());
    });
    form->addRow(QStringLiteral("字体"), m_textFont);

    m_textSize = new QSpinBox(content);
    m_textSize->setObjectName(QStringLiteral("textSize"));
    m_textSize->setRange(8, 512);
    m_textSize->setValue(96);
    m_textSize->setSuffix(QStringLiteral(" px"));
    connect(m_textSize, &QSpinBox::valueChanged, this,
            [this](int size) { applyData(QStringLiteral("size"), size); });
    form->addRow(QStringLiteral("字号"), m_textSize);

    m_textColorButton = new QPushButton(content);
    m_textColorButton->setObjectName(QStringLiteral("textColor"));
    setColorButton(m_textColorButton, m_textColorValue);
    connect(m_textColorButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_textColorValue, QStringLiteral("文字颜色"));
        if (!color.isValid())
            return;
        m_textColorValue = color;
        setColorButton(m_textColorButton, color);
        applyData(QStringLiteral("color"), color);
    });
    form->addRow(QStringLiteral("颜色"), m_textColorButton);

    m_textStrokeWidth = new QDoubleSpinBox(content);
    m_textStrokeWidth->setObjectName(QStringLiteral("textStrokeWidth"));
    m_textStrokeWidth->setRange(0, 24);
    m_textStrokeWidth->setDecimals(1);
    m_textStrokeWidth->setSingleStep(0.5);
    m_textStrokeWidth->setSuffix(QStringLiteral(" px"));
    connect(m_textStrokeWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applyData(QStringLiteral("textStrokeWidth"), value);
            });
    form->addRow(QStringLiteral("描边粗细"), m_textStrokeWidth);

    m_textStrokeColorButton = new QPushButton(content);
    m_textStrokeColorButton->setObjectName(QStringLiteral("textStrokeColor"));
    setColorButton(m_textStrokeColorButton, Qt::black);
    connect(m_textStrokeColorButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_textStrokeColorValue,
                                       QStringLiteral("文字描边颜色"));
        if (!color.isValid())
            return;
        m_textStrokeColorValue = color;
        setColorButton(m_textStrokeColorButton, color);
        applyData(QStringLiteral("textStrokeColor"), color);
    });
    form->addRow(QStringLiteral("描边颜色"), m_textStrokeColorButton);

    m_textAlign = new QComboBox(content);
    m_textAlign->setObjectName(QStringLiteral("textAlign"));
    m_textAlign->addItem(QStringLiteral("居中"), int(Qt::AlignCenter));
    m_textAlign->addItem(QStringLiteral("左对齐"), int(Qt::AlignLeft | Qt::AlignVCenter));
    m_textAlign->addItem(QStringLiteral("右对齐"), int(Qt::AlignRight | Qt::AlignVCenter));
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, [this](int) {
        applyData(QStringLiteral("align"), m_textAlign->currentData());
    });
    form->addRow(QStringLiteral("对齐"), m_textAlign);

    m_textDirection = new QComboBox(content);
    m_textDirection->setObjectName(QStringLiteral("textDirection"));
    m_textDirection->addItem(QStringLiteral("横排"), 0);
    m_textDirection->addItem(QStringLiteral("竖排 · 从右到左"), 1);
    m_textDirection->addItem(QStringLiteral("竖排 · 从左到右"), 2);
    connect(m_textDirection, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_sessionMode)
        {
            QVariantMap data;
            data.insert(QStringLiteral("vertical"), index > 0);
            data.insert(QStringLiteral("verticalRtl"), index != 2);
            if (!m_sessionUuid.isEmpty())
            {
                runSessionContentEdit(
                    QStringLiteral("pvf: edit text direction"),
                    [&data](model::Content& content) {
                        if (content.kind != ContentKind::Text)
                            return false;
                        content.text.vertical =
                            data.value(QStringLiteral("vertical")).toBool();
                        content.text.verticalRtl =
                            data.value(QStringLiteral("verticalRtl")).toBool();
                        return true;
                    });
            }
            return;
        }
        if (m_entity == kInvalidEntity)
            return;
        QVariantMap data;
        data.insert(QStringLiteral("vertical"), index > 0);
        data.insert(QStringLiteral("verticalRtl"), index != 2);
        emit dataEdited(m_entity, data);
    });
    form->addRow(QStringLiteral("排版方向"), m_textDirection);

    auto* styleRow = new QWidget(content);
    auto* styleLayout = new QHBoxLayout(styleRow);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->setSpacing(10);
    m_textBold = new QCheckBox(QStringLiteral("加粗"), styleRow);
    m_textBold->setObjectName(QStringLiteral("textBold"));
    m_textItalic = new QCheckBox(QStringLiteral("斜体"), styleRow);
    m_textItalic->setObjectName(QStringLiteral("textItalic"));
    m_textUnderline = new QCheckBox(QStringLiteral("下划线"), styleRow);
    m_textUnderline->setObjectName(QStringLiteral("textUnderline"));
    connect(m_textBold, &QCheckBox::toggled, this,
            [this](bool on) { applyData(QStringLiteral("bold"), on); });
    connect(m_textItalic, &QCheckBox::toggled, this,
            [this](bool on) { applyData(QStringLiteral("italic"), on); });
    connect(m_textUnderline, &QCheckBox::toggled, this,
            [this](bool on) { applyData(QStringLiteral("underline"), on); });
    styleLayout->addWidget(m_textBold);
    styleLayout->addWidget(m_textItalic);
    styleLayout->addWidget(m_textUnderline);
    styleLayout->addStretch(1);
    form->addRow(QStringLiteral("字形"), styleRow);

    auto* layoutButton = new QPushButton(QStringLiteral("排版…"), content);
    layoutButton->setObjectName(QStringLiteral("textLayoutButton"));
    layoutButton->setToolTip(
        QStringLiteral("打开文字排版编辑器：换行宽度、行距、内边距"));
    connect(layoutButton, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
        {
            const model::Entity* entity = sessionEntity();
            if (!entity || !sessionEditable()
                || entity->content.kind != ContentKind::Text)
                return;
            TextLayoutDialog dialog(textStyleFromContent(entity->content.text),
                                    this);
            if (dialog.exec() != QDialog::Accepted)
            {
                refresh();
                return;
            }
            const model::TextContent edited = textContentFromStyle(
                entity->content.text, dialog.style());
            runSessionContentEdit(
                QStringLiteral("pvf: edit text layout"),
                [edited](model::Content& content) {
                    if (content.kind != ContentKind::Text)
                        return false;
                    content.text = edited;
                    return true;
                });
            return;
        }
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        if (!entity)
            return;
        TextLayoutDialog dialog(TextStyle::fromData(entity->data), this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QVariantMap data;
        dialog.style().writeTo(data);
        emit dataEdited(m_entity, data);
    });
    form->addRow(layoutButton);

    m_textSection = new CollapsibleSection(QStringLiteral("文字"), content, this);
    m_textSection->setExpanded(true);
    layout->addWidget(m_textSection);
}

void PropertiesPanel::addDataRow(QFormLayout* form, const QString& label,
                                 const QString& key, double lo, double hi, int decimals,
                                 double step)
{
    auto* spin = new QDoubleSpinBox(form->parentWidget());
    spin->setObjectName(QStringLiteral("data_") + key);
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    connect(spin, &QDoubleSpinBox::valueChanged, this,
            [this, key](double value) { emitDataEdit(key, value); });
    DataRow row;
    row.key = key;
    row.spin = spin;
    m_dataRows.append(row);
    form->addRow(label, spin);
}

void PropertiesPanel::addDataCount(QFormLayout* form, const QString& label,
                                   const QString& key, int lo, int hi)
{
    auto* spin = new QSpinBox(form->parentWidget());
    spin->setObjectName(QStringLiteral("data_") + key);
    spin->setRange(lo, hi);
    connect(spin, &QSpinBox::valueChanged, this,
            [this, key](int value) { emitDataEdit(key, value); });
    DataRow row;
    row.key = key;
    row.count = spin;
    m_dataRows.append(row);
    form->addRow(label, spin);
}

void PropertiesPanel::addDataColor(QFormLayout* form, const QString& label,
                                   const QString& key, const QColor& initial)
{
    auto* button = new QPushButton(form->parentWidget());
    button->setObjectName(QStringLiteral("data_") + key);
    m_dataColors.insert(key, initial);
    setColorButton(button, initial);
    connect(button, &QPushButton::clicked, this, [this, key, button] {
        const QColor color = pickColor(m_dataColors.value(key), QStringLiteral("颜色"));
        if (!color.isValid())
            return;
        m_dataColors.insert(key, color);
        setColorButton(button, color);
        emitDataEdit(key, color);
    });
    DataRow row;
    row.key = key;
    row.color = button;
    m_dataRows.append(row);
    form->addRow(label, button);
}

void PropertiesPanel::emitDataEdit(const QString& key, const QVariant& value)
{
    if (m_sessionMode)
    {
        applySessionData(key, value);
        return;
    }
    if (m_entity == kInvalidEntity)
        return;
    QVariantMap data;
    data.insert(key, value);
    emit dataEdited(m_entity, data);
}

void PropertiesPanel::buildComicSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_comicShape = new QComboBox(content);
    m_comicShape->addItem(QStringLiteral("椭圆"), 0);
    m_comicShape->addItem(QStringLiteral("矩形"), 1);
    m_comicShape->addItem(QStringLiteral("云朵"), 2);
    m_comicShape->addItem(QStringLiteral("爆炸"), 3);
    connect(m_comicShape, &QComboBox::currentIndexChanged, this, [this](int) {
        emitDataEdit(QStringLiteral("bubbleShape"), m_comicShape->currentData());
    });
    form->addRow(QStringLiteral("形状"), m_comicShape);

    m_comicTail = new QCheckBox(QStringLiteral("尖角"), content);
    m_comicTail->setToolTip(QStringLiteral("拖动画面上的小圆点可以移动尖角"));
    connect(m_comicTail, &QCheckBox::toggled, this, [this](bool on) {
        emitDataEdit(QStringLiteral("bubbleTail"), on);
    });
    form->addRow(m_comicTail);

    addDataRow(form, QStringLiteral("圆角"), QStringLiteral("bubbleRadius"), 0, 400, 0, 1);
    addDataRow(form, QStringLiteral("尖角 X"), QStringLiteral("bubbleTipX"), -3, 3, 2, 0.05);
    addDataRow(form, QStringLiteral("尖角 Y"), QStringLiteral("bubbleTipY"), -3, 3, 2, 0.05);
    addDataRow(form, QStringLiteral("描边宽度"), QStringLiteral("strokeWidth"), 0, 60, 1, 1);
    addDataColor(form, QStringLiteral("底色"), QStringLiteral("fill"), QColor(Qt::white));
    addDataColor(form, QStringLiteral("描边色"), QStringLiteral("stroke"),
                 QColor(24, 24, 28));

    m_comicSection = new CollapsibleSection(QStringLiteral("漫画框"), content, this);
    m_comicSection->setExpanded(true);
    layout->addWidget(m_comicSection);
}

void PropertiesPanel::buildLinesSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);
    m_linesStack = new QStackedWidget(content);

    auto* radial = new QWidget(m_linesStack);
    auto* radialForm = new QFormLayout(radial);
    radialForm->setContentsMargins(0, 0, 0, 0);
    addDataCount(radialForm, QStringLiteral("线条数"), QStringLiteral("count"), 1, 400);
    addDataRow(radialForm, QStringLiteral("半径"), QStringLiteral("radius"), 10, 4000, 0, 10);
    addDataRow(radialForm, QStringLiteral("线宽"), QStringLiteral("width"), 1, 120, 1, 1);
    addDataRow(radialForm, QStringLiteral("旋转 °/s"), QStringLiteral("spin"), -180, 180, 1,
               1);
    addDataRow(radialForm, QStringLiteral("随机速度 Hz"), QStringLiteral("flicker"), 0, 60, 1,
               1);
    addDataRow(radialForm, QStringLiteral("随机种子"), QStringLiteral("seed"), 1, 9999, 0, 1);
    addDataColor(radialForm, QStringLiteral("颜色"), QStringLiteral("color"), QColor(Qt::white));
    m_linesStack->addWidget(radial);

    auto* flow = new QWidget(m_linesStack);
    auto* flowForm = new QFormLayout(flow);
    flowForm->setContentsMargins(0, 0, 0, 0);
    addDataCount(flowForm, QStringLiteral("线条数"), QStringLiteral("count"), 1, 400);
    addDataRow(flowForm, QStringLiteral("长度"), QStringLiteral("length"), 20, 4000, 0, 10);
    addDataRow(flowForm, QStringLiteral("间距"), QStringLiteral("spacing"), 5, 600, 0, 5);
    addDataRow(flowForm, QStringLiteral("粗细"), QStringLiteral("thickness"), 1, 80, 1, 1);
    addDataRow(flowForm, QStringLiteral("速度 px/s"), QStringLiteral("speed"), 0, 6000, 0, 50);
    addDataRow(flowForm, QStringLiteral("角度"), QStringLiteral("angle"), -180, 180, 0, 5);
    addDataRow(flowForm, QStringLiteral("随机种子"), QStringLiteral("seed"), 1, 9999, 0, 1);
    addDataColor(flowForm, QStringLiteral("颜色"), QStringLiteral("color"), QColor(Qt::white));
    m_linesStack->addWidget(flow);

    box->addWidget(m_linesStack);
    m_linesSection = new CollapsibleSection(QStringLiteral("装饰线条"), content, this);
    m_linesSection->setExpanded(true);
    layout->addWidget(m_linesSection);
}

void PropertiesPanel::buildBrandWallSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_wallInfo = new QLabel(content);
    m_wallInfo->setWordWrap(true);
    m_wallInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_wallInfo->setMinimumWidth(0);
    form->addRow(m_wallInfo);

    auto* edit = new QPushButton(QStringLiteral("编辑瓷砖墙…"), content);
    edit->setObjectName(QStringLiteral("wallEditButton"));
    connect(edit, &QPushButton::clicked, this, [this] {
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        if (!entity || entity->type != EntityType::BrandWall)
            return;
        const BrandWallParams params = BrandWallParams::fromData(entity->data);
        BrandWallDialog dialog(params, entity->wallLogos, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QVariantMap data;
        dialog.params().writeTo(data);
        emit dataEdited(m_entity, data);
    });
    form->addRow(edit);

    m_wallSection = new CollapsibleSection(QStringLiteral("品牌瓷砖墙"), content, this);
    m_wallSection->setExpanded(true);
    layout->addWidget(m_wallSection);
}

void PropertiesPanel::buildHalftoneSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    addDataRow(form, QStringLiteral("范围"), QStringLiteral("area"), 100, 4000, 0, 50);
    addDataRow(form, QStringLiteral("点间距"), QStringLiteral("spacing"), 4, 200, 1, 1);
    addDataRow(form, QStringLiteral("最大点径"), QStringLiteral("dotSize"), 0.5, 60, 1,
               0.5);
    addDataRow(form, QStringLiteral("网格角度"), QStringLiteral("angle"), -180, 180, 1,
               5);
    addDataRow(form, QStringLiteral("渐隐角度"), QStringLiteral("gradientAngle"), -180,
               180, 1, 5);
    addDataRow(form, QStringLiteral("最小点比例"), QStringLiteral("minRatio"), 0, 1, 2,
               0.05);
    addDataRow(form, QStringLiteral("手绘抖动"), QStringLiteral("jitter"), 0, 1, 2, 0.05);
    addDataRow(form, QStringLiteral("随机种子"), QStringLiteral("seed"), 1, 9999, 0, 1);
    addDataColor(form, QStringLiteral("颜色"), QStringLiteral("color"), Qt::white);

    m_halftoneSection =
        new CollapsibleSection(QStringLiteral("漫画点阵"), content, this);
    m_halftoneSection->setExpanded(true);
    layout->addWidget(m_halftoneSection);
}

void PropertiesPanel::buildHtmlSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* htmlLayout = new QVBoxLayout(content);
    htmlLayout->setContentsMargins(10, 8, 10, 8);
    htmlLayout->setSpacing(6);

    m_htmlInfo = new QLabel(content);
    m_htmlInfo->setWordWrap(true);
    m_htmlInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_htmlInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_htmlInfo->setMinimumWidth(0);
    htmlLayout->addWidget(m_htmlInfo);

    m_htmlVariables = new QWidget(content);
    m_htmlVariablesLayout = new QVBoxLayout(m_htmlVariables);
    m_htmlVariablesLayout->setContentsMargins(0, 0, 0, 0);
    m_htmlVariablesLayout->setSpacing(4);
    htmlLayout->addWidget(m_htmlVariables);

    m_htmlWarning = new QLabel(content);
    m_htmlWarning->setObjectName(QStringLiteral("htmlWarning"));
    m_htmlWarning->setWordWrap(true);
    m_htmlWarning->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_htmlWarning->setMinimumWidth(0);
    m_htmlWarning->setStyleSheet(QStringLiteral("color:#e0a050;"));
    m_htmlWarning->hide();
    htmlLayout->addWidget(m_htmlWarning);

    m_htmlReplace = new QPushButton(QStringLiteral("整份替换 HTML 资产…"), content);
    m_htmlReplace->setObjectName(QStringLiteral("htmlReplaceButton"));
    m_htmlReplace->setToolTip(
        QStringLiteral("换掉文档，按变量名保留同名 var.* 轨道，孤立轨会保留并标记"));
    connect(m_htmlReplace, &QPushButton::clicked, this, [this] {
        if (m_entity != kInvalidEntity)
            emit replaceSourceRequested(m_entity);
    });
    htmlLayout->addWidget(m_htmlReplace);

    m_htmlSection = new CollapsibleSection(QStringLiteral("HTML"), content, this);
    m_htmlSection->setExpanded(true);
    layout->addWidget(m_htmlSection);
}

void PropertiesPanel::buildSpecialSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);

    m_specialEditor = new next::SpecialContentEditor(content);
    m_specialEditor->setObjectName(QStringLiteral("specialContentPage"));
    // Keep the stacked pages at their natural height instead of letting the
    // multi-line editors eat the whole scroll column.
    m_specialEditor->setSizePolicy(QSizePolicy::Preferred,
                                   QSizePolicy::Maximum);
    box->addWidget(m_specialEditor);
    connect(m_specialEditor, &next::SpecialContentEditor::edited, this,
            &PropertiesPanel::commitSpecialContentEdit);

    m_specialSection =
        new CollapsibleSection(QStringLiteral("特殊内容"), content, this);
    m_specialSection->setObjectName(QStringLiteral("specialContentSection"));
    m_specialSection->setExpanded(true);
    layout->insertWidget(layout->count() - 1, m_specialSection);
    m_specialSection->hide();
}

void PropertiesPanel::buildSessionHtmlSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);

    m_sessionHtmlInfo = new QLabel(content);
    m_sessionHtmlInfo->setObjectName(QStringLiteral("sessionHtmlInfo"));
    m_sessionHtmlInfo->setWordWrap(true);
    m_sessionHtmlInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sessionHtmlInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_sessionHtmlInfo->setMinimumWidth(0);
    box->addWidget(m_sessionHtmlInfo);

    m_sessionHtmlEmpty = new QLabel(QStringLiteral("（manifest 未声明变量）"), content);
    m_sessionHtmlEmpty->setObjectName(QStringLiteral("sessionHtmlEmpty"));
    m_sessionHtmlEmpty->setWordWrap(true);
    box->addWidget(m_sessionHtmlEmpty);

    m_sessionHtmlVariables = new QWidget(content);
    m_sessionHtmlVariablesLayout = new QVBoxLayout(m_sessionHtmlVariables);
    m_sessionHtmlVariablesLayout->setContentsMargins(0, 0, 0, 0);
    m_sessionHtmlVariablesLayout->setSpacing(4);
    box->addWidget(m_sessionHtmlVariables);

    m_sessionHtmlWarning = new QLabel(content);
    m_sessionHtmlWarning->setObjectName(QStringLiteral("sessionHtmlWarning"));
    m_sessionHtmlWarning->setWordWrap(true);
    m_sessionHtmlWarning->setSizePolicy(QSizePolicy::Ignored,
                                        QSizePolicy::Preferred);
    m_sessionHtmlWarning->setMinimumWidth(0);
    m_sessionHtmlWarning->setStyleSheet(QStringLiteral("color:#e0a050;"));
    m_sessionHtmlWarning->hide();
    box->addWidget(m_sessionHtmlWarning);

    m_sessionHtmlSection =
        new CollapsibleSection(QStringLiteral("HTML 变量"), content, this);
    m_sessionHtmlSection->setObjectName(QStringLiteral("sessionHtmlSection"));
    m_sessionHtmlSection->setExpanded(true);
    layout->insertWidget(layout->count() - 1, m_sessionHtmlSection);
    m_sessionHtmlSection->hide();
}

void PropertiesPanel::rebuildHtmlRows(const html::HtmlAsset& asset)
{
    m_htmlRows.clear();
    if (!m_htmlVariablesLayout)
        return;

    while (QLayoutItem* item = m_htmlVariablesLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    for (const html::HtmlComponent& component : asset.components)
    {
        auto* header = new QLabel(
            component.name.isEmpty() ? component.id : component.name, m_htmlVariables);
        header->setToolTip(component.id);
        QFont font = header->font();
        font.setBold(true);
        header->setFont(font);
        m_htmlVariablesLayout->addWidget(header);

        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 4);
        form->setVerticalSpacing(4);
        for (const html::HtmlVariable& variable : component.variables)
            addHtmlVariableRow(form, component, variable);
        m_htmlVariablesLayout->addLayout(form);
    }
}

void PropertiesPanel::addHtmlVariableRow(QFormLayout* form,
                                         const html::HtmlComponent& component,
                                         const html::HtmlVariable& variable)
{
    const QString prop =
        QStringLiteral("var.") + component.id + QLatin1Char('.') + variable.name;
    auto* rowWidget = new QWidget(m_htmlVariables);
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);

    auto* spin = new QDoubleSpinBox(rowWidget);
    spin->setObjectName(QStringLiteral("htmlVar_") + prop);
    spin->setRange(variable.min, variable.max);
    spin->setDecimals(variable.integer ? 0 : 3);
    spin->setSingleStep(variable.integer
                            ? 1.0
                            : std::max(0.001, (variable.max - variable.min) / 100.0));
    spin->setKeyboardTracking(false);
    rowLayout->addWidget(spin, 1);

    auto* key = new QToolButton(rowWidget);
    key->setObjectName(QStringLiteral("htmlKey_") + prop);
    key->setFixedSize(18, 18);
    key->setAutoRaise(true);
    key->setText(QStringLiteral("◇"));
    key->setToolTip(QStringLiteral("在当前帧打关键帧"));
    key->setCursor(Qt::PointingHandCursor);
    connect(key, &QToolButton::clicked, this, [this, prop] {
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        if (!entity || m_project->isEffectivelyLocked(m_entity))
            return;
        bool hasKey = false;
        const auto track = entity->tracks.constFind(prop);
        if (track != entity->tracks.constEnd())
            for (const Keyframe& frame : track->keys)
                if (std::abs(frame.frame - m_frame) < 1e-6)
                {
                    hasKey = true;
                    break;
                }
        emit keyframeToggled(m_entity, prop, m_frame, hasKey);
    });
    rowLayout->addWidget(key);

    auto* labelWidget = new QLabel(variable.name, m_htmlVariables);
    labelWidget->setToolTip(prop);
    form->addRow(labelWidget, rowWidget);

    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, prop](double value) {
        if (m_entity == kInvalidEntity)
            return;
        emit valueEdited(m_entity, prop, value);
    });

    HtmlRow row;
    row.prop = prop;
    row.label = labelWidget;
    row.spin = spin;
    row.key = key;
    m_htmlRows.append(row);
}

void PropertiesPanel::rebuildSessionHtmlRows(const QJsonObject& variables)
{
    m_sessionHtmlRows.clear();
    if (!m_sessionHtmlVariablesLayout)
        return;

    while (QLayoutItem* item = m_sessionHtmlVariablesLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
    {
        const HtmlVarSpec spec = htmlVarSpec(it.key(), it.value());
        auto* rowWidget = new QWidget(m_sessionHtmlVariables);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* nameLabel = new QLabel(spec.name, rowWidget);
        nameLabel->setObjectName(QStringLiteral("sessionHtmlVarName_%1")
                                    .arg(spec.track));
        nameLabel->setToolTip(
            QStringLiteral("%1 · 默认值 %2")
                .arg(spec.track)
                .arg(spec.numeric ? QString::number(spec.defaultValue, 'g', 8)
                                  : spec.text));
        rowLayout->addWidget(nameLabel, 1);

        SessionHtmlRow row;
        row.name = spec.name;
        row.track = spec.track;
        row.numeric = spec.numeric;
        row.defaultValue = spec.defaultValue;
        row.minimum = spec.minimum;
        row.maximum = spec.maximum;
        row.step = spec.step;
        row.decimals = spec.decimals;
        row.readOnlyText = spec.text;
        row.nameLabel = nameLabel;

        if (spec.numeric)
        {
            auto* spin = new QDoubleSpinBox(rowWidget);
            spin->setObjectName(QStringLiteral("sessionHtmlVarValue_%1")
                                    .arg(spec.track));
            spin->setRange(spec.minimum, spec.maximum);
            spin->setDecimals(spec.decimals);
            spin->setSingleStep(spec.step);
            spin->setKeyboardTracking(false);
            spin->setToolTip(
                QStringLiteral("manifest 默认值 %1；修改后按自动关键帧规则写入 %2")
                    .arg(spec.defaultValue)
                    .arg(spec.track));
            rowLayout->addWidget(spin, 1);
            row.spin = spin;

            auto* key = new QToolButton(rowWidget);
            key->setObjectName(QStringLiteral("sessionHtmlVarDiamond_%1")
                                   .arg(spec.track));
            key->setFixedSize(18, 18);
            key->setAutoRaise(true);
            key->setText(QStringLiteral("◇"));
            key->setCursor(Qt::PointingHandCursor);
            rowLayout->addWidget(key);
            row.key = key;

            const QString track = spec.track;
            connect(key, &QToolButton::clicked, this, [this, track, spin] {
                toggleSessionHtmlKey(track, spin->value());
            });
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                    [this, track, spin] {
                        editSessionHtmlVariable(track, spin->value());
                    });
        }
        else
        {
            auto* value = new QLabel(spec.text, rowWidget);
            value->setObjectName(QStringLiteral("sessionHtmlVarValue_%1")
                                     .arg(spec.track));
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setToolTip(QStringLiteral("%1（只读）").arg(spec.track));
            rowLayout->addWidget(value, 1);
            row.readOnlyValue = value;
        }

        m_sessionHtmlVariablesLayout->addWidget(rowWidget);
        m_sessionHtmlRows.append(row);
    }
}

void PropertiesPanel::reloadSessionHtml(const model::Entity& entity)
{
    const bool isHtml = entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Html;
    m_sessionHtmlSection->setVisible(isHtml);
    if (!isHtml)
    {
        m_sessionHtmlUuid.clear();
        m_sessionHtmlHash.clear();
        m_sessionHtmlRows.clear();
        return;
    }

    const AssetHash hash = entity.content.asset.hash;
    if (entity.uuid != m_sessionHtmlUuid || hash != m_sessionHtmlHash)
    {
        m_sessionHtmlUuid = entity.uuid;
        m_sessionHtmlHash = hash;
        m_sessionHtmlPresent = false;
        m_sessionHtmlTimeDependent = true;
        QJsonObject variables;
        QString name;
        int width = 0;
        int height = 0;
        if (const auto stored = m_session->assets().get(hash))
        {
            m_sessionHtmlPresent = true;
            m_sessionHtmlTimeDependent = stored->metadata
                                             .value(QStringLiteral("timeDependent"))
                                             .toBool(true);
            variables = stored->metadata.value(QStringLiteral("variables"))
                            .toObject();
            name = stored->name;
            width = stored->metadata.value(QStringLiteral("width")).toInt(0);
            height = stored->metadata.value(QStringLiteral("height")).toInt(0);
        }
        rebuildSessionHtmlRows(variables);

        if (!m_sessionHtmlPresent)
        {
            m_sessionHtmlInfo->setText(
                QStringLiteral("timeDependent：未知（素材缺失）"));
        }
        else
        {
            QString info = name.isEmpty() ? QStringLiteral("HTML")
                                          : name;
            if (width > 0 && height > 0)
                info += QStringLiteral(" · %1×%2").arg(width).arg(height);
            info += QStringLiteral(" · timeDependent：%1")
                        .arg(m_sessionHtmlTimeDependent
                                 ? QStringLiteral("是（每帧渲染）")
                                 : QStringLiteral("否（可静态缓存）"));
            m_sessionHtmlInfo->setText(info);
        }
    }

    m_sessionHtmlEmpty->setVisible(m_sessionHtmlRows.isEmpty());

    QStringList warnings;
    if (!m_sessionHtmlPresent)
        warnings << QStringLiteral("素材缺失，无法读取 manifest 变量。");

    QSet<QString> declared;
    for (const SessionHtmlRow& row : m_sessionHtmlRows)
        declared.insert(row.track);
    QStringList orphans;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
    {
        if (it.key().startsWith(QStringLiteral("var."))
            && !declared.contains(it.key()) && !it->isEmpty())
            orphans << it.key().mid(4);
    }
    if (!orphans.isEmpty())
        warnings << QStringLiteral("未使用的变量轨：%1")
                        .arg(orphans.join(QStringLiteral(", ")));
    m_sessionHtmlWarning->setText(warnings.join(QStringLiteral("\n")));
    m_sessionHtmlWarning->setVisible(!warnings.isEmpty());

    const int relative = sessionRelativeFrame(entity);
    const bool inRange = relative >= 0 && relative < entity.duration();
    const int sampleFrame = std::clamp(relative, 0, std::max(0, entity.duration() - 1));
    const bool editable = sessionEditable();
    for (SessionHtmlRow& row : m_sessionHtmlRows)
    {
        if (!row.numeric)
        {
            QString text = row.readOnlyText;
            const auto trackIt = entity.tracks.constFind(row.track);
            if (trackIt != entity.tracks.constEnd())
            {
                if (const auto value = model::evaluateTrack(trackIt.value(),
                                                            sampleFrame, false))
                    text = QString::number(*value, 'g', 8);
            }
            row.readOnlyValue->setText(text);
            continue;
        }

        double value = row.defaultValue;
        const auto trackIt = entity.tracks.constFind(row.track);
        if (trackIt != entity.tracks.constEnd())
        {
            if (const auto evaluated = model::evaluateTrack(trackIt.value(),
                                                            sampleFrame, false))
                value = *evaluated;
        }
        setSpinIfIdle(row.spin, value);

        const bool hasKey = inRange && hasKeyAt(entity, row.track, relative);
        row.key->setText(hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
        row.key->setToolTip(
            hasKey ? QStringLiteral("删除当前帧的「%1」关键帧").arg(row.name)
                   : QStringLiteral("在当前帧给「%1」打关键帧，初值为 %2")
                         .arg(row.name)
                         .arg(row.spin->value()));
        row.key->setEnabled(editable && inRange);
        row.spin->setEnabled(editable && inRange);
    }
}

void PropertiesPanel::editSessionHtmlVariable(const QString& track, double value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
    {
        reportSessionError(QStringLiteral("播放头不在实体范围内，无法修改变量"));
        refresh();
        return;
    }
    if (!m_autoKey && !hasKeyAt(*entity, track, relative))
    {
        reportSessionError(
            QStringLiteral("HTML 变量没有静态值：先点菱形打关键帧，或开启自动关键帧"));
        refresh();
        return;
    }
    runSessionEdit(QStringLiteral("pvf: edit HTML variable"),
                   [track, relative, value](model::Entity& e) {
                       setKeyframeValue(e, track, relative, value);
                       return true;
                   });
}

void PropertiesPanel::toggleSessionHtmlKey(const QString& track, double fallback)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
    {
        reportSessionError(QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                               .arg(relative)
                               .arg(entity->duration() - 1));
        return;
    }
    if (hasKeyAt(*entity, track, relative))
    {
        runSessionEdit(QStringLiteral("pvf: delete HTML variable key"),
                       [track, relative](model::Entity& e) {
                           removeKeyframeAt(e, track, relative);
                           return true;
                       });
        return;
    }
    runSessionEdit(QStringLiteral("pvf: add HTML variable key"),
                   [track, relative, fallback](model::Entity& e) {
                       setKeyframeValue(e, track, relative, fallback);
                       return true;
                   });
}

void PropertiesPanel::buildVectorSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_vectorShape = new QComboBox(content);
    m_vectorShape->setObjectName(QStringLiteral("vectorShape"));
    m_vectorShape->addItem(QStringLiteral("矩形"), 0);
    m_vectorShape->addItem(QStringLiteral("圆角矩形"), 3);
    m_vectorShape->addItem(QStringLiteral("椭圆"), 1);
    m_vectorShape->addItem(QStringLiteral("直线"), 2);
    m_vectorShape->addItem(QStringLiteral("三角形"), 4);
    m_vectorShape->addItem(QStringLiteral("心形"), 5);
    m_vectorShape->addItem(QStringLiteral("星形"), 6);
    m_vectorShape->addItem(QStringLiteral("多边形"), 7);
    m_vectorShape->addItem(QStringLiteral("箭头"), 8);
    connect(m_vectorShape, &QComboBox::currentIndexChanged, this, [this](int) {
        applyData(QStringLiteral("shape"), m_vectorShape->currentData());
    });
    form->addRow(QStringLiteral("形状"), m_vectorShape);

    m_vectorCorner = new QDoubleSpinBox(content);
    m_vectorCorner->setObjectName(QStringLiteral("vectorCorner"));
    m_vectorCorner->setRange(0, 2000);
    m_vectorCorner->setDecimals(1);
    m_vectorCorner->setSuffix(QStringLiteral(" px"));
    connect(m_vectorCorner, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("cornerRadius"), value); });
    form->addRow(QStringLiteral("圆角"), m_vectorCorner);

    m_vectorSides = new QSpinBox(content);
    m_vectorSides->setObjectName(QStringLiteral("vectorSides"));
    m_vectorSides->setRange(3, 64);
    connect(m_vectorSides, &QSpinBox::valueChanged, this,
            [this](int value) { applyData(QStringLiteral("sides"), value); });
    form->addRow(QStringLiteral("边数"), m_vectorSides);

    m_vectorInner = new QDoubleSpinBox(content);
    m_vectorInner->setObjectName(QStringLiteral("vectorInner"));
    m_vectorInner->setRange(0.05, 1.0);
    m_vectorInner->setSingleStep(0.05);
    m_vectorInner->setDecimals(2);
    connect(m_vectorInner, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("innerRatio"), value); });
    form->addRow(QStringLiteral("内径比"), m_vectorInner);

    m_vectorWidth = new QDoubleSpinBox(content);
    m_vectorWidth->setObjectName(QStringLiteral("vectorWidth"));
    m_vectorWidth->setRange(1, 20000);
    m_vectorWidth->setDecimals(1);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    connect(m_vectorWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("width"), value); });
    form->addRow(QStringLiteral("宽度"), m_vectorWidth);

    m_vectorHeight = new QDoubleSpinBox(content);
    m_vectorHeight->setObjectName(QStringLiteral("vectorHeight"));
    m_vectorHeight->setRange(1, 20000);
    m_vectorHeight->setDecimals(1);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    connect(m_vectorHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("height"), value); });
    form->addRow(QStringLiteral("高度"), m_vectorHeight);

    m_fillButton = new QPushButton(content);
    m_fillButton->setObjectName(QStringLiteral("vectorFill"));
    setColorButton(m_fillButton, m_fillValue);
    connect(m_fillButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_fillValue, QStringLiteral("填充颜色"));
        if (!color.isValid())
            return;
        m_fillValue = color;
        setColorButton(m_fillButton, color);
        applyData(QStringLiteral("fill"), color);
    });
    form->addRow(QStringLiteral("填充"), m_fillButton);

    m_strokeButton = new QPushButton(content);
    m_strokeButton->setObjectName(QStringLiteral("vectorStroke"));
    setColorButton(m_strokeButton, m_strokeValue);
    connect(m_strokeButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_strokeValue, QStringLiteral("描边颜色"));
        if (!color.isValid())
            return;
        m_strokeValue = color;
        setColorButton(m_strokeButton, color);
        applyData(QStringLiteral("stroke"), color);
    });
    form->addRow(QStringLiteral("描边"), m_strokeButton);

    m_strokeWidth = new QDoubleSpinBox(content);
    m_strokeWidth->setObjectName(QStringLiteral("vectorStrokeWidth"));
    m_strokeWidth->setRange(0, 200);
    m_strokeWidth->setDecimals(1);
    m_strokeWidth->setSuffix(QStringLiteral(" px"));
    connect(m_strokeWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("strokeWidth"), value); });
    form->addRow(QStringLiteral("描边宽度"), m_strokeWidth);

    m_vectorSection = new CollapsibleSection(QStringLiteral("矢量"), content, this);
    m_vectorSection->setExpanded(true);
    layout->addWidget(m_vectorSection);
}

void PropertiesPanel::buildMediaSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* mediaLayout = new QVBoxLayout(content);
    mediaLayout->setContentsMargins(10, 8, 10, 8);
    m_mediaLayout = mediaLayout;
    m_mediaInfo = new QLabel(content);
    m_mediaInfo->setWordWrap(true);
    m_mediaInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_mediaInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_mediaInfo->setMinimumWidth(0);
    mediaLayout->addWidget(m_mediaInfo);
    m_replaceSource = new QPushButton(QStringLiteral("更换素材…"), content);
    m_replaceSource->setObjectName(QStringLiteral("replaceSourceButton"));
    connect(m_replaceSource, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
        {
            if (!m_sessionUuid.isEmpty())
                emit sessionReplaceSourceRequested(m_sessionUuid);
            return;
        }
        if (m_entity != kInvalidEntity)
            emit replaceSourceRequested(m_entity);
    });
    mediaLayout->addWidget(m_replaceSource);
    m_mediaSection = new CollapsibleSection(QStringLiteral("素材"), content, this);
    m_mediaSection->setExpanded(true);
    layout->addWidget(m_mediaSection);
}

void PropertiesPanel::buildAudioSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    auto addCaptureRow = [this, content, form](const QString& label, QWidget* field,
                                               const QString& objectName,
                                               const QString& tooltip,
                                               std::function<void()> capture) {
        auto* row = new QWidget(content);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        field->setParent(row);
        field->setMinimumWidth(0);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        rowLayout->addWidget(field, 1);
        auto* button = new QPushButton(row);
        button->setObjectName(objectName);
        button->setText(QStringLiteral("取"));
        button->setToolTip(tooltip);
        button->setFixedWidth(30);
        connect(button, &QPushButton::clicked, this, capture);
        rowLayout->addWidget(button);
        form->addRow(label, row);
    };

    m_audioStart = new QSpinBox(content);
    m_audioStart->setObjectName(QStringLiteral("audioStartFrame"));
    m_audioStart->setRange(0, 1000000);
    m_audioStart->setSuffix(QStringLiteral(" 帧"));
    m_audioStart->setKeyboardTracking(false);
    m_audioStart->setToolTip(QStringLiteral("音频在时间轴上的起始帧；也可以拖动时间轴上的音频块"));
    addCaptureRow(QStringLiteral("时间轴开始"), m_audioStart,
                  QStringLiteral("audioStartFromPlayhead"),
                  QStringLiteral("把音频在时间轴上的开始位置设为播放头位置"),
                  [this] { emitDataEdit(QStringLiteral("startFrame"), double(m_frame)); });

    m_audioTrimStart = new QDoubleSpinBox(content);
    m_audioTrimStart->setObjectName(QStringLiteral("audioTrimStart"));
    m_audioTrimStart->setRange(0.0, 36000.0);
    m_audioTrimStart->setDecimals(3);
    m_audioTrimStart->setSuffix(QStringLiteral(" s"));
    m_audioTrimStart->setKeyboardTracking(false);
    m_audioTrimStart->setToolTip(
        QStringLiteral("裁掉音频开头的秒数；不改变音频在时间轴上的位置"));
    addCaptureRow(
        QStringLiteral("裁掉开头"), m_audioTrimStart,
        QStringLiteral("audioTrimStartFromPlayhead"),
        QStringLiteral("从播放头对应的音频位置开始，裁掉前面部分；后面的波形保持原位"),
        [this] { applyAudioTrimStart(m_frame); });

    m_audioTrimEnd = new QDoubleSpinBox(content);
    m_audioTrimEnd->setObjectName(QStringLiteral("audioTrimEnd"));
    m_audioTrimEnd->setRange(0.0, 36000.0);
    m_audioTrimEnd->setDecimals(3);
    m_audioTrimEnd->setSuffix(QStringLiteral(" s"));
    m_audioTrimEnd->setKeyboardTracking(false);
    m_audioTrimEnd->setToolTip(
        QStringLiteral("裁掉音频结尾的秒数；不改变音频在时间轴上的位置"));
    addCaptureRow(
        QStringLiteral("裁掉结尾"), m_audioTrimEnd,
        QStringLiteral("audioTrimEndFromPlayhead"),
        QStringLiteral("在播放头对应的音频位置结束，裁掉后面部分；前面的波形保持原位"),
        [this] { applyAudioTrimEnd(m_frame); });

    m_audioVolume = new QSpinBox(content);
    m_audioVolume->setObjectName(QStringLiteral("audioVolume"));
    m_audioVolume->setRange(0, 200);
    m_audioVolume->setSuffix(QStringLiteral(" %"));
    m_audioVolume->setKeyboardTracking(false);
    form->addRow(QStringLiteral("音量"), m_audioVolume);

    m_audioFadeIn = new QDoubleSpinBox(content);
    m_audioFadeIn->setObjectName(QStringLiteral("audioFadeIn"));
    m_audioFadeIn->setRange(0.0, 36000.0);
    m_audioFadeIn->setDecimals(3);
    m_audioFadeIn->setSuffix(QStringLiteral(" s"));
    m_audioFadeIn->setKeyboardTracking(false);
    m_audioFadeIn->setToolTip(QStringLiteral("从音频开头渐入的秒数"));
    form->addRow(QStringLiteral("淡入"), m_audioFadeIn);

    m_audioFadeOut = new QDoubleSpinBox(content);
    m_audioFadeOut->setObjectName(QStringLiteral("audioFadeOut"));
    m_audioFadeOut->setRange(0.0, 36000.0);
    m_audioFadeOut->setDecimals(3);
    m_audioFadeOut->setSuffix(QStringLiteral(" s"));
    m_audioFadeOut->setKeyboardTracking(false);
    m_audioFadeOut->setToolTip(QStringLiteral("到音频结尾前渐出的秒数"));
    form->addRow(QStringLiteral("淡出"), m_audioFadeOut);

    auto* hint =
        new QLabel(QStringLiteral("直接拖动时间轴上的音频块可以整体移动；「时间轴开始」决定位置，"
                                  "「裁掉开头/结尾」会缩短音频块，但保留的波形保持原位。"
                                  "「取」把播放头位置填入字段。"),
                   content);
    hint->setWordWrap(true);
    form->addRow(hint);

    m_audioSection = new CollapsibleSection(QStringLiteral("音频"), content, this);
    m_audioSection->setExpanded(true);
    layout->addWidget(m_audioSection);

    connect(m_audioStart, &QSpinBox::valueChanged, this,
            [this](int value) { emitDataEdit(QStringLiteral("startFrame"), double(value)); });
    connect(m_audioTrimStart, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("trimStart"), value); });
    connect(m_audioTrimEnd, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("trimEnd"), value); });
    connect(m_audioVolume, &QSpinBox::valueChanged, this,
            [this](int value) { emitDataEdit(QStringLiteral("volume"), value / 100.0); });
    connect(m_audioFadeIn, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("fadeIn"), value); });
    connect(m_audioFadeOut, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("fadeOut"), value); });
}

void PropertiesPanel::buildPivotSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_usePivot = new QCheckBox(QStringLiteral("启用自定义轴心"), content);
    m_usePivot->setObjectName(QStringLiteral("usePivot"));
    form->addRow(m_usePivot);

    m_pivotX = new QDoubleSpinBox(content);
    m_pivotX->setObjectName(QStringLiteral("pivotX"));
    m_pivotX->setRange(-10000, 10000);
    m_pivotX->setDecimals(1);
    m_pivotX->setKeyboardTracking(false);
    form->addRow(QStringLiteral("轴心 X"), m_pivotX);

    m_pivotY = new QDoubleSpinBox(content);
    m_pivotY->setObjectName(QStringLiteral("pivotY"));
    m_pivotY->setRange(-10000, 10000);
    m_pivotY->setDecimals(1);
    m_pivotY->setKeyboardTracking(false);
    form->addRow(QStringLiteral("轴心 Y"), m_pivotY);

    auto* hint = new QLabel(QStringLiteral("轴心相对实体中心，旋转围绕轴心。"), content);
    hint->setWordWrap(true);
    form->addRow(hint);

    connect(m_usePivot, &QCheckBox::toggled, this, [this](bool on) {
        Entity* e = m_project->entity(m_entity);
        if (!e)
            return;
        e->data[QStringLiteral("rotPointCustom")] = on;
        m_project->notifyChanged();
        refresh();
    });
    connect(m_pivotX, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_sessionMode)
        {
            sessionSetTrackValue(QStringLiteral("pivotX"), value);
            return;
        }
        if (m_entity != kInvalidEntity)
            emit pivotEdited(m_entity, value,
                             m_project->value(m_entity, QStringLiteral("rotPointY"), m_frame));
    });
    connect(m_pivotY, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_sessionMode)
        {
            sessionSetTrackValue(QStringLiteral("pivotY"), value);
            return;
        }
        if (m_entity != kInvalidEntity)
            emit pivotEdited(m_entity,
                             m_project->value(m_entity, QStringLiteral("rotPointX"), m_frame),
                             value);
    });

    m_pivotSection = new CollapsibleSection(QStringLiteral("轴心"), content, this);
    m_pivotSection->setExpanded(false);
    layout->addWidget(m_pivotSection);
}

void PropertiesPanel::applyData(const QString& key, const QVariant& value)
{
    if (m_sessionMode)
    {
        applySessionData(key, value);
        return;
    }
    Entity* e = m_project->entity(m_entity);
    if (!e)
        return;
    e->data[key] = value;
    m_project->modified = true;
    m_project->notifyChanged();
}

void PropertiesPanel::applyAudioTrimStart(int playheadFrame)
{
    Entity* e = m_project->entity(m_entity);
    if (!e)
        return;
    const double fps = std::max(1, m_project->fps);
    const double startFrame =
        e->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    const double duration =
        e->data.value(QStringLiteral("duration"), 0.0).toDouble();
    const double trimStart =
        e->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double trimEnd =
        e->data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
    const double audibleFrames =
        std::max(0.0, (duration - trimStart - trimEnd) * fps);
    if (audibleFrames <= 1e-9)
        return;

    // Trimming the left edge moves the clip start to the playhead while the
    // rest of the waveform stays exactly where it was. Leave one frame.
    const double deltaFrames =
        std::clamp(playheadFrame - startFrame, 0.0,
                   std::max(0.0, audibleFrames - 1.0));
    if (deltaFrames <= 1e-9)
        return;

    QVariantMap data;
    data.insert(QStringLiteral("startFrame"), startFrame + deltaFrames);
    data.insert(QStringLiteral("trimStart"), trimStart + deltaFrames / fps);
    emit dataEdited(m_entity, data);
}

void PropertiesPanel::applyAudioTrimEnd(int playheadFrame)
{
    Entity* e = m_project->entity(m_entity);
    if (!e)
        return;
    const double fps = std::max(1, m_project->fps);
    const double startFrame =
        e->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    const double duration =
        e->data.value(QStringLiteral("duration"), 0.0).toDouble();
    const double trimStart =
        e->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double trimEnd =
        e->data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
    const double audibleFrames =
        std::max(0.0, (duration - trimStart - trimEnd) * fps);
    if (audibleFrames <= 1.0)
        return;

    // The playhead is relative to the clip start; trimEnd counts from the end
    // of the source, so trimStart has to be subtracted as well.
    const double deltaFrames =
        std::clamp(playheadFrame - startFrame, 1.0, audibleFrames);
    const double value =
        std::max(0.0, duration - trimStart - deltaFrames / fps);
    if (std::abs(value - trimEnd) < 1e-9)
        return;

    QVariantMap data;
    data.insert(QStringLiteral("trimEnd"), value);
    emit dataEdited(m_entity, data);
}

const model::Entity* PropertiesPanel::sessionEntity() const
{
    if (!m_session || m_sessionUuid.isEmpty())
        return nullptr;
    const auto& entities = m_session->project().entities;
    const auto it = entities.constFind(m_sessionUuid);
    return it == entities.constEnd() ? nullptr : &it.value();
}

bool PropertiesPanel::sessionEditable() const
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !m_session)
        return false;
    return !next::isEntityLocked(m_session->project(), entity->uuid);
}

int PropertiesPanel::sessionRelativeFrame(const model::Entity& entity) const
{
    const int playhead = m_session ? m_session->playhead() : m_frame;
    return playhead - entity.start;
}

void PropertiesPanel::reportSessionError(const QString& message)
{
    emit sessionEditFailed(message.isEmpty() ? QStringLiteral("未知错误") : message);
}

bool PropertiesPanel::runSessionEdit(
    const QString& message, const std::function<bool(model::Entity&)>& mutate,
    bool ignoreLock)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return false;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateEntity(
        m_sessionUuid, message, mutate, &error, ignoreLock);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
    return ok;
}

bool PropertiesPanel::runSessionContentEdit(
    const QString& message, const std::function<bool(model::Content&)>& mutate)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return false;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateContent(
        m_sessionUuid, message, mutate, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
    return ok;
}

void PropertiesPanel::commitSpecialContentEdit()
{
    const model::Entity* entity = sessionEntity();
    if (!m_session || m_editing || !m_specialEditor || !entity
        || !next::SpecialContentEditor::handles(entity->content.kind))
        return;
    if (m_specialEditor->kind() != entity->content.kind)
        return;

    model::Content content = entity->content;
    m_specialEditor->applyTo(content);

    // The model accepts an empty brand-wall label list (it draws empty tiles),
    // but saving an editor page with no label is meaningless.
    if (content.kind == ContentKind::BrandWall
        && content.brandWall.logos.isEmpty())
    {
        reportSessionError(QStringLiteral("未修改：品牌墙至少需要一个标签"));
        refresh();
        return;
    }

    runSessionContentEdit(
        QStringLiteral("pvf: edit %1 content")
            .arg(contentKindName(content.kind)),
        [content](model::Content& updated) {
            if (updated.kind != content.kind)
                return false;
            updated = content;
            return true;
        });
}

void PropertiesPanel::sessionSetTrackValue(const QString& track, double value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const bool camera = entity->content.kind == ContentKind::Camera;
    const bool cameraScale =
        camera && (track == QLatin1String("scaleX")
                   || track == QLatin1String("scaleY"));
    double stored = value;
    if (cameraScale)
        stored = std::clamp(std::abs(value), 0.05, 1.0);

    runSessionEdit(QStringLiteral("pvf: edit %1").arg(track),
                   [this, track, stored, cameraScale](model::Entity& e) {
                       if (cameraScale)
                       {
                           e.transform.scaleX = stored;
                           e.transform.scaleY = stored;
                       }
                       else if (!setStaticTrackValue(e, track, stored))
                       {
                           return false;
                       }

                       if (m_autoKey)
                       {
                           const int relative =
                               m_session->playhead() - e.start;
                           if (relative >= 0 && relative < e.duration()
                               && !trackBlockedAt(e, track, relative))
                           {
                               setKeyframeValue(e, track, relative, stored);
                               if (cameraScale)
                                   setKeyframeValue(e, QStringLiteral("scaleY"),
                                                    relative, stored);
                           }
                       }
                       return true;
                   });
}

void PropertiesPanel::sessionSetKeyValue(const QString& track, double value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
        return;
    if (trackBlockedAt(*entity, track, relative))
    {
        reportSessionError(QStringLiteral("轨道 %1 的第 %2 帧由求解段驱动，"
                                          "不能写入普通关键帧")
                               .arg(track)
                               .arg(relative));
        return;
    }

    const bool cameraScale =
        entity->content.kind == ContentKind::Camera
        && (track == QLatin1String("scaleX")
            || track == QLatin1String("scaleY"));
    const double stored =
        cameraScale ? std::clamp(std::abs(value), 0.05, 1.0) : value;

    runSessionEdit(QStringLiteral("pvf: edit keyframe %1").arg(track),
                   [this, track, relative, stored, cameraScale](model::Entity& e) {
                       if (cameraScale)
                       {
                           e.transform.scaleX = stored;
                           e.transform.scaleY = stored;
                       }
                       else if (!setStaticTrackValue(e, track, stored))
                       {
                           return false;
                       }
                       setKeyframeValue(e, track, relative, stored);
                       if (cameraScale)
                           setKeyframeValue(e, QStringLiteral("scaleY"), relative,
                                            stored);
                       return true;
                   });
}

void PropertiesPanel::sessionToggleKey(const QString& track)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
    {
        reportSessionError(QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                               .arg(relative)
                               .arg(entity->duration() - 1));
        return;
    }
    if (trackBlockedAt(*entity, track, relative))
    {
        reportSessionError(QStringLiteral("轨道 %1 的第 %2 帧由求解段驱动，"
                                          "不能写入普通关键帧")
                               .arg(track)
                               .arg(relative));
        return;
    }

    m_editing = true;
    QString error;
    bool ok = false;
    if (hasKeyAt(*entity, track, relative))
    {
        ok = m_session->editActions()->deleteKeyframes(
            {KeyRef{m_sessionUuid, track, relative}}, &error);
    }
    else
    {
        ok = m_session->editActions()->addKeyframe(m_sessionUuid, track,
                                                   relative, &error);
    }
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::sessionAddTransformKeys()
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
    {
        reportSessionError(QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                               .arg(relative)
                               .arg(entity->duration() - 1));
        return;
    }
    const bool camera = entity->content.kind == ContentKind::Camera;
    QStringList tracks;
    for (const Row& row : m_rows)
    {
        if (camera && (row.prop == QLatin1String("scaleY")
                       || row.prop == QLatin1String("opacity")))
            continue;
        tracks.append(row.prop);
    }

    runSessionEdit(QStringLiteral("pvf: key transform"),
                   [this, tracks, relative, camera](model::Entity& e) {
                       bool changed = false;
                       for (const QString& track : tracks)
                       {
                           if (trackBlockedAt(e, track, relative))
                               continue;
                           double value =
                               currentTrackValue(e, track, relative,
                                                 isStepTrack(track));
                           if (camera && track == QLatin1String("scaleX"))
                               value = std::clamp(std::abs(value), 0.05, 1.0);
                           setKeyframeValue(e, track, relative, value);
                           changed = true;
                           if (camera && track == QLatin1String("scaleX"))
                               setKeyframeValue(e, QStringLiteral("scaleY"),
                                                relative, value);
                       }
                       return changed;
                   });
}

void PropertiesPanel::sessionSetGroupEasing(const model::EasingSpec& easing)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration()
        || !hasAnyKeyAt(*entity, relative))
        return;

    // An empty track asks EditActions for the merged keyframe group: every
    // track key at this frame gets the same easing in one commit.
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->setKeyframeEasing(
        {KeyRef{m_sessionUuid, QString(), relative}}, easing, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::sessionPromptGroupBezier()
{
    const model::Entity* entity = sessionEntity();
    if (!entity)
        return;
    const int relative = sessionRelativeFrame(*entity);
    model::EasingSpec current;
    bool mixed = false;
    if (!frameEasing(*entity, relative, &current, &mixed) || mixed)
        current = model::EasingSpec();

    EasingSpec legacy;
    legacy.easing = easingFromName(model::canonicalEasingName(current.type));
    legacy.inX = current.inX;
    legacy.inY = current.inY;
    legacy.outX = current.outX;
    legacy.outY = current.outY;
    if (legacy.easing != Easing::Bezier)
    {
        legacy.easing = Easing::Bezier;
        legacy.inX = 0.42;
        legacy.inY = 0.0;
        legacy.outX = 0.58;
        legacy.outY = 1.0;
    }

    BezierEditorDialog dialog(legacy, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        refresh();
        return;
    }
    const EasingSpec edited = dialog.easing();
    model::EasingSpec spec;
    spec.type = easingName(edited.easing);
    spec.inX = edited.inX;
    spec.inY = edited.inY;
    spec.outX = edited.outX;
    spec.outY = edited.outY;
    sessionSetGroupEasing(spec);
}

void PropertiesPanel::applySessionData(const QString& key, const QVariant& value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;

    const ContentKind kind = entity->content.kind;
    if (kind == ContentKind::Text)
    {
        if (key == QLatin1String("size"))
        {
            sessionSetTrackValue(QStringLiteral("fontSize"), value.toInt());
            return;
        }
        if (key == QLatin1String("textStrokeWidth"))
        {
            sessionSetTrackValue(QStringLiteral("strokeWidth"),
                                 value.toDouble());
            return;
        }
        if (key == QLatin1String("lineHeight")
            || key == QLatin1String("letterSpacing")
            || key == QLatin1String("padding")
            || key == QLatin1String("boxWidth")
            || key == QLatin1String("boxHeight"))
        {
            sessionSetTrackValue(key, value.toDouble());
            return;
        }

        runSessionContentEdit(QStringLiteral("pvf: edit text content"),
                              [key, value](model::Content& content) {
                                  if (content.kind != ContentKind::Text)
                                      return false;
                                  model::TextContent& text = content.text;
                                  if (key == QLatin1String("text"))
                                      text.text = value.toString();
                                  else if (key == QLatin1String("font"))
                                      text.fontFamily = value.toString();
                                  else if (key == QLatin1String("color"))
                                      text.color = colorString(
                                          value.value<QColor>());
                                  else if (key == QLatin1String("textStrokeColor"))
                                      text.strokeColor = colorString(
                                          value.value<QColor>());
                                  else if (key == QLatin1String("align"))
                                      text.align = value.toInt();
                                  else if (key == QLatin1String("bold"))
                                      text.bold = value.toBool();
                                  else if (key == QLatin1String("italic"))
                                      text.italic = value.toBool();
                                  else if (key == QLatin1String("underline"))
                                      text.underline = value.toBool();
                                  else if (key == QLatin1String("vertical"))
                                      text.vertical = value.toBool();
                                  else if (key == QLatin1String("verticalRtl"))
                                      text.verticalRtl = value.toBool();
                                  else
                                      return false;
                                  return true;
                              });
        return;
    }

    if (kind == ContentKind::Vector)
    {
        if (key == QLatin1String("cornerRadius")
            || key == QLatin1String("width") || key == QLatin1String("height")
            || key == QLatin1String("strokeWidth"))
        {
            sessionSetTrackValue(key, value.toDouble());
            return;
        }

        runSessionContentEdit(QStringLiteral("pvf: edit vector content"),
                              [key, value](model::Content& content) {
                                  if (content.kind != ContentKind::Vector)
                                      return false;
                                  model::VectorContent& vector = content.vector;
                                  if (key == QLatin1String("shape"))
                                      vector.shape = value.toInt();
                                  else if (key == QLatin1String("sides"))
                                      vector.sides = value.toInt();
                                  else if (key == QLatin1String("innerRatio"))
                                      vector.innerRatio = value.toDouble();
                                  else if (key == QLatin1String("fill"))
                                      vector.fillColor = colorString(
                                          value.value<QColor>());
                                  else if (key == QLatin1String("stroke"))
                                      vector.strokeColor = colorString(
                                          value.value<QColor>());
                                  else
                                      return false;
                                  return true;
                              });
        return;
    }

    if (kind == ContentKind::Asset)
    {
        if (key == QLatin1String("sourceIn"))
            applySessionAsset(QStringLiteral("sourceIn"), value);
        else if (key == QLatin1String("volume"))
            applySessionAsset(QStringLiteral("volume"), value);
    }
}

void PropertiesPanel::applySessionParent(const Uuid& parent)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable() || entity->parentUuid == parent)
        return;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->reparent(
        m_sessionUuid, parent, false, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::applySessionAsset(const QString& field, const QVariant& value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable()
        || entity->content.kind != ContentKind::Asset)
        return;

    int maxSourceIn = 1000000;
    const auto stored =
        m_session->assets().getIncludingMissing(entity->content.asset.hash);
    if (stored)
    {
        const int frames =
            stored->metadata.value(QStringLiteral("durationFrames")).toInt(0);
        if (frames > 0)
            maxSourceIn = std::max(0, frames - 1);
    }

    runSessionContentEdit(QStringLiteral("pvf: edit asset content"),
                          [field, value, maxSourceIn](model::Content& content) {
                              if (content.kind != ContentKind::Asset)
                                  return false;
                              if (field == QLatin1String("sourceIn"))
                                  content.asset.sourceIn = std::clamp(
                                      value.toInt(), 0, maxSourceIn);
                              else if (field == QLatin1String("volume"))
                                  content.asset.volume = std::clamp(
                                      value.toDouble(), 0.0, 2.0);
                              else
                                  return false;
                              return true;
                          });
}

void PropertiesPanel::rebuildTrackRows(const model::Entity& entity)
{
    for (TrackRow& row : m_trackRows)
    {
        QWidget* container = row.diamond ? row.diamond->parentWidget() : nullptr;
        if (!container)
            container = row.name;
        if (!container)
            continue;
        for (QWidget* child : container->findChildren<QWidget*>())
            child->setObjectName(QString());
        container->setObjectName(QString());
        container->hide();
        container->deleteLater();
    }
    m_trackRows.clear();
    m_trackRowsEntity = entity.uuid;
    m_trackRowsKind = entity.content.kind;

    const QVector<TrackSpec> specs = trackSpecsFor(entity);
    for (const TrackSpec& spec : specs)
    {
        TrackRow row;
        row.track = spec.name;
        row.label = spec.label;
        row.step = spec.step;
        row.minimum = spec.minimum;
        row.maximum = spec.maximum;
        row.singleStep = spec.singleStep;
        row.decimals = spec.decimals;
        row.suffix = spec.suffix;

        auto* rowWidget = new QWidget(m_trackHost);
        auto* rowLayout = new QVBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto* topLine = new QHBoxLayout();
        topLine->setContentsMargins(0, 0, 0, 0);
        topLine->setSpacing(3);
        auto* labelWidget = new QLabel(spec.label, rowWidget);
        labelWidget->setObjectName(QStringLiteral("trackLabel_") + spec.name);
        topLine->addWidget(labelWidget);
        topLine->addStretch(1);

        auto* diamond = new QToolButton(rowWidget);
        diamond->setObjectName(QStringLiteral("trackDiamond_") + spec.name);
        diamond->setFixedSize(18, 18);
        diamond->setAutoRaise(true);
        diamond->setText(QStringLiteral("◇"));
        diamond->setCursor(Qt::PointingHandCursor);
        topLine->addWidget(diamond);

        auto* value = new QDoubleSpinBox(rowWidget);
        value->setObjectName(QStringLiteral("trackValue_") + spec.name);
        value->setRange(spec.minimum, spec.maximum);
        value->setDecimals(spec.decimals);
        value->setSingleStep(spec.singleStep);
        if (!spec.suffix.isEmpty())
            value->setSuffix(spec.suffix);
        value->setKeyboardTracking(false);
        topLine->addWidget(value, 1);
        rowLayout->addLayout(topLine);

        m_trackForm->addRow(rowWidget);

        const QString track = spec.name;
        connect(diamond, &QToolButton::clicked, this,
                [this, track] { sessionToggleKey(track); });
        connect(value, &QDoubleSpinBox::editingFinished, this,
                [this, track, value] {
                    if (!m_editing)
                        sessionSetKeyValue(track, value->value());
                });

        row.name = labelWidget;
        row.diamond = diamond;
        row.value = value;
        m_trackRows.append(row);
    }
}

void PropertiesPanel::reloadTrackRows(const model::Entity& entity)
{
    const QVector<TrackSpec> specs = trackSpecsFor(entity);
    bool sameStructure = entity.uuid == m_trackRowsEntity
        && entity.content.kind == m_trackRowsKind
        && m_trackRows.size() == specs.size();
    for (int i = 0; sameStructure && i < specs.size(); ++i)
    {
        sameStructure = m_trackRows.at(i).track == specs.at(i).name
            && m_trackRows.at(i).step == specs.at(i).step;
    }
    if (!sameStructure)
        rebuildTrackRows(entity);

    const int relative = sessionRelativeFrame(entity);
    const bool inRange = relative >= 0 && relative < entity.duration();
    const bool editable = sessionEditable();
    for (int i = 0; i < m_trackRows.size() && i < specs.size(); ++i)
    {
        TrackRow& row = m_trackRows[i];
        const TrackSpec& spec = specs.at(i);
        const bool hasKey = inRange && hasKeyAt(entity, row.track, relative);
        const bool blocked =
            inRange && trackBlockedAt(entity, row.track, relative);

        row.diamond->setText(hasKey ? QStringLiteral("◆")
                                    : QStringLiteral("◇"));
        row.diamond->setEnabled(editable && inRange && !blocked);
        if (!inRange)
            row.diamond->setToolTip(
                QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                    .arg(relative)
                    .arg(entity.duration() - 1));
        else if (blocked)
            row.diamond->setToolTip(
                QStringLiteral("该帧由求解段驱动，不能写入普通关键帧"));
        else
            row.diamond->setToolTip(
                hasKey ? QStringLiteral("删除当前帧（%1）的「%2」关键帧")
                             .arg(relative)
                             .arg(spec.label)
                       : QStringLiteral("在当前帧（%1）插入「%2」关键帧")
                             .arg(relative)
                             .arg(spec.label));

        setSpinIfIdle(row.value, currentTrackValue(entity, row.track, relative,
                                                   spec.step));
        row.value->setEnabled(editable && inRange && !blocked);
    }

    if (m_groupEasing)
    {
        const bool hasGroup = inRange && hasAnyKeyAt(entity, relative);
        const int previous =
            inRange ? previousGroupKeyframeFrame(entity, relative) : -1;
        model::EasingSpec current;
        bool mixed = false;
        if (hasGroup)
            frameEasing(entity, relative, &current, &mixed);
        const QSignalBlocker blocker(m_groupEasing);
        if (hasGroup && previous >= 0 && !mixed)
        {
            const QString type = model::canonicalEasingName(current.type);
            int index = m_groupEasing->findData(type);
            if (index < 0)
            {
                m_groupEasing->addItem(type, type);
                index = m_groupEasing->count() - 1;
            }
            m_groupEasing->setCurrentIndex(index);
        }
        else
        {
            m_groupEasing->setCurrentIndex(-1);
        }
        m_groupEasing->setPlaceholderText(
            !hasGroup
                ? QStringLiteral("当前帧无关键帧")
                : previous < 0 ? QStringLiteral("首帧无进入段")
                               : mixed     ? QStringLiteral("混合")
                                           : QStringLiteral("选择缓动"));
        m_groupEasing->setEnabled(editable && hasGroup && previous >= 0);
        m_groupEasing->setToolTip(
            !hasGroup
                ? QStringLiteral("当前帧没有可编辑的关键帧")
                : previous < 0
                    ? QStringLiteral("首帧没有进入段；请先在前一帧打关键帧")
                    : QStringLiteral("修改当前帧所有轨道关键帧的进入段曲线；"
                                     "自定义贝塞尔仍可选"));
    }
}

void PropertiesPanel::reloadSolverCreate(const model::Entity& entity)
{
    const Uuid previous = m_solverTargetCombo->currentData().toString();
    {
        const QSignalBlocker blocker(m_solverTargetCombo);
        m_solverTargetCombo->clear();
        m_solverTargetCombo->addItem(QStringLiteral("（选择目标实体）"),
                                     QString());
        for (auto it = m_session->project().entities.constBegin();
             it != m_session->project().entities.constEnd(); ++it)
        {
            if (it.key() == entity.uuid)
                continue;
            const QString label =
                it.value().name.isEmpty()
                    ? it.key().left(8)
                    : QStringLiteral("%1 (%2)")
                          .arg(it.value().name, it.key().left(8));
            m_solverTargetCombo->addItem(label, it.key());
        }
        const int index = m_solverTargetCombo->findData(previous);
        m_solverTargetCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    const int duration = entity.duration();
    m_solverEndSpin->setRange(-1, std::max(0, duration - 1));
    if (m_solverEndSpin->value() > duration - 1)
        m_solverEndSpin->setValue(duration - 1);
    m_solverEndRow->setVisible(m_solverManualEnd->isChecked());
    if (m_solverManualEnd->isChecked() && m_solverEndSpin->value() < 0)
        m_solverEndSpin->setValue(std::max(0, duration - 1));
    reloadSolverConvert(entity);
}

bool PropertiesPanel::canConvertCurrentKey(const model::Entity& entity,
                                           QString* reason) const
{
    const int relative = sessionRelativeFrame(entity);
    if (relative < 0 || relative >= entity.duration())
    {
        if (reason)
            *reason = QStringLiteral("播放头不在片段范围内：相对帧 %1，允许 0..%2")
                          .arg(relative)
                          .arg(entity.duration() - 1);
        return false;
    }
    if (!hasAnyKeyAt(entity, relative))
    {
        if (reason)
            *reason = QStringLiteral("当前帧没有关键帧；先在播放头处打一个普通关键帧");
        return false;
    }
    if (!model::isLastKeyframeInClip(entity, relative))
    {
        if (reason)
            *reason = QStringLiteral("后面还有关键帧，不能转换");
        return false;
    }
    for (const model::SolverSegment& solver : entity.solvers)
    {
        const int end = model::solverStructuralEnd(entity, solver);
        if (relative >= solver.startFrame && relative <= end)
        {
            if (reason)
                *reason = QStringLiteral("该关键帧已在求解段内，不能再次转换");
            return false;
        }
    }
    if (reason)
        *reason = QStringLiteral(
            "将当前关键帧作为求解开始；求解在后续第一个关键帧前一帧、"
            "物理停止或片段末尾结束");
    return true;
}

void PropertiesPanel::reloadSolverConvert(const model::Entity& entity)
{
    if (!m_convertSolverButton || !m_solverCreateButton)
        return;
    QString reason;
    const bool convert = sessionEditable()
                         && canConvertCurrentKey(entity, &reason);
    m_convertSolverButton->setEnabled(convert);
    m_solverCreateButton->setEnabled(convert);
    m_convertSolverButton->setToolTip(reason);
    m_solverCreateButton->setToolTip(reason);
    if (m_convertSolverHint)
        m_convertSolverHint->setText(reason);
}

void PropertiesPanel::updateSolverCards(const model::Entity& entity)
{
    QVector<model::SolverSegment> solvers = entity.solvers;
    std::sort(solvers.begin(), solvers.end(),
              [](const model::SolverSegment& a, const model::SolverSegment& b) {
                  if (a.startFrame != b.startFrame)
                      return a.startFrame < b.startFrame;
                  return a.id < b.id;
              });

    bool sameStructure = solvers.size() == m_solverCards.size();
    for (int i = 0; sameStructure && i < solvers.size(); ++i)
    {
        sameStructure = m_solverCards.at(i).id == solvers.at(i).id
            && m_solverCards.at(i).effect == solvers.at(i).effect;
    }
    if (!sameStructure)
        rebuildSolverCards(entity.uuid, solvers);
    else
        for (int i = 0; i < solvers.size(); ++i)
            fillSolverCard(m_solverCards[i], entity.uuid, solvers.at(i));

    syncSolverFocus(solvers);
    reloadSolverCreate(entity);
    updateSolverPreview(entity, solvers);
    m_solverEmpty->setVisible(solvers.isEmpty());
}

void PropertiesPanel::updateSolverPreview(
    const model::Entity& entity, const QVector<model::SolverSegment>& solvers)
{
    if (!m_solverPreview)
        return;

    // Follow the card selection; without one fall back to the segment under
    // the playhead, then to the first segment, so the window is never empty
    // while the entity has solvers.
    Uuid solverId;
    for (const model::SolverSegment& solver : solvers)
        if (solver.id == m_selectedSolverId)
            solverId = solver.id;
    const int relative = sessionRelativeFrame(entity);
    if (solverId.isEmpty())
    {
        for (const model::SolverSegment& solver : solvers)
        {
            const int end = model::solverStructuralEnd(entity, solver);
            if (relative >= solver.startFrame && relative <= end)
            {
                solverId = solver.id;
                break;
            }
        }
    }
    if (solverId.isEmpty() && !solvers.isEmpty())
        solverId = solvers.first().id;

    if (solverId.isEmpty())
    {
        m_solverPreview->clearSolver();
        m_solverPreview->hide();
        return;
    }

    // setSolver() already evaluates on a rebind; refreshPreview() covers the
    // parameter-change case with the same ids.
    const bool sameBinding = m_solverPreview->entityUuid() == entity.uuid
                             && m_solverPreview->solverId() == solverId;
    m_solverPreview->setSolver(entity.uuid, solverId);
    if (sameBinding)
        m_solverPreview->refreshPreview();
    m_solverPreview->setEnabled(sessionEditable());
    m_solverPreview->show();
}

void PropertiesPanel::syncSolverFocus(
    const QVector<model::SolverSegment>& solvers)
{
    bool found = false;
    for (const model::SolverSegment& solver : solvers)
        if (solver.id == m_selectedSolverId)
        {
            found = true;
            break;
        }
    if (!found && !m_selectedSolverId.isEmpty())
    {
        m_selectedSolverId.clear();
        emit solverSelected(m_sessionUuid, Uuid());
    }
}

void PropertiesPanel::selectSolver(const Uuid& solverId)
{
    if (m_selectedSolverId == solverId)
        return;
    m_selectedSolverId = solverId;
    emit solverSelected(m_sessionUuid, solverId);
    for (SolverCard& card : m_solverCards)
    {
        if (!card.box)
            continue;
        const QString title = card.box->title();
        const QString bare = title.startsWith(QStringLiteral("▶ "))
                                 ? title.mid(2)
                                 : title;
        card.box->setTitle(card.id == solverId
                               ? QStringLiteral("▶ ") + bare
                               : bare);
    }
    if (const model::Entity* entity = sessionEntity())
        updateSolverPreview(*entity, entity->solvers);
}

bool PropertiesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::FocusIn)
    {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        for (const SolverCard& card : m_solverCards)
        {
            if (card.box == watched
                || (widget && card.box && card.box->isAncestorOf(widget)))
            {
                selectSolver(card.id);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PropertiesPanel::rebuildSolverCards(
    const Uuid& entityUuid, const QVector<model::SolverSegment>& solvers)
{
    for (SolverCard& card : m_solverCards)
    {
        if (!card.box)
            continue;
        m_solverCardsLayout->removeWidget(card.box);
        card.box->setObjectName(QString());
        for (QWidget* child : card.box->findChildren<QWidget*>())
            child->setObjectName(QString());
        card.box->hide();
        card.box->deleteLater();
    }
    m_solverCards.clear();

    for (int index = 0; index < solvers.size(); ++index)
    {
        const model::SolverSegment& solver = solvers.at(index);
        SolverCard card;
        card.id = solver.id;
        card.effect = solver.effect;
        card.box = new QGroupBox(m_solverHost);
        card.box->setObjectName(QStringLiteral("solverCard%1").arg(index));
        card.box->installEventFilter(this);
        auto* form = new QFormLayout(card.box);
        form->setContentsMargins(8, 6, 8, 6);
        form->setVerticalSpacing(4);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        auto* effectLabel = new QLabel(solverEffectName(solver.effect), card.box);
        effectLabel->setObjectName(QStringLiteral("solverEffect%1").arg(index));
        form->addRow(QStringLiteral("效果"), effectLabel);

        card.start = new QSpinBox(card.box);
        card.start->setObjectName(QStringLiteral("solverStart%1").arg(index));
        card.start->setRange(0, 1000000);
        card.start->setKeyboardTracking(false);
        form->addRow(QStringLiteral("开始帧"), card.start);

        card.endInfo = new QLabel(card.box);
        card.endInfo->setObjectName(
            QStringLiteral("solverEndInfo%1").arg(index));
        card.endInfo->setWordWrap(true);
        card.endInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(card.endInfo);

        card.manualEnd =
            new QCheckBox(QStringLiteral("手动指定结束帧"), card.box);
        card.manualEnd->setObjectName(
            QStringLiteral("solverManualEnd%1").arg(index));
        card.manualEnd->setToolTip(
            QStringLiteral("高级覆盖：只解到指定帧，之后冻结。默认由物理停止决定。"));
        form->addRow(card.manualEnd);

        card.endRow = new QWidget(card.box);
        auto* endLayout = new QHBoxLayout(card.endRow);
        endLayout->setContentsMargins(0, 0, 0, 0);
        endLayout->addWidget(new QLabel(QStringLiteral("结束帧"), card.endRow));
        card.end = new QSpinBox(card.endRow);
        card.end->setObjectName(QStringLiteral("solverEnd%1").arg(index));
        card.end->setRange(-1, 1000000);
        card.end->setSpecialValueText(QStringLiteral("自动"));
        card.end->setKeyboardTracking(false);
        endLayout->addWidget(card.end, 1);
        card.endRow->hide();
        form->addRow(card.endRow);

        auto* velocityHint =
            new QLabel(QStringLiteral("初速度（主入口：上方预览窗口的红色箭头）"),
                       card.box);
        velocityHint->setObjectName(
            QStringLiteral("solverVelocityHint%1").arg(index));
        velocityHint->setWordWrap(true);
        form->addRow(velocityHint);

        // X/Y stay as an advanced numeric fallback, collapsed by default so
        // they do not compete with the arrow handle.
        card.advanced = new QToolButton(card.box);
        card.advanced->setObjectName(
            QStringLiteral("solverAdvanced%1").arg(index));
        card.advanced->setText(QStringLiteral("高级数值（X/Y）"));
        card.advanced->setCheckable(true);
        card.advanced->setAutoRaise(true);
        form->addRow(card.advanced);

        card.velocityRow = new QWidget(card.box);
        card.velocityRow->setObjectName(
            QStringLiteral("solverVelocityRow%1").arg(index));
        auto* velocityForm = new QFormLayout(card.velocityRow);
        velocityForm->setContentsMargins(0, 0, 0, 0);
        velocityForm->setVerticalSpacing(4);
        velocityForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        card.velocityX = new QDoubleSpinBox(card.velocityRow);
        card.velocityX->setObjectName(QStringLiteral("solverVelocityX%1").arg(index));
        card.velocityX->setRange(-1000000.0, 1000000.0);
        card.velocityX->setDecimals(1);
        card.velocityX->setSingleStep(10.0);
        card.velocityX->setSuffix(QStringLiteral(" px/s"));
        card.velocityX->setKeyboardTracking(false);
        velocityForm->addRow(QStringLiteral("X"), card.velocityX);

        card.velocityY = new QDoubleSpinBox(card.velocityRow);
        card.velocityY->setObjectName(QStringLiteral("solverVelocityY%1").arg(index));
        card.velocityY->setRange(-1000000.0, 1000000.0);
        card.velocityY->setDecimals(1);
        card.velocityY->setSingleStep(10.0);
        card.velocityY->setSuffix(QStringLiteral(" px/s"));
        card.velocityY->setKeyboardTracking(false);
        velocityForm->addRow(QStringLiteral("Y"), card.velocityY);
        card.velocityRow->hide();
        form->addRow(card.velocityRow);
        connect(card.advanced, &QToolButton::toggled, card.velocityRow,
                &QWidget::setVisible);

        const Uuid solverId = solver.id;
        card.remove = new QPushButton(QStringLiteral("删除求解段"), card.box);
        card.remove->setObjectName(QStringLiteral("solverDelete%1").arg(index));
        form->addRow(card.remove);
        connect(card.remove, &QPushButton::clicked, this,
                [this, solverId] { deleteSolver(solverId); });

        connect(card.start, &QSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const int value = card.start->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver start"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.startFrame = value;
                                     return true;
                                 });
                });
        connect(card.end, &QSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const int value = card.end->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver end"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.endFrame = value;
                                     return true;
                                 });
                });
        connect(card.manualEnd, &QCheckBox::toggled, this,
                [this, solverId, card](bool on) {
                    if (m_editing)
                        return;
                    const int value = std::max(0, card.end->value());
                    updateSolver(
                        solverId,
                        QStringLiteral("pvf: edit solver end mode"),
                        [solverId, on, value](model::SolverSegment& s) {
                            if (s.id != solverId)
                                return false;
                            s.endFrame = on ? std::max(s.startFrame, value) : -1;
                            return true;
                        });
                });
        connect(card.velocityX, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityX->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver velocity"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.initialVelocity.setX(value);
                                     return true;
                                 });
                });
        connect(card.velocityY, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityY->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver velocity"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.initialVelocity.setY(value);
                                     return true;
                                 });
                });

        const QVector<ParamSpec> specs = solverParams(solver.effect);
        for (const ParamSpec& spec : specs)
        {
            QWidget* editor = nullptr;
            if (spec.kind == ParamKind::Uuid)
            {
                auto* combo = new QComboBox(card.box);
                combo->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                combo->addItem(QStringLiteral("（选择目标实体）"), QString());
                for (auto it = m_session->project().entities.constBegin();
                     it != m_session->project().entities.constEnd(); ++it)
                {
                    if (it.key() == entityUuid)
                        continue;
                    const QString label =
                        it.value().name.isEmpty()
                            ? it.key().left(8)
                            : QStringLiteral("%1 (%2)")
                                  .arg(it.value().name, it.key().left(8));
                    combo->addItem(label, it.key());
                }
                editor = combo;
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, solverId, spec, combo](int) {
                            if (m_editing)
                                return;
                            const QString value = combo->currentData().toString();
                            if (value.isEmpty())
                                return;
                            updateSolver(
                                solverId,
                                QStringLiteral("pvf: edit solver %1").arg(spec.key),
                                [solverId, spec, value](model::SolverSegment& s) {
                                    if (s.id != solverId)
                                        return false;
                                    s.params.insert(spec.key, value);
                                    return true;
                                });
                        });
            }
            else if (spec.kind == ParamKind::Direction)
            {
                auto* combo = new QComboBox(card.box);
                combo->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                combo->addItem(QStringLiteral("顺时针 (+1)"), 1);
                combo->addItem(QStringLiteral("逆时针 (-1)"), -1);
                editor = combo;
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, solverId, spec, combo](int) {
                            if (m_editing)
                                return;
                            const int value = combo->currentData().toInt();
                            updateSolver(
                                solverId,
                                QStringLiteral("pvf: edit solver %1").arg(spec.key),
                                [solverId, spec, value](model::SolverSegment& s) {
                                    if (s.id != solverId)
                                        return false;
                                    s.params.insert(spec.key, value);
                                    return true;
                                });
                        });
            }
            else
            {
                auto* spin = new QDoubleSpinBox(card.box);
                spin->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                spin->setRange(spec.minimum, spec.maximum);
                spin->setDecimals(spec.decimals);
                spin->setSingleStep(spec.step);
                if (!spec.suffix.isEmpty())
                    spin->setSuffix(QStringLiteral(" ") + spec.suffix);
                spin->setKeyboardTracking(false);
                editor = spin;
                connect(spin, &QDoubleSpinBox::editingFinished, this,
                        [this, solverId, spec, spin] {
                            const double value = spin->value();
                            updateSolver(
                                solverId,
                                QStringLiteral("pvf: edit solver %1").arg(spec.key),
                                [solverId, spec, value](model::SolverSegment& s) {
                                    if (s.id != solverId)
                                        return false;
                                    s.params.insert(spec.key, value);
                                    return true;
                                });
                        });
            }
            form->addRow(spec.label, editor);
            card.params.insert(spec.key, editor);
        }

        for (QWidget* child : card.box->findChildren<QWidget*>())
            child->installEventFilter(this);
        m_solverCardsLayout->addWidget(card.box);
        fillSolverCard(card, entityUuid, solver);
        m_solverCards.append(card);
    }
}

void PropertiesPanel::fillSolverCard(SolverCard& card, const Uuid& entityUuid,
                                     const model::SolverSegment& solver)
{
    const QString title = QStringLiteral("%1 · %2")
                              .arg(solverEffectName(solver.effect),
                                   solverRangeText(solver));
    card.box->setTitle(card.id == m_selectedSolverId
                           ? QStringLiteral("▶ ") + title
                           : title);
    setSpinIfIdle(card.start, solver.startFrame);

    SolverPreviewEval preview;
    const model::Entity* entity = nullptr;
    if (m_session)
    {
        const auto it = m_session->project().entities.constFind(entityUuid);
        if (it != m_session->project().entities.constEnd())
        {
            entity = &it.value();
            preview = evaluateSolverPreview(m_session->project(),
                                            m_session->assets(), it.value(),
                                            solver);
        }
    }
    if (card.endInfo)
    {
        card.endInfo->setText(
            entity ? solverStopInfoText(*entity, solver, preview)
                   : QStringLiteral("结束：无法解算"));
        if (!preview.error.isEmpty())
            card.endInfo->setToolTip(preview.error);
        else
            card.endInfo->setToolTip(
                QStringLiteral("自动结束由后续第一个关键帧、物理停止或片段"
                               "末尾决定；勾选下方高级选项可手动覆盖。"));
    }

    const int duration = entity ? entity->duration() : 1;
    int effectiveEnd = solver.endFrame >= 0
                           ? solver.endFrame
                           : std::max(0, duration - 1);
    if (entity && solver.endFrame < 0)
    {
        effectiveEnd = model::solverStructuralEnd(*entity, solver);
        if (preview.result.stopFrame >= 0)
            effectiveEnd = std::min(effectiveEnd, preview.result.stopFrame);
    }
    const int suggestedEnd = std::max(solver.startFrame, effectiveEnd);
    if (card.manualEnd)
    {
        const QSignalBlocker blocker(card.manualEnd);
        card.manualEnd->setChecked(solver.endFrame >= 0);
    }
    if (card.endRow)
        card.endRow->setVisible(solver.endFrame >= 0);
    if (card.end)
    {
        card.end->setRange(std::max(0, solver.startFrame),
                           std::max(0, duration - 1));
        setSpinIfIdle(card.end, suggestedEnd);
    }
    setSpinIfIdle(card.velocityX, solver.initialVelocity.x());
    setSpinIfIdle(card.velocityY, solver.initialVelocity.y());

    const bool usesVelocity = solver.effect == SolverEffect::Gravity
        || solver.effect == SolverEffect::BlackHole;
    card.velocityX->setEnabled(usesVelocity);
    card.velocityY->setEnabled(usesVelocity);

    const QVector<ParamSpec> specs = solverParams(solver.effect);
    for (const ParamSpec& spec : specs)
    {
        QWidget* editor = card.params.value(spec.key);
        if (!editor)
            continue;
        if (auto* combo = qobject_cast<QComboBox*>(editor))
        {
            const QSignalBlocker blocker(combo);
            if (spec.kind == ParamKind::Uuid)
            {
                const QString value = solver.params.value(spec.key).toString();
                int index = combo->findData(value);
                combo->setCurrentIndex(index >= 0 ? index : 0);
            }
            else
            {
                const int value =
                    solver.params.value(spec.key, int(spec.defaultValue)).toInt();
                int index = combo->findData(value);
                combo->setCurrentIndex(index >= 0 ? index : 0);
            }
        }
        else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor))
        {
            setSpinIfIdle(spin,
                          solver.params.value(spec.key, spec.defaultValue).toDouble());
        }
    }
}

void PropertiesPanel::showSolverConvertMenu()
{
    if (!m_convertSolverButton || !m_convertSolverButton->isEnabled())
        return;

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("solverConvertMenu"));
    const QVector<QPair<SolverEffect, QString>> effects = {
        {SolverEffect::Gravity, QStringLiteral("重力")},
        {SolverEffect::Orbit, QStringLiteral("公转")},
        {SolverEffect::Spin, QStringLiteral("自转")},
        {SolverEffect::BlackHole, QStringLiteral("黑洞")},
    };
    for (const auto& item : effects)
    {
        QAction* action = menu.addAction(item.second);
        action->setData(int(item.first));
    }
    QAction* chosen = menu.exec(m_convertSolverButton->mapToGlobal(
        QPoint(0, m_convertSolverButton->height())));
    if (chosen)
        convertSolverFromKey(SolverEffect(chosen->data().toInt()));
}

void PropertiesPanel::createSolverFromPlayhead()
{
    if (!m_solverEffectCombo)
        return;
    convertSolverFromKey(
        SolverEffect(m_solverEffectCombo->currentData().toInt()));
}

void PropertiesPanel::convertSolverFromKey(SolverEffect effect)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;

    QString reason;
    if (!canConvertCurrentKey(*entity, &reason))
    {
        reportSessionError(reason);
        return;
    }

    const int relative = sessionRelativeFrame(*entity);
    const int duration = entity->duration();
    const Uuid entityUuid = entity->uuid;

    Uuid target;
    if (solverNeedsTarget(effect))
    {
        target = m_solverTargetCombo->currentData().toString();
        if (target.isEmpty() || target == entityUuid
            || !m_session->project().entities.contains(target))
        {
            reportSessionError(
                QStringLiteral("请先在下方“求解段”区选择有效的目标实体（不能是自己）"));
            return;
        }
    }

    int endFrame = m_solverManualEnd && m_solverManualEnd->isChecked()
                       ? m_solverEndSpin->value()
                       : -1;
    if (endFrame < 0)
        endFrame = -1; // automatic: next key / physical stop / clip end
    if (endFrame >= 0 && (endFrame < relative || endFrame >= duration))
    {
        reportSessionError(QStringLiteral("结束帧必须位于 %1..%2（或使用自动结束）")
                               .arg(relative)
                               .arg(duration - 1));
        return;
    }

    model::SolverSegment segment;
    segment.id = model::newUuid();
    segment.effect = effect;
    segment.startFrame = relative;
    segment.endFrame = endFrame;
    segment.version = 1;
    segment.initialVelocity = QPointF(0.0, 0.0);
    switch (effect)
    {
    case SolverEffect::Gravity:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        break;
    case SolverEffect::Orbit:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        segment.params.insert(QStringLiteral("aspect"), 1.0);
        segment.params.insert(QStringLiteral("angle"), 0.0);
        segment.params.insert(QStringLiteral("direction"), 1);
        segment.params.insert(QStringLiteral("periodFrames"), 60);
        break;
    case SolverEffect::Spin:
        segment.params.insert(QStringLiteral("angularVelocity"), 6.0);
        segment.params.insert(QStringLiteral("direction"), 1);
        break;
    case SolverEffect::BlackHole:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        segment.params.insert(QStringLiteral("simulationSpeed"), 1.0);
        segment.params.insert(QStringLiteral("mass"), 1000.0);
        segment.params.insert(QStringLiteral("angularMomentum"), 0.0);
        segment.params.insert(QStringLiteral("eventHorizonRadius"), 80.0);
        segment.params.insert(QStringLiteral("objectRadius"), 40.0);
        segment.params.insert(QStringLiteral("softening"), 60.0);
        segment.params.insert(QStringLiteral("maxSpeed"), 4000.0);
        break;
    }

    const Uuid solverId = segment.id;
    const bool ok = runSessionEdit(
        QStringLiteral("pvf: convert keyframe to solver start"),
        [segment](model::Entity& e) {
            e.solvers.append(segment);
            return true;
        });
    if (ok)
    {
        selectSolver(solverId);
        if (m_solverManualEnd)
        {
            const QSignalBlocker blocker(m_solverManualEnd);
            m_solverManualEnd->setChecked(false);
        }
        m_solverEndSpin->setValue(-1);
        m_solverEndRow->hide();
    }
    refresh();
}

void PropertiesPanel::updateSolver(
    const Uuid& solverId, const QString& message,
    const std::function<bool(model::SolverSegment&)>& mutate)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateEntity(
        m_sessionUuid, message,
        [solverId, &mutate](model::Entity& entity) {
            for (model::SolverSegment& segment : entity.solvers)
                if (segment.id == solverId)
                    return mutate(segment);
            return false;
        },
        &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::deleteSolver(const Uuid& solverId)
{
    const bool ok = runSessionEdit(
        QStringLiteral("pvf: delete solver"),
        [solverId](model::Entity& entity) {
            for (int i = 0; i < entity.solvers.size(); ++i)
            {
                if (entity.solvers.at(i).id == solverId)
                {
                    entity.solvers.removeAt(i);
                    return true;
                }
            }
            return false;
        });
    if (ok && m_selectedSolverId == solverId)
        selectSolver(Uuid());
}

void PropertiesPanel::setColorButton(QPushButton* button, const QColor& color)
{
    button->setText(color.name().toUpper());
    const QString text = color.lightness() > 140 ? QStringLiteral("#000")
                                                 : QStringLiteral("#fff");
    button->setStyleSheet(
        QStringLiteral("background-color:%1;color:%2;").arg(color.name(), text));
}

QColor PropertiesPanel::pickColor(const QColor& initial, const QString& title)
{
    return QColorDialog::getColor(initial, this, title, QColorDialog::ShowAlphaChannel);
}

void PropertiesPanel::setEntity(EntityId id)
{
    m_entity = id;
    refresh();
}

void PropertiesPanel::setFrame(int frame)
{
    m_frame = frame;
    refresh();
}

void PropertiesPanel::setSelectedUuid(const Uuid& uuid)
{
    if (!m_sessionMode)
        return;
    if (m_sessionUuid == uuid)
    {
        refresh();
        return;
    }
    // A different entity owns different solver ids: drop the focus and the
    // mini preview binding before the rebind in refreshSession().
    if (m_solverPreview)
        m_solverPreview->clearSolver();
    m_selectedSolverId.clear();
    m_sessionUuid = uuid;
    refresh();
}

void PropertiesPanel::setAutoKey(bool on)
{
    if (m_autoKey == on)
        return;
    m_autoKey = on;
    if (m_autoKeyCheck)
    {
        const QSignalBlocker blocker(m_autoKeyCheck);
        m_autoKeyCheck->setChecked(on);
    }
}

void PropertiesPanel::refreshSession()
{
    if (m_editing)
        return;
    const model::Entity* entity = sessionEntity();
    const bool valid = entity != nullptr;
    if (!valid)
    {
        m_title->setText(QStringLiteral("未选择对象"));
        for (CollapsibleSection* section :
             {m_basicSection, m_transformSection, m_keySection, m_textSection,
              m_comicSection, m_linesSection, m_wallSection, m_halftoneSection,
              m_htmlSection, m_specialSection, m_sessionHtmlSection,
              m_vectorSection, m_mediaSection, m_audioSection, m_pivotSection,
              m_cameraSection, m_solverSection})
        {
            if (section)
                section->hide();
        }
        m_specialUuid.clear();
        if (m_solverPreview)
        {
            m_solverPreview->clearSolver();
            m_solverPreview->hide();
        }
        return;
    }

    const bool editable = sessionEditable();
    const ContentKind kind = entity->content.kind;
    const bool camera = kind == ContentKind::Camera;
    const bool text = kind == ContentKind::Text;
    const bool vector = kind == ContentKind::Vector;
    const bool asset = kind == ContentKind::Asset;
    const bool audio = asset && entity->content.asset.type == AssetType::Audio;
    const bool special = next::SpecialContentEditor::handles(kind);

    m_title->setText(entity->name + QStringLiteral(" · ")
                     + contentKindLabel(kind));

    m_basicSection->show();
    m_transformSection->show();
    m_keySection->show();
    m_pivotSection->setVisible(!audio);
    m_textSection->setVisible(text);
    m_vectorSection->setVisible(vector);
    m_mediaSection->setVisible(asset);
    m_audioSection->hide();
    m_comicSection->hide();
    m_linesSection->hide();
    m_wallSection->hide();
    m_halftoneSection->hide();
    m_htmlSection->hide();
    m_specialSection->setVisible(special);
    m_cameraSection->setVisible(camera);
    m_solverSection->show();

    m_basicSection->setEnabled(true);
    m_transformSection->setEnabled(editable);
    m_keySection->setEnabled(true);
    m_pivotSection->setEnabled(editable);
    m_textSection->setEnabled(editable);
    m_vectorSection->setEnabled(editable);
    m_mediaSection->setEnabled(editable);
    m_specialSection->setEnabled(editable);
    m_sessionHtmlSection->setEnabled(editable);
    m_cameraSection->setEnabled(true);
    m_solverSection->setEnabled(editable);

    if (!m_basicName->hasFocus())
    {
        const QSignalBlocker blocker(m_basicName);
        m_basicName->setText(entity->name);
    }
    setSpinIfIdle(m_basicStart, entity->start);
    setSpinIfIdle(m_basicEnd, entity->end);
    setSpinIfIdle(m_basicRow, entity->row);
    {
        const QSignalBlocker blocker(m_basicHidden);
        m_basicHidden->setChecked(entity->hidden);
    }
    {
        const QSignalBlocker blocker(m_basicLocked);
        m_basicLocked->setChecked(entity->locked);
    }
    {
        const QSignalBlocker blocker(m_basicVisible);
        m_basicVisible->setChecked(entity->visible);
    }
    {
        const QSignalBlocker blocker(m_basicInherit);
        m_basicInherit->setChecked(entity->inheritVisibility);
    }
    {
        const QSignalBlocker blocker(m_basicPhysics);
        m_basicPhysics->setChecked(entity->physicsInteract);
    }
    m_basicName->setEnabled(editable);
    m_basicStart->setEnabled(editable);
    m_basicEnd->setEnabled(editable);
    m_basicRow->setEnabled(editable);
    m_basicHidden->setEnabled(editable);
    m_basicLocked->setEnabled(true);
    m_basicVisible->setEnabled(editable);
    m_basicInherit->setEnabled(editable);
    m_basicPhysics->setEnabled(editable);

    {
        const QSignalBlocker blocker(m_parent);
        m_parent->clear();
        m_parent->addItem(QStringLiteral("工作台（世界坐标）"), QString());
        const model::Project& project = m_session->project();
        std::function<void(const Uuid&, int)> appendChildren =
            [&](const Uuid& parent, int depth) {
                for (const Uuid& uuid : model::childrenOf(project, parent))
                {
                    if (uuid == entity->uuid
                        || isDescendant(project, uuid, entity->uuid))
                        continue;
                    const model::Entity& candidate = project.entities[uuid];
                    if (camera
                        && candidate.content.kind == ContentKind::Camera)
                        continue;
                    if (candidate.content.kind == ContentKind::Asset
                        && candidate.content.asset.type == AssetType::Audio)
                        continue;
                    const QString prefix = QStringLiteral("  ").repeated(depth);
                    const QString typePrefix =
                        candidate.content.kind == ContentKind::Empty
                            ? QStringLiteral("[空] ")
                        : candidate.content.kind == ContentKind::Camera
                            ? QStringLiteral("[摄像机] ")
                            : QString();
                    m_parent->addItem(prefix + typePrefix + candidate.name,
                                      uuid);
                    appendChildren(uuid, depth + 1);
                }
            };
        appendChildren(Uuid(), 0);
        const int index = m_parent->findData(entity->parentUuid);
        m_parent->setCurrentIndex(index >= 0 ? index : 0);
    }
    m_parent->setEnabled(editable);
    m_parentRow->show();
    if (m_parentLabel)
        m_parentLabel->show();

    const int relative = sessionRelativeFrame(*entity);
    const bool inRange = relative >= 0 && relative < entity->duration();
    m_keyButton->setEnabled(editable && inRange);

    for (Row& row : m_rows)
    {
        const bool hiddenRow =
            camera && (row.prop == QLatin1String("scaleY")
                       || row.prop == QLatin1String("opacity"));
        // Hide the whole form row: a hidden spin box used to leave its key
        // diamond dangling below the camera framing row.
        if (row.key && row.key->parentWidget())
            row.key->parentWidget()->setVisible(!hiddenRow);
        row.spin->setVisible(!hiddenRow);
        row.label->setVisible(!hiddenRow);
        if (row.key)
            row.key->setVisible(!hiddenRow);
        {
            const QSignalBlocker blocker(row.spin);
            if (camera && row.prop == QLatin1String("scaleX"))
            {
                row.label->setText(QStringLiteral("取景比例"));
                row.spin->setRange(5.0, 100.0);
                row.spin->setDecimals(1);
                row.spin->setSingleStep(1.0);
                row.spin->setSuffix(QStringLiteral("%"));
                row.spin->setToolTip(
                    QStringLiteral("摄像机取景比例，绝对值限制 0.05–1.0"
                                   "（5%–100%）"));
                row.factor = 0.01;
            }
            else if (row.prop == QLatin1String("scaleX"))
            {
                row.label->setText(row.baseLabel);
                row.spin->setRange(-100.0, 100.0);
                row.spin->setDecimals(3);
                row.spin->setSingleStep(0.01);
                row.spin->setSuffix(QString());
                row.spin->setToolTip(QString());
                row.factor = 1.0;
            }
            double value = currentTrackValue(*entity, row.prop, relative,
                                             isStepTrack(row.prop))
                / row.factor;
            if (camera && row.prop == QLatin1String("scaleX"))
                value = std::abs(value);
            if (!row.spin->hasFocus())
                row.spin->setValue(value);
        }
        row.spin->setEnabled(editable && !hiddenRow);

        if (row.key)
        {
            const bool hasKey = inRange && hasKeyAt(*entity, row.prop, relative);
            const bool blocked =
                inRange && trackBlockedAt(*entity, row.prop, relative);
            row.key->setText(hasKey ? QStringLiteral("◆")
                                    : QStringLiteral("◇"));
            row.key->setToolTip(
                blocked
                    ? QStringLiteral("该帧由求解段驱动，不能写入普通关键帧")
                    : hasKey
                        ? QStringLiteral("删除当前帧的关键帧")
                        : QStringLiteral("在当前帧打关键帧"));
            row.key->setEnabled(editable && inRange && !blocked);
        }
    }

    m_usePivot->hide();
    setSpinIfIdle(m_pivotX, currentTrackValue(*entity, QStringLiteral("pivotX"),
                                              relative, false));
    setSpinIfIdle(m_pivotY, currentTrackValue(*entity, QStringLiteral("pivotY"),
                                              relative, false));
    m_pivotX->setEnabled(editable);
    m_pivotY->setEnabled(editable);

    if (text)
    {
        const model::TextContent& content = entity->content.text;
        if (!m_textContent->hasFocus())
        {
            const QSignalBlocker blocker(m_textContent);
            m_textContent->setText(content.text);
        }
        if (!m_textFont->hasFocus())
        {
            const QSignalBlocker blocker(m_textFont);
            m_textFont->setCurrentFont(
                content.fontFamily.isEmpty() ? QFont()
                                             : QFont(content.fontFamily));
        }
        setSpinIfIdle(m_textSize, content.pixelSize);
        m_textColorValue = QColor(content.color);
        setColorButton(m_textColorButton, m_textColorValue);
        setSpinIfIdle(m_textStrokeWidth, content.strokeWidth);
        m_textStrokeColorValue = QColor(content.strokeColor);
        setColorButton(m_textStrokeColorButton, m_textStrokeColorValue);
        {
            const QSignalBlocker blocker(m_textAlign);
            selectComboData(m_textAlign, content.align);
        }
        {
            const QSignalBlocker blocker(m_textDirection);
            m_textDirection->setCurrentIndex(
                content.vertical ? (content.verticalRtl ? 1 : 2) : 0);
        }
        {
            const QSignalBlocker blocker(m_textBold);
            m_textBold->setChecked(content.bold);
        }
        {
            const QSignalBlocker blocker(m_textItalic);
            m_textItalic->setChecked(content.italic);
        }
        {
            const QSignalBlocker blocker(m_textUnderline);
            m_textUnderline->setChecked(content.underline);
        }
        setSpinIfIdle(m_textLineHeight, content.lineHeight);
        setSpinIfIdle(m_textLetterSpacing, content.letterSpacing);
        setSpinIfIdle(m_textPadding, content.padding);
        setSpinIfIdle(m_textBoxWidth, content.boxWidth);
        setSpinIfIdle(m_textBoxHeight, content.boxHeight);
    }

    if (vector)
    {
        const model::VectorContent& content = entity->content.vector;
        {
            const QSignalBlocker blocker(m_vectorShape);
            selectComboData(m_vectorShape, content.shape);
        }
        setSpinIfIdle(m_vectorCorner, content.cornerRadius);
        setSpinIfIdle(m_vectorSides, content.sides);
        setSpinIfIdle(m_vectorInner, content.innerRatio);
        setSpinIfIdle(m_vectorWidth, content.width);
        setSpinIfIdle(m_vectorHeight, content.height);
        m_fillValue = QColor(content.fillColor);
        setColorButton(m_fillButton, m_fillValue);
        m_strokeValue = QColor(content.strokeColor);
        setColorButton(m_strokeButton, m_strokeValue);
        setSpinIfIdle(m_strokeWidth, content.strokeWidth);
        m_vectorCorner->setEnabled(editable && content.shape == 3);
        m_vectorSides->setEnabled(editable
                                  && (content.shape == 6 || content.shape == 7));
        m_vectorInner->setEnabled(editable && content.shape == 6);
    }

    if (asset)
    {
        const model::AssetContent& content = entity->content.asset;
        m_assetHash->setText(content.hash);
        m_assetType->setText(assetTypeLabel(content.type));

        int frames = 0;
        const auto stored = m_session->assets().getIncludingMissing(content.hash);
        if (stored)
            frames = stored->metadata.value(QStringLiteral("durationFrames")).toInt(0);
        m_assetSourceIn->setMaximum(frames > 0 ? std::max(0, frames - 1)
                                               : 1000000);
        setSpinIfIdle(m_assetSourceIn, content.sourceIn);
        setSpinIfIdle(m_assetVolume, content.volume);
        m_assetSourceIn->setEnabled(editable);
        m_assetVolume->setEnabled(editable);
        m_replaceSource->show();
        m_replaceSource->setEnabled(editable);

        QString info = assetTypeLabel(content.type);
        if (!stored)
            info += QStringLiteral(" · 素材缺失");
        else if (!stored->name.isEmpty())
            info += QStringLiteral(" · ") + stored->name;
        if (frames > 0)
            info += QStringLiteral(" · %1 帧").arg(frames);
        m_mediaInfo->setText(info);
        m_mediaInfo->setToolTip(content.hash);
    }

    if (special && m_specialEditor)
    {
        const bool reload = m_specialUuid != entity->uuid
            || m_specialEditor->kind() != kind;
        m_specialUuid = entity->uuid;
        QWidget* focus = QApplication::focusWidget();
        if (reload || !focus || !m_specialEditor->isAncestorOf(focus))
            m_specialEditor->setContent(entity->content);
    }
    else
    {
        m_specialUuid.clear();
    }

    reloadSessionHtml(*entity);

    reloadTrackRows(*entity);
    updateSolverCards(*entity);
    for (SolverCard& card : m_solverCards)
        card.box->setEnabled(editable);
    reloadSolverConvert(*entity);
    m_solverEffectCombo->setEnabled(editable);
    m_solverTargetCombo->setEnabled(editable);
    m_solverEndSpin->setEnabled(editable);

    {
        const QSignalBlocker blocker(m_autoKeyCheck);
        m_autoKeyCheck->setChecked(m_autoKey);
    }
}

void PropertiesPanel::refresh()
{
    if (m_sessionMode)
    {
        refreshSession();
        return;
    }
    const Entity* e = m_project->entity(m_entity);
    const bool valid = e != nullptr;
    const bool audio = valid && e->isAudio();

    m_transformSection->setVisible(valid && !audio);
    m_keySection->setVisible(valid && !audio);
    const bool comic = valid && e->type == EntityType::ComicText;
    const bool radial = valid && e->type == EntityType::RadialLines;
    const bool flow = valid && e->type == EntityType::FlowLines;
    const bool halftone = valid && e->type == EntityType::HalftoneDots;
    const bool htmlLayer = valid && e->type == EntityType::HtmlLayer;
    m_textSection->setVisible(valid && (e->type == EntityType::Text || comic));
    m_comicSection->setVisible(comic);
    m_linesSection->setVisible(radial || flow);
    m_wallSection->setVisible(valid && e->type == EntityType::BrandWall);
    m_halftoneSection->setVisible(halftone);
    m_htmlSection->setVisible(htmlLayer);
    if (valid && e->type == EntityType::BrandWall && m_wallInfo)
    {
        const BrandWallParams params = BrandWallParams::fromData(e->data);
        m_wallInfo->setText(
            QStringLiteral("%1 个 Logo · %2×%3 瓷砖 · 间距 %4 px")
                .arg(params.logos.size())
                .arg(params.columns)
                .arg(params.rows)
                .arg(int(params.gap)));
    }
    if (htmlLayer)
    {
        const QString assetPath = e->htmlAssetPath();
        const QString assetStamp = htmlFileStamp(assetPath);
        if (!m_htmlAssetCacheValid || m_htmlAssetCachePath != assetPath
            || m_htmlAssetCacheStamp != assetStamp)
        {
            m_htmlAssetCachePath = assetPath;
            m_htmlAssetCacheStamp = assetStamp;
            m_htmlAssetCacheError.clear();
            m_htmlAssetCache = html::HtmlAsset{};
            if (!assetPath.isEmpty())
                m_htmlAssetCache = html::HtmlAsset::parseFile(
                    assetPath, &m_htmlAssetCacheError);
            else
                m_htmlAssetCacheError = QStringLiteral("没有 HTML 资产");
            m_htmlAssetCacheValid = true;
        }
        const html::HtmlAsset& asset = m_htmlAssetCache;
        const QString assetError = m_htmlAssetCacheError;

        if (m_htmlRowsEntity != e->id || m_htmlAssetPath != assetPath
            || m_htmlRowsStamp != assetStamp)
        {
            m_htmlRowsEntity = e->id;
            m_htmlAssetPath = assetPath;
            m_htmlRowsStamp = assetStamp;
            rebuildHtmlRows(asset);
        }

        m_htmlInfo->setText(
            QStringLiteral("%1\n逻辑尺寸 %2×%3 · %4")
                .arg(assetError.isEmpty()
                         ? QFileInfo(assetPath).fileName()
                         : QStringLiteral("资产不可用：%1").arg(assetError))
                .arg(e->htmlLogicalSize().width())
                .arg(e->htmlLogicalSize().height())
                .arg(e->htmlTimeDependent() ? QStringLiteral("时间相关")
                                            : QStringLiteral("静态")));
        m_htmlInfo->setToolTip(assetPath);

        QStringList warnings;
        if (!assetError.isEmpty())
            warnings << assetError;
        QSet<QString> declared;
        QStringList missing;
        for (const QString& key : asset.variableKeys())
        {
            const QString prop = QStringLiteral("var.") + key;
            declared.insert(prop);
            const auto track = e->tracks.constFind(prop);
            const bool animated = track != e->tracks.constEnd()
                                  && !track->isEmpty();
            if (!e->values.contains(prop) && !animated)
                missing << key;
        }
        if (!missing.isEmpty())
            warnings << QStringLiteral("变量缺少当前值，使用清单默认值：%1")
                            .arg(missing.join(QStringLiteral(", ")));
        QStringList orphans;
        for (auto it = e->tracks.constBegin(); it != e->tracks.constEnd(); ++it)
        {
            if (it.key().startsWith(QStringLiteral("var."))
                && !declared.contains(it.key()) && !it->isEmpty())
                orphans << it.key().mid(4);
        }
        for (auto it = e->values.constBegin(); it != e->values.constEnd(); ++it)
        {
            if (it.key().startsWith(QStringLiteral("var."))
                && !declared.contains(it.key()) && !orphans.contains(it.key().mid(4)))
                orphans << it.key().mid(4);
        }
        if (!orphans.isEmpty())
            warnings << QStringLiteral("未使用的变量轨：%1")
                            .arg(orphans.join(QStringLiteral(", ")));
        m_htmlWarning->setText(warnings.join(QStringLiteral("\n")));
        m_htmlWarning->setVisible(!warnings.isEmpty());
        if (m_htmlReplace)
            m_htmlReplace->setEnabled(!m_project->isEffectivelyLocked(e->id));
    }
    else if (m_htmlWarning)
    {
        m_htmlWarning->hide();
    }
    if (m_linesStack)
        m_linesStack->setCurrentIndex(flow ? 1 : 0);
    m_vectorSection->setVisible(valid && e->type == EntityType::Vector);
    m_mediaSection->setVisible(valid
                               && (e->type == EntityType::Image
                                   || e->type == EntityType::Video
                                   || e->type == EntityType::Audio));
    m_audioSection->setVisible(audio);
    m_pivotSection->setVisible(valid && !e->isCamera() && !audio);

    const bool editable = valid && !m_project->isEffectivelyLocked(e->id);
    const bool camera = valid && e->isCamera();
    m_pivotSection->setEnabled(editable);
    m_transformSection->setEnabled(editable);
    m_keySection->setEnabled(editable);
    m_textSection->setEnabled(editable);
    m_comicSection->setEnabled(editable);
    m_linesSection->setEnabled(editable);
    m_wallSection->setEnabled(editable);
    m_halftoneSection->setEnabled(editable);
    m_htmlSection->setEnabled(editable);
    m_vectorSection->setEnabled(editable);
    m_audioSection->setEnabled(editable);
    if (m_replaceSource)
        m_replaceSource->setEnabled(editable);
    m_keyButton->setEnabled(editable);

    {
        const QSignalBlocker blocker(m_parent);
        m_parent->clear();
        m_parent->addItem(QStringLiteral("工作台（世界坐标）"),
                          int(kInvalidEntity));
        if (valid && !audio)
        {
            for (int index : m_project->entityOrder())
            {
                const Entity& candidate = m_project->entities()[index];
                if (candidate.id == e->id || candidate.isAudio())
                    continue;
                if (e->isCamera() && candidate.isCamera())
                    continue;
                if (m_project->isDescendantOf(candidate.id, e->id))
                    continue;

                int depth = 0;
                QSet<EntityId> seen;
                EntityId parentId = candidate.parent;
                while (parentId != kInvalidEntity && !seen.contains(parentId)
                       && depth < 32)
                {
                    seen.insert(parentId);
                    const Entity* parent = m_project->entity(parentId);
                    if (!parent)
                        break;
                    ++depth;
                    parentId = parent->parent;
                }
                const QString prefix = QStringLiteral("  ").repeated(depth);
                const QString typePrefix =
                    candidate.isFolder() ? QStringLiteral("[文件夹] ")
                    : candidate.isCamera() ? QStringLiteral("[摄像机] ")
                                           : QString();
                m_parent->addItem(prefix + typePrefix + candidate.name,
                                  candidate.id);
            }
            const int parentIndex = m_parent->findData(e->parent);
            m_parent->setCurrentIndex(parentIndex >= 0 ? parentIndex : 0);
        }
        m_parentRow->setVisible(valid && !audio);
        if (m_parentLabel)
            m_parentLabel->setVisible(valid && !audio);
        m_parentRow->setEnabled(editable && !audio);
    }

    for (Row& r : m_rows)
    {
        const bool hiddenRow = camera
                               && (r.prop == QStringLiteral("scaleY")
                                   || r.prop == QStringLiteral("opacity"));
        r.spin->setVisible(!hiddenRow);
        r.label->setVisible(!hiddenRow);

        const QSignalBlocker blocker(r.spin);
        if (camera && r.prop == QStringLiteral("scaleX"))
        {
            r.label->setText(QStringLiteral("取景比例"));
            r.spin->setRange(5.0, 100.0);
            r.spin->setDecimals(1);
            r.spin->setSingleStep(1.0);
            r.spin->setSuffix(QStringLiteral("%"));
            r.factor = 0.01;
        }
        else if (r.prop == QStringLiteral("scaleX"))
        {
            r.label->setText(r.baseLabel);
            r.spin->setRange(-100.0, 100.0);
            r.spin->setDecimals(3);
            r.spin->setSingleStep(0.01);
            r.spin->setSuffix(QString());
            r.factor = 1.0;
        }
        r.spin->setEnabled(editable);
    }

    EasingSpec currentEase;
    bool hasKeyframe = false;
    if (valid)
    {
        for (auto it = e->tracks.constBegin(); it != e->tracks.constEnd(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - m_frame) < 1e-6)
                {
                    currentEase = key.ease;
                    hasKeyframe = true;
                    break;
                }
            }
            if (hasKeyframe)
                break;
        }
    }
    m_easeButton->setEnabled(editable && hasKeyframe);
    m_easeButton->setText(hasKeyframe
                              ? QStringLiteral("缓动：")
                                    + easingDisplayName(currentEase.easing)
                              : QStringLiteral("缓动（当前帧无关键帧）"));
    if (hasKeyframe)
    {
        const double previous = m_project->previousKeyframeFrame(m_entity, m_frame);
        if (previous >= 0.0)
        {
            const int fps = std::max(1, m_project->fps);
            m_easeButton->setEnabled(editable);
            m_easeButton->setText(
                QStringLiteral("缓动 %1s → %2s：%3")
                    .arg(previous / double(fps), 0, 'f', 2)
                    .arg(m_frame / double(fps), 0, 'f', 2)
                    .arg(easingDisplayName(currentEase.easing)));
        }
        else
        {
            m_easeButton->setEnabled(false);
            m_easeButton->setText(QStringLiteral("缓动（首帧无进入段）"));
        }
    }

    if (!valid)
    {
        m_title->setText(QStringLiteral("未选择对象"));
        for (Row& r : m_rows)
        {
            const QSignalBlocker blocker(r.spin);
            r.spin->setValue(0);
        }
        return;
    }

    m_title->setText(e->name + QStringLiteral(" · ") + typeName(e->type));

    {
        const QSignalBlocker blocker(m_usePivot);
        m_usePivot->setChecked(e->rotPointCustom());
    }
    {
        const QSignalBlocker blocker(m_pivotX);
        m_pivotX->setValue(m_project->value(m_entity, QStringLiteral("rotPointX"), m_frame));
    }
    {
        const QSignalBlocker blocker(m_pivotY);
        m_pivotY->setValue(m_project->value(m_entity, QStringLiteral("rotPointY"), m_frame));
    }
    const bool pivotEnabled = e->rotPointCustom();
    m_pivotX->setEnabled(editable && pivotEnabled);
    m_pivotY->setEnabled(editable && pivotEnabled);

    if (e->type == EntityType::Audio)
    {
        const double duration = e->data.value(QStringLiteral("duration"), 0.0).toDouble();
        {
            const QSignalBlocker blocker(m_audioStart);
            m_audioStart->setValue(
                int(std::lround(e->data.value(QStringLiteral("startFrame"), 0.0).toDouble())));
        }
        {
            const QSignalBlocker blocker(m_audioTrimStart);
            const QSignalBlocker blockerEnd(m_audioTrimEnd);
            m_audioTrimStart->setRange(0.0, std::max(0.0, duration));
            m_audioTrimEnd->setRange(0.0, std::max(0.0, duration));
            m_audioTrimStart->setValue(
                e->data.value(QStringLiteral("trimStart"), 0.0).toDouble());
            m_audioTrimEnd->setValue(
                e->data.value(QStringLiteral("trimEnd"), 0.0).toDouble());
        }
        {
            const QSignalBlocker blocker(m_audioVolume);
            m_audioVolume->setValue(int(
                std::lround(e->data.value(QStringLiteral("volume"), 1.0).toDouble()
                            * 100.0)));
        }
        {
            const QSignalBlocker blockerIn(m_audioFadeIn);
            const QSignalBlocker blockerOut(m_audioFadeOut);
            m_audioFadeIn->setValue(
                e->data.value(QStringLiteral("fadeIn"), 0.0).toDouble());
            m_audioFadeOut->setValue(
                e->data.value(QStringLiteral("fadeOut"), 0.0).toDouble());
        }
    }

    for (Row& r : m_rows)
    {
        const QSignalBlocker blocker(r.spin);
        r.spin->setValue(m_project->value(m_entity, r.prop, m_frame) / r.factor);
        if (r.key)
        {
            bool hasKey = false;
            const auto track = e->tracks.constFind(r.prop);
            if (track != e->tracks.constEnd())
                for (const Keyframe& key : track->keys)
                    if (std::abs(key.frame - m_frame) < 1e-6)
                    {
                        hasKey = true;
                        break;
                    }
            r.key->setText(hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
            r.key->setToolTip(hasKey ? QStringLiteral("删除当前帧的关键帧")
                                     : QStringLiteral("在当前帧打关键帧"));
            r.key->setEnabled(editable);
        }
    }

    for (HtmlRow& r : m_htmlRows)
    {
        const QSignalBlocker blocker(r.spin);
        r.spin->setValue(m_project->value(m_entity, r.prop, m_frame));
        r.spin->setEnabled(editable);
        if (r.label)
            r.label->setEnabled(editable);
        bool hasKey = false;
        const auto track = e->tracks.constFind(r.prop);
        if (track != e->tracks.constEnd())
            for (const Keyframe& key : track->keys)
                if (std::abs(key.frame - m_frame) < 1e-6)
                {
                    hasKey = true;
                    break;
                }
        r.key->setText(hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
        r.key->setToolTip(hasKey ? QStringLiteral("删除当前帧的关键帧")
                                 : QStringLiteral("在当前帧打关键帧"));
        r.key->setEnabled(editable);
    }

    if (e->type == EntityType::Text || e->type == EntityType::ComicText)
    {
        if (!m_textContent->hasFocus())
        {
            const QSignalBlocker blocker(m_textContent);
            m_textContent->setText(e->textContent());
        }
        if (!m_textFont->hasFocus())
        {
            const QSignalBlocker blocker(m_textFont);
            m_textFont->setCurrentFont(QFont(e->fontFamily()));
        }
        {
            const QSignalBlocker blocker(m_textSize);
            m_textSize->setValue(e->fontSize());
        }
        m_textColorValue = e->textColor();
        setColorButton(m_textColorButton, m_textColorValue);
        {
            const QSignalBlocker blocker(m_textStrokeWidth);
            m_textStrokeWidth->setValue(
                e->data.value(QStringLiteral("textStrokeWidth"), 0.0).toDouble());
        }
        const QColor stroke =
            e->data.value(QStringLiteral("textStrokeColor")).value<QColor>();
        m_textStrokeColorValue = stroke.isValid() ? stroke : QColor(Qt::black);
        setColorButton(m_textStrokeColorButton, m_textStrokeColorValue);
        {
            const QSignalBlocker blocker(m_textDirection);
            const bool vertical = e->data.value(QStringLiteral("vertical"), false).toBool();
            const bool rtl = e->data.value(QStringLiteral("verticalRtl"), true).toBool();
            m_textDirection->setCurrentIndex(vertical ? (rtl ? 1 : 2) : 0);
        }
        {
            const QSignalBlocker blocker(m_textBold);
            m_textBold->setChecked(e->data.value(QStringLiteral("bold"), false).toBool());
        }
        {
            const QSignalBlocker blocker(m_textItalic);
            m_textItalic->setChecked(
                e->data.value(QStringLiteral("italic"), false).toBool());
        }
        {
            const QSignalBlocker blocker(m_textUnderline);
            m_textUnderline->setChecked(
                e->data.value(QStringLiteral("underline"), false).toBool());
        }
        {
            const QSignalBlocker blocker(m_textAlign);
            int index = 0;
            for (int i = 0; i < m_textAlign->count(); ++i)
            {
                if (m_textAlign->itemData(i).toInt() == e->textAlign())
                {
                    index = i;
                    break;
                }
            }
            m_textAlign->setCurrentIndex(index);
        }
    }
    else if (comic)
    {
        const auto select = [](QComboBox* combo, int value) {
            if (!combo)
                return;
            const QSignalBlocker blocker(combo);
            for (int index = 0; index < combo->count(); ++index)
            {
                if (combo->itemData(index).toInt() == value)
                {
                    combo->setCurrentIndex(index);
                    return;
                }
            }
        };
        select(m_comicShape, e->data.value(QStringLiteral("bubbleShape"), 0).toInt());
        {
            const QSignalBlocker blocker(m_comicTail);
            m_comicTail->setChecked(
                e->data.value(QStringLiteral("bubbleTail"), true).toBool());
        }
        const int shape = e->data.value(QStringLiteral("bubbleShape"), 0).toInt();
        const bool tailOn = e->data.value(QStringLiteral("bubbleTail"), true).toBool()
                            && shape != 3;
        for (const DataRow& row : m_dataRows)
        {
            if ((row.key == QStringLiteral("bubbleTipX")
                 || row.key == QStringLiteral("bubbleTipY"))
                && row.spin)
                row.spin->setEnabled(editable && tailOn);
        }
    }
    else if (e->type == EntityType::Vector)
    {
        {
            const QSignalBlocker blocker(m_vectorShape);
            for (int i = 0; i < m_vectorShape->count(); ++i)
            {
                if (m_vectorShape->itemData(i).toInt() == e->vectorShape())
                {
                    m_vectorShape->setCurrentIndex(i);
                    break;
                }
            }
        }
        {
            const QSignalBlocker blocker(m_vectorCorner);
            m_vectorCorner->setValue(e->cornerRadius());
        }
        {
            const QSignalBlocker blocker(m_vectorSides);
            m_vectorSides->setValue(e->vectorSides());
        }
        {
            const QSignalBlocker blocker(m_vectorInner);
            m_vectorInner->setValue(e->vectorInnerRatio());
        }
        const int shape = e->vectorShape();
        m_vectorCorner->setEnabled(editable && shape == 3);
        m_vectorSides->setEnabled(editable && (shape == 6 || shape == 7));
        m_vectorInner->setEnabled(editable && shape == 6);
        {
            const QSignalBlocker blocker(m_vectorWidth);
            m_vectorWidth->setValue(e->vectorWidth());
        }
        {
            const QSignalBlocker blocker(m_vectorHeight);
            m_vectorHeight->setValue(e->vectorHeight());
        }
        m_fillValue = e->fillColor();
        setColorButton(m_fillButton, m_fillValue);
        m_strokeValue = e->strokeColor();
        setColorButton(m_strokeButton, m_strokeValue);
        {
            const QSignalBlocker blocker(m_strokeWidth);
            m_strokeWidth->setValue(e->strokeWidth());
        }
    }
    else
    {
        QString info = typeName(e->type);
        int mediaWidth = e->image.width();
        int mediaHeight = e->image.height();
        if (e->type == EntityType::Video)
        {
            mediaWidth =
                e->data.value(QStringLiteral("sourceWidth"), mediaWidth).toInt();
            mediaHeight =
                e->data.value(QStringLiteral("sourceHeight"), mediaHeight).toInt();
        }
        if (mediaWidth > 0 && mediaHeight > 0)
            info += QStringLiteral(" · %1×%2").arg(mediaWidth).arg(mediaHeight);
        if (e->type == EntityType::Audio)
        {
            const double duration =
                e->data.value(QStringLiteral("duration"), 0.0).toDouble();
            info += QStringLiteral(" · %1 秒").arg(duration, 0, 'f', 2);
            if (e->audio)
                info += QStringLiteral(" · 48 kHz stereo");
            else
                info += QStringLiteral(" · 解码失败或未解码");
        }
        if (!e->sourcePath.isEmpty())
        {
            QString shown = e->sourcePath;
            constexpr int maxChars = 54;
            if (shown.size() > maxChars)
                shown = shown.left(8) + QStringLiteral("…") + shown.right(maxChars - 9);
            info += QStringLiteral("\n") + shown;
            m_mediaInfo->setToolTip(e->sourcePath);
        }
        else
        {
            m_mediaInfo->setToolTip(QString());
        }
        m_mediaInfo->setText(info);
    }

    // Tool specific data fields.
    if (valid)
    {
        for (const DataRow& row : m_dataRows)
        {
            const QVariant value = e->data.value(row.key);
            if (row.spin)
            {
                const QSignalBlocker blocker(row.spin);
                row.spin->setValue(value.toDouble());
            }
            else if (row.count)
            {
                const QSignalBlocker blocker(row.count);
                row.count->setValue(value.toInt());
            }
            else if (row.color)
            {
                const QColor color = value.value<QColor>();
                if (color.isValid())
                {
                    m_dataColors.insert(row.key, color);
                    setColorButton(row.color, color);
                }
            }
        }
    }
}

} // namespace pvf
