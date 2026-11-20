#include "PropertiesPanelInternal.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>

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
