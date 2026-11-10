#pragma once

#include "ui/TimelineData.h"

#include <QHash>

namespace pvf {

class EditorSession;

// Converts EditorSession::project() (new entity model) into the read-only DTO
// the legacy TimelinePanel already knows how to paint.  It owns only the
// int <-> uuid view mapping and a display snapshot; project data is never
// copied back, so there is no second source of truth.
class TimelineAdapter
{
public:
    explicit TimelineAdapter(EditorSession* session);

    void refresh();
    const TimelineSnapshot& snapshot() const { return m_snapshot; }

    // Session-stable entity ids for the legacy int based signals.
    int idForUuid(const Uuid& uuid) const;
    Uuid uuidForId(int id) const;

private:
    int idFor(const Uuid& uuid);

    EditorSession* m_session = nullptr;
    TimelineSnapshot m_snapshot;
    QHash<Uuid, int> m_idByUuid;
    QHash<int, Uuid> m_uuidById;
    int m_nextId = 1;
};

} // namespace pvf
