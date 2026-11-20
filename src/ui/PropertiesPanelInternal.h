#pragma once

#include "common/Types.h"
#include "model/Entity.h"
#include "model/Project.h"

#include <QColor>
#include <QMargins>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QSpinBox;
class QWidget;

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

// Native Qt form rhythm shared by every property section: left-aligned labels,
// one growing field column and identical margins/spacing.
void configurePropertyForm(
    QFormLayout* form,
    const QMargins& margins = QMargins(10, 8, 10, 8));

// Gives every label column the same floor so forms in different sections line
// their right-hand field columns up.
void equalizePropertyFormLabels(QFormLayout* form, int minimumWidth = 80);

// Secondary explanatory text, using the palette instead of a hardcoded colour.
void styleHintLabel(QLabel* label);

// Spin boxes, combos and line edits all fill the same right-hand column.
void stylePropertyField(QWidget* field);

} // namespace pvf::ui::detail
