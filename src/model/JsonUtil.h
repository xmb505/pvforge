#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace pvf::model::detail {

inline void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

inline QString jsonString(const QJsonObject& object, const QString& key,
                          const QString& fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

inline int jsonInt(const QJsonObject& object, const QString& key, int fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt(fallback) : fallback;
}

inline double jsonDouble(const QJsonObject& object, const QString& key, double fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

inline bool jsonBool(const QJsonObject& object, const QString& key, bool fallback)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

inline QStringList jsonStringList(const QJsonObject& object, const QString& key)
{
    QStringList result;
    const QJsonArray array = object.value(key).toArray();
    for (const QJsonValue& value : array)
    {
        if (value.isString())
            result.append(value.toString());
    }
    return result;
}

} // namespace pvf::model::detail
