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

} // namespace pvf::next
