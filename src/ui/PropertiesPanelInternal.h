#pragma once

#include "common/Types.h"
#include "model/Easing.h"
#include "model/Entity.h"
#include "model/Project.h"

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace pvf::ui::detail {

// Helpers shared by the split PropertiesPanel sources.  They live here instead
// of a per-translation-unit anonymous namespace because the core, content,
// keyframe, HTML and solver pages all need them.

// Localised label for a content kind ("文字", "素材", ...).
QString contentKindLabel(ContentKind kind);

// Localised label for an asset type.
QString assetTypeLabel(AssetType type);

// Serialised colour string used by the model (hex RGB, or ARGB with alpha).
QString colorString(const QColor& color);

// One animated track row in the keyframe page.
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
                       const QString& suffix = {});
TrackSpec stepTrackSpec(const QString& name, const QString& label);
QVector<TrackSpec> commonTrackSpecs(bool camera);
QVector<TrackSpec> trackSpecsFor(const model::Entity& entity);

bool isStepTrack(const QString& track);
std::optional<double> staticTrackValue(const model::Entity& entity,
                                       const QString& track);
bool setStaticTrackValue(model::Entity& entity, const QString& track,
                         double value);
bool hasKeyAt(const model::Entity& entity, const QString& trackName, int frame);
bool hasAnyKeyAt(const model::Entity& entity, int frame);
// Reads the easing of the merged keyframe group at `frame`; `mixed` is set when
// the tracks disagree.
bool frameEasing(const model::Entity& entity, int frame,
                 model::EasingSpec* easing, bool* mixed);
int previousGroupKeyframeFrame(const model::Entity& entity, int frame);
void setKeyframeValue(model::Entity& entity, const QString& track, int frame,
                      double value);
void removeKeyframeAt(model::Entity& entity, const QString& track, int frame);
double currentTrackValue(const model::Entity& entity, const QString& track,
                         int relativeFrame, bool step,
                         std::optional<double> fallback = std::nullopt);

// Tracks a solver drives: rotation for spin, x/y for the rest.
QStringList solverDrivenTracks(SolverEffect effect);
bool trackBlockedAt(const model::Entity& entity, const QString& track, int frame);
bool isDescendant(const model::Project& project, const Uuid& candidate,
                  const Uuid& ancestor);

void selectComboData(QComboBox* combo, int value);
void setSpinIfIdle(QDoubleSpinBox* spin, double value);
void setSpinIfIdle(QSpinBox* spin, int value);

} // namespace pvf::ui::detail
