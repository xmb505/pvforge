#pragma once

#include "model/Project.h"

#include <QString>
#include <QVector>

#include <limits>

class QWidget;

namespace pvf {

class EditorSession;

// One validated, whole-project edit.  Plans are produced from the current
// document without writing anything; the timeline previews a plan during a
// drag and commits the same plan once on release.
struct EditPlan
{
    model::Project project;
    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    QString message;
    // True when the plan rewrites project.json (markers) instead of entity
    // files.  Marker gestures and entity gestures never mix in one plan.
    bool projectChanged = false;

    bool isEmpty() const
    {
        return upserts.isEmpty() && deletes.isEmpty() && !projectChanged;
    }
};

// One keyframe address.  frame is entity-relative.  An empty track means
// "every track that has a key at this frame" (the merged diamond).
struct KeyRef
{
    Uuid entity;
    QString track;
    int frame = 0;
};

// A keyframe with its track, used by copy/paste.
struct TrackKey
{
    QString track;
    model::Keyframe key;
};

// The only entry point for human timeline writes.  A gesture builds one plan
// and calls apply() once, so ProjectDocument records exactly one undo step.
// Entity ranges and their cascade rules come from next::applyEntityRange.
class EditActions
{
public:
    explicit EditActions(EditorSession* session);

    EditorSession* session() const { return m_session; }
    bool isValid() const;

    // Drag previews.  They never touch the document.  siblingOrder is the
    // desired sibling order for the single moved root (empty = keep rows).
    bool planMove(const QVector<Uuid>& roots, int deltaFrames,
                  const QVector<Uuid>& siblingOrder, EditPlan* plan,
                  QString* error = nullptr) const;
    bool planTrim(const Uuid& uuid, int newStart, int newEnd, EditPlan* plan,
                  QString* error = nullptr) const;
    bool planReparent(const Uuid& uuid, const Uuid& newParent, bool keepWorld,
                      EditPlan* plan, QString* error = nullptr) const;
    bool planDelete(const QVector<Uuid>& roots, EditPlan* plan,
                    QString* error = nullptr) const;

    // Keyframes.  All frames are entity-relative.  planKeyframe inserts or
    // updates one key with the value the track evaluates to at that frame.
    bool planKeyframe(const Uuid& uuid, const QString& track, int frame,
                      EditPlan* plan, QString* error = nullptr) const;
    bool planMoveKeyframes(const QVector<KeyRef>& keys, int delta,
                           EditPlan* plan, QString* error = nullptr) const;
    bool planDeleteKeyframes(const QVector<KeyRef>& keys, EditPlan* plan,
                             QString* error = nullptr) const;
    bool planSetKeyframeEasing(const QVector<KeyRef>& keys,
                               const model::EasingSpec& easing, EditPlan* plan,
                               QString* error = nullptr) const;
    bool planPasteKeyframes(const Uuid& uuid, const QVector<TrackKey>& keys,
                            EditPlan* plan, QString* error = nullptr) const;

    // Splits one clip (and its subtree) at the absolute frame.
    bool planSplit(const Uuid& uuid, int frame, EditPlan* plan,
                   QString* error = nullptr) const;

    // Tracks a double-click can key: the entity's existing tracks first, then
    // the standard transform/visibility tracks it supports.
    QStringList trackNames(const Uuid& uuid) const;

    // Project markers.  Each marker edit is one project.json commit.
    bool addMarker(const model::Marker& marker, QString* error = nullptr);
    bool updateMarker(const model::Marker& marker, QString* error = nullptr);
    bool deleteMarker(const QString& id, QString* error = nullptr);
    bool moveMarker(const QString& id, int frame, QString* error = nullptr);

    // Bounds used by the drag preview to clamp a gesture.  The source material
    // and parent range are both enforced by planTrim as well.
    int minMoveDelta(const QVector<Uuid>& roots) const;
    int minTrimStart(const Uuid& uuid) const;
    // std::numeric_limits<int>::max() when nothing limits the end.
    int maxTrimEnd(const Uuid& uuid) const;

    // Direct operations.  Each one validates, applies and commits once.
    bool moveClips(const QVector<Uuid>& roots, int deltaFrames,
                   QString* error = nullptr);
    bool moveClips(const QVector<Uuid>& roots, int deltaFrames,
                   const QVector<Uuid>& siblingOrder, QString* error = nullptr);
    bool trimClip(const Uuid& uuid, int newStart, int newEnd,
                  QString* error = nullptr);
    bool reparent(const Uuid& uuid, const Uuid& newParent, bool keepWorld,
                  QString* error = nullptr);
    // Cascades to the whole subtree.  confirm=true asks the user first.
    bool deleteEntities(const QVector<Uuid>& roots, bool confirm = true,
                        QWidget* parent = nullptr, QString* error = nullptr);

    // Direct keyframe/split operations.  Each validates, applies and commits
    // exactly once.
    bool addKeyframe(const Uuid& uuid, const QString& track, int frame,
                     QString* error = nullptr);
    bool deleteKeyframes(const QVector<KeyRef>& keys, QString* error = nullptr);
    bool moveKeyframes(const QVector<KeyRef>& keys, int delta,
                       QString* error = nullptr);
    bool setKeyframeEasing(const QVector<KeyRef>& keys,
                           const model::EasingSpec& easing,
                           QString* error = nullptr);
    bool pasteKeyframes(const Uuid& uuid, const QVector<TrackKey>& keys,
                        QString* error = nullptr);
    bool splitClip(const Uuid& uuid, int frame, QString* error = nullptr);

    // Writes a plan into the document and commits upserts/deletes once.
    // On failure the in-memory project is restored and no undo step exists.
    bool apply(const EditPlan& plan, QString* error = nullptr);

private:
    EditorSession* m_session = nullptr;
};

} // namespace pvf
