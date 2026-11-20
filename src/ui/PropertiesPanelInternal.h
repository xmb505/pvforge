#pragma once

#include "common/Types.h"
#include "model/Entity.h"
#include "model/Project.h"

#include <QColor>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace pvf::ui::detail {

// Helpers shared by the split PropertiesPanel sources.

// Localised label for a content kind ("文字", "素材", ...).
QString contentKindLabel(ContentKind kind);

// Localised label for an asset type.
QString assetTypeLabel(AssetType type);

// Serialised colour string used by the model (hex RGB, or ARGB with alpha).
QString colorString(const QColor& color);

// True when `candidate` is `ancestor` or one of its descendants.
bool isDescendant(const model::Project& project, const Uuid& candidate,
                  const Uuid& ancestor);

void selectComboData(QComboBox* combo, int value);
void setSpinIfIdle(QDoubleSpinBox* spin, double value);
void setSpinIfIdle(QSpinBox* spin, int value);

} // namespace pvf::ui::detail
