#pragma once

#include "JsonUtil.h"
#include "common/Types.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

namespace pvf::model {

struct Transform
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double pivotX = 0.0;
    double pivotY = 0.0;
    double opacity = 1.0;
};

struct TextContent
{
    QString text;
    QString fontFamily;
    int pixelSize = 96;
    QString color = QStringLiteral("#ffffff");
    QString strokeColor = QStringLiteral("#000000");
    double strokeWidth = 0.0;
    int align = 132;
    double lineHeight = 1.0;
    double letterSpacing = 0.0;
    double padding = 6.0;
    double boxWidth = 0.0;
    double boxHeight = 0.0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool vertical = false;
    bool verticalRtl = true;
};

struct VectorContent
{
    int shape = 0;
    double width = 400.0;
    double height = 300.0;
    double cornerRadius = 40.0;
    int sides = 5;
    double innerRatio = 0.45;
    double strokeWidth = 0.0;
    QString fillColor = QStringLiteral("#ffffff");
    QString strokeColor = QStringLiteral("#000000");
};

struct CameraContent
{
};

struct AssetContent
{
    AssetHash hash;
    AssetType type = AssetType::Image;
    int sourceIn = 0;
    double volume = 1.0;
    // Audio-only envelope in seconds.  Fades are clipped to the entity range.
    double fadeIn = 0.0;
    double fadeOut = 0.0;
};

struct Content
{
    ContentKind kind = ContentKind::Empty;
    TextContent text;
    VectorContent vector;
    CameraContent camera;
    AssetContent asset;
    // Unknown top-level keys, preserved verbatim across a load/save cycle.
    QJsonObject extras;
};

QJsonObject toJson(const Transform& transform);
std::optional<Transform> transformFromJson(const QJsonObject& object,
                                           QString* error = nullptr);

QJsonObject toJson(const Content& content);
std::optional<Content> contentFromJson(const QJsonObject& object,
                                       const ParseOptions& options,
                                       QString* error = nullptr);
std::optional<Content> contentFromJson(const QJsonObject& object,
                                       QString* error = nullptr);

// Checks the content fields that have range constraints.  Unknown enum names,
// non-finite numbers and out-of-range sizes/counts produce a specific error.
bool validateContent(const Content& content, QString* error = nullptr);

// ---------------------------------------------------------------------------
// Animation parameter schema
//
// Every parameter is either an animation parameter (continuous number, kept in
// Keyframe::content and interpolated) or a definition parameter (static, part
// of Content itself).  The schema is fixed here; project JSON never declares
// types.  HTML numeric variables live in a Keyframe's htmlVars instead.
//
//   text:   pixelSize, lineHeight, letterSpacing, padding, boxWidth,
//           boxHeight, strokeWidth
//   vector: width, height, cornerRadius, strokeWidth
//   audio:  volume
//   image/video/html/camera/empty: no content animation parameters
// ---------------------------------------------------------------------------

// Animation parameter names for this content kind (empty for kinds without
// content animation).
QStringList animationKeys(const Content& content);
bool isAnimationKey(const Content& content, const QString& key);

// Current static values of every animation parameter, suitable as the
// `fallback.content` of evaluateKeyframes().  Definition parameters are not
// included.
QJsonObject animationValues(const Content& content);

// Writes `values` into a Content copy.  Unknown keys are ignored here; loading
// validates them first.  Integer fields (pixelSize) are rounded.
void applyAnimationValues(const QJsonObject& values, Content* content);

// Validates one keyframe's `content` object: every key must be an animation
// parameter of this content and every value must be a finite number.
bool validateKeyframeContent(const Content& content, const QJsonObject& values,
                             QString* error = nullptr);
bool validateKeyframeHtmlVars(const QJsonObject& values,
                              QString* error = nullptr);

} // namespace pvf::model
