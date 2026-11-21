#include "PropertiesPanelInternal.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPalette>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace pvf::ui::detail {

namespace {

constexpr double kAudioMuteDb = -60.0;
constexpr double kAudioMaxDb = 24.0;

} // namespace

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

bool isAudioAsset(const model::Entity& entity)
{
    return entity.content.kind == ContentKind::Asset
           && entity.content.asset.type == AssetType::Audio;
}

double audioGainDbFromLinear(double volume)
{
    if (volume <= 1e-3)
        return kAudioMuteDb;
    return std::clamp(20.0 * std::log10(volume), kAudioMuteDb, kAudioMaxDb);
}

double audioLinearGainFromDb(double db)
{
    return db <= kAudioMuteDb ? 0.0 : std::pow(10.0, db / 20.0);
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

void configurePropertyForm(QFormLayout* form, const QMargins& margins)
{
    if (!form)
        return;
    form->setContentsMargins(margins);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
}

void equalizePropertyFormLabels(QFormLayout* form, int minimumWidth)
{
    if (!form)
        return;
    for (int row = 0; row < form->rowCount(); ++row)
    {
        QLayoutItem* item = form->itemAt(row, QFormLayout::LabelRole);
        if (QWidget* label = item ? item->widget() : nullptr)
            label->setMinimumWidth(minimumWidth);
    }
}

void styleHintLabel(QLabel* label)
{
    if (!label)
        return;
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText,
                     palette.color(QPalette::Disabled, QPalette::WindowText));
    label->setPalette(palette);
}

void stylePropertyField(QWidget* field)
{
    if (field)
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

} // namespace pvf::ui::detail
