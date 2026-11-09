#pragma once

#include "common/Types.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>

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
};

struct Content
{
    ContentKind kind = ContentKind::Empty;
    TextContent text;
    VectorContent vector;
    CameraContent camera;
    AssetContent asset;
};

QJsonObject toJson(const Transform& transform);
std::optional<Transform> transformFromJson(const QJsonObject& object,
                                           QString* error = nullptr);

QJsonObject toJson(const Content& content);
std::optional<Content> contentFromJson(const QJsonObject& object,
                                       QString* error = nullptr);

} // namespace pvf::model
