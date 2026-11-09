#include "next/core/Document.h"

namespace pvf::next {

Document::Document(QObject* parent)
    : QObject(parent)
{
}

Document::~Document() = default;

bool Document::commitEntities(const QVector<Uuid>& uuids, const QString& message,
                              QString* error)
{
    for (const Uuid& uuid : uuids)
    {
        if (!commitEntity(uuid, message, error))
            return false;
    }
    return true;
}

bool Document::deleteEntities(const QVector<Uuid>& uuids, const QString& message,
                              QString* error)
{
    Q_UNUSED(uuids);
    Q_UNUSED(message);
    if (error)
        *error = QStringLiteral("deleteEntities 未实现");
    return false;
}

bool Document::commitChanges(const QVector<Uuid>& upserts,
                             const QVector<Uuid>& deletes,
                             const QString& message,
                             QString* error)
{
    if (!deleteEntities(deletes, message, error))
        return false;
    return commitEntities(upserts, message, error);
}

} // namespace pvf::next
