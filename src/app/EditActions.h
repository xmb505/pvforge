#pragma once

#include "model/Project.h"

#include <QString>
#include <QVector>

#include <functional>
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
    // Effective frame delta for keyframe plans after boundary clamping.  The
    // timeline uses it to remap the selected diamonds after a drag.
    int frameDelta = 0;
    // True when the plan rewrites project.json (markers) instead of entity
    // files.  Marker gestures and entity gestures never mix in one plan.
    bool projectChanged = false;

    bool isEmpty() const
    {
        return upserts.isEmpty() && deletes.isEmpty() && !projectChanged;
    }
};

// One whole-frame keyframe address.  frame is entity-relative.
struct KeyRef
{
    Uuid entity;
    int frame = 0;
};

// One timeline keyframe clipboard entry.  `offset` is relative to the earliest
// copied key; `keyframe` is the complete whole-frame snapshot (its own `frame`
// is ignored on paste).
struct KeyframeClipEntry
{
    int offset = 0;
    model::Keyframe keyframe;
};

// Timeline Ctrl+C clipboard: keyframes only, never entities.  `sourceContent`
// records the source content schema so `content` / `htmlVars` can be dropped
// when the paste target is incompatible (e.g. vector -> text).
struct KeyframeClipboard
{
    model::Content sourceContent;
    QVector<KeyframeClipEntry> entries;

    bool isEmpty() const { return entries.isEmpty(); }
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
    // Moves one root in time and pins it to the model row under the pointer.
    // A track is a layer, not a fixed slot: several clips may share a row when
    // their [start, end] ranges do not overlap.  Dropping on an overlapping
    // clip is rejected by validation instead of silently swapping rows.
    bool planMoveToRow(const Uuid& uuid, int deltaFrames, int row,
                       EditPlan* plan, QString* error = nullptr) const;
    // Moves a multi-selection as one block: every selected top-level entity
    // shifts by deltaRows while its relative row spacing stays intact, and the
    // whole subtree shifts by deltaFrames.  Root rows are clamped so the block
    // cannot fall below row 0; private-timeline rows keep their positive /
    // negative layer order (the 0 divider is never an entity slot).  A
    // selection spanning different parents is rejected when deltaRows != 0.
    // Row conflicts with an unselected sibling are rejected, never overwritten.
    bool planMoveClipsToRows(const QVector<Uuid>& roots, int deltaFrames,
                             int deltaRows, EditPlan* plan,
                             QString* error = nullptr) const;
    bool planTrim(const Uuid& uuid, int newStart, int newEnd, EditPlan* plan,
                  QString* error = nullptr) const;
    bool planReparent(const Uuid& uuid, const Uuid& newParent, bool keepWorld,
                      EditPlan* plan, QString* error = nullptr) const;
    bool planDelete(const QVector<Uuid>& roots, EditPlan* plan,
                    QString* error = nullptr) const;

    // Keyframes.  All frames are entity-relative; every key carries the whole
    // frame's animation parameters.  planKeyframe inserts the frame evaluated
    // at `frame` (including an open solver's instantaneous pose) or updates
    // the existing key while keeping its easing/note.
    bool planKeyframe(const Uuid& uuid, int frame, EditPlan* plan,
                      QString* error = nullptr) const;
    bool planUpsertKeyframe(const Uuid& uuid, const model::Keyframe& keyframe,
                            EditPlan* plan, QString* error = nullptr) const;
    bool planMoveKeyframes(const QVector<KeyRef>& keys, int delta,
                           EditPlan* plan, QString* error = nullptr) const;
    bool planDeleteKeyframes(const QVector<KeyRef>& keys, EditPlan* plan,
                             QString* error = nullptr) const;
    bool planSetKeyframeEasing(const QVector<KeyRef>& keys,
                               const model::EasingSpec& easing, EditPlan* plan,
                               QString* error = nullptr) const;
    bool planSetKeyframeNote(const QVector<KeyRef>& keys, const QString& note,
                             EditPlan* plan, QString* error = nullptr) const;
    // Copies animation parameters (public fields, content and htmlVars) from
    // the source key onto every in-range target key.  Each target keeps its
    // own frame, easing, note and extras.  Targets outside the target entity's
    // 0..duration-1 range are dropped.  Content/htmlVars are only copied between
    // entities with a compatible content type (and the same HTML asset).
    bool planCopyKeyframeParams(const KeyRef& source,
                                const QVector<KeyRef>& targets, EditPlan* plan,
                                QString* error = nullptr) const;
    // Pastes a keyframe-only clipboard onto `target`: every entry lands on
    // anchor + entry.offset.  Public animation parameters, easing and note
    // always travel; content/htmlVars only when the target has a compatible
    // content schema.  Same-frame keys are overwritten, out-of-range frames and
    // frames inside an explicit solver segment are dropped.  pastedFrames
    // (optional) receives the target-relative frames actually written.
    bool planPasteKeyframes(const KeyframeClipboard& clipboard, const Uuid& target,
                            int anchor, EditPlan* plan,
                            QVector<int>* pastedFrames = nullptr,
                            QString* error = nullptr) const;

    // Splits one clip (and its subtree) at the absolute frame.
    bool planSplit(const Uuid& uuid, int frame, EditPlan* plan,
                   QString* error = nullptr) const;

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
    // Direct follow toggle: one validated commit, one undo step.  Enabling
    // rewrites the range to the direct parent's range; disabling only clears
    // the flag.
    bool setFollowParentRange(const Uuid& uuid, bool follow,
                              QString* error = nullptr);
    bool reparent(const Uuid& uuid, const Uuid& newParent, bool keepWorld,
                  QString* error = nullptr);
    // Cascades to the whole subtree.  confirm=true asks the user first.
    bool deleteEntities(const QVector<Uuid>& roots, bool confirm = true,
                        QWidget* parent = nullptr, QString* error = nullptr);

    // Direct keyframe/split operations.  Each validates, applies and commits
    // exactly once.
    bool addKeyframe(const Uuid& uuid, int frame, QString* error = nullptr);
    bool upsertKeyframe(const Uuid& uuid, const model::Keyframe& keyframe,
                        QString* error = nullptr);
    bool deleteKeyframes(const QVector<KeyRef>& keys, QString* error = nullptr);
    bool moveKeyframes(const QVector<KeyRef>& keys, int delta,
                       QString* error = nullptr);
    bool setKeyframeEasing(const KeyRef& key,
                           const model::EasingSpec& easing,
                           QString* error = nullptr);
    bool setKeyframeNote(const KeyRef& key, const QString& note,
                         QString* error = nullptr);
    bool copyKeyframeParams(const KeyRef& source,
                            const QVector<KeyRef>& targets,
                            QString* error = nullptr);
    bool splitClip(const Uuid& uuid, int frame, QString* error = nullptr);

    // One whole-entity edit built from the current document, validated as an
    // entity and committed once.  Used by the properties panel for basic
    // fields, transform/content values, keyframe values and solver segments.
    // `ignoreLock` is only for toggling the entity's own locked flag.
    bool updateEntity(const Uuid& uuid, const QString& message,
                      const std::function<bool(model::Entity&)>& mutate,
                      QString* error = nullptr, bool ignoreLock = false);
    // Same path, restricted to the content block.
    bool updateContent(const Uuid& uuid, const QString& message,
                       const std::function<bool(model::Content&)>& mutate,
                       QString* error = nullptr);

    // Creates the visual entity for a stored asset at startFrame and, for a
    // video with a split audio track, its independent WAV audio entity.  For a
    // root insertion row < 0 picks the model's auto row; inside a private
    // timeline a negative row is a real track.  An empty parent creates roots.  Both
    // entities are written in one commit and the new UUIDs are returned.
    QVector<Uuid> addAssetEntities(const AssetHash& hash, int startFrame,
                                   int row, const Uuid& parent = Uuid(),
                                   QString* error = nullptr);

    // Replaces an entity's asset in one commit.  Visual assets (image/video/
    // html) only swap with visual assets, audio with audio; empty entities
    // accept any group and camera/generated content reject asset replacement.
    // Timed assets clamp sourceIn and trim the entity end (dropping keys past
    // the new duration); children that would fall outside are rejected.
    // A video's split-out audio child is updated in the same commit: its hash
    // follows the new video's embedded audio (removed when the new video is
    // silent) unless syncChildAudio is false, in which case only its range
    // follows the new duration.
    bool replaceAsset(const Uuid& uuid, const AssetHash& hash,
                      QString* error = nullptr, bool syncChildAudio = true);

    // Batch clip helpers used by the timeline multi-selection.  Each one
    // commits exactly one plan.  duplicateEntities clones the selected
    // subtrees under newParent (empty = keep the original parent);
    // pasteEntities clones a clipboard captured by the panel; groupIntoFolder
    // creates one empty folder around two or more siblings.
    QVector<Uuid> duplicateEntities(const QVector<Uuid>& roots, int deltaFrames,
                                    const Uuid& newParent,
                                    QString* error = nullptr);
    QVector<Uuid> pasteEntities(const QVector<model::Entity>& clipboard,
                                const Uuid& parent, int deltaFrames,
                                QString* error = nullptr);
    bool groupIntoFolder(const QVector<Uuid>& roots, Uuid* folder,
                         QString* error = nullptr);

    // Writes a plan into the document and commits upserts/deletes once.
    // On failure the in-memory project is restored and no undo step exists.
    bool apply(const EditPlan& plan, QString* error = nullptr);

private:
    EditorSession* m_session = nullptr;
};

} // namespace pvf
