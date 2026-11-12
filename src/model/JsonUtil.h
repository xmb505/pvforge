#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace pvf::model {

// How a JSON document is read.
//
// Strict is used when opening a project: a missing required key is an error
// that names the exact JSON path. Lenient is used by tests and by migration
// helpers: a missing key falls back to its default (with a warning), so an
// old document can still be inspected.
//
// Unknown keys are never lost in either mode: they are collected into the
// `extras` object of the parsed value and written back by toJson().
enum class ParseMode
{
    Strict,
    Lenient,
};

struct ParseOptions
{
    ParseMode mode = ParseMode::Lenient;
    // JSON path of the object being parsed, e.g. "content" or "markers[2]".
    QString prefix;
    // Optional diagnostics sink. Missing optional keys, missing required keys
    // in lenient mode and wrongly typed values are appended here.
    QStringList* warnings = nullptr;
};

namespace detail {

inline void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

inline QString joinPath(const QString& prefix, const QString& key)
{
    return prefix.isEmpty() ? key : prefix + QLatin1Char('.') + key;
}

inline QString pathOf(const ParseOptions& options, const QString& key)
{
    return joinPath(options.prefix, key);
}

inline void warn(const ParseOptions& options, const QString& message)
{
    if (options.warnings)
        options.warnings->append(message);
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

// True when the key exists with a concrete (non-null) value.
inline bool hasField(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    return !value.isUndefined() && !value.isNull();
}

// Options for a nested object: the JSON path prefix grows so diagnostics name
// the enclosing field.
inline ParseOptions childOptions(const ParseOptions& options,
                                 const QString& prefix)
{
    ParseOptions child = options;
    child.prefix = joinPath(options.prefix, prefix);
    return child;
}

// Required key. In strict mode a missing key fails with its full JSON path; in
// lenient mode the caller keeps its default and one warning is recorded.
inline bool requireField(const QJsonObject& object, const QString& key,
                         const ParseOptions& options, QString* error)
{
    if (hasField(object, key))
        return true;

    const QString path = pathOf(options, key);
    if (options.mode == ParseMode::Strict)
    {
        setError(error, QStringLiteral("缺少必填字段：%1").arg(path));
        return false;
    }
    warn(options, QStringLiteral("缺少必填字段 %1，使用默认值").arg(path));
    return true;
}

// Optional key: silence would hide a truncated document, so a missing key is
// always reported as a warning when a sink is provided.
inline void warnDefaulted(const QJsonObject& object, const QString& key,
                          const ParseOptions& options)
{
    if (!hasField(object, key))
    {
        warn(options, QStringLiteral("缺少可选字段 %1，使用默认值")
                          .arg(pathOf(options, key)));
    }
}

inline void warnWrongType(const QString& path, const QString& expected,
                          const ParseOptions& options)
{
    warn(options, QStringLiteral("字段 %1 不是%2，使用默认值")
                      .arg(path, expected));
}

inline QString optionalString(const QJsonObject& object, const QString& key,
                              const QString& fallback,
                              const ParseOptions& options)
{
    if (!hasField(object, key))
    {
        warnDefaulted(object, key, options);
        return fallback;
    }
    if (!object.value(key).isString())
    {
        warnWrongType(pathOf(options, key), QStringLiteral("字符串"), options);
        return fallback;
    }
    return object.value(key).toString();
}

inline int optionalInt(const QJsonObject& object, const QString& key,
                       int fallback, const ParseOptions& options)
{
    if (!hasField(object, key))
    {
        warnDefaulted(object, key, options);
        return fallback;
    }
    if (!object.value(key).isDouble())
    {
        warnWrongType(pathOf(options, key), QStringLiteral("数字"), options);
        return fallback;
    }
    return object.value(key).toInt(fallback);
}

inline bool optionalBool(const QJsonObject& object, const QString& key,
                         bool fallback, const ParseOptions& options)
{
    if (!hasField(object, key))
    {
        warnDefaulted(object, key, options);
        return fallback;
    }
    if (!object.value(key).isBool())
    {
        warnWrongType(pathOf(options, key), QStringLiteral("布尔值"), options);
        return fallback;
    }
    return object.value(key).toBool();
}

inline double optionalDouble(const QJsonObject& object, const QString& key,
                             double fallback, const ParseOptions& options)
{
    if (!hasField(object, key))
    {
        warnDefaulted(object, key, options);
        return fallback;
    }
    if (!object.value(key).isDouble())
    {
        warnWrongType(pathOf(options, key), QStringLiteral("数字"), options);
        return fallback;
    }
    return object.value(key).toDouble();
}

// Unknown top-level keys of `object`, in document order. toJson() writes them
// back so a document that passes through this program keeps fields added by a
// newer version or by an external tool.
inline QJsonObject extrasFrom(const QJsonObject& object,
                              const QStringList& known)
{
    QJsonObject extras;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        bool isKnown = false;
        for (const QString& key : known)
        {
            if (it.key() == key)
            {
                isKnown = true;
                break;
            }
        }
        if (!isKnown)
            extras.insert(it.key(), it.value());
    }
    return extras;
}

// Writes preserved unknown keys last. A known key always wins, so a stale
// extras entry can never shadow a real field.
inline void mergeExtras(QJsonObject& object, const QJsonObject& extras)
{
    for (auto it = extras.constBegin(); it != extras.constEnd(); ++it)
    {
        if (!object.contains(it.key()))
            object.insert(it.key(), it.value());
    }
}

} // namespace detail

} // namespace pvf::model
