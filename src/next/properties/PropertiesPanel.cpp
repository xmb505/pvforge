#include "PropertiesPanel.h"

#include "SolverPreviewWidget.h"
#include "SpecialContentEditor.h"
#include "TextLayoutDialog.h"
#include "common/Types.h"
#include "model/Easing.h"
#include "next/timeline/EntityRange.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <utility>

namespace pvf::next {

namespace {

struct AlignOption
{
    const char* label;
    int value;
};

const AlignOption kAlignOptions[] = {
    {"左上", Qt::AlignLeft | Qt::AlignTop},
    {"上中", Qt::AlignHCenter | Qt::AlignTop},
    {"右上", Qt::AlignRight | Qt::AlignTop},
    {"左中", Qt::AlignLeft | Qt::AlignVCenter},
    {"居中", Qt::AlignHCenter | Qt::AlignVCenter},
    {"右中", Qt::AlignRight | Qt::AlignVCenter},
    {"左下", Qt::AlignLeft | Qt::AlignBottom},
    {"下中", Qt::AlignHCenter | Qt::AlignBottom},
    {"右下", Qt::AlignRight | Qt::AlignBottom},
};

struct ShapeOption
{
    const char* label;
    int value;
};

// Same shape numbering as the legacy vector editor; the new schema has not
// frozen the enum yet.
const ShapeOption kShapeOptions[] = {
    {"矩形", 0}, {"圆角矩形", 3}, {"椭圆", 1}, {"直线", 2}, {"三角形", 4},
    {"心形", 5}, {"星形", 6}, {"多边形", 7}, {"箭头", 8},
};

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
    QString tooltip;
    ParamKind kind = ParamKind::Number;
    double defaultValue = 0.0;
    double minimum = -1000000000.0;
    double maximum = 1000000000.0;
    double step = 1.0;
    int decimals = 2;
    QString suffix;
    // The spiral numbers its direction the other way round: +1 is
    // counter-clockwise there, while orbit/spin keep +1 = clockwise.
    bool ccwPositive = false;
};

ParamSpec numberParam(const QString& key, const QString& label, const QString& tooltip,
                      double defaultValue, double minimum, double maximum,
                      double step = 1.0, int decimals = 2, const QString& suffix = {})
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
    spec.tooltip = tooltip;
    spec.kind = ParamKind::Number;
    spec.defaultValue = defaultValue;
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.step = step;
    spec.decimals = decimals;
    spec.suffix = suffix;
    return spec;
}

ParamSpec uuidParam(const QString& key, const QString& label, const QString& tooltip)
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
    spec.tooltip = tooltip;
    spec.kind = ParamKind::Uuid;
    return spec;
}

ParamSpec directionParam(bool ccwPositive = false)
{
    ParamSpec spec;
    spec.key = QStringLiteral("direction");
    spec.label = QStringLiteral("方向");
    spec.kind = ParamKind::Direction;
    spec.defaultValue = 1.0;
    spec.ccwPositive = ccwPositive;
    return spec;
}

// Labels and tooltips mirror docs/MCP_TOOLS.md 5.2; they are shown next to each
// parameter so the panel is self-explanatory without the MCP guide.
QVector<ParamSpec> solverParams(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("目标实体"),
                      QStringLiteral("重力地面/方向实体；它的局部 +Y 是重力方向，目标旋转时方向跟着转。")),
            numberParam(QStringLiteral("strength"), QStringLiteral("重力强度 (px/s²)"),
                        QStringLiteral("重力加速度。数值越大下落越快；方向始终是目标实体的局部 +Y，实体旋转时重力方向跟着转。"),
                        980.0, 0.0, 1000000.0, 10.0, 2),
            numberParam(QStringLiteral("elasticity"), QStringLiteral("弹性 (0..1)"),
                        QStringLiteral("碰撞反弹保留的法向速度比例。0 = 完全不反弹，1 = 无能量损失。"),
                        0.35, 0.0, 1.0, 0.05, 3),
            numberParam(QStringLiteral("friction"), QStringLiteral("摩擦 (0..1)"),
                        QStringLiteral("碰撞时削减的切向速度比例。0 = 无摩擦、沿地面滑行；1 = 立即停止切向运动。"),
                        0.2, 0.0, 1.0, 0.05, 3),
            numberParam(QStringLiteral("stopThreshold"), QStringLiteral("停止阈值 (px/s)"),
                        QStringLiteral("速度低于该值就停止并保持当前值，避免无限微小抖动。"),
                        2.0, 0.0, 1000.0, 0.5, 2),
        };
    case SolverEffect::Orbit:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("中心实体"),
                      QStringLiteral("公转中心实体。每帧读取它当前的位置和旋转；不能是目标实体自己，也不能形成循环依赖。")),
            numberParam(QStringLiteral("aspect"), QStringLiteral("扁率 (短/长轴)"),
                        QStringLiteral("椭圆的短轴/长轴。1 = 正圆；越小轨道越扁。"),
                        1.0, 0.01, 1.0, 0.05, 3),
            numberParam(QStringLiteral("angle"), QStringLiteral("椭圆角度 (°)"),
                        QStringLiteral("椭圆长轴相对中心实体局部坐标的旋转角。"),
                        0.0, -360.0, 360.0, 1.0, 2),
            directionParam(),
            numberParam(QStringLiteral("periodFrames"), QStringLiteral("周期 (帧)"),
                        QStringLiteral("转一圈所需帧数。"),
                        60.0, 1.0, 1000000.0, 1.0, 0),
        };
    case SolverEffect::Spin:
        return {
            numberParam(QStringLiteral("angularVelocity"), QStringLiteral("角速度 (°/帧)"),
                        QStringLiteral("每帧旋转角度。30fps 下 6 = 180°/秒。"),
                        6.0, 0.0, 360.0, 0.5, 2),
            directionParam(),
        };
    case SolverEffect::BlackHole:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("黑洞实体"),
                      QStringLiteral("黑洞实体。每帧读取它的位置；不能是目标实体自己。")),
            numberParam(QStringLiteral("simulationSpeed"), QStringLiteral("模拟速度 (倍率)"),
                        QStringLiteral("模拟时间倍率。> 1 加速演化，< 1 放慢；只影响解算，不改工程帧率。"),
                        1.0, 0.01, 20.0, 0.1, 2),
            numberParam(QStringLiteral("mass"), QStringLiteral("质量"),
                        QStringLiteral("黑洞质量。越大引力越强、捕获范围越大。"),
                        1000.0, 0.0001, 1000000000.0, 10.0, 2),
            numberParam(QStringLiteral("angularMomentum"), QStringLiteral("角动量"),
                        QStringLiteral("角动量。非零时产生切向拖拽，轨迹呈旋涡；正负决定旋涡方向。"),
                        0.0, -1000000.0, 1000000.0, 10.0, 2),
            numberParam(QStringLiteral("eventHorizonRadius"), QStringLiteral("视界半径 (px)"),
                        QStringLiteral("事件视界半径，用于可视化和捕获判定参考。它不是硬边界，物体可以穿过后继续运动。"),
                        80.0, 0.0, 1000000.0, 1.0, 1),
            numberParam(QStringLiteral("objectRadius"), QStringLiteral("物体半径 (px)"),
                        QStringLiteral("运动物体半径，用于和视界/其他物体做距离判定。"),
                        40.0, 0.0, 1000000.0, 1.0, 1),
            numberParam(QStringLiteral("softening"), QStringLiteral("软化半径 (px)"),
                        QStringLiteral("引力软化半径，避免距离趋近 0 时力发散；越大中心附近越平缓。"),
                        60.0, 0.0001, 1000000.0, 1.0, 1),
            numberParam(QStringLiteral("maxSpeed"), QStringLiteral("最大速度 (px/s)"),
                        QStringLiteral("速度上限，防止数值爆炸或物体瞬间飞出画布。"),
                        4000.0, 0.0001, 1000000.0, 100.0, 1),
        };
    case SolverEffect::Spiral:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("中心实体"),
                      QStringLiteral("螺旋中心实体。每帧读取它当前位置，圆心跟着它移动；不能是目标实体自己。")),
            numberParam(QStringLiteral("turns"), QStringLiteral("圈数"),
                        QStringLiteral("从起点绕到终点一共转多少圈。速度 > 0 时按速度覆盖。"),
                        3.0, 0.0, 1000.0, 0.5, 2),
            directionParam(true),
            numberParam(QStringLiteral("endRadius"), QStringLiteral("结束半径 (px)"),
                        QStringLiteral("段末离中心的距离。0 = 直接落到中心；大于 0 = 停在外圈。半径从起点线性缩小。"),
                        0.0, 0.0, 1000000.0, 1.0, 1),
            numberParam(QStringLiteral("speedDegPerSec"),
                        QStringLiteral("速度 (°/s，覆盖圈数)"),
                        QStringLiteral("大于 0 时按该角速度换算总角度，忽略圈数；0 = 按圈数在整段内均分。"),
                        0.0, 0.0, 1000000.0, 10.0, 1),
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

void styleColorButton(QPushButton* button, const QColor& color)
{
    if (!color.isValid())
        return;
    const QColor text = color.lightnessF() > 0.55 ? QColor(0, 0, 0) : QColor(255, 255, 255);
    button->setText(color.name(QColor::HexRgb));
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2;"
                       " border: 1px solid #888; border-radius: 0; padding: 2px 6px; }")
            .arg(color.name(QColor::HexRgb), text.name(QColor::HexRgb)));
}

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

// docs/ENTITY_MODEL.md 6.2: images, video and HTML swap freely; audio only
// swaps with audio.
bool isVisualAsset(AssetType type)
{
    return type == AssetType::Image || type == AssetType::Video
        || type == AssetType::Html;
}

bool isTimedAsset(AssetType type)
{
    return type == AssetType::Video || type == AssetType::Audio;
}

bool sameAssetGroup(AssetType a, AssetType b)
{
    if (a == AssetType::Audio || b == AssetType::Audio)
        return a == b;
    return isVisualAsset(a) && isVisualAsset(b);
}

QString assetContentPath(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

// Decoded thumbnail capped at maxSize on its longest edge. Null for missing or
// non-image assets.
QPixmap assetThumbnail(const assets::StoredAsset& asset, int maxSize)
{
    if (asset.type != AssetType::Image)
        return {};
    const QString path = assetContentPath(asset);
    if (path.isEmpty())
        return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    return QPixmap::fromImage(
        image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QString assetInfoText(const assets::StoredAsset& asset)
{
    QStringList parts;
    parts.append(assetTypeLabel(asset.type));
    const int width = asset.metadata.value(QStringLiteral("width")).toInt(0);
    const int height = asset.metadata.value(QStringLiteral("height")).toInt(0);
    if (width > 0 && height > 0)
        parts.append(QStringLiteral("%1×%2").arg(width).arg(height));
    const int frames = asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
    if (frames > 0)
        parts.append(QStringLiteral("%1 帧").arg(frames));
    else
    {
        const double seconds =
            asset.metadata.value(QStringLiteral("durationSeconds")).toDouble(0.0);
        if (seconds > 0.0)
            parts.append(QStringLiteral("%1 秒").arg(seconds, 0, 'f', 2));
    }
    return parts.join(QStringLiteral(" · "));
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
// ENTITY_MODEL.md 6.4 only promises an object, so accept both shapes.
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
        const double minimum = object.value(QStringLiteral("min"))
                                   .toDouble(spec.minimum);
        const double maximum = object.value(QStringLiteral("max"))
                                   .toDouble(spec.maximum);
        if (maximum > minimum)
        {
            spec.minimum = minimum;
            spec.maximum = maximum;
        }
        const bool integer = object.value(QStringLiteral("integer")).toBool(false);
        spec.decimals = integer ? 0 : 3;
        spec.step = integer ? 1.0 : 0.01;
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("number"))
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

// Mirrors the MCP asset.assign sizing rule (src/mcp2/AssetTools.cpp applyAsset):
// timed assets clamp sourceIn into 0..frames-1 and trim the entity when the
// remaining material is shorter; images and HTML keep the entity duration.
struct AssetReplacementPlan
{
    int sourceIn = 0;
    int duration = 1;
    bool needsTrim = false;
};

AssetReplacementPlan planAssetReplacement(const model::Entity& entity,
                                          const assets::StoredAsset& asset)
{
    AssetReplacementPlan plan;
    plan.duration = entity.duration();

    if (!isTimedAsset(asset.type))
        return plan;

    if (entity.content.kind == ContentKind::Asset
        && isTimedAsset(entity.content.asset.type))
    {
        plan.sourceIn = entity.content.asset.sourceIn;
    }

    const int frames = asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
    if (frames <= 0)
        return plan;

    plan.sourceIn = qBound(0, plan.sourceIn, qMax(0, frames - 1));
    const int available = frames - plan.sourceIn;
    if (entity.duration() > available)
        plan.duration = available;
    plan.needsTrim = plan.duration != entity.duration();
    return plan;
}

void applyAssetReplacement(model::Entity& entity, const assets::StoredAsset& asset,
                           const AssetReplacementPlan& plan)
{
    if (plan.needsTrim)
    {
        entity.end = entity.start + plan.duration;
        // docs/ENTITY_MODEL.md 6.2: trimming end drops keyframes at or past the
        // new duration.
        for (auto it = entity.tracks.begin(); it != entity.tracks.end();)
        {
            QVector<model::Keyframe>& keys = it.value().keys;
            keys.erase(std::remove_if(keys.begin(), keys.end(),
                                      [duration = plan.duration](
                                          const model::Keyframe& keyframe) {
                                          return keyframe.frame >= duration;
                                      }),
                       keys.end());
            if (keys.isEmpty())
                it = entity.tracks.erase(it);
            else
                ++it;
        }

        // docs/ENTITY_MODEL.md 5.4: a trim that cuts into a solver moves its
        // end marker to the new last frame; segments entirely outside are
        // dropped. Legacy solver endFrame must stay inside the clip.
        QVector<model::SolverSegment> solvers;
        for (model::SolverSegment& solver : entity.solvers)
        {
            if (solver.startFrame >= plan.duration)
                continue;
            if (solver.endFrame != -1 && solver.endFrame >= plan.duration)
                solver.endFrame = plan.duration - 1;
            solvers.append(solver);
        }
        entity.solvers = solvers;
    }

    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = asset.hash;
    entity.content.asset.type = asset.type;
    entity.content.asset.sourceIn = plan.sourceIn;
}

// QListWidget that forwards hovered items so the asset picker can show a
// thumbnail preview without selecting the row.
class HoverListWidget : public QListWidget
{
public:
    explicit HoverListWidget(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
    }

    std::function<void(QListWidgetItem*)> hovered;

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        QListWidget::mouseMoveEvent(event);
        QListWidgetItem* item = itemAt(event->position().toPoint());
        if (item == m_hovered)
            return;
        m_hovered = item;
        if (hovered)
            hovered(item);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = nullptr;
        QListWidget::leaveEvent(event);
    }

private:
    QListWidgetItem* m_hovered = nullptr;
};

QString solverRangeText(const model::SolverSegment& solver)
{
    if (solver.endFrame < 0)
        return QStringLiteral("%1 帧 – 片段末帧").arg(solver.startFrame);
    return QStringLiteral("%1 – %2 帧").arg(solver.startFrame).arg(solver.endFrame);
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

TrackSpec numericTrack(const QString& name, const QString& label,
                       double minimum, double maximum, double singleStep,
                       int decimals, const QString& suffix = {})
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
    specs.append(numericTrack(QStringLiteral("scaleX"), QStringLiteral("缩放 X"),
                              -1000.0, 1000.0, 0.01, 4));
    specs.append(numericTrack(QStringLiteral("scaleY"), QStringLiteral("缩放 Y"),
                              -1000.0, 1000.0, 0.01, 4));
    if (camera)
        return specs;

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
                                      0.0, 1000.0, 0.05, 3));
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
        // Static special content this batch: no content keyframe tracks.
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
            if (const auto value = model::evaluateTrack(it.value(), relativeFrame, step))
                return *value;
        }
    }
    if (fallback)
        return *fallback;
    return staticTrackValue(entity, track).value_or(0.0);
}

bool entitySupportsTrack(const model::Entity& entity, const QString& track)
{
    // HTML manifest variables become numeric tracks named var.<name>.
    if (track.startsWith(QLatin1String("var.")))
    {
        return entity.content.kind == ContentKind::Asset
            && entity.content.asset.type == AssetType::Html;
    }
    for (const TrackSpec& spec : trackSpecsFor(entity))
    {
        if (spec.name == track)
            return true;
    }
    return false;
}

int solverEffectiveEnd(const model::SolverSegment& solver, int duration)
{
    return solver.endFrame < 0 ? duration - 1 : solver.endFrame;
}

QString solverConflictText(const model::SolverSegment& solver, int duration)
{
    return QStringLiteral("%1–%2（%3，%4）")
        .arg(solver.startFrame)
        .arg(solverEffectiveEnd(solver, duration))
        .arg(solverEffectName(solver.effect))
        .arg(solver.id.left(8));
}

QFormLayout* addSectionForm(QVBoxLayout* layout, QWidget* parent, const QString& title,
                            QGroupBox** group = nullptr)
{
    auto* box = new QGroupBox(title, parent);
    if (group)
        *group = box;
    auto* form = new QFormLayout(box);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(box);
    return form;
}

} // namespace

PropertiesPanel::PropertiesPanel(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("propertiesPanel"));
    setMinimumWidth(300);
    buildUi();

    if (m_document)
    {
        connect(m_document, &Document::selectionChanged, this, &PropertiesPanel::refresh);
        connect(m_document, &Document::playheadChanged, this, &PropertiesPanel::refresh);
        connect(m_document, &Document::changed, this, [this] {
            if (!m_committing)
                refresh();
        });
    }
    refresh();
}

void PropertiesPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_title = new QLabel(QStringLiteral("未选择对象"), this);
    m_title->setObjectName(QStringLiteral("panelTitle"));
    m_title->setContentsMargins(10, 8, 10, 8);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    root->addWidget(m_title);

    m_placeholder = new QLabel(QStringLiteral("在时间轴中选择一个实体来编辑属性"), this);
    m_placeholder->setObjectName(QStringLiteral("noSelectionLabel"));
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setContentsMargins(16, 16, 16, 16);
    root->addWidget(m_placeholder);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("propertiesScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget(m_scroll);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll, 1);

    buildBasicSection();
    buildTransformSection();
    buildKeyframeSection();
    buildContentSection();
    buildHtmlVariablesSection();
    buildSolverSection();
    m_contentLayout->addStretch(1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setContentsMargins(10, 4, 10, 6);
    m_status->setWordWrap(true);
    root->addWidget(m_status);
}

void PropertiesPanel::buildBasicSection()
{
    QFormLayout* form = addSectionForm(m_contentLayout, m_content, QStringLiteral("基础"));

    m_nameEdit = new QLineEdit(m_content);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    form->addRow(QStringLiteral("名称"), m_nameEdit);
    addEditable(m_nameEdit);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_loading)
            return;
        const QString text = m_nameEdit->text();
        if (text.isEmpty())
        {
            setStatus(QStringLiteral("名称不能为空"));
            refresh();
            return;
        }
        commitEdit(QStringLiteral("name"), [text](model::Entity& entity) {
            entity.name = text;
            return true;
        });
    });

    m_startSpin = new QSpinBox(m_content);
    m_startSpin->setObjectName(QStringLiteral("startSpin"));
    m_startSpin->setRange(0, 1000000);
    m_startSpin->setKeyboardTracking(false);
    form->addRow(QStringLiteral("开始帧"), m_startSpin);
    addEditable(m_startSpin);
    connect(m_startSpin, &QSpinBox::editingFinished, this, [this] {
        if (m_loading)
            return;
        const model::Entity* entity = selectedEntity();
        if (!entity)
            return;
        applyRangeEdit(m_startSpin->value(), entity->end,
                       QStringLiteral("start"));
    });

    m_endSpin = new QSpinBox(m_content);
    m_endSpin->setObjectName(QStringLiteral("endSpin"));
    m_endSpin->setRange(1, 1000000);
    m_endSpin->setKeyboardTracking(false);
    form->addRow(QStringLiteral("结束帧"), m_endSpin);
    addEditable(m_endSpin);
    connect(m_endSpin, &QSpinBox::editingFinished, this, [this] {
        if (m_loading)
            return;
        const model::Entity* entity = selectedEntity();
        if (!entity)
            return;
        applyRangeEdit(entity->start, m_endSpin->value(),
                       QStringLiteral("end"));
    });

    m_rowSpin = new QSpinBox(m_content);
    m_rowSpin->setObjectName(QStringLiteral("rowSpin"));
    m_rowSpin->setRange(0, 100000);
    m_rowSpin->setKeyboardTracking(false);
    form->addRow(QStringLiteral("行号"), m_rowSpin);
    addEditable(m_rowSpin);
    connect(m_rowSpin, &QSpinBox::editingFinished, this, [this] {
        const int value = m_rowSpin->value();
        commitEdit(QStringLiteral("row"), [value](model::Entity& entity) {
            entity.row = value;
            return true;
        });
    });

    m_hiddenCheck = new QCheckBox(QStringLiteral("编辑器隐藏"), m_content);
    m_hiddenCheck->setObjectName(QStringLiteral("hiddenCheck"));
    form->addRow(QString(), m_hiddenCheck);
    bindCheckBox(m_hiddenCheck, &model::Entity::hidden, QStringLiteral("hidden"));

    m_lockedCheck = new QCheckBox(QStringLiteral("锁定（不可编辑）"), m_content);
    m_lockedCheck->setObjectName(QStringLiteral("lockedCheck"));
    form->addRow(QString(), m_lockedCheck);
    connect(m_lockedCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_loading)
            return;
        commitEdit(QStringLiteral("locked"), [checked](model::Entity& entity) {
            entity.locked = checked;
            return true;
        }, true);
    });

    m_visibleCheck = new QCheckBox(QStringLiteral("渲染可见"), m_content);
    m_visibleCheck->setObjectName(QStringLiteral("visibleCheck"));
    form->addRow(QString(), m_visibleCheck);
    bindCheckBox(m_visibleCheck, &model::Entity::visible, QStringLiteral("visible"));

    m_inheritCheck = new QCheckBox(QStringLiteral("继承父级可见性"), m_content);
    m_inheritCheck->setObjectName(QStringLiteral("inheritVisibilityCheck"));
    form->addRow(QString(), m_inheritCheck);
    bindCheckBox(m_inheritCheck, &model::Entity::inheritVisibility,
                 QStringLiteral("inheritVisibility"));

    m_physicsCheck = new QCheckBox(QStringLiteral("参与物理碰撞"), m_content);
    m_physicsCheck->setObjectName(QStringLiteral("physicsInteractCheck"));
    form->addRow(QString(), m_physicsCheck);
    bindCheckBox(m_physicsCheck, &model::Entity::physicsInteract,
                 QStringLiteral("physicsInteract"));
}

void PropertiesPanel::buildTransformSection()
{
    QFormLayout* form = addSectionForm(m_contentLayout, m_content, QStringLiteral("变换"));

    auto makeSpin = [this, form](const QString& label, const QString& objectName,
                                 double minimum, double maximum, double step,
                                 int decimals) {
        auto* spin = new QDoubleSpinBox(m_content);
        spin->setObjectName(objectName);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setKeyboardTracking(false);
        form->addRow(label, spin);
        addEditable(spin);
        return spin;
    };

    m_xSpin = makeSpin(QStringLiteral("X (px)"), QStringLiteral("transformXSpin"),
                       -1000000000.0, 1000000000.0, 1.0, 2);
    m_ySpin = makeSpin(QStringLiteral("Y (px)"), QStringLiteral("transformYSpin"),
                       -1000000000.0, 1000000000.0, 1.0, 2);
    m_rotationSpin = makeSpin(QStringLiteral("旋转 (°)"), QStringLiteral("transformRotationSpin"),
                              -36000.0, 36000.0, 1.0, 2);
    m_scaleXSpin = makeSpin(QStringLiteral("缩放 X"), QStringLiteral("transformScaleXSpin"),
                            -1000.0, 1000.0, 0.01, 4);
    m_scaleYSpin = makeSpin(QStringLiteral("缩放 Y"), QStringLiteral("transformScaleYSpin"),
                            -1000.0, 1000.0, 0.01, 4);
    m_pivotXSpin = makeSpin(QStringLiteral("轴心 X (px)"), QStringLiteral("transformPivotXSpin"),
                            -1000000000.0, 1000000000.0, 1.0, 2);
    m_pivotYSpin = makeSpin(QStringLiteral("轴心 Y (px)"), QStringLiteral("transformPivotYSpin"),
                            -1000000000.0, 1000000000.0, 1.0, 2);
    m_opacitySpin = makeSpin(QStringLiteral("不透明度"), QStringLiteral("transformOpacitySpin"),
                             0.0, 1.0, 0.01, 3);

    bindTransform(m_xSpin, QStringLiteral("transform.x"), &model::Transform::x);
    bindTransform(m_ySpin, QStringLiteral("transform.y"), &model::Transform::y);
    bindTransform(m_rotationSpin, QStringLiteral("transform.rotation"),
                  &model::Transform::rotation);
    bindTransform(m_scaleXSpin, QStringLiteral("transform.scaleX"), &model::Transform::scaleX);
    bindTransform(m_scaleYSpin, QStringLiteral("transform.scaleY"), &model::Transform::scaleY);
    bindTransform(m_pivotXSpin, QStringLiteral("transform.pivotX"), &model::Transform::pivotX);
    bindTransform(m_pivotYSpin, QStringLiteral("transform.pivotY"), &model::Transform::pivotY);
    bindTransform(m_opacitySpin, QStringLiteral("transform.opacity"), &model::Transform::opacity);
}

void PropertiesPanel::buildKeyframeSection()
{
    auto* group = new QGroupBox(QStringLiteral("关键帧"), m_content);
    m_keyframeGroup = group;
    auto* outer = new QVBoxLayout(group);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);

    m_autoKeyCheck = new QCheckBox(QStringLiteral("自动关键帧 (Auto-key)"), group);
    m_autoKeyCheck->setObjectName(QStringLiteral("autoKeyCheck"));
    m_autoKeyCheck->setChecked(m_autoKey);
    m_autoKeyCheck->setToolTip(
        QStringLiteral("开启后，在变换或内容里改可动画属性，会在播放头处插入或更新关键帧。"));
    outer->addWidget(m_autoKeyCheck);

    auto* hint = new QLabel(
        QStringLiteral("◆ 表示当前帧该轨道已有关键帧，◇ 表示没有；点菱形可插入或删除。"
                       "播放头不在实体范围内时只能改静态值。"),
        group);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    m_keyframeGrid = new QGridLayout();
    m_keyframeGrid->setContentsMargins(0, 0, 0, 0);
    m_keyframeGrid->setHorizontalSpacing(6);
    m_keyframeGrid->setVerticalSpacing(4);
    m_keyframeGrid->setColumnStretch(2, 1);
    const QStringList headers = {QStringLiteral("轨道"), QStringLiteral("关键帧"),
                                 QStringLiteral("当前值"), QStringLiteral("缓动")};
    for (int column = 0; column < headers.size(); ++column)
    {
        auto* header = new QLabel(headers.at(column), group);
        QFont headerFont = header->font();
        headerFont.setBold(true);
        header->setFont(headerFont);
        m_keyframeGrid->addWidget(header, 0, column);
    }
    outer->addLayout(m_keyframeGrid);

    m_contentLayout->addWidget(group);

    connect(m_autoKeyCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_loading)
            setAutoKey(checked);
    });
}

void PropertiesPanel::buildContentSection()
{
    auto* group = new QGroupBox(QStringLiteral("内容"), m_content);
    m_contentGroup = group;
    auto* outer = new QVBoxLayout(group);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);
    m_contentLayout->addWidget(group);

    auto addPage = [this, outer](const QString& objectName) {
        auto* page = new QWidget(m_contentGroup);
        page->setObjectName(objectName);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        form->setVerticalSpacing(6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        outer->addWidget(page);
        page->setVisible(false);
        return form;
    };

    // empty
    QFormLayout* emptyForm = addPage(QStringLiteral("emptyPage"));
    m_emptyPage = emptyForm->parentWidget();
    auto* emptyLabel = new QLabel(
        QStringLiteral("空实体没有内容；可以当父级，也可以之后挂素材或内联内容。"),
        m_emptyPage);
    emptyLabel->setWordWrap(true);
    emptyForm->addRow(emptyLabel);

    // text
    QFormLayout* textForm = addPage(QStringLiteral("textPage"));
    m_textPage = textForm->parentWidget();

    m_textEdit = new QLineEdit(m_textPage);
    m_textEdit->setObjectName(QStringLiteral("textContentEdit"));
    textForm->addRow(QStringLiteral("文本"), m_textEdit);
    addEditable(m_textEdit);
    connect(m_textEdit, &QLineEdit::editingFinished, this, [this] {
        const QString text = m_textEdit->text();
        commitEdit(QStringLiteral("content.text.text"), [text](model::Entity& entity) {
            entity.content.text.text = text;
            return true;
        });
    });

    m_textFont = new QFontComboBox(m_textPage);
    m_textFont->setObjectName(QStringLiteral("textFontCombo"));
    // Only a dropdown pick changes the font, so one pick is one commit.
    m_textFont->setEditable(false);
    textForm->addRow(QStringLiteral("字体"), m_textFont);
    addEditable(m_textFont);
    connect(m_textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        const QString family = font.family();
        commitEdit(QStringLiteral("content.text.fontFamily"), [family](model::Entity& entity) {
            entity.content.text.fontFamily = family;
            return true;
        });
    });

    m_textPixelSize = new QSpinBox(m_textPage);
    m_textPixelSize->setObjectName(QStringLiteral("textPixelSizeSpin"));
    m_textPixelSize->setRange(1, 100000);
    m_textPixelSize->setSuffix(QStringLiteral(" px"));
    m_textPixelSize->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("字号"), m_textPixelSize);
    addEditable(m_textPixelSize);
    connect(m_textPixelSize, &QSpinBox::editingFinished, this, [this] {
        const int value = m_textPixelSize->value();
        commitEdit(QStringLiteral("content.text.pixelSize"), [this, value](model::Entity& entity) {
            entity.content.text.pixelSize = value;
            applyAutoKey(entity, QStringLiteral("fontSize"), value);
            return true;
        });
    });

    m_textColorButton = new QPushButton(m_textPage);
    m_textColorButton->setObjectName(QStringLiteral("textColorButton"));
    textForm->addRow(QStringLiteral("颜色"), m_textColorButton);
    addEditable(m_textColorButton);
    connect(m_textColorButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_textColorValue, this, QStringLiteral("文字颜色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.text.color"), [color](model::Entity& entity) {
            entity.content.text.color = color;
            return true;
        });
    });

    m_textAlign = new QComboBox(m_textPage);
    m_textAlign->setObjectName(QStringLiteral("textAlignCombo"));
    for (const AlignOption& option : kAlignOptions)
        m_textAlign->addItem(QString::fromUtf8(option.label), option.value);
    textForm->addRow(QStringLiteral("对齐"), m_textAlign);
    addEditable(m_textAlign);
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_textAlign->currentData().toInt();
        commitEdit(QStringLiteral("content.text.align"), [value](model::Entity& entity) {
            entity.content.text.align = value;
            return true;
        });
    });

    m_textLineHeight = new QDoubleSpinBox(m_textPage);
    m_textLineHeight->setObjectName(QStringLiteral("textLineHeightSpin"));
    m_textLineHeight->setRange(0.01, 100.0);
    m_textLineHeight->setDecimals(3);
    m_textLineHeight->setSingleStep(0.1);
    m_textLineHeight->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("行高"), m_textLineHeight);
    addEditable(m_textLineHeight);
    connect(m_textLineHeight, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textLineHeight->value();
        commitEdit(QStringLiteral("content.text.lineHeight"), [this, value](model::Entity& entity) {
            entity.content.text.lineHeight = value;
            applyAutoKey(entity, QStringLiteral("lineHeight"), value);
            return true;
        });
    });

    m_textLetterSpacing = new QDoubleSpinBox(m_textPage);
    m_textLetterSpacing->setObjectName(QStringLiteral("textLetterSpacingSpin"));
    m_textLetterSpacing->setRange(-10000.0, 10000.0);
    m_textLetterSpacing->setDecimals(2);
    m_textLetterSpacing->setSingleStep(1.0);
    m_textLetterSpacing->setSuffix(QStringLiteral(" px"));
    m_textLetterSpacing->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("字距"), m_textLetterSpacing);
    addEditable(m_textLetterSpacing);
    connect(m_textLetterSpacing, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textLetterSpacing->value();
        commitEdit(QStringLiteral("content.text.letterSpacing"), [this, value](model::Entity& entity) {
            entity.content.text.letterSpacing = value;
            applyAutoKey(entity, QStringLiteral("letterSpacing"), value);
            return true;
        });
    });

    m_textStrokeWidth = new QDoubleSpinBox(m_textPage);
    m_textStrokeWidth->setObjectName(QStringLiteral("textStrokeWidthSpin"));
    m_textStrokeWidth->setRange(0.0, 10000.0);
    m_textStrokeWidth->setDecimals(2);
    m_textStrokeWidth->setSingleStep(0.5);
    m_textStrokeWidth->setSuffix(QStringLiteral(" px"));
    m_textStrokeWidth->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("描边宽度"), m_textStrokeWidth);
    addEditable(m_textStrokeWidth);
    connect(m_textStrokeWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textStrokeWidth->value();
        commitEdit(QStringLiteral("content.text.strokeWidth"), [this, value](model::Entity& entity) {
            entity.content.text.strokeWidth = value;
            applyAutoKey(entity, QStringLiteral("strokeWidth"), value);
            return true;
        });
    });

    m_textLayoutButton = new QPushButton(QStringLiteral("排版…"), m_textPage);
    m_textLayoutButton->setObjectName(QStringLiteral("textLayoutButton"));
    m_textLayoutButton->setToolTip(
        QStringLiteral("打开文字排版：多行文本、字体、描边、粗体/斜体/下划线、"
                       "对齐、行高/字距/内边距、框宽高、竖排，并实时预览。"
                       "确定后一次性写回。"));
    textForm->addRow(m_textLayoutButton);
    addEditable(m_textLayoutButton);
    connect(m_textLayoutButton, &QPushButton::clicked, this,
            &PropertiesPanel::openTextLayoutDialog);

    // vector
    QFormLayout* vectorForm = addPage(QStringLiteral("vectorPage"));
    m_vectorPage = vectorForm->parentWidget();

    m_vectorShape = new QComboBox(m_vectorPage);
    m_vectorShape->setObjectName(QStringLiteral("vectorShapeCombo"));
    for (const ShapeOption& option : kShapeOptions)
        m_vectorShape->addItem(QString::fromUtf8(option.label), option.value);
    vectorForm->addRow(QStringLiteral("形状"), m_vectorShape);
    addEditable(m_vectorShape);
    connect(m_vectorShape, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_vectorShape->currentData().toInt();
        commitEdit(QStringLiteral("content.vector.shape"), [value](model::Entity& entity) {
            entity.content.vector.shape = value;
            return true;
        });
    });

    m_vectorWidth = new QDoubleSpinBox(m_vectorPage);
    m_vectorWidth->setObjectName(QStringLiteral("vectorWidthSpin"));
    m_vectorWidth->setRange(0.0, 1000000000.0);
    m_vectorWidth->setDecimals(2);
    m_vectorWidth->setSingleStep(1.0);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    m_vectorWidth->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("宽度"), m_vectorWidth);
    addEditable(m_vectorWidth);
    connect(m_vectorWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorWidth->value();
        commitEdit(QStringLiteral("content.vector.width"), [this, value](model::Entity& entity) {
            entity.content.vector.width = value;
            applyAutoKey(entity, QStringLiteral("width"), value);
            return true;
        });
    });

    m_vectorHeight = new QDoubleSpinBox(m_vectorPage);
    m_vectorHeight->setObjectName(QStringLiteral("vectorHeightSpin"));
    m_vectorHeight->setRange(0.0, 1000000000.0);
    m_vectorHeight->setDecimals(2);
    m_vectorHeight->setSingleStep(1.0);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    m_vectorHeight->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("高度"), m_vectorHeight);
    addEditable(m_vectorHeight);
    connect(m_vectorHeight, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorHeight->value();
        commitEdit(QStringLiteral("content.vector.height"), [this, value](model::Entity& entity) {
            entity.content.vector.height = value;
            applyAutoKey(entity, QStringLiteral("height"), value);
            return true;
        });
    });

    m_vectorCorner = new QDoubleSpinBox(m_vectorPage);
    m_vectorCorner->setObjectName(QStringLiteral("vectorCornerRadiusSpin"));
    m_vectorCorner->setRange(0.0, 1000000000.0);
    m_vectorCorner->setDecimals(2);
    m_vectorCorner->setSingleStep(1.0);
    m_vectorCorner->setSuffix(QStringLiteral(" px"));
    m_vectorCorner->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("圆角"), m_vectorCorner);
    addEditable(m_vectorCorner);
    connect(m_vectorCorner, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorCorner->value();
        commitEdit(QStringLiteral("content.vector.cornerRadius"), [this, value](model::Entity& entity) {
            entity.content.vector.cornerRadius = value;
            applyAutoKey(entity, QStringLiteral("cornerRadius"), value);
            return true;
        });
    });

    m_vectorFillButton = new QPushButton(m_vectorPage);
    m_vectorFillButton->setObjectName(QStringLiteral("vectorFillButton"));
    vectorForm->addRow(QStringLiteral("填充色"), m_vectorFillButton);
    addEditable(m_vectorFillButton);
    connect(m_vectorFillButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_vectorFillValue, this, QStringLiteral("矢量填充色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.vector.fillColor"), [color](model::Entity& entity) {
            entity.content.vector.fillColor = color;
            return true;
        });
    });

    m_vectorStrokeButton = new QPushButton(m_vectorPage);
    m_vectorStrokeButton->setObjectName(QStringLiteral("vectorStrokeColorButton"));
    vectorForm->addRow(QStringLiteral("描边色"), m_vectorStrokeButton);
    addEditable(m_vectorStrokeButton);
    connect(m_vectorStrokeButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_vectorStrokeValue, this, QStringLiteral("矢量描边色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.vector.strokeColor"), [color](model::Entity& entity) {
            entity.content.vector.strokeColor = color;
            return true;
        });
    });

    m_vectorStrokeWidth = new QDoubleSpinBox(m_vectorPage);
    m_vectorStrokeWidth->setObjectName(QStringLiteral("vectorStrokeWidthSpin"));
    m_vectorStrokeWidth->setRange(0.0, 1000000000.0);
    m_vectorStrokeWidth->setDecimals(2);
    m_vectorStrokeWidth->setSingleStep(0.5);
    m_vectorStrokeWidth->setSuffix(QStringLiteral(" px"));
    m_vectorStrokeWidth->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("描边宽度"), m_vectorStrokeWidth);
    addEditable(m_vectorStrokeWidth);
    connect(m_vectorStrokeWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorStrokeWidth->value();
        commitEdit(QStringLiteral("content.vector.strokeWidth"), [this, value](model::Entity& entity) {
            entity.content.vector.strokeWidth = value;
            applyAutoKey(entity, QStringLiteral("strokeWidth"), value);
            return true;
        });
    });

    // asset
    QFormLayout* assetForm = addPage(QStringLiteral("assetPage"));
    m_assetPage = assetForm->parentWidget();

    m_assetHash = new QLineEdit(m_assetPage);
    m_assetHash->setObjectName(QStringLiteral("assetHashEdit"));
    m_assetHash->setReadOnly(true);
    assetForm->addRow(QStringLiteral("素材 hash"), m_assetHash);

    m_assetPreview = new QLabel(m_assetPage);
    m_assetPreview->setObjectName(QStringLiteral("assetPreviewLabel"));
    m_assetPreview->setAlignment(Qt::AlignCenter);
    m_assetPreview->setWordWrap(true);
    m_assetPreview->setVisible(false);
    assetForm->addRow(QStringLiteral("预览"), m_assetPreview);

    m_assetType = new QComboBox(m_assetPage);
    m_assetType->setObjectName(QStringLiteral("assetTypeCombo"));
    m_assetType->addItem(QStringLiteral("图片"), int(AssetType::Image));
    m_assetType->addItem(QStringLiteral("视频"), int(AssetType::Video));
    m_assetType->addItem(QStringLiteral("音频"), int(AssetType::Audio));
    m_assetType->addItem(QStringLiteral("HTML"), int(AssetType::Html));
    assetForm->addRow(QStringLiteral("素材类型"), m_assetType);
    addEditable(m_assetType);
    connect(m_assetType, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_assetType->currentData().toInt();
        commitEdit(QStringLiteral("content.asset.assetType"), [value](model::Entity& entity) {
            entity.content.asset.type = AssetType(value);
            return true;
        });
    });

    m_assetSourceIn = new QSpinBox(m_assetPage);
    m_assetSourceIn->setObjectName(QStringLiteral("assetSourceInSpin"));
    m_assetSourceIn->setRange(0, 1000000);
    m_assetSourceIn->setSuffix(QStringLiteral(" 帧"));
    m_assetSourceIn->setKeyboardTracking(false);
    assetForm->addRow(QStringLiteral("源起点"), m_assetSourceIn);
    addEditable(m_assetSourceIn);
    connect(m_assetSourceIn, &QSpinBox::editingFinished, this, [this] {
        const int value = m_assetSourceIn->value();
        commitEdit(QStringLiteral("content.asset.sourceIn"), [value](model::Entity& entity) {
            entity.content.asset.sourceIn = value;
            return true;
        });
    });

    m_assetVolume = new QDoubleSpinBox(m_assetPage);
    m_assetVolume->setObjectName(QStringLiteral("assetVolumeSpin"));
    m_assetVolume->setRange(0.0, 1000.0);
    m_assetVolume->setDecimals(3);
    m_assetVolume->setSingleStep(0.05);
    m_assetVolume->setKeyboardTracking(false);
    assetForm->addRow(QStringLiteral("音量"), m_assetVolume);
    addEditable(m_assetVolume);
    connect(m_assetVolume, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_assetVolume->value();
        commitEdit(QStringLiteral("content.asset.volume"), [this, value](model::Entity& entity) {
            entity.content.asset.volume = value;
            applyAutoKey(entity, QStringLiteral("volume"), value);
            return true;
        });
    });

    m_assetReplaceButton = new QPushButton(QStringLiteral("更换素材…"), m_assetPage);
    m_assetReplaceButton->setObjectName(QStringLiteral("assetReplaceButton"));
    m_assetReplaceButton->setToolTip(
        QStringLiteral("从素材箱里选一个同组素材（视觉：图片/视频/HTML；音频：音频）。"
                       "视频/音频比实体短时，实体结束帧会按素材长度裁短。"));
    assetForm->addRow(m_assetReplaceButton);
    addEditable(m_assetReplaceButton);
    connect(m_assetReplaceButton, &QPushButton::clicked, this,
            &PropertiesPanel::chooseReplacementAsset);

    // camera
    QFormLayout* cameraForm = addPage(QStringLiteral("cameraPage"));
    m_cameraPage = cameraForm->parentWidget();
    auto* cameraHint = new QLabel(
        QStringLiteral("摄像机使用 transform 取景：x/y 是取景框中心，scaleX/scaleY 是画布占比"
                       "（0.05..1），rotation 是旋转。"),
        m_cameraPage);
    cameraHint->setWordWrap(true);
    cameraForm->addRow(cameraHint);

    // Static special content (comicText / radialLines / flowLines /
    // halftoneDots / brandWall).  One page per kind is inside the editor, so
    // this is a single content page here.
    m_specialEditor = new SpecialContentEditor(m_contentGroup);
    m_specialEditor->setObjectName(QStringLiteral("specialContentPage"));
    m_specialEditor->setVisible(false);
    outer->addWidget(m_specialEditor);
    addEditable(m_specialEditor);
    connect(m_specialEditor, &SpecialContentEditor::edited, this,
            &PropertiesPanel::commitSpecialContentEdit);
}

void PropertiesPanel::buildHtmlVariablesSection()
{
    auto* group = new QGroupBox(QStringLiteral("HTML 变量"), m_content);
    m_htmlGroup = group;
    auto* outer = new QVBoxLayout(group);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);

    m_htmlStatus = new QLabel(group);
    m_htmlStatus->setObjectName(QStringLiteral("htmlTimeDependentLabel"));
    m_htmlStatus->setWordWrap(true);
    m_htmlStatus->setToolTip(
        QStringLiteral("timeDependent=true 的 HTML 每帧渲染；false 时可以只缓存一张图。"
                       "变量值来自 .pvhtml manifest，轨道名是 var.<变量名>。"));
    outer->addWidget(m_htmlStatus);

    m_htmlEmpty = new QLabel(QStringLiteral("（manifest 未声明变量）"), group);
    m_htmlEmpty->setObjectName(QStringLiteral("htmlVariablesEmptyLabel"));
    m_htmlEmpty->setWordWrap(true);
    outer->addWidget(m_htmlEmpty);

    m_htmlGrid = new QGridLayout();
    m_htmlGrid->setContentsMargins(0, 0, 0, 0);
    m_htmlGrid->setHorizontalSpacing(6);
    m_htmlGrid->setVerticalSpacing(4);
    m_htmlGrid->setColumnStretch(2, 1);
    const QStringList headers = {QStringLiteral("变量"), QStringLiteral("关键帧"),
                                 QStringLiteral("当前值")};
    for (int column = 0; column < headers.size(); ++column)
    {
        auto* header = new QLabel(headers.at(column), group);
        QFont headerFont = header->font();
        headerFont.setBold(true);
        header->setFont(headerFont);
        m_htmlGrid->addWidget(header, 0, column);
    }
    outer->addLayout(m_htmlGrid);

    group->setVisible(false);
    m_contentLayout->addWidget(group);
}

void PropertiesPanel::buildSolverSection()
{
    auto* group = new QGroupBox(QStringLiteral("求解段"), m_content);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto* createBox = new QGroupBox(QStringLiteral("新建求解段"), group);
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
    m_solverEffectCombo->addItem(QStringLiteral("螺旋"), int(SolverEffect::Spiral));
    createForm->addRow(QStringLiteral("效果"), m_solverEffectCombo);

    m_solverTargetCombo = new QComboBox(createBox);
    m_solverTargetCombo->setObjectName(QStringLiteral("solverTargetCombo"));
    m_solverTargetCombo->setToolTip(
        QStringLiteral("重力地面、公转中心、黑洞或螺旋中心实体；不能是当前实体自己。自转不需要目标。"));
    createForm->addRow(QStringLiteral("目标实体"), m_solverTargetCombo);

    m_solverEndSpin = new QSpinBox(createBox);
    m_solverEndSpin->setObjectName(QStringLiteral("solverCreateEnd"));
    m_solverEndSpin->setRange(-1, 1000000);
    m_solverEndSpin->setSpecialValueText(QStringLiteral("片段末帧"));
    m_solverEndSpin->setValue(-1);
    m_solverEndSpin->setKeyboardTracking(false);
    m_solverEndSpin->setToolTip(
        QStringLiteral("求解结束帧是停止标记，不存值；-1 表示一直解到片段最后一帧。"));
    createForm->addRow(QStringLiteral("结束帧"), m_solverEndSpin);

    m_solverCreateButton = new QPushButton(
        QStringLiteral("将当前关键帧转换为求解开始帧"), createBox);
    m_solverCreateButton->setObjectName(QStringLiteral("solverCreateButton"));
    m_solverCreateButton->setToolTip(
        QStringLiteral("开始帧取播放头相对实体 start 的帧号；该帧必须已有普通关键帧。"));
    createForm->addRow(m_solverCreateButton);

    layout->addWidget(createBox);

    m_solverEmpty = new QLabel(QStringLiteral("（没有求解段）"), group);
    m_solverEmpty->setObjectName(QStringLiteral("solverEmptyLabel"));
    layout->addWidget(m_solverEmpty);

    m_solverHost = new QWidget(group);
    m_solverCardsLayout = new QVBoxLayout(m_solverHost);
    m_solverCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_solverCardsLayout->setSpacing(6);
    layout->addWidget(m_solverHost);

    m_contentLayout->addWidget(group);

    connect(m_solverEffectCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applySolverEnabledState();
    });
    connect(m_solverCreateButton, &QPushButton::clicked, this,
            &PropertiesPanel::createSolverFromPlayhead);
}

void PropertiesPanel::reloadBasic(const model::Entity& entity)
{
    m_nameEdit->setText(entity.name);
    m_startSpin->setValue(entity.start);
    m_endSpin->setValue(entity.end);
    m_rowSpin->setValue(entity.row);
    m_hiddenCheck->setChecked(entity.hidden);
    m_lockedCheck->setChecked(entity.locked);
    m_visibleCheck->setChecked(entity.visible);
    m_inheritCheck->setChecked(entity.inheritVisibility);
    m_physicsCheck->setChecked(entity.physicsInteract);
}

void PropertiesPanel::reloadTransform(const model::Entity& entity)
{
    const model::Transform& transform = entity.transform;
    m_xSpin->setValue(transform.x);
    m_ySpin->setValue(transform.y);
    m_rotationSpin->setValue(transform.rotation);
    m_scaleXSpin->setValue(transform.scaleX);
    m_scaleYSpin->setValue(transform.scaleY);
    m_pivotXSpin->setValue(transform.pivotX);
    m_pivotYSpin->setValue(transform.pivotY);
    m_opacitySpin->setValue(transform.opacity);
}

void PropertiesPanel::rebuildKeyframeRows(const model::Entity& entity)
{
    for (KeyframeRow& row : m_keyframeRows)
    {
        delete row.name;
        delete row.diamond;
        delete row.value;
        delete row.easing;
    }
    m_keyframeRows.clear();
    m_keyframeUuid = entity.uuid;

    const QVector<TrackSpec> specs = trackSpecsFor(entity);
    int gridRow = 1;
    for (const TrackSpec& spec : specs)
    {
        KeyframeRow row;
        row.track = spec.name;
        row.step = spec.step;

        row.name = new QLabel(spec.label, m_keyframeGroup);
        row.name->setObjectName(QStringLiteral("keyLabel_%1").arg(spec.name));
        m_keyframeGrid->addWidget(row.name, gridRow, 0);

        row.diamond = new QPushButton(QStringLiteral("◇"), m_keyframeGroup);
        row.diamond->setObjectName(QStringLiteral("keyDiamond_%1").arg(spec.name));
        row.diamond->setFixedWidth(28);
        row.diamond->setToolTip(QStringLiteral("在播放头处插入或删除关键帧"));
        m_keyframeGrid->addWidget(row.diamond, gridRow, 1);

        row.value = new QDoubleSpinBox(m_keyframeGroup);
        row.value->setObjectName(QStringLiteral("keyValue_%1").arg(spec.name));
        row.value->setRange(spec.minimum, spec.maximum);
        row.value->setDecimals(spec.decimals);
        row.value->setSingleStep(spec.singleStep);
        if (!spec.suffix.isEmpty())
            row.value->setSuffix(spec.suffix);
        row.value->setKeyboardTracking(false);
        m_keyframeGrid->addWidget(row.value, gridRow, 2);

        row.easing = new QComboBox(m_keyframeGroup);
        row.easing->setObjectName(QStringLiteral("keyEasing_%1").arg(spec.name));
        // Presets use their canonical lowercase JSON names as labels; the list
        // order follows model::easingPresetNames(). bezier opens the custom
        // handle editor instead of saving a preset.
        row.easing->addItem(QStringLiteral("linear"), QStringLiteral("linear"));
        row.easing->setItemData(0, QStringLiteral("线性：匀速变化。"), Qt::ToolTipRole);
        row.easing->addItem(QStringLiteral("instant"), QStringLiteral("instant"));
        row.easing->setItemData(1, QStringLiteral("立即切换：保持当前值直到下一个关键帧。"),
                                Qt::ToolTipRole);
        const QStringList& presets = model::easingPresetNames();
        for (int i = 0; i < presets.size(); ++i)
        {
            row.easing->addItem(presets.at(i), presets.at(i));
            row.easing->setItemData(row.easing->count() - 1,
                                    QStringLiteral("预设缓动曲线，名称即工程 JSON 里的规范名。"),
                                    Qt::ToolTipRole);
        }
        row.easing->addItem(QStringLiteral("bezier"), QStringLiteral("bezier"));
        row.easing->setItemData(row.easing->count() - 1,
                                QStringLiteral("打开自定义贝塞尔编辑器，调整四个控制手柄。"),
                                Qt::ToolTipRole);
        if (spec.step)
            row.easing->setToolTip(QStringLiteral("可见/物理干涉使用 step 插值，不需要缓动。"));
        else
            row.easing->setToolTip(
                QStringLiteral("设置当前帧关键帧到下一个关键帧之间的缓动。"
                               "预设项使用英文规范名；bezier 打开自定义编辑器。"));
        m_keyframeGrid->addWidget(row.easing, gridRow, 3);

        const QString track = spec.name;
        connect(row.diamond, &QPushButton::clicked, this, [this, track] {
            toggleKeyframe(track);
        });
        connect(row.value, &QDoubleSpinBox::editingFinished, this,
                [this, track, spin = row.value] {
                    if (m_loading)
                        return;
                    editKeyframeValue(track, spin->value());
                });
        connect(row.easing, &QComboBox::currentIndexChanged, this,
                [this, track, combo = row.easing](int) {
                    if (m_loading)
                        return;
                    const QString type = combo->currentData().toString();
                    if (type == QLatin1String("bezier"))
                    {
                        promptCustomEasing(track);
                        return;
                    }
                    model::EasingSpec easing;
                    easing.type = model::canonicalEasingName(type);
                    setTrackEasing(track, easing);
                });

        m_keyframeRows.append(row);
        ++gridRow;
    }
}

void PropertiesPanel::reloadKeyframes(const model::Entity& entity)
{
    const QVector<TrackSpec> specs = trackSpecsFor(entity);
    bool sameStructure = entity.uuid == m_keyframeUuid
        && m_keyframeRows.size() == specs.size();
    for (int i = 0; sameStructure && i < specs.size(); ++i)
    {
        sameStructure = m_keyframeRows.at(i).track == specs.at(i).name
            && m_keyframeRows.at(i).step == specs.at(i).step;
    }
    if (!sameStructure)
        rebuildKeyframeRows(entity);

    const int relativeFrame = m_document ? m_document->playhead() - entity.start : -1;
    const bool inRange = relativeFrame >= 0 && relativeFrame < entity.duration();

    for (int i = 0; i < m_keyframeRows.size() && i < specs.size(); ++i)
    {
        KeyframeRow& row = m_keyframeRows[i];
        const TrackSpec& spec = specs.at(i);
        row.hasKey = inRange && hasKeyAt(entity, row.track, relativeFrame);

        row.diamond->setText(row.hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
        if (!inRange)
        {
            row.diamond->setToolTip(
                QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                    .arg(relativeFrame)
                    .arg(entity.duration() - 1));
        }
        else if (row.hasKey)
        {
            row.diamond->setToolTip(
                QStringLiteral("删除当前帧（%1）的「%2」关键帧")
                    .arg(relativeFrame)
                    .arg(spec.label));
        }
        else
        {
            row.diamond->setToolTip(
                QStringLiteral("在当前帧（%1）插入「%2」关键帧")
                    .arg(relativeFrame)
                    .arg(spec.label));
        }

        {
            QSignalBlocker blocker(row.value);
            row.value->setValue(
                currentTrackValue(entity, row.track, relativeFrame, spec.step));
        }

        {
            QSignalBlocker blocker(row.easing);
            if (row.hasKey && !spec.step)
            {
                QString type = QStringLiteral("linear");
                const auto track = entity.tracks.constFind(row.track);
                if (track != entity.tracks.constEnd())
                {
                    for (const model::Keyframe& keyframe : track.value().keys)
                    {
                        if (keyframe.frame == relativeFrame)
                        {
                            // Legacy aliases (easeIn/easeOut/easeInOut/step) and
                            // any casing map onto the canonical preset item.
                            type = model::canonicalEasingName(keyframe.easing.type);
                            break;
                        }
                    }
                }
                int index = row.easing->findData(type);
                if (index < 0)
                {
                    row.easing->addItem(type, type);
                    index = row.easing->count() - 1;
                }
                row.easing->setCurrentIndex(index);
            }
            else
            {
                row.easing->setCurrentIndex(-1);
            }
        }
    }

    applyKeyframeEnabledState();
}

QPixmap PropertiesPanel::cachedAssetThumbnail(const AssetHash& hash,
                                             const assets::StoredAsset& asset)
{
    const auto cached = m_assetThumbnails.constFind(hash);
    if (cached != m_assetThumbnails.constEnd())
        return *cached;
    const QPixmap pixmap = assetThumbnail(asset, 320);
    m_assetThumbnails.insert(hash, pixmap);
    return pixmap;
}

void PropertiesPanel::reloadContent(const model::Entity& entity)
{
    const model::Content& content = entity.content;
    const ContentKind kind = content.kind;
    m_contentGroup->setTitle(QStringLiteral("内容 · %1").arg(contentKindLabel(kind)));

    m_emptyPage->setVisible(kind == ContentKind::Empty);
    m_textPage->setVisible(kind == ContentKind::Text);
    m_vectorPage->setVisible(kind == ContentKind::Vector);
    m_assetPage->setVisible(kind == ContentKind::Asset);
    m_cameraPage->setVisible(kind == ContentKind::Camera);
    const bool special = SpecialContentEditor::handles(kind);
    m_specialEditor->setVisible(special);
    if (special)
        m_specialEditor->setContent(content);

    const model::TextContent& text = content.text;
    m_textEdit->setText(text.text);
    m_textFont->setCurrentFont(text.fontFamily.isEmpty() ? QFont() : QFont(text.fontFamily));
    m_textPixelSize->setValue(text.pixelSize);
    m_textColorValue = QColor(text.color);
    styleColorButton(m_textColorButton, m_textColorValue);
    selectComboData(m_textAlign, text.align);
    m_textLineHeight->setValue(text.lineHeight);
    m_textLetterSpacing->setValue(text.letterSpacing);
    m_textStrokeWidth->setValue(text.strokeWidth);

    const model::VectorContent& vector = content.vector;
    selectComboData(m_vectorShape, vector.shape);
    m_vectorWidth->setValue(vector.width);
    m_vectorHeight->setValue(vector.height);
    m_vectorCorner->setValue(vector.cornerRadius);
    m_vectorFillValue = QColor(vector.fillColor);
    m_vectorStrokeValue = QColor(vector.strokeColor);
    styleColorButton(m_vectorFillButton, m_vectorFillValue);
    styleColorButton(m_vectorStrokeButton, m_vectorStrokeValue);
    m_vectorStrokeWidth->setValue(vector.strokeWidth);

    const model::AssetContent& asset = content.asset;
    m_assetHash->setText(asset.hash);
    selectComboData(m_assetType, int(asset.type));
    m_assetSourceIn->setValue(asset.sourceIn);
    m_assetVolume->setValue(asset.volume);

    const std::optional<assets::StoredAsset> stored =
        m_document ? m_document->assets().get(asset.hash) : std::nullopt;
    m_assetMissing = !stored;
    if (stored && stored->type == AssetType::Image)
    {
        QPixmap thumbnail = cachedAssetThumbnail(asset.hash, *stored);
        if (thumbnail.width() > 280 || thumbnail.height() > 280)
        {
            thumbnail = thumbnail.scaled(280, 280, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        }
        m_assetPreview->setPixmap(thumbnail);
        m_assetPreview->setText(thumbnail.isNull()
                                    ? QStringLiteral("图片无法解码")
                                    : QString());
        m_assetPreview->setToolTip(stored->name);
        m_assetPreview->setVisible(true);
    }
    else
    {
        m_assetPreview->clear();
        m_assetPreview->setToolTip(QString());
        m_assetPreview->setVisible(false);
    }
}

void PropertiesPanel::reloadHtmlVariables(const model::Entity& entity)
{
    const bool isHtml = entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Html;
    m_htmlGroup->setVisible(isHtml);
    if (!isHtml)
    {
        m_htmlUuid.clear();
        m_htmlHash.clear();
        m_htmlVariableDefs = QJsonObject();
        return;
    }

    const AssetHash hash = entity.content.asset.hash;
    if (entity.uuid != m_htmlUuid || hash != m_htmlHash)
    {
        m_htmlUuid = entity.uuid;
        m_htmlHash = hash;
        m_htmlVariableDefs = QJsonObject();
        m_htmlAssetPresent = false;
        if (m_document)
        {
            if (const auto stored = m_document->assets().get(hash))
            {
                m_htmlAssetPresent = true;
                m_htmlTimeDependent = stored->metadata
                                          .value(QStringLiteral("timeDependent"))
                                          .toBool(true);
                m_htmlVariableDefs = stored->metadata
                                         .value(QStringLiteral("variables"))
                                         .toObject();
            }
        }
        rebuildHtmlVariableRows();
    }

    if (!m_htmlAssetPresent)
    {
        m_htmlStatus->setText(QStringLiteral("timeDependent：未知（素材缺失）"));
        m_htmlEmpty->setText(QStringLiteral("素材缺失，无法读取 manifest 变量。"));
        m_htmlEmpty->setVisible(true);
    }
    else
    {
        m_htmlStatus->setText(
            QStringLiteral("timeDependent：%1")
                .arg(m_htmlTimeDependent ? QStringLiteral("是（每帧渲染）")
                                         : QStringLiteral("否（可静态缓存）")));
        m_htmlEmpty->setText(QStringLiteral("（manifest 未声明变量）"));
        m_htmlEmpty->setVisible(m_htmlRows.isEmpty());
    }

    const int relativeFrame = m_document ? m_document->playhead() - entity.start : -1;
    const int duration = entity.duration();
    const bool inRange = relativeFrame >= 0 && relativeFrame < duration;
    const int sampleFrame = qBound(0, relativeFrame, qMax(0, duration - 1));

    for (HtmlVariableRow& row : m_htmlRows)
    {
        row.inRange = inRange;
        const auto trackIt = entity.tracks.constFind(row.track);
        if (!row.numeric)
        {
            QString text = row.readOnlyText;
            if (trackIt != entity.tracks.constEnd())
            {
                if (const auto value =
                        model::evaluateTrack(trackIt.value(), sampleFrame, false))
                {
                    text = QString::number(*value, 'g', 8);
                }
            }
            row.readOnlyValue->setText(text);
            continue;
        }

        double value = row.defaultValue;
        if (trackIt != entity.tracks.constEnd())
        {
            if (const auto evaluated =
                    model::evaluateTrack(trackIt.value(), sampleFrame, false))
            {
                value = *evaluated;
            }
        }

        const bool hasKey = inRange && hasKeyAt(entity, row.track, relativeFrame);
        {
            QSignalBlocker blocker(row.value);
            row.value->setValue(value);
        }
        row.diamond->setText(hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
        if (!inRange)
        {
            row.diamond->setToolTip(
                QStringLiteral("播放头不在实体范围内（相对帧 %1），不能打关键帧")
                    .arg(relativeFrame));
        }
        else if (hasKey)
        {
            row.diamond->setToolTip(
                QStringLiteral("删除当前帧（%1）的「%2」关键帧")
                    .arg(relativeFrame)
                    .arg(row.name));
        }
        else
        {
            row.diamond->setToolTip(
                QStringLiteral("在当前帧（%1）给「%2」打关键帧，初值为 manifest 默认 %3")
                    .arg(relativeFrame)
                    .arg(row.name)
                    .arg(row.defaultValue));
        }
    }

    applyKeyframeEnabledState();
}

void PropertiesPanel::rebuildHtmlVariableRows()
{
    // Clear every row below the header; widgets are owned by the grid.
    for (int row = m_htmlGrid->rowCount() - 1; row >= 1; --row)
    {
        for (int column = 0; column < m_htmlGrid->columnCount(); ++column)
        {
            QLayoutItem* item = m_htmlGrid->itemAtPosition(row, column);
            if (!item)
                continue;
            m_htmlGrid->removeItem(item);
            delete item->widget();
            delete item;
        }
    }
    m_htmlRows.clear();

    int gridRow = 1;
    for (auto it = m_htmlVariableDefs.constBegin();
         it != m_htmlVariableDefs.constEnd(); ++it)
    {
        const HtmlVarSpec spec = htmlVarSpec(it.key(), it.value());
        HtmlVariableRow row;
        row.name = spec.name;
        row.track = spec.track;
        row.numeric = spec.numeric;
        row.defaultValue = spec.defaultValue;
        row.minimum = spec.minimum;
        row.maximum = spec.maximum;
        row.step = spec.step;
        row.decimals = spec.decimals;
        row.readOnlyText = spec.text;

        auto* nameLabel = new QLabel(spec.name, m_htmlGroup);
        nameLabel->setObjectName(QStringLiteral("htmlVarName_%1").arg(spec.track));
        nameLabel->setToolTip(spec.track);
        m_htmlGrid->addWidget(nameLabel, gridRow, 0);

        if (spec.numeric)
        {
            row.diamond = new QPushButton(QStringLiteral("◇"), m_htmlGroup);
            row.diamond->setObjectName(QStringLiteral("htmlVarDiamond_%1").arg(spec.track));
            row.diamond->setFixedWidth(28);
            m_htmlGrid->addWidget(row.diamond, gridRow, 1);

            row.value = new QDoubleSpinBox(m_htmlGroup);
            row.value->setObjectName(QStringLiteral("htmlVarValue_%1").arg(spec.track));
            row.value->setRange(spec.minimum, spec.maximum);
            row.value->setDecimals(spec.decimals);
            row.value->setSingleStep(spec.step);
            row.value->setKeyboardTracking(false);
            row.value->setToolTip(
                QStringLiteral("manifest 默认值 %1；修改后按 Auto-key 规则写入 %2 轨道。")
                    .arg(spec.defaultValue)
                    .arg(spec.track));
            m_htmlGrid->addWidget(row.value, gridRow, 2);

            const QString track = spec.track;
            const double fallback = spec.defaultValue;
            connect(row.diamond, &QPushButton::clicked, this, [this, track, fallback] {
                toggleKeyframe(track, fallback);
            });
            connect(row.value, &QDoubleSpinBox::editingFinished, this,
                    [this, track, spin = row.value] {
                        if (m_loading)
                            return;
                        const model::Entity* entity = selectedEntity();
                        if (!entity || !m_document)
                            return;
                        const int relativeFrame =
                            m_document->playhead() - entity->start;
                        if (relativeFrame < 0
                            || relativeFrame >= entity->duration())
                        {
                            setStatus(QStringLiteral(
                                "播放头不在实体范围内，无法修改变量"));
                            refresh();
                            return;
                        }
                        if (!m_autoKey && !hasKeyAt(*entity, track, relativeFrame))
                        {
                            setStatus(QStringLiteral(
                                "HTML 变量没有静态值：先点菱形打关键帧，或开启 Auto-key"));
                            refresh();
                            return;
                        }
                        editKeyframeValue(track, spin->value());
                    });
        }
        else
        {
            auto* placeholder = new QLabel(QStringLiteral("—"), m_htmlGroup);
            placeholder->setObjectName(
                QStringLiteral("htmlVarDiamondPlaceholder_%1").arg(spec.track));
            placeholder->setAlignment(Qt::AlignCenter);
            m_htmlGrid->addWidget(placeholder, gridRow, 1);
            // The placeholder only exists for column alignment; not tracked.

            row.readOnlyValue = new QLabel(spec.text, m_htmlGroup);
            row.readOnlyValue->setObjectName(
                QStringLiteral("htmlVarReadOnly_%1").arg(spec.track));
            row.readOnlyValue->setToolTip(
                QStringLiteral("布尔/字符串变量第一版只读；数值变量才能打关键帧。"));
            m_htmlGrid->addWidget(row.readOnlyValue, gridRow, 2);
        }

        m_htmlRows.append(row);
        ++gridRow;
    }
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
    {
        rebuildSolverCards(entity.uuid, solvers);
    }
    else
    {
        for (int i = 0; i < solvers.size(); ++i)
            fillSolverCard(m_solverCards[i], entity.uuid, solvers.at(i));
    }

    reloadSolverCreate(entity);
    applySolverCardSelection();
    m_solverEmpty->setVisible(solvers.isEmpty());
}

void PropertiesPanel::rebuildSolverCards(
    const Uuid& entityUuid, const QVector<model::SolverSegment>& solvers)
{
    for (SolverCard& card : m_solverCards)
        delete card.box;
    m_solverCards.clear();

    for (int index = 0; index < solvers.size(); ++index)
    {
        const model::SolverSegment& solver = solvers.at(index);
        SolverCard card;
        card.id = solver.id;
        card.effect = solver.effect;
        card.box = new QGroupBox(m_solverHost);
        card.box->setObjectName(QStringLiteral("solverCard%1").arg(index));
        auto* form = new QFormLayout(card.box);
        form->setContentsMargins(8, 6, 8, 6);
        form->setVerticalSpacing(4);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        auto* effectLabel = new QLabel(solverEffectName(solver.effect), card.box);
        effectLabel->setObjectName(QStringLiteral("solverEffect%1").arg(index));
        form->addRow(QStringLiteral("效果"), effectLabel);

        card.preview = new SolverPreviewWidget(m_document, card.box);
        card.preview->setObjectName(QStringLiteral("solverPreview%1").arg(index));
        card.preview->setMinimumHeight(180);
        form->addRow(card.preview);

        card.start = new QSpinBox(card.box);
        card.start->setObjectName(QStringLiteral("solverStart%1").arg(index));
        card.start->setRange(0, 1000000);
        card.start->setKeyboardTracking(false);
        card.start->setToolTip(QStringLiteral("求解开始帧，相对实体 start；开始帧必须已有被驱动属性的关键帧。"));
        form->addRow(QStringLiteral("开始帧"), card.start);

        card.end = new QSpinBox(card.box);
        card.end->setObjectName(QStringLiteral("solverEnd%1").arg(index));
        card.end->setRange(-1, 1000000);
        card.end->setSpecialValueText(QStringLiteral("片段末帧"));
        card.end->setKeyboardTracking(false);
        card.end->setToolTip(QStringLiteral("求解结束帧是停止标记，不存值；-1 表示一直解到片段最后一帧。"));
        form->addRow(QStringLiteral("结束帧"), card.end);

        card.velocityX = new QDoubleSpinBox(card.box);
        card.velocityX->setObjectName(QStringLiteral("solverVelocityX%1").arg(index));
        card.velocityX->setRange(-1000000.0, 1000000.0);
        card.velocityX->setDecimals(1);
        card.velocityX->setSingleStep(10.0);
        card.velocityX->setSuffix(QStringLiteral(" px/s"));
        card.velocityX->setKeyboardTracking(false);
        form->addRow(QStringLiteral("初速度 X"), card.velocityX);

        card.velocityY = new QDoubleSpinBox(card.box);
        card.velocityY->setObjectName(QStringLiteral("solverVelocityY%1").arg(index));
        card.velocityY->setRange(-1000000.0, 1000000.0);
        card.velocityY->setDecimals(1);
        card.velocityY->setSingleStep(10.0);
        card.velocityY->setSuffix(QStringLiteral(" px/s"));
        card.velocityY->setKeyboardTracking(false);
        form->addRow(QStringLiteral("初速度 Y"), card.velocityY);

        const Uuid solverId = solver.id;
        card.remove = new QPushButton(QStringLiteral("删除求解段"), card.box);
        card.remove->setObjectName(QStringLiteral("solverDelete%1").arg(index));
        card.remove->setToolTip(
            QStringLiteral("删除这个求解段；开始帧的普通关键帧会保留。"));
        form->addRow(card.remove);
        connect(card.remove, &QPushButton::clicked, this, [this, solverId] {
            deleteSolverSegment(solverId);
        });

        connect(card.start, &QSpinBox::editingFinished, this, [this, solverId, card] {
            const int value = card.start->value();
            commitSolverEdit(
                solverId, QStringLiteral("solver startFrame"),
                [solverId, value](model::Entity& entity) {
                    for (model::SolverSegment& segment : entity.solvers)
                    {
                        if (segment.id == solverId)
                        {
                            segment.startFrame = value;
                            return true;
                        }
                    }
                    return false;
                });
        });
        connect(card.end, &QSpinBox::editingFinished, this, [this, solverId, card] {
            const int value = card.end->value();
            commitSolverEdit(
                solverId, QStringLiteral("solver endFrame"),
                [solverId, value](model::Entity& entity) {
                    for (model::SolverSegment& segment : entity.solvers)
                    {
                        if (segment.id == solverId)
                        {
                            segment.endFrame = value;
                            return true;
                        }
                    }
                    return false;
                });
        });
        connect(card.velocityX, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityX->value();
                    commitSolverEdit(
                        solverId, QStringLiteral("solver initialVelocity.x"),
                        [solverId, value](model::Entity& entity) {
                            for (model::SolverSegment& segment : entity.solvers)
                            {
                                if (segment.id == solverId)
                                {
                                    segment.initialVelocity.setX(value);
                                    return true;
                                }
                            }
                            return false;
                        });
                });
        connect(card.velocityY, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityY->value();
                    commitSolverEdit(
                        solverId, QStringLiteral("solver initialVelocity.y"),
                        [solverId, value](model::Entity& entity) {
                            for (model::SolverSegment& segment : entity.solvers)
                            {
                                if (segment.id == solverId)
                                {
                                    segment.initialVelocity.setY(value);
                                    return true;
                                }
                            }
                            return false;
                        });
                });

        const QVector<ParamSpec> specs = solverParams(solver.effect);
        for (const ParamSpec& spec : specs)
        {
            QWidget* editor = nullptr;
            if (spec.kind == ParamKind::Uuid)
            {
                auto* edit = new QLineEdit(card.box);
                edit->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                editor = edit;
                connect(edit, &QLineEdit::editingFinished, this,
                        [this, solverId, spec, edit] {
                    const QString value = edit->text().trimmed();
                    if (value.isEmpty())
                    {
                        setStatus(QStringLiteral("%1不能为空").arg(spec.label));
                        refresh();
                        return;
                    }
                    if (m_document && !m_document->project().entities.contains(value))
                    {
                        setStatus(QStringLiteral("目标实体不存在：%1").arg(value));
                        refresh();
                        return;
                    }
                    commitSolverEdit(
                        solverId, QStringLiteral("solver %1").arg(spec.key),
                        [solverId, spec, value](model::Entity& entity) {
                            for (model::SolverSegment& segment : entity.solvers)
                            {
                                if (segment.id == solverId)
                                {
                                    segment.params.insert(spec.key, value);
                                    return true;
                                }
                            }
                            return false;
                        });
                });
            }
            else if (spec.kind == ParamKind::Direction)
            {
                auto* combo = new QComboBox(card.box);
                combo->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                combo->addItem(spec.ccwPositive ? QStringLiteral("逆时针 (+1)")
                                                : QStringLiteral("顺时针 (+1)"),
                               1);
                combo->addItem(spec.ccwPositive ? QStringLiteral("顺时针 (-1)")
                                                : QStringLiteral("逆时针 (-1)"),
                               -1);
                editor = combo;
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, solverId, spec, combo](int) {
                            const int value = combo->currentData().toInt();
                            commitSolverEdit(
                                solverId,
                                QStringLiteral("solver %1").arg(spec.key),
                                [solverId, spec, value](model::Entity& entity) {
                                    for (model::SolverSegment& segment : entity.solvers)
                                    {
                                        if (segment.id == solverId)
                                        {
                                            segment.params.insert(spec.key, value);
                                            return true;
                                        }
                                    }
                                    return false;
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
                            commitSolverEdit(
                                solverId,
                                QStringLiteral("solver %1").arg(spec.key),
                                [solverId, spec, value](model::Entity& entity) {
                                    for (model::SolverSegment& segment : entity.solvers)
                                    {
                                        if (segment.id == solverId)
                                        {
                                            segment.params.insert(spec.key, value);
                                            return true;
                                        }
                                    }
                                    return false;
                                });
                        });
            }

            QString tooltip = spec.tooltip;
            if (spec.kind == ParamKind::Number)
            {
                tooltip += QStringLiteral("\n范围 %1..%2，默认 %3")
                               .arg(spec.minimum)
                               .arg(spec.maximum)
                               .arg(spec.defaultValue);
            }
            editor->setToolTip(tooltip);
            form->addRow(spec.label, editor);
            card.params.insert(spec.key, editor);
        }

        m_solverCardsLayout->addWidget(card.box);
        fillSolverCard(card, entityUuid, solver);
        m_solverCards.append(card);
    }

    applySolverEnabledState();
}

void PropertiesPanel::fillSolverCard(SolverCard& card, const Uuid& entityUuid,
                                     const model::SolverSegment& solver)
{
    card.box->setTitle(
        QStringLiteral("%1 · %2").arg(solverEffectName(solver.effect), solverRangeText(solver)));
    if (card.preview)
        card.preview->setSolver(entityUuid, solver.id);
    card.start->setValue(solver.startFrame);
    card.end->setValue(solver.endFrame);
    card.velocityX->setValue(solver.initialVelocity.x());
    card.velocityY->setValue(solver.initialVelocity.y());

    const bool usesVelocity =
        solver.effect == SolverEffect::Gravity || solver.effect == SolverEffect::BlackHole;
    const QString velocityTip =
        usesVelocity
            ? QStringLiteral("开始帧的求解空间初速度，单位 px/s；负值向上。")
            : QStringLiteral("orbit / spin / spiral 不使用初速度。");
    card.velocityX->setToolTip(velocityTip);
    card.velocityY->setToolTip(velocityTip);

    const QVector<ParamSpec> specs = solverParams(solver.effect);
    for (const ParamSpec& spec : specs)
    {
        QWidget* editor = card.params.value(spec.key);
        if (!editor)
            continue;

        if (auto* edit = qobject_cast<QLineEdit*>(editor))
        {
            edit->setText(solver.params.value(spec.key).toString());
        }
        else if (auto* combo = qobject_cast<QComboBox*>(editor))
        {
            const int value = solver.params.value(spec.key, int(spec.defaultValue)).toInt();
            const int comboIndex = combo->findData(value);
            combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
        }
        else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor))
        {
            spin->setValue(solver.params.value(spec.key, spec.defaultValue).toDouble());
        }
    }
}

void PropertiesPanel::reloadSolverCreate(const model::Entity& entity)
{
    const Uuid previous = m_solverTargetCombo->currentData().toString();
    QSignalBlocker blocker(m_solverTargetCombo);
    m_solverTargetCombo->clear();
    m_solverTargetCombo->addItem(QStringLiteral("（选择目标实体）"), QString());

    QVector<const model::Entity*> others;
    for (auto it = m_document->project().entities.constBegin();
         it != m_document->project().entities.constEnd(); ++it)
    {
        if (it.key() != entity.uuid)
            others.append(&it.value());
    }
    std::sort(others.begin(), others.end(),
              [](const model::Entity* a, const model::Entity* b) {
                  if (a->name != b->name)
                      return a->name < b->name;
                  return a->uuid < b->uuid;
              });
    for (const model::Entity* other : others)
    {
        const QString label = other->name.isEmpty()
            ? other->uuid.left(8)
            : QStringLiteral("%1 (%2)").arg(other->name, other->uuid.left(8));
        m_solverTargetCombo->addItem(label, other->uuid);
    }

    const int previousIndex = m_solverTargetCombo->findData(previous);
    m_solverTargetCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);

    const int duration = entity.duration();
    m_solverEndSpin->setRange(-1, qMax(0, duration - 1));
    if (m_solverEndSpin->value() > duration - 1)
        m_solverEndSpin->setValue(duration - 1);
}

void PropertiesPanel::applySolverCardSelection()
{
    bool found = false;
    for (SolverCard& card : m_solverCards)
    {
        const bool selected = !m_selectedSolverId.isEmpty()
            && card.id == m_selectedSolverId;
        card.box->setStyleSheet(selected
                                    ? QStringLiteral("QGroupBox { border: 2px solid #ffbe3c; }")
                                    : QString());
        if (selected)
        {
            found = true;
            m_scroll->ensureWidgetVisible(card.box);
        }
    }
    if (!found)
        m_selectedSolverId.clear();
}

SolverPreviewWidget* PropertiesPanel::solverPreview(const Uuid& solverId) const
{
    for (const SolverCard& card : m_solverCards)
    {
        if (card.id == solverId)
            return card.preview;
    }
    return nullptr;
}

void PropertiesPanel::commitSolverEdit(
    const Uuid& solverId, const QString& what,
    const std::function<bool(model::Entity&)>& mutate)
{
    if (!commitEdit(what, mutate))
        return;
    if (SolverPreviewWidget* preview = solverPreview(solverId))
        preview->refreshPreview();
}

SolverEffect PropertiesPanel::selectedSolverEffect() const
{
    return SolverEffect(m_solverEffectCombo->currentData().toInt());
}

void PropertiesPanel::createSolverFromPlayhead()
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能创建求解段"));
        return;
    }

    const int relativeFrame = m_document->playhead() - entity->start;
    const int duration = entity->duration();
    if (relativeFrame < 0 || relativeFrame >= duration)
    {
        setStatus(QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                      .arg(relativeFrame)
                      .arg(duration - 1));
        return;
    }

    const SolverEffect effect = selectedSolverEffect();
    const QStringList drivenTracks = solverDrivenTracks(effect);
    bool hasStartKey = true;
    for (const QString& trackName : drivenTracks)
        hasStartKey = hasStartKey && hasKeyAt(*entity, trackName, relativeFrame);
    if (!hasStartKey)
    {
        setStatus(QStringLiteral("请先在播放头处给 X/Y（或旋转）打普通关键帧"));
        return;
    }

    const Uuid self = entity->uuid;
    Uuid target;
    if (solverNeedsTarget(effect))
    {
        target = m_solverTargetCombo->currentData().toString();
        if (target.isEmpty())
        {
            setStatus(QStringLiteral("请先选择目标实体"));
            return;
        }
        if (target == self)
        {
            setStatus(QStringLiteral("目标实体不能是自己"));
            return;
        }
        if (!m_document->project().entities.contains(target))
        {
            setStatus(QStringLiteral("目标实体不存在：%1").arg(target));
            return;
        }
    }

    const int endFrame = m_solverEndSpin->value();
    if (endFrame != -1 && (endFrame < relativeFrame || endFrame >= duration))
    {
        setStatus(QStringLiteral("结束帧必须为 -1 或位于 %1..%2")
                      .arg(relativeFrame)
                      .arg(duration - 1));
        return;
    }
    const int effectiveEnd = endFrame < 0 ? duration - 1 : endFrame;

    QStringList conflicts;
    for (const model::SolverSegment& existing : entity->solvers)
    {
        const int existingEnd = solverEffectiveEnd(existing, duration);
        if (relativeFrame <= existingEnd && existing.startFrame <= effectiveEnd)
            conflicts.append(solverConflictText(existing, duration));
    }
    if (!conflicts.isEmpty())
    {
        setStatus(QStringLiteral("新求解段与已有求解段重叠：%1")
                      .arg(conflicts.join(QStringLiteral("、"))));
        return;
    }

    QStringList innerKeys;
    for (const QString& trackName : drivenTracks)
    {
        const auto track = entity->tracks.constFind(trackName);
        if (track == entity->tracks.constEnd())
            continue;
        for (const model::Keyframe& keyframe : track.value().keys)
        {
            if (keyframe.frame > relativeFrame && keyframe.frame <= effectiveEnd)
            {
                innerKeys.append(
                    QStringLiteral("%1@%2").arg(trackName).arg(keyframe.frame));
            }
        }
    }
    if (!innerKeys.isEmpty())
    {
        setStatus(QStringLiteral("求解段内不能有普通关键帧：%1")
                      .arg(innerKeys.join(QStringLiteral("、"))));
        return;
    }

    model::SolverSegment segment;
    segment.id = model::newUuid();
    segment.effect = effect;
    segment.startFrame = relativeFrame;
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
    case SolverEffect::Spiral:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        segment.params.insert(QStringLiteral("turns"), 3.0);
        segment.params.insert(QStringLiteral("direction"), 1);
        segment.params.insert(QStringLiteral("endRadius"), 0.0);
        segment.params.insert(QStringLiteral("speedDegPerSec"), 0.0);
        break;
    }

    const Uuid solverId = segment.id;
    entity->solvers.append(segment);

    QString error;
    m_committing = true;
    const bool ok = m_document->commitEntity(
        self, QStringLiteral("pvf: convert keyframe to solver start"), &error);
    m_committing = false;

    if (!ok)
    {
        auto it = m_document->mutableProject().entities.find(self);
        if (it != m_document->mutableProject().entities.end())
        {
            QVector<model::SolverSegment>& solvers = it.value().solvers;
            for (int i = 0; i < solvers.size(); ++i)
            {
                if (solvers.at(i).id == solverId)
                {
                    solvers.removeAt(i);
                    break;
                }
            }
        }
        setStatus(QStringLiteral("创建求解段失败：%1")
                      .arg(error.isEmpty() ? QStringLiteral("保存失败") : error));
        refresh();
        return;
    }

    m_selectedSolverId = solverId;
    setStatus(QStringLiteral("已创建求解段：%1（%2–%3）")
                  .arg(solverEffectName(effect))
                  .arg(relativeFrame)
                  .arg(effectiveEnd));
    refresh();
}

void PropertiesPanel::deleteSolverSegment(const Uuid& solverId)
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能删除求解段"));
        return;
    }

    int index = -1;
    for (int i = 0; i < entity->solvers.size(); ++i)
    {
        if (entity->solvers.at(i).id == solverId)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    const Uuid self = entity->uuid;
    const model::SolverSegment removed = entity->solvers.at(index);
    entity->solvers.removeAt(index);

    QString error;
    m_committing = true;
    const bool ok = m_document->commitEntity(
        self,
        QStringLiteral("pvf: delete solver \"%1\" %2")
            .arg(entity->name, solverEffectName(removed.effect)),
        &error);
    m_committing = false;

    if (!ok)
    {
        auto it = m_document->mutableProject().entities.find(self);
        if (it != m_document->mutableProject().entities.end())
        {
            QVector<model::SolverSegment>& solvers = it.value().solvers;
            solvers.insert(qMin(index, solvers.size()), removed);
        }
        setStatus(QStringLiteral("删除求解段失败：%1")
                      .arg(error.isEmpty() ? QStringLiteral("保存失败") : error));
        refresh();
        return;
    }

    if (m_selectedSolverId == solverId)
        m_selectedSolverId.clear();
    setStatus(QStringLiteral("已删除求解段"));
    refresh();
}

bool PropertiesPanel::replaceAsset(const AssetHash& hash, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }

    model::Entity* entity = selectedEntity();
    if (!entity)
    {
        if (error)
            *error = QStringLiteral("未选择实体");
        return false;
    }
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        if (error)
            *error = QStringLiteral("实体或其祖先已锁定");
        setStatus(QStringLiteral("实体或其祖先已锁定，不能更换素材"));
        return false;
    }
    if (entity->content.kind != ContentKind::Asset)
    {
        if (error)
            *error = QStringLiteral("实体不是素材内容");
        return false;
    }

    const auto stored = m_document->assets().get(hash);
    if (!stored)
    {
        if (error)
            *error = QStringLiteral("素材不存在：%1").arg(hash);
        return false;
    }
    if (!sameAssetGroup(entity->content.asset.type, stored->type))
    {
        if (error)
            *error = QStringLiteral("素材类型不兼容：%1 → %2")
                         .arg(assetTypeLabel(entity->content.asset.type),
                              assetTypeLabel(stored->type));
        setStatus(QStringLiteral("不能把 %1 换成 %2 素材")
                      .arg(assetTypeLabel(entity->content.asset.type),
                           assetTypeLabel(stored->type)));
        return false;
    }
    if (hash == entity->content.asset.hash)
    {
        if (error)
            *error = QStringLiteral("已经使用该素材");
        return false;
    }

    const AssetReplacementPlan plan = planAssetReplacement(*entity, *stored);
    if (plan.needsTrim)
    {
        // MCP rejects the swap when trimming would delete child entities; do
        // the same here instead of leaving children outside the new range.
        const Uuid self = entity->uuid;
        const int newEnd = entity->start + plan.duration;
        for (auto it = m_document->project().entities.constBegin();
             it != m_document->project().entities.constEnd(); ++it)
        {
            if (it.value().parentUuid != self)
                continue;
            if (it.value().end > newEnd)
            {
                if (error)
                    *error = QStringLiteral(
                        "换素材会裁剪子实体 %1，请先裁剪子实体").arg(it.value().name);
                setStatus(QStringLiteral(
                    "换素材会裁剪子实体，请先裁剪子实体：%1").arg(it.value().name));
                return false;
            }
        }
    }

    const assets::StoredAsset asset = *stored;
    const bool ok = commitEdit(
        QStringLiteral("content.asset"), [asset, plan](model::Entity& target) {
            applyAssetReplacement(target, asset, plan);
            return true;
        });
    if (ok)
    {
        setStatus(QStringLiteral("已更换素材：%1%2")
                      .arg(asset.name,
                           plan.needsTrim
                               ? QStringLiteral("（时长裁到 %1 帧）").arg(plan.duration)
                               : QString()));
    }
    return ok;
}

void PropertiesPanel::openTextLayoutDialog()
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity
        || isEntityLocked(m_document->project(), entity->uuid)
        || entity->content.kind != ContentKind::Text)
        return;

    const Uuid uuid = entity->uuid;
    TextLayoutDialog dialog(entity->content.text, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // The modal dialog cannot change selection through the UI, but guard
    // against a document signal having replaced the selected entity.
    model::Entity* current = selectedEntity();
    if (!current || current->uuid != uuid)
        return;

    const model::TextContent content = dialog.content();
    const QJsonObject before = model::toJson(*current);
    const model::Entity original = *current;
    model::Entity updated = *current;
    updated.content.text = content;

    QString error;
    if (!model::validateEntity(updated, &error))
    {
        setStatus(QStringLiteral("未修改：%1").arg(error));
        refresh();
        return;
    }
    if (model::toJson(updated) == before)
        return;

    *current = updated;
    m_committing = true;
    const bool ok =
        m_document->commitEntity(uuid, QStringLiteral("pvf: edit text layout"),
                                 &error);
    m_committing = false;

    if (!ok)
    {
        model::Entity* latest = selectedEntity();
        if (latest && latest->uuid == original.uuid)
            *latest = original;
        showSaveError(error);
        refresh();
        return;
    }

    setStatus(QStringLiteral("已保存：文字排版"));
    refresh();
}

void PropertiesPanel::commitSpecialContentEdit()
{
    if (m_loading || m_committing || !m_document || !m_specialEditor)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity || !SpecialContentEditor::handles(entity->content.kind))
        return;
    if (m_specialEditor->kind() != entity->content.kind)
        return;

    model::Content content = entity->content;
    m_specialEditor->applyTo(content);

    // model::validateContent permits an empty brand-wall logo list (the
    // renderer just draws empty tiles), but an editor save without a logo is
    // meaningless.  Reject it here before the commit.
    if (content.kind == ContentKind::BrandWall
        && content.brandWall.logos.isEmpty())
    {
        setStatus(QStringLiteral("未修改：品牌墙至少需要一个 logo"));
        refresh();
        return;
    }

    commitEditWithMessage(
        QStringLiteral("%1 内容").arg(contentKindLabel(content.kind)),
        QStringLiteral("pvf: edit %1 content")
            .arg(contentKindName(content.kind)),
        [content](model::Entity& updated) {
            updated.content = content;
            return true;
        });
}

void PropertiesPanel::chooseReplacementAsset()
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity
        || isEntityLocked(m_document->project(), entity->uuid)
        || entity->content.kind != ContentKind::Asset)
        return;

    const AssetType currentType = entity->content.asset.type;
    if (!m_document->assets().get(entity->content.asset.hash))
    {
        setStatus(QStringLiteral("素材缺失，无法更换；请先把素材放回 assets/"));
        refresh();
        return;
    }

    QVector<assets::StoredAsset> candidates;
    for (const assets::StoredAsset& asset : m_document->assets().list())
    {
        if (sameAssetGroup(currentType, asset.type))
            candidates.append(asset);
    }
    if (candidates.isEmpty())
    {
        QMessageBox::information(
            this, QStringLiteral("更换素材"),
            QStringLiteral("素材箱里没有与当前实体兼容的素材。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("更换素材 · %1").arg(entity->name));
    dialog.resize(560, 520);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    auto* search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("assetPickSearch"));
    search->setPlaceholderText(QStringLiteral("搜索名称、类型或 hash…"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    auto* list = new HoverListWidget(&dialog);
    list->setObjectName(QStringLiteral("assetPickList"));
    list->setIconSize(QSize(64, 40));
    layout->addWidget(list, 1);

    auto* preview = new QLabel(&dialog);
    preview->setObjectName(QStringLiteral("assetPickPreview"));
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(200, 160);
    preview->setText(QStringLiteral("选择或悬停一个素材查看预览"));
    layout->addWidget(preview);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("更换"));

    auto makeItem = [](const assets::StoredAsset& asset) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(asset.name, assetInfoText(asset)));
        item->setData(Qt::UserRole, asset.hash);
        item->setToolTip(asset.name);
        return item;
    };
    for (const assets::StoredAsset& asset : candidates)
        list->addItem(makeItem(asset));

    auto showPreview = [this, preview, list](QListWidgetItem* item) {
        if (!item || !m_document)
        {
            preview->setPixmap(QPixmap());
            preview->setText(QStringLiteral("选择或悬停一个素材查看预览"));
            return;
        }
        const auto asset = m_document->assets().get(item->data(Qt::UserRole).toString());
        if (!asset || asset->type != AssetType::Image)
        {
            preview->setPixmap(QPixmap());
            preview->setText(asset
                                 ? QStringLiteral("%1 素材没有图片预览").arg(
                                       assetTypeLabel(asset->type))
                                 : QStringLiteral("素材缺失"));
            return;
        }
        const QPixmap thumbnail = cachedAssetThumbnail(asset->hash, *asset);
        if (thumbnail.isNull())
        {
            preview->setPixmap(QPixmap());
            preview->setText(QStringLiteral("图片无法解码"));
            return;
        }
        preview->setText(QString());
        preview->setPixmap(thumbnail);
    };
    list->hovered = showPreview;

    auto currentHash = [list]() -> AssetHash {
        QListWidgetItem* item = list->currentItem();
        if (!item || item->isHidden())
            return {};
        return item->data(Qt::UserRole).toString();
    };
    auto updateOk = [buttons, currentHash]() {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!currentHash().isEmpty());
    };

    connect(list, &QListWidget::currentItemChanged, this,
            [showPreview, updateOk](QListWidgetItem* current, QListWidgetItem*) {
                showPreview(current);
                updateOk();
            });
    connect(list, &QListWidget::itemActivated, &dialog, [&dialog, currentHash] {
        if (!currentHash().isEmpty())
            dialog.accept();
    });
    connect(search, &QLineEdit::textChanged, &dialog,
            [list, updateOk](const QString& text) {
                const QString query = text.trimmed();
                for (int i = 0; i < list->count(); ++i)
                {
                    QListWidgetItem* item = list->item(i);
                    item->setHidden(!query.isEmpty()
                                    && !item->text().contains(query,
                                                              Qt::CaseInsensitive));
                }
                if (QListWidgetItem* current = list->currentItem();
                    current && current->isHidden())
                {
                    current->setSelected(false);
                    list->setCurrentItem(nullptr);
                }
                updateOk();
            });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, currentHash] {
        if (!currentHash().isEmpty())
            dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (!list->count())
        return;
    list->setCurrentRow(0);
    updateOk();

    if (dialog.exec() != QDialog::Accepted)
        return;

    const AssetHash chosen = currentHash();
    if (chosen.isEmpty())
        return;
    QString error;
    if (!replaceAsset(chosen, &error) && !error.isEmpty())
        setStatus(QStringLiteral("更换素材失败：%1").arg(error));
}

void PropertiesPanel::setAutoKey(bool on)
{
    m_autoKey = on;
    if (m_autoKeyCheck && m_autoKeyCheck->isChecked() != on)
    {
        QSignalBlocker blocker(m_autoKeyCheck);
        m_autoKeyCheck->setChecked(on);
    }
}

bool PropertiesPanel::autoKey() const
{
    return m_autoKey;
}

void PropertiesPanel::toggleKeyframe(const QString& track,
                                     std::optional<double> fallback)
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能编辑关键帧"));
        return;
    }

    const int relativeFrame = m_document->playhead() - entity->start;
    const int duration = entity->duration();
    if (relativeFrame < 0 || relativeFrame >= duration)
    {
        setStatus(QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                      .arg(relativeFrame)
                      .arg(duration - 1));
        refresh();
        return;
    }

    const bool remove = hasKeyAt(*entity, track, relativeFrame);
    if (remove && isSolverStartKey(*entity, track, relativeFrame))
    {
        setStatus(QStringLiteral("求解段开始帧的关键帧不能删除，请先删除求解段"));
        refresh();
        return;
    }
    const double value =
        currentTrackValue(*entity, track, relativeFrame, isStepTrack(track), fallback);
    const bool ok = commitEdit(
        QStringLiteral("关键帧 %1@%2").arg(track).arg(relativeFrame),
        [track, relativeFrame, value, remove](model::Entity& target) {
            if (remove)
                removeKeyframeAt(target, track, relativeFrame);
            else
                setKeyframeValue(target, track, relativeFrame, value);
            return true;
        });
    if (ok)
    {
        setStatus(remove
                      ? QStringLiteral("已删除 %1 帧的「%2」关键帧")
                            .arg(relativeFrame)
                            .arg(track)
                      : QStringLiteral("已在 %1 帧插入「%2」关键帧")
                            .arg(relativeFrame)
                            .arg(track));
    }
}

void PropertiesPanel::editKeyframeValue(const QString& track, double value)
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity || isEntityLocked(m_document->project(), entity->uuid))
        return;

    const int relativeFrame = m_document->playhead() - entity->start;
    const bool inRange = relativeFrame >= 0 && relativeFrame < entity->duration();
    const bool hasKey = inRange && hasKeyAt(*entity, track, relativeFrame);
    const bool writeKey = inRange && (m_autoKey || hasKey);
    const int keyFrame = writeKey ? relativeFrame : -1;

    const bool ok = commitEdit(
        QStringLiteral("关键帧 %1 值").arg(track),
        [track, value, keyFrame](model::Entity& target) {
            setStaticTrackValue(target, track, value);
            if (keyFrame >= 0)
                setKeyframeValue(target, track, keyFrame, value);
            return true;
        });
    if (ok && m_autoKey && !inRange)
    {
        setStatus(QStringLiteral("播放头不在实体范围内：只改了静态值（相对帧 %1）")
                      .arg(relativeFrame));
    }
}

void PropertiesPanel::applyAutoKey(model::Entity& entity, const QString& track,
                                   double value)
{
    if (!m_autoKey || m_loading || !m_document)
        return;
    if (!entitySupportsTrack(entity, track))
        return;
    const int relativeFrame = m_document->playhead() - entity.start;
    if (relativeFrame < 0 || relativeFrame >= entity.duration())
        return;
    setKeyframeValue(entity, track, relativeFrame, value);
}

bool PropertiesPanel::setTrackEasing(const QString& track,
                                     const model::EasingSpec& easing)
{
    if (m_loading || !m_document)
        return false;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return false;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能修改关键帧缓动"));
        return false;
    }

    const int relativeFrame = m_document->playhead() - entity->start;
    if (relativeFrame < 0 || relativeFrame >= entity->duration()
        || !hasKeyAt(*entity, track, relativeFrame))
    {
        setStatus(QStringLiteral("当前帧没有「%1」关键帧，先点菱形插入关键帧")
                      .arg(track));
        refresh();
        return false;
    }

    return commitEdit(
        QStringLiteral("关键帧 %1 缓动").arg(track),
        [track, relativeFrame, easing](model::Entity& target) {
            auto it = target.tracks.find(track);
            if (it == target.tracks.end())
                return false;
            for (model::Keyframe& keyframe : it.value().keys)
            {
                if (keyframe.frame == relativeFrame)
                {
                    keyframe.easing = easing;
                    return true;
                }
            }
            return false;
        });
}

void PropertiesPanel::promptCustomEasing(const QString& track)
{
    if (m_loading || !m_document)
        return;

    model::EasingSpec initial;
    if (const model::Entity* entity = selectedEntity())
    {
        const int relativeFrame = m_document->playhead() - entity->start;
        const auto trackIt = entity->tracks.constFind(track);
        if (trackIt != entity->tracks.constEnd())
        {
            for (const model::Keyframe& keyframe : trackIt.value().keys)
            {
                if (keyframe.frame == relativeFrame)
                {
                    initial = keyframe.easing;
                    break;
                }
            }
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("自定义贝塞尔缓动"));
    auto* form = new QFormLayout(&dialog);
    form->setContentsMargins(12, 12, 12, 8);
    form->setVerticalSpacing(6);

    auto makeHandle = [&dialog](double value, double minimum, double maximum) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setRange(minimum, maximum);
        spin->setDecimals(3);
        spin->setSingleStep(0.05);
        spin->setKeyboardTracking(false);
        spin->setValue(value);
        return spin;
    };

    QDoubleSpinBox* inX = makeHandle(initial.inX, 0.0, 1.0);
    QDoubleSpinBox* inY = makeHandle(initial.inY, -2.0, 2.0);
    QDoubleSpinBox* outX = makeHandle(initial.outX, 0.0, 1.0);
    QDoubleSpinBox* outY = makeHandle(initial.outY, -2.0, 2.0);
    form->addRow(QStringLiteral("inX"), inX);
    form->addRow(QStringLiteral("inY"), inY);
    form->addRow(QStringLiteral("outX"), outX);
    form->addRow(QStringLiteral("outY"), outY);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        refresh();
        return;
    }

    model::EasingSpec easing;
    easing.type = QStringLiteral("bezier");
    easing.inX = inX->value();
    easing.inY = inY->value();
    easing.outX = outX->value();
    easing.outY = outY->value();
    if (!setTrackEasing(track, easing))
        refresh();
}

void PropertiesPanel::applyEditingState()
{
    const bool canEdit = editable();
    for (QWidget* widget : m_editable)
        widget->setEnabled(canEdit);

    // The replacement dialog needs the current asset to exist; with a missing
    // asset the entity shows a placeholder and the button stays disabled.
    m_assetReplaceButton->setEnabled(canEdit && !m_assetMissing);

    const model::Entity* selected = selectedEntity();
    m_lockedCheck->setEnabled(
        selected != nullptr
        && !hasLockedAncestor(m_document->project(), selected->uuid));
    applyKeyframeEnabledState();
    applySolverEnabledState();
}

void PropertiesPanel::applyKeyframeEnabledState()
{
    const bool canEdit = editable();
    for (KeyframeRow& row : m_keyframeRows)
    {
        row.diamond->setEnabled(canEdit);
        row.value->setEnabled(canEdit);
        row.easing->setEnabled(canEdit && row.hasKey && !row.step);
    }
    for (HtmlVariableRow& row : m_htmlRows)
    {
        if (!row.numeric)
            continue;
        const bool rangeOk = canEdit && row.inRange;
        row.diamond->setEnabled(rangeOk);
        row.value->setEnabled(rangeOk);
    }
}

void PropertiesPanel::applySolverEnabledState()
{
    const bool canEdit = editable();
    const bool needsTarget = solverNeedsTarget(selectedSolverEffect());
    m_solverEffectCombo->setEnabled(canEdit);
    m_solverTargetCombo->setEnabled(canEdit && needsTarget);
    m_solverEndSpin->setEnabled(canEdit);
    m_solverCreateButton->setEnabled(canEdit);

    for (SolverCard& card : m_solverCards)
    {
        card.start->setEnabled(canEdit);
        card.end->setEnabled(canEdit);
        const bool usesVelocity =
            card.effect == SolverEffect::Gravity || card.effect == SolverEffect::BlackHole;
        card.velocityX->setEnabled(canEdit && usesVelocity);
        card.velocityY->setEnabled(canEdit && usesVelocity);
        card.remove->setEnabled(canEdit);
        for (QWidget* editor : card.params)
            editor->setEnabled(canEdit);
    }
}

model::Entity* PropertiesPanel::selectedEntity() const
{
    if (!m_document)
        return nullptr;
    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
        return nullptr;
    auto it = m_document->mutableProject().entities.find(uuid);
    if (it == m_document->mutableProject().entities.end())
        return nullptr;
    return &it.value();
}

bool PropertiesPanel::editable() const
{
    const model::Entity* entity = selectedEntity();
    return entity && !isEntityLocked(m_document->project(), entity->uuid);
}

bool PropertiesPanel::commitEdit(const QString& what,
                                 const std::function<bool(model::Entity&)>& mutate,
                                 bool ignoreLock)
{
    const model::Entity* entity = selectedEntity();
    if (!entity)
        return false;
    const QString message =
        QStringLiteral("pvf: update entity \"%1\" %2").arg(entity->name, what);
    return commitEditWithMessage(what, message, mutate, ignoreLock);
}

bool PropertiesPanel::commitEditWithMessage(
    const QString& what, const QString& message,
    const std::function<bool(model::Entity&)>& mutate, bool ignoreLock)
{
    if (m_loading || !m_document)
        return false;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return false;
    if (isEntityLocked(m_document->project(), entity->uuid) && !ignoreLock)
        return false;

    const QJsonObject before = model::toJson(*entity);
    const model::Entity original = *entity;
    model::Entity updated = *entity;
    if (!mutate(updated))
    {
        refresh();
        return false;
    }

    QString error;
    if (!model::validateEntity(updated, &error))
    {
        setStatus(QStringLiteral("未修改：%1").arg(error));
        refresh();
        return false;
    }
    if (model::toJson(updated) == before)
        return false;

    const Uuid uuid = updated.uuid;
    *entity = updated;

    m_committing = true;
    const bool ok = m_document->commitEntity(uuid, message, &error);
    m_committing = false;

    if (!ok)
    {
        // The store failed to save; put the in-memory entity back so the panel
        // never shows a half-applied edit.
        model::Entity* current = selectedEntity();
        if (current && current->uuid == original.uuid)
            *current = original;
        showSaveError(error);
        refresh();
        return false;
    }

    setStatus(QStringLiteral("已保存：%1").arg(what));
    refresh();
    return true;
}

void PropertiesPanel::applyRangeEdit(int newStart, int newEnd,
                                     const QString& what)
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能修改时间范围"));
        refresh();
        return;
    }

    const Uuid uuid = entity->uuid;
    const QString name = entity->name;
    if (newStart == entity->start && newEnd == entity->end)
        return;

    const model::Project snapshot = m_document->project();
    model::Project updated;
    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    QString error;
    if (!applyEntityRange(snapshot, m_document->assets(), uuid, newStart,
                          newEnd, &updated, &upserts, &deletes, &error))
    {
        setStatus(QStringLiteral("未修改：%1").arg(error));
        if (isVisible())
            QMessageBox::warning(this, QStringLiteral("不能修改时间范围"), error);
        refresh();
        return;
    }
    if (upserts.isEmpty() && deletes.isEmpty())
        return;

    m_document->mutableProject() = std::move(updated);
    m_committing = true;
    const bool ok = m_document->commitChanges(
        upserts, deletes,
        QStringLiteral("pvf: change entity range \"%1\" %2").arg(name, what),
        &error);
    m_committing = false;

    if (!ok)
    {
        m_document->mutableProject() = snapshot;
        showSaveError(error);
        refresh();
        return;
    }

    setStatus(QStringLiteral("已保存：%1").arg(what));
    refresh();
}

void PropertiesPanel::bindTransform(QDoubleSpinBox* spin, const QString& field,
                                    double model::Transform::*member)
{
    const QString track = field.section(QLatin1Char('.'), -1);
    connect(spin, &QDoubleSpinBox::editingFinished, this,
            [this, spin, field, track, member] {
                const double value = spin->value();
                commitEdit(field, [this, member, track, value](model::Entity& entity) {
                    entity.transform.*member = value;
                    applyAutoKey(entity, track, value);
                    return true;
                });
            });
}

void PropertiesPanel::bindCheckBox(QCheckBox* box, bool model::Entity::*member,
                                   const QString& field)
{
    connect(box, &QCheckBox::toggled, this, [this, member, field](bool checked) {
        if (m_loading)
            return;
        commitEdit(field, [member, checked](model::Entity& entity) {
            entity.*member = checked;
            return true;
        });
    });
}

void PropertiesPanel::addEditable(QWidget* widget)
{
    m_editable.append(widget);
}

void PropertiesPanel::setStatus(const QString& text)
{
    m_status->setText(text);
}

void PropertiesPanel::showSaveError(const QString& error)
{
    const QString text = error.isEmpty() ? QStringLiteral("保存失败") : error;
    setStatus(QStringLiteral("保存失败：%1").arg(text));
    // Keep automated/headless runs from blocking on a modal dialog; the status
    // label still carries the error and the entity was rolled back above.
    if (isVisible())
        QMessageBox::warning(this, QStringLiteral("保存失败"), text);
}

void PropertiesPanel::refresh()
{
    if (m_loading)
        return;

    m_loading = true;
    model::Entity* entity = selectedEntity();
    const Uuid uuid = entity ? entity->uuid : Uuid();

    if (!entity)
    {
        m_title->setText(QStringLiteral("未选择对象"));
        m_title->setToolTip(QString());
        m_placeholder->setVisible(true);
        m_scroll->setVisible(false);
    }
    else
    {
        QString title =
            QStringLiteral("%1 · %2")
                .arg(entity->name, contentKindLabel(entity->content.kind));
        if (isEntityLocked(m_document->project(), entity->uuid))
            title += QStringLiteral(" · 已锁定");
        m_title->setText(title);
        m_title->setToolTip(entity->uuid);
        m_placeholder->setVisible(false);
        m_scroll->setVisible(true);

        reloadBasic(*entity);
        reloadTransform(*entity);
        reloadKeyframes(*entity);
        reloadContent(*entity);
        reloadHtmlVariables(*entity);
        updateSolverCards(*entity);
    }

    if (uuid != m_lastUuid)
    {
        m_lastUuid = uuid;
        m_status->clear();
    }

    applyEditingState();
    m_loading = false;
}

} // namespace pvf::next
