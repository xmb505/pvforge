#include "Lang.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace pvf {
namespace Lang {

namespace {
QHash<QString, QString> g_strings;
}

void load(const QString& code)
{
    g_strings.clear();

    QFile file(QStringLiteral(":/lang/%1.json").arg(code));
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        g_strings.insert(it.key(), it.value().toString());
}

QString t(const QString& key, const QString& fallback)
{
    const auto it = g_strings.constFind(key);
    if (it != g_strings.constEnd())
        return it.value();
    return fallback.isEmpty() ? key : fallback;
}

} // namespace Lang
} // namespace pvf
