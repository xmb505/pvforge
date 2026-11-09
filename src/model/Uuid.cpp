#include "Uuid.h"

#include <QUuid>

namespace pvf::model {

Uuid newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isValidUuid(const Uuid& uuid)
{
    if (uuid.size() != 36)
        return false;

    for (int i = 0; i < uuid.size(); ++i)
    {
        const QChar ch = uuid.at(i);
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (ch != QLatin1Char('-'))
                return false;
            continue;
        }

        const bool hex = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                         || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
                         || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if (!hex)
            return false;
    }

    return !QUuid::fromString(uuid).isNull();
}

} // namespace pvf::model
