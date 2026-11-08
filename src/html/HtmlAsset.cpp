#include "html/HtmlAsset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <cmath>

namespace pvf {
namespace html {

namespace {

bool readSize(const QJsonValue& value, QSize* size, QString* error)
{
    if (!value.isArray())
    {
        *error = QStringLiteral("manifest.size must be [width, height]");
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array.at(0).isDouble() || !array.at(1).isDouble())
    {
        *error = QStringLiteral("manifest.size must be [width, height]");
        return false;
    }
    const double width = array.at(0).toDouble();
    const double height = array.at(1).toDouble();
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0)
    {
        *error = QStringLiteral("manifest.size values must be positive");
        return false;
    }
    if (width > Limits::kMaxEdge || height > Limits::kMaxEdge)
    {
        *error = QStringLiteral("manifest.size exceeds the %1 px limit")
                     .arg(Limits::kMaxEdge);
        return false;
    }
    *size = QSize(qRound(width), qRound(height));
    if (size->width() <= 0 || size->height() <= 0)
    {
        *error = QStringLiteral("manifest.size rounds to an empty size");
        return false;
    }
    return true;
}

bool readVariable(const QJsonObject& object, HtmlVariable* variable, QString* error)
{
    variable->name = object.value(QStringLiteral("name")).toString();
    if (variable->name.isEmpty())
    {
        *error = QStringLiteral("a manifest variable has no name");
        return false;
    }
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("number"))
    {
        *error = QStringLiteral("variable '%1' must have type 'number'").arg(variable->name);
        return false;
    }
    const QJsonValue min = object.value(QStringLiteral("min"));
    const QJsonValue max = object.value(QStringLiteral("max"));
    if (!min.isDouble() || !max.isDouble())
    {
        *error = QStringLiteral("variable '%1' needs numeric min and max").arg(variable->name);
        return false;
    }
    variable->min = min.toDouble();
    variable->max = max.toDouble();
    if (!std::isfinite(variable->min) || !std::isfinite(variable->max)
        || variable->min > variable->max)
    {
        *error = QStringLiteral("variable '%1' has an invalid range").arg(variable->name);
        return false;
    }
    const QJsonValue defaultValue = object.value(QStringLiteral("default"));
    variable->defaultValue = defaultValue.isDouble() ? defaultValue.toDouble() : variable->min;
    if (!std::isfinite(variable->defaultValue)
        || variable->defaultValue < variable->min - 1e-9
        || variable->defaultValue > variable->max + 1e-9)
    {
        *error = QStringLiteral("variable '%1' has a default outside its range")
                     .arg(variable->name);
        return false;
    }
    variable->integer = object.value(QStringLiteral("integer")).toBool(false);
    return true;
}

} // namespace

HtmlAsset HtmlAsset::parse(const QByteArray& json, QString* error)
{
    HtmlAsset asset;
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return HtmlAsset{};
    };

    if (json.size() > Limits::kMaxAssetBytes)
        return fail(QStringLiteral("HTML asset exceeds %1 MB")
                        .arg(Limits::kMaxAssetBytes / (1024 * 1024)));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(QStringLiteral("invalid asset JSON at offset %1: %2")
                        .arg(parseError.offset)
                        .arg(parseError.errorString()));
    if (!document.isObject())
        return fail(QStringLiteral("HTML asset must be a JSON object"));

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("pvforge-html"))
        return fail(QStringLiteral("format must be 'pvforge-html'"));
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return fail(QStringLiteral("unsupported pvforge-html version (expected 1)"));

    asset.html = root.value(QStringLiteral("html")).toString();
    if (asset.html.isEmpty())
        return fail(QStringLiteral("html must be a non-empty string"));
    const int htmlBytes = asset.html.toUtf8().size();
    if (htmlBytes > Limits::kMaxHtmlBytes)
        return fail(QStringLiteral("inline HTML exceeds %1 MB")
                        .arg(Limits::kMaxHtmlBytes / (1024 * 1024)));
    asset.sourceBytes = int(json.size());

    const QJsonValue manifestValue = root.value(QStringLiteral("manifest"));
    if (!manifestValue.isObject())
        return fail(QStringLiteral("manifest must be an object"));
    const QJsonObject manifest = manifestValue.toObject();

    QString sizeError;
    if (!readSize(manifest.value(QStringLiteral("size")), &asset.logicalSize, &sizeError))
        return fail(sizeError);

    asset.transparent = manifest.value(QStringLiteral("transparent")).toBool(true);
    asset.timeDependent = manifest.value(QStringLiteral("timeDependent")).toBool(false);

    const QJsonValue componentsValue = manifest.value(QStringLiteral("components"));
    if (!componentsValue.isArray())
        return fail(QStringLiteral("manifest.components must be an array"));
    const QJsonArray components = componentsValue.toArray();
    if (components.size() > Limits::kMaxComponents)
        return fail(QStringLiteral("manifest declares more than %1 components")
                        .arg(Limits::kMaxComponents));

    QSet<QString> componentIds;
    for (const QJsonValue& componentValue : components)
    {
        if (!componentValue.isObject())
            return fail(QStringLiteral("manifest.components entries must be objects"));
        const QJsonObject componentObject = componentValue.toObject();
        HtmlComponent component;
        component.id = componentObject.value(QStringLiteral("id")).toString();
        component.name = componentObject.value(QStringLiteral("name")).toString();
        if (component.id.isEmpty())
            return fail(QStringLiteral("a manifest component has no id"));
        if (componentIds.contains(component.id))
            return fail(QStringLiteral("duplicate component id '%1'").arg(component.id));
        componentIds.insert(component.id);

        const QJsonValue variablesValue = componentObject.value(QStringLiteral("variables"));
        if (!variablesValue.isUndefined() && !variablesValue.isArray())
            return fail(QStringLiteral("component '%1' variables must be an array")
                            .arg(component.id));
        const QJsonArray variables = variablesValue.toArray();
        if (variables.size() > Limits::kMaxVariablesPerComponent)
            return fail(QStringLiteral("component '%1' declares more than %2 variables")
                            .arg(component.id)
                            .arg(Limits::kMaxVariablesPerComponent));
        QSet<QString> variableNames;
        for (const QJsonValue& variableValue : variables)
        {
            if (!variableValue.isObject())
                return fail(QStringLiteral("component '%1' has an invalid variable")
                                .arg(component.id));
            HtmlVariable variable;
            QString variableError;
            if (!readVariable(variableValue.toObject(), &variable, &variableError))
                return fail(variableError);
            if (variableNames.contains(variable.name))
                return fail(QStringLiteral("component '%1' declares duplicate variable '%2'")
                                .arg(component.id, variable.name));
            variableNames.insert(variable.name);
            component.variables.append(variable);
        }
        asset.components.append(component);
    }

    if (error)
        error->clear();
    return asset;
}

HtmlAsset HtmlAsset::parseFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("cannot open HTML asset: %1").arg(file.errorString());
        return {};
    }
    if (file.size() > Limits::kMaxAssetBytes)
    {
        if (error)
            *error = QStringLiteral("HTML asset exceeds %1 MB")
                         .arg(Limits::kMaxAssetBytes / (1024 * 1024));
        return {};
    }
    return parse(file.readAll(), error);
}

QVector<QString> HtmlAsset::variableKeys() const
{
    QVector<QString> keys;
    for (const HtmlComponent& component : components)
        for (const HtmlVariable& variable : component.variables)
            keys.append(component.id + QLatin1Char('.') + variable.name);
    return keys;
}

const HtmlVariable* HtmlAsset::findVariable(const QString& key) const
{
    const int dot = key.indexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == key.size() - 1)
        return nullptr;
    const QString componentId = key.left(dot);
    const QString variableName = key.mid(dot + 1);
    for (const HtmlComponent& component : components)
    {
        if (component.id != componentId)
            continue;
        for (const HtmlVariable& variable : component.variables)
            if (variable.name == variableName)
                return &variable;
    }
    return nullptr;
}

} // namespace html
} // namespace pvf
