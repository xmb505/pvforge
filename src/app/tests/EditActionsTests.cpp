#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "app/TimelineAdapter.h"
#include "model/Entity.h"
#include "model/Project.h"
#include "next/app/ProjectDocument.h"
#include "ui/TimelineSnapshot.h"

#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

QString q(const char* text)
{
    return QString::fromUtf8(text);
}

} // namespace

class AppEditActionsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void addKeyframeWritesWholeFrame();
    void upsertReplacesWholeFrame();
    void copyParamsDropsOutOfRangeTargets();
    void moveKeysDropsOutOfRangeFrames();
    void deleteKeysDedupesAndProtectsSolverStart();
    void solverBoundaries();
    void trimKeepsRelativeFramesAndDropsOutOfRange();
    void timelineSnapshotUsesAbsoluteWholeFrames();

private:
    pvf::Uuid addEntity(const QString& name, int start, int end,
                        pvf::ContentKind kind = pvf::ContentKind::Empty);
    void addGravitySolver(const pvf::Uuid& entity, const pvf::Uuid& target,
                          int startFrame, int endFrame);
    pvf::model::Entity entity(const pvf::Uuid& uuid) const;
    pvf::model::Keyframe keyAt(const pvf::Uuid& uuid, int frame) const;
    pvf::EditActions* actions() const;

    std::unique_ptr<QTemporaryDir> m_temp;
    std::unique_ptr<pvf::EditorSession> m_session;
};

void AppEditActionsTests::initTestCase()
{
    pvf::EditorSession::setMcpPortOverrideForTest(0);
}

void AppEditActionsTests::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    pvf::EditorSession::setMcpConfigPathForTest(m_temp->filePath(q("mcp.json")));

    pvf::model::ProjectSettings settings;
    settings.name = q("app-edit-tests");
    QString error;
    m_session = pvf::EditorSession::createProject(
        m_temp->filePath(q("test.pvproj")), settings, &error, false);
    QVERIFY2(m_session != nullptr, qPrintable(error));
}

void AppEditActionsTests::cleanup()
{
    m_session.reset();
    m_temp.reset();
}

pvf::EditActions* AppEditActionsTests::actions() const
{
    return m_session ? m_session->editActions() : nullptr;
}

pvf::Uuid AppEditActionsTests::addEntity(const QString& name, int start, int end,
                                         pvf::ContentKind kind)
{
    pvf::model::Entity created;
    created.uuid = pvf::model::newUuid();
    created.name = name;
    created.start = start;
    created.end = end;
    created.row = int(m_session->project().entities.size());
    created.content.kind = kind;
    if (kind == pvf::ContentKind::Text)
        created.content.text.text = q("字幕");
    m_session->document()->mutableProject().entities.insert(created.uuid,
                                                            created);
    QString error;
    if (!m_session->document()->commitEntity(created.uuid, q("setup"), &error))
        qFatal("setup commit failed: %s", qPrintable(error));
    m_session->flush();
    return created.uuid;
}

void AppEditActionsTests::addGravitySolver(const pvf::Uuid& uuid,
                                           const pvf::Uuid& target,
                                           int startFrame, int endFrame)
{
    QString error;
    const bool ok = actions()->updateEntity(
        uuid, q("setup solver"),
        [target, startFrame, endFrame](pvf::model::Entity& entity) {
            pvf::model::SolverSegment segment;
            segment.id = pvf::model::newUuid();
            segment.effect = pvf::SolverEffect::Gravity;
            segment.startFrame = startFrame;
            segment.endFrame = endFrame;
            segment.params.insert(q("targetUuid"), target);
            entity.solvers.append(segment);
            return true;
        },
        &error);
    QVERIFY2(ok, qPrintable(error));
    m_session->flush();
}

pvf::model::Entity AppEditActionsTests::entity(const pvf::Uuid& uuid) const
{
    return m_session->project().entities.value(uuid);
}

pvf::model::Keyframe AppEditActionsTests::keyAt(const pvf::Uuid& uuid,
                                                int frame) const
{
    const auto found =
        pvf::model::keyframeAt(entity(uuid).keyframes, frame);
    return found ? *found : pvf::model::Keyframe{};
}

void AppEditActionsTests::addKeyframeWritesWholeFrame()
{
    const pvf::Uuid clip =
        addEntity(q("字幕"), 10, 70, pvf::ContentKind::Text);

    QString error;
    QVERIFY2(actions()->addKeyframe(clip, 0, &error), qPrintable(error));
    {
        const auto keys = entity(clip).keyframes;
        QCOMPARE(keys.size(), 1);
        const pvf::model::Keyframe& key = keys.first();
        QCOMPARE(key.frame, 0);
        QCOMPARE(key.x, 0.0);
        QCOMPARE(key.y, 0.0);
        QCOMPARE(key.scaleX, 1.0);
        QCOMPARE(key.opacity, 1.0);
        QVERIFY(key.visible);
        QCOMPARE(key.content.value(q("pixelSize")).toDouble(), 96.0);
        QVERIFY(key.note.isEmpty());
    }

    // frame == duration is a legal ordinary key; duration + 1 is not.
    QVERIFY2(actions()->addKeyframe(clip, 60, &error), qPrintable(error));
    QVERIFY(!actions()->addKeyframe(clip, 61, &error));
    QVERIFY(!actions()->addKeyframe(clip, -1, &error));
    QCOMPARE(entity(clip).keyframes.size(), 2);

    // Re-evaluating an existing frame keeps its easing and note.
    pvf::model::EasingSpec easing;
    easing.type = q("easeoutquad");
    QVERIFY2(actions()->setKeyframeEasing({clip, 0}, easing, &error),
             qPrintable(error));
    QVERIFY2(actions()->setKeyframeNote({clip, 0}, q("起点"), &error),
             qPrintable(error));
    QVERIFY2(actions()->addKeyframe(clip, 0, &error), qPrintable(error));
    const pvf::model::Keyframe key = keyAt(clip, 0);
    QCOMPARE(key.easing.type, q("easeoutquad"));
    QCOMPARE(key.note, q("起点"));
}

void AppEditActionsTests::upsertReplacesWholeFrame()
{
    const pvf::Uuid clip =
        addEntity(q("字幕"), 0, 20, pvf::ContentKind::Text);

    pvf::model::Keyframe source;
    source.frame = 5;
    source.x = 42.0;
    source.y = -7.0;
    source.rotation = 33.0;
    source.scaleX = 2.0;
    source.scaleY = 3.0;
    source.opacity = 0.5;
    source.visible = false;
    source.physicsInteract = true;
    source.content.insert(q("pixelSize"), 12.0);
    source.htmlVars.insert(q("var.speed"), 1.5);
    source.easing.type = q("easeinquad");
    source.note = q("源");
    QString error;
    QVERIFY2(actions()->upsertKeyframe(clip, source, &error),
             qPrintable(error));

    {
        const pvf::model::Keyframe stored = keyAt(clip, 5);
        QCOMPARE(stored.x, 42.0);
        QCOMPARE(stored.y, -7.0);
        QCOMPARE(stored.rotation, 33.0);
        QCOMPARE(stored.scaleX, 2.0);
        QCOMPARE(stored.opacity, 0.5);
        QVERIFY(!stored.visible);
        QVERIFY(stored.physicsInteract);
        QCOMPARE(stored.content.value(q("pixelSize")).toDouble(), 12.0);
        QCOMPARE(stored.htmlVars.value(q("var.speed")).toDouble(), 1.5);
        QCOMPARE(stored.easing.type, q("easeinquad"));
        QCOMPARE(stored.note, q("源"));
    }

    // Upsert is a whole-frame replacement: omitted metadata is cleared and no
    // duplicate frame is created.
    pvf::model::Keyframe replacement;
    replacement.frame = 5;
    replacement.x = 1.0;
    QVERIFY2(actions()->upsertKeyframe(clip, replacement, &error),
             qPrintable(error));
    QCOMPARE(entity(clip).keyframes.size(), 1);
    {
        const pvf::model::Keyframe stored = keyAt(clip, 5);
        QCOMPARE(stored.x, 1.0);
        QCOMPARE(stored.y, 0.0);
        QVERIFY(stored.content.isEmpty());
        QVERIFY(stored.note.isEmpty());
        QCOMPARE(stored.easing.type, q("linear"));
    }

    pvf::model::Keyframe endKey;
    endKey.frame = 20;
    endKey.x = 1.0;
    QVERIFY2(actions()->upsertKeyframe(clip, endKey, &error),
             qPrintable(error));

    pvf::model::Keyframe outside;
    outside.frame = 21;
    QVERIFY(!actions()->upsertKeyframe(clip, outside, &error));

    pvf::model::Keyframe duplicate = keyAt(clip, 5);
    QVERIFY2(actions()->upsertKeyframe(clip, duplicate, &error),
             qPrintable(error));
    QCOMPARE(entity(clip).keyframes.size(), 2);
}

void AppEditActionsTests::copyParamsDropsOutOfRangeTargets()
{
    const pvf::Uuid clip =
        addEntity(q("字幕"), 0, 20, pvf::ContentKind::Text);

    pvf::model::Keyframe source;
    source.frame = 2;
    source.x = 30.0;
    source.y = 40.0;
    source.opacity = 0.25;
    source.content.insert(q("pixelSize"), 40.0);
    source.easing.type = q("easeinquad");
    source.note = q("s");
    QString error;
    QVERIFY2(actions()->upsertKeyframe(clip, source, &error),
             qPrintable(error));

    for (int frame : {10, 20})
    {
        pvf::model::Keyframe target;
        target.frame = frame;
        target.y = 99.0;
        target.content.insert(q("pixelSize"), 80.0);
        target.note = q("t");
        QVERIFY2(actions()->upsertKeyframe(clip, target, &error),
                 qPrintable(error));
    }

    QVERIFY2(actions()->copyKeyframeParams(
                 {clip, 2}, {{clip, 10}, {clip, 25}, {clip, 20}}, &error),
             qPrintable(error));

    const auto keys = entity(clip).keyframes;
    QCOMPARE(keys.size(), 3);
    for (int frame : {10, 20})
    {
        const pvf::model::Keyframe copied = keyAt(clip, frame);
        QCOMPARE(copied.x, 30.0);
        QCOMPARE(copied.y, 40.0);
        QCOMPARE(copied.opacity, 0.25);
        QCOMPARE(copied.content.value(q("pixelSize")).toDouble(), 40.0);
        // Target metadata stays put.
        QCOMPARE(copied.easing.type, q("linear"));
        QCOMPARE(copied.note, q("t"));
    }

    // All targets out of range: dropped, no error, nothing changes.
    QVERIFY2(actions()->copyKeyframeParams({clip, 2}, {{clip, 21}}, &error),
             qPrintable(error));
    QCOMPARE(entity(clip).keyframes.size(), 3);
    QVERIFY(!actions()->copyKeyframeParams({clip, 3}, {{clip, 10}}, &error));
}

void AppEditActionsTests::moveKeysDropsOutOfRangeFrames()
{
    const pvf::Uuid clip = addEntity(q("字幕"), 0, 10);
    QString error;
    for (int frame : {2, 5})
    {
        pvf::model::Keyframe key;
        key.frame = frame;
        key.x = frame;
        QVERIFY2(actions()->upsertKeyframe(clip, key, &error),
                 qPrintable(error));
    }

    QVERIFY2(actions()->moveKeyframes({{clip, 2}, {clip, 5}}, 8, &error),
             qPrintable(error));
    {
        const auto keys = entity(clip).keyframes;
        QCOMPARE(keys.size(), 1);
        QCOMPARE(keys.first().frame, 10);
    }

    // Dragging past zero drops the out-of-range frame instead of leaving it.
    QVERIFY2(actions()->moveKeyframes({{clip, 10}}, -11, &error),
             qPrintable(error));
    QCOMPARE(entity(clip).keyframes.size(), 0);
}

void AppEditActionsTests::deleteKeysDedupesAndProtectsSolverStart()
{
    const pvf::Uuid target = addEntity(q("目标"), 0, 30);
    const pvf::Uuid clip = addEntity(q("求解"), 0, 30);
    addGravitySolver(clip, target, 5, 10);

    QString error;
    QVERIFY2(actions()->addKeyframe(clip, 5, &error), qPrintable(error));
    QVERIFY2(actions()->addKeyframe(clip, 11, &error), qPrintable(error));

    QVERIFY(!actions()->deleteKeyframes({{clip, 5}}, &error));
    QVERIFY2(actions()->deleteKeyframes({{clip, 11}, {clip, 11}}, &error),
             qPrintable(error));
    QVERIFY(!pvf::model::hasKeyframeAt(entity(clip).keyframes, 11));
    QVERIFY(pvf::model::hasKeyframeAt(entity(clip).keyframes, 5));
    QVERIFY(!actions()->deleteKeyframes({{clip, 12}}, &error));
}

void AppEditActionsTests::solverBoundaries()
{
    const pvf::Uuid target = addEntity(q("目标"), 0, 40);
    const pvf::Uuid explicitClip = addEntity(q("显式"), 0, 30);
    addGravitySolver(explicitClip, target, 5, 10);

    QString error;
    QVERIFY2(actions()->addKeyframe(explicitClip, 5, &error),
             qPrintable(error));
    QVERIFY(!actions()->addKeyframe(explicitClip, 6, &error));
    QVERIFY(!actions()->addKeyframe(explicitClip, 10, &error));
    QVERIFY2(actions()->addKeyframe(explicitClip, 11, &error),
             qPrintable(error));
    QVERIFY(!actions()->moveKeyframes({{explicitClip, 5}}, 1, &error));
    QVERIFY(!actions()->moveKeyframes({{explicitClip, 11}}, -1, &error));

    pvf::model::Keyframe blocked;
    blocked.frame = 6;
    QVERIFY(!actions()->upsertKeyframe(explicitClip, blocked, &error));

    const pvf::Uuid openClip = addEntity(q("开放"), 0, 30);
    addGravitySolver(openClip, target, 5, -1);
    QVERIFY2(actions()->addKeyframe(openClip, 12, &error), qPrintable(error));
    {
        const pvf::model::Entity entityAfter = entity(openClip);
        QCOMPARE(pvf::model::solverStructuralEnd(
                     entityAfter, entityAfter.solvers.first()),
                 11);
    }
    QVERIFY2(actions()->addKeyframe(openClip, 30, &error), qPrintable(error));
    {
        const pvf::model::Entity entityAfter = entity(openClip);
        QCOMPARE(pvf::model::solverStructuralEnd(
                     entityAfter, entityAfter.solvers.first()),
                 11);
        QVERIFY(pvf::model::hasKeyframeAt(entityAfter.keyframes, 30));
    }
}

void AppEditActionsTests::trimKeepsRelativeFramesAndDropsOutOfRange()
{
    const pvf::Uuid clip = addEntity(q("字幕"), 10, 30);
    QString error;
    for (int frame : {0, 5, 20})
    {
        pvf::model::Keyframe key;
        key.frame = frame;
        key.x = frame;
        QVERIFY2(actions()->upsertKeyframe(clip, key, &error),
                 qPrintable(error));
    }

    // Moving the clip does not touch entity-relative frames.
    QVERIFY2(actions()->moveClips({clip}, 5, &error), qPrintable(error));
    QCOMPARE(entity(clip).start, 15);
    {
        const auto keys = entity(clip).keyframes;
        QCOMPARE(keys.size(), 3);
        QCOMPARE(keys.at(0).frame, 0);
        QCOMPARE(keys.at(1).frame, 5);
        QCOMPARE(keys.at(2).frame, 20);
    }

    // Trim start +5: -5 is dropped, 0/15 survive; frame == duration is kept.
    QVERIFY2(actions()->trimClip(clip, 20, 35, &error), qPrintable(error));
    QCOMPARE(entity(clip).start, 20);
    QCOMPARE(entity(clip).end, 35);
    {
        const auto keys = entity(clip).keyframes;
        QCOMPARE(keys.size(), 2);
        QCOMPARE(keys.at(0).frame, 0);
        QCOMPARE(keys.at(1).frame, 15);
    }

    // Trim end: frames beyond the new duration are dropped.
    QVERIFY2(actions()->trimClip(clip, 20, 30, &error), qPrintable(error));
    {
        const auto keys = entity(clip).keyframes;
        QCOMPARE(keys.size(), 1);
        QCOMPARE(keys.first().frame, 0);
    }
}

void AppEditActionsTests::timelineSnapshotUsesAbsoluteWholeFrames()
{
    const pvf::Uuid target = addEntity(q("目标"), 0, 40);
    const pvf::Uuid clip =
        addEntity(q("字幕"), 10, 40, pvf::ContentKind::Text);
    addGravitySolver(clip, target, 5, 10);
    addGravitySolver(clip, target, 20, 20);

    QString error;
    pvf::model::Keyframe startKey;
    startKey.frame = 0;
    startKey.note = q("a");
    startKey.easing.type = q("easeinquad");
    QVERIFY2(actions()->upsertKeyframe(clip, startKey, &error),
             qPrintable(error));
    for (int frame : {5, 20})
    {
        pvf::model::Keyframe key;
        key.frame = frame;
        QVERIFY2(actions()->upsertKeyframe(clip, key, &error),
                 qPrintable(error));
    }

    pvf::TimelineAdapter adapter(m_session.get());
    adapter.refresh();
    const auto& snapshot = adapter.snapshot();
    const int id = adapter.idForUuid(clip);
    QVERIFY(id != pvf::kInvalidEntity);

    const pvf::Entity* view = nullptr;
    for (const pvf::Entity& candidate : snapshot.entities)
    {
        if (candidate.id == id)
        {
            view = &candidate;
            break;
        }
    }
    QVERIFY(view != nullptr);
    QCOMPARE(view->type, pvf::EntityType::Text);
    QCOMPARE(view->keyframes.size(), 3);
    QCOMPARE(view->keyframes.at(0).frame, 10);
    QCOMPARE(view->keyframes.at(1).frame, 15);
    QCOMPARE(view->keyframes.at(2).frame, 30);
    QCOMPARE(view->keyframes.at(0).note, q("a"));
    QCOMPARE(view->keyframes.at(0).ease.type, q("easeinquad"));
    QVERIFY(view->keyframes.at(1).solverStart);
    QVERIFY(!view->keyframes.at(1).solverEnd);
    QVERIFY(view->keyframes.at(2).solverStart);
    QVERIFY(view->keyframes.at(2).solverEnd);

    // patchSnapshot refreshes the same ids and keyframes on the drag-preview
    // fast path without rebuilding the entity list.
    pvf::TimelineSnapshot patched = snapshot;
    QVERIFY2(actions()->setKeyframeNote({clip, 0}, q("b"), &error),
             qPrintable(error));
    QVERIFY(adapter.patchSnapshot(patched, m_session->project(), 7));
    QCOMPARE(patched.playhead, 7);
    const pvf::Entity* patchedView = nullptr;
    for (const pvf::Entity& candidate : patched.entities)
    {
        if (candidate.id == id)
        {
            patchedView = &candidate;
            break;
        }
    }
    QVERIFY(patchedView != nullptr);
    QCOMPARE(patchedView->keyframes.size(), 3);
    QCOMPARE(patchedView->keyframes.at(0).frame, 10);
    QCOMPARE(patchedView->keyframes.at(0).note, q("b"));
}

QTEST_MAIN(AppEditActionsTests)
#include "EditActionsTests.moc"
