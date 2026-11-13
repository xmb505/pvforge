#include "PropertiesPanel.h"

#include "PropertiesPanelInternal.h"
#include "common/Types.h"
#include "model/Easing.h"
#include "next/core/EntityLock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace pvf::next {

namespace {

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

} // namespace

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
        row.hasKey = inRange && detail::hasKeyAt(entity, row.track, relativeFrame);

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

    const bool remove = detail::hasKeyAt(*entity, track, relativeFrame);
    if (remove && detail::isSolverStartKey(*entity, track, relativeFrame))
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
    const bool hasKey = inRange && detail::hasKeyAt(*entity, track, relativeFrame);
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
        || !detail::hasKeyAt(*entity, track, relativeFrame))
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

} // namespace pvf::next
