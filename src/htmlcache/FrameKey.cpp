#include "htmlcache/FrameKey.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace pvf::htmlcache {
namespace {

constexpr int kReadablePrefixLimit = 160;
constexpr int kDigestChars = 32;

void appendPart(QByteArray& payload, const QByteArray& part)
{
    payload += QByteArray::number(part.size());
    payload += ':';
    payload += part;
    payload += ';';
}

QString formatDouble(double value)
{
    // Normalise negative zero so 0.0 and -0.0 share a cache key.
    if (value == 0.0)
        value = 0.0;
    if (std::isnan(value))
        return QStringLiteral("nan");
    if (std::isinf(value))
        return value < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    return QString::number(value, 'f', 6);
}

QString encodeTyped(const QVariant& value)
{
    switch (value.typeId())
    {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("b:1") : QStringLiteral("b:0");
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QStringLiteral("i:") + QString::number(value.toLongLong());
    case QMetaType::Float:
    case QMetaType::Double:
        return QStringLiteral("f:") + formatDouble(value.toDouble());
    case QMetaType::QString:
        return QStringLiteral("s:") + value.toString();
    case QMetaType::QByteArray:
        return QStringLiteral("x:") + QString::fromLatin1(value.toByteArray().toHex());
    case QMetaType::QStringList: {
        const QStringList list = value.toStringList();
        QStringList encoded;
        encoded.reserve(list.size());
        for (const QString& item : list)
            encoded << QString::number(item.size()) + QLatin1Char(':') + item;
        return QStringLiteral("l:[") + encoded.join(QLatin1Char(';')) + QLatin1Char(']');
    }
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        QStringList encoded;
        encoded.reserve(list.size());
        for (const QVariant& item : list)
        {
            const QString part = encodeTyped(item);
            encoded << QString::number(part.size()) + QLatin1Char(':') + part;
        }
        return QStringLiteral("a:[") + encoded.join(QLatin1Char(';')) + QLatin1Char(']');
    }
    case QMetaType::QVariantMap:
    case QMetaType::QVariantHash: {
        const QVariantMap map = value.toMap();
        QStringList names = map.keys();
        std::sort(names.begin(), names.end());
        QStringList encoded;
        encoded.reserve(names.size());
        for (const QString& name : names)
        {
            const QString part = encodeTyped(map.value(name));
            encoded << QString::number(name.size()) + QLatin1Char(':') + name
                           + QLatin1Char('=') + QString::number(part.size()) + QLatin1Char(':')
                           + part;
        }
        return QStringLiteral("m:{") + encoded.join(QLatin1Char(';')) + QLatin1Char('}');
    }
    default:
        break;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(value);
    if (document.isNull())
        return QStringLiteral("j:") + value.toString();
    if (document.isArray())
        return QStringLiteral("j:") + QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    if (document.isObject())
        return QStringLiteral("j:") + QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    return QStringLiteral("j:") + value.toString();
}

QString displayVariant(const QVariant& value)
{
    switch (value.typeId())
    {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Float:
    case QMetaType::Double:
        return formatDouble(value.toDouble());
    case QMetaType::QByteArray:
        return QString::fromLatin1(value.toByteArray().toHex());
    case QMetaType::QStringList:
        return value.toStringList().join(QLatin1Char(','));
    default:
        break;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(value);
    if (document.isNull())
        return value.toString();
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

QString encodeComponent(const QString& text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

} // namespace

QString canonicalKey(const FrameKey& key)
{
    QStringList names = key.variables.keys();
    std::sort(names.begin(), names.end());

    QByteArray payload;
    appendPart(payload, QByteArrayLiteral("pvf-htmlcache-frame-v1"));
    appendPart(payload, key.assetHash.toUtf8());
    appendPart(payload, QByteArray::number(key.localFrame));
    appendPart(payload, QByteArray::number(key.size.width()));
    appendPart(payload, QByteArray::number(key.size.height()));
    appendPart(payload, key.chromeVersion.toUtf8());
    appendPart(payload, key.entryHash.toUtf8());
    appendPart(payload, QByteArray::number(names.size()));
    for (const QString& name : names)
    {
        appendPart(payload, name.toUtf8());
        appendPart(payload, encodeTyped(key.variables.value(name)).toUtf8());
    }

    const QString digest =
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());

    QStringList parts;
    parts.reserve(7);
    parts << QStringLiteral("html1");
    parts << encodeComponent(key.assetHash);
    parts << QStringLiteral("f%1").arg(key.localFrame);
    parts << QStringLiteral("%1x%2").arg(key.size.width()).arg(key.size.height());
    parts << QStringLiteral("c%1").arg(encodeComponent(key.chromeVersion));
    parts << QStringLiteral("e%1").arg(encodeComponent(key.entryHash));
    if (!names.isEmpty())
    {
        QStringList variables;
        variables.reserve(names.size());
        for (const QString& name : names)
        {
            variables << encodeComponent(name) + QLatin1Char('=')
                             + encodeComponent(displayVariant(key.variables.value(name)));
        }
        parts << QStringLiteral("v%1").arg(variables.join(QLatin1Char('&')));
    }

    QString readable = parts.join(QLatin1Char('-'));
    readable = readable.left(kReadablePrefixLimit);
    return readable + QLatin1Char('-') + digest.left(kDigestChars);
}

} // namespace pvf::htmlcache
