// Timeline keyframe clipboard: Ctrl+C / Ctrl+V move whole keyframes only,
// Ctrl+Shift+C / Ctrl+Shift+V stay on the clip (entity) layer.  These are
// behavioural checks against a live panel, not EditActions-only unit tests.

#include "TestApp.h"
#include "TestRegistry.h"

#include <QKeyEvent>

namespace {

int keyframeClipboardTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("keyframe-clipboard.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("关键帧剪贴板");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "keyframe-clipboard-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto* actions = session->editActions();
    if (!actions)
    {
        qCritical() << "keyframe-clipboard-test: no EditActions";
        return 1;
    }
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "keyframe-clipboard-test:" << message;
        return condition;
    };

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid clip =
        makeEntity(QStringLiteral("关键帧片段"), 0, 60, 0, pvf::ContentKind::Text);
    const pvf::Uuid textTarget =
        makeEntity(QStringLiteral("文本目标"), 100, 160, 1,
                   pvf::ContentKind::Text);
    const pvf::Uuid square =
        makeEntity(QStringLiteral("正方形"), 200, 260, 2,
                   pvf::ContentKind::Vector);
    const pvf::Uuid solver =
        makeEntity(QStringLiteral("求解片段"), 0, 60, 3, pvf::ContentKind::Text);

    auto addKey = [&project](const pvf::Uuid& uuid, int frame,
                             double rotation) -> pvf::model::Keyframe& {
        pvf::model::Keyframe key;
        key.frame = frame;
        key.rotation = rotation;
        project.entities[uuid].keyframes.append(key);
        return project.entities[uuid].keyframes.last();
    };
    addKey(clip, 0, 0.0);
    pvf::model::Keyframe& sourceTail = addKey(clip, 10, 90.0);
    sourceTail.easing.type = QStringLiteral("easeinoutcubic");
    sourceTail.note = QStringLiteral("源注释");

    pvf::model::Keyframe& squareKey = addKey(square, 0, 45.0);
    squareKey.content.insert(QStringLiteral("width"), 123.0);

    addKey(solver, 10, 0.0);
    pvf::model::SolverSegment segment;
    segment.id = pvf::model::newUuid();
    segment.effect = pvf::SolverEffect::Gravity;
    segment.startFrame = 10;
    segment.endFrame = 40;
    segment.params.insert(QStringLiteral("targetUuid"), textTarget);
    project.entities[solver].solvers.append(segment);

    if (!document->commitEntities({clip, textTarget, square, solver},
                                  QStringLiteral("clipboard setup"), &error))
    {
        qCritical() << "keyframe-clipboard-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline,
               QStringLiteral("session window/timeline missing")))
        return 1;

    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "keyframe-clipboard-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto undo = [document, &flush] {
        flush();
        QString undoError;
        if (!document->undo(&undoError))
        {
            qCritical() << "keyframe-clipboard-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto entityCount = [live] { return live->project().entities.size(); };
    auto keyCount = [live](const pvf::Uuid& uuid) {
        const auto it = live->project().entities.constFind(uuid);
        return it == live->project().entities.constEnd()
                   ? 0
                   : int(it.value().keyframes.size());
    };
    auto keyAt = [live](const pvf::Uuid& uuid,
                        int frame) -> const pvf::model::Keyframe* {
        const auto it = live->project().entities.constFind(uuid);
        if (it == live->project().entities.constEnd())
            return nullptr;
        return pvf::model::keyframeAt(it.value().keyframes, frame);
    };
    auto entityId = [timeline](const pvf::Uuid& uuid) {
        return timeline->entityIdForUuid(uuid);
    };
    auto selectEntity = [&](const pvf::Uuid& uuid) {
        timeline->setSelectedEntity(entityId(uuid));
        live->document()->setSelection(uuid);
    };
    auto sendKey = [timeline](int key, Qt::KeyboardModifiers modifiers) {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        QApplication::sendEvent(timeline, &event);
        QApplication::processEvents();
    };

    QString status;
    QObject::connect(timeline, &pvf::TimelinePanel::editCommitted,
                     [&status](const QString& text) { status = text; });
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         qCritical() << "keyframe-clipboard-test: edit failed"
                                     << message;
                     });

    // 1) 0f/10f copied with the playhead at 20f land on 20f/30f; no entity is
    //    created and the whole paste is one undo step.
    {
        selectEntity(clip);
        timeline->setSelectedKeyframesForTest(
            {{entityId(clip), 0.0}, {entityId(clip), 10.0}});
        live->setPlayhead(20);
        const int beforeEntities = entityCount();
        const int beforeDepth = depth();
        status.clear();
        sendKey(Qt::Key_C, Qt::ControlModifier);
        if (!check(status.contains(QStringLiteral("已复制")),
                   QStringLiteral("Ctrl+C did not copy keyframes: %1")
                       .arg(status)))
            return 1;
        sendKey(Qt::Key_V, Qt::ControlModifier);
        flush();
        const pvf::model::Keyframe* first = keyAt(clip, 20);
        const pvf::model::Keyframe* second = keyAt(clip, 30);
        if (!check(first && second,
                   QStringLiteral("Ctrl+V did not create 20f/30f keys")))
            return 1;
        if (!check(std::abs(first->rotation) < 1e-6
                       && std::abs(second->rotation - 90.0) < 1e-6,
                   QStringLiteral("pasted keys lost their rotation offsets")))
            return 1;
        if (!check(second->easing.type == QStringLiteral("easeinoutcubic")
                       && second->note == QStringLiteral("源注释"),
                   QStringLiteral("pasted key lost easing/note")))
            return 1;
        if (!check(entityCount() == beforeEntities,
                   QStringLiteral("keyframe paste created entities")))
            return 1;
        if (!check(depth() == beforeDepth + 1,
                   QStringLiteral("keyframe paste was not one commit")))
            return 1;
        if (!check(live->playhead() == 20,
                   QStringLiteral("keyframe paste moved the playhead")))
            return 1;
        const QVector<pvf::KeyframeSelection> selected =
            timeline->selectedKeyframes();
        if (!check(selected.size() == 2
                       && int(std::lround(selected.at(0).frame)) == 20
                       && int(std::lround(selected.at(1).frame)) == 30,
                   QStringLiteral("pasted keys were not selected")))
            return 1;
        if (!undo())
            return 1;
        timeline->setSelectedKeyframesForTest({});
        if (!check(!keyAt(clip, 20) && !keyAt(clip, 30),
                   QStringLiteral("undo did not remove the pasted keys")))
            return 1;
    }

    // 2) Offsets past the clip end are dropped instead of stretching it:
    //    playhead 55 + offsets 0/10 writes 55f and drops 65f.
    {
        live->setPlayhead(55);
        status.clear();
        sendKey(Qt::Key_V, Qt::ControlModifier);
        flush();
        if (!check(keyAt(clip, 55),
                   QStringLiteral("in-range pasted frame was dropped")))
            return 1;
        if (!check(keyCount(clip) == 3,
                   QStringLiteral("out-of-range pasted frame was written")))
            return 1;
        if (!check(status.contains(QStringLiteral("越界")),
                   QStringLiteral("out-of-range paste had no status: %1")
                       .arg(status)))
            return 1;
        if (!undo())
            return 1;
        timeline->setSelectedKeyframesForTest({});
    }

    // 3) Cross-entity paste vector -> text: public parameters travel, the
    //    vector content schema never pollutes the text key.
    {
        selectEntity(textTarget);
        timeline->setSelectedKeyframesForTest({{entityId(square), 200.0}});
        status.clear();
        sendKey(Qt::Key_C, Qt::ControlModifier);
        if (!check(status.contains(QStringLiteral("已复制")),
                   QStringLiteral("cross-entity copy failed")))
            return 1;
        live->setPlayhead(110);
        const int beforeEntities = entityCount();
        sendKey(Qt::Key_V, Qt::ControlModifier);
        flush();
        const pvf::model::Keyframe* pasted = keyAt(textTarget, 10);
        if (!check(pasted && std::abs(pasted->rotation - 45.0) < 1e-6,
                   QStringLiteral("cross-entity paste lost rotation")))
            return 1;
        if (!check(pasted && !pasted->content.contains(QStringLiteral("width"))
                       && !pasted->content.contains(QStringLiteral("height")),
                   QStringLiteral("vector content leaked into the text key")))
            return 1;
        if (!check(pasted && pasted->content.contains(QStringLiteral("pixelSize")),
                   QStringLiteral("text content schema was not kept")))
            return 1;
        if (!check(entityCount() == beforeEntities,
                   QStringLiteral("cross-entity paste created entities")))
            return 1;
        if (!undo())
            return 1;
        timeline->setSelectedKeyframesForTest({});
    }

    // 4) With no diamond selected, the playhead key of the current entity is
    //    the single-frame source.
    {
        selectEntity(clip);
        timeline->setSelectedKeyframesForTest({});
        live->setPlayhead(5);
        status.clear();
        sendKey(Qt::Key_C, Qt::ControlModifier);
        if (!check(status.contains(QStringLiteral("没有可复制的关键帧")),
                   QStringLiteral("empty Ctrl+C did not report a reason: %1")
                       .arg(status)))
            return 1;
        live->setPlayhead(10);
        status.clear();
        sendKey(Qt::Key_C, Qt::ControlModifier);
        if (!check(status.contains(QStringLiteral("1 个关键帧")),
                   QStringLiteral("playhead fallback did not copy: %1")
                       .arg(status)))
            return 1;
        live->setPlayhead(45);
        sendKey(Qt::Key_V, Qt::ControlModifier);
        flush();
        if (!check(keyAt(clip, 45) && keyCount(clip) == 3,
                   QStringLiteral("playhead fallback paste failed")))
            return 1;
        if (!undo())
            return 1;
        timeline->setSelectedKeyframesForTest({});
    }

    // 5) Ctrl+Shift+C / Ctrl+Shift+V stay on the clip layer and do create an
    //    entity.
    {
        timeline->setSelectedKeyframesForTest({});
        selectEntity(clip);
        live->setPlayhead(300);
        const int beforeEntities = entityCount();
        status.clear();
        sendKey(Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        if (!check(status.contains(QStringLiteral("已复制片段")),
                   QStringLiteral("Ctrl+Shift+C did not copy clips: %1")
                       .arg(status)))
            return 1;
        sendKey(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        flush();
        if (!check(entityCount() == beforeEntities + 1,
                   QStringLiteral("Ctrl+Shift+V did not create one clip")))
            return 1;
        bool pastedAtPlayhead = false;
        for (auto it = live->project().entities.constBegin();
             it != live->project().entities.constEnd(); ++it)
        {
            if (it.key() != clip && it->name == QStringLiteral("关键帧片段")
                && it->start == 300)
                pastedAtPlayhead = true;
        }
        if (!check(pastedAtPlayhead,
                   QStringLiteral("pasted clip is not at the playhead")))
            return 1;
        if (!undo())
            return 1;
        selectEntity(clip);
    }

    // 6) A paste landing inside an explicit solver segment is skipped with a
    //    status message; the solver key set stays untouched.
    {
        selectEntity(clip);
        timeline->setSelectedKeyframesForTest({{entityId(clip), 10.0}});
        sendKey(Qt::Key_C, Qt::ControlModifier);
        selectEntity(solver);
        live->setPlayhead(20);
        const int beforeDepth = depth();
        const int beforeKeys = keyCount(solver);
        status.clear();
        sendKey(Qt::Key_V, Qt::ControlModifier);
        flush();
        if (!check(keyCount(solver) == beforeKeys && !keyAt(solver, 20),
                   QStringLiteral("paste wrote a key inside a solver segment")))
            return 1;
        if (!check(depth() == beforeDepth,
                   QStringLiteral("blocked paste created an undo step")))
            return 1;
        if (!check(status.contains(QStringLiteral("求解")),
                   QStringLiteral("blocked paste had no solver status: %1")
                       .arg(status)))
            return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "keyframe-clipboard-test OK: keyframe-only Ctrl+C/V, cross-entity"
               " schema guard, playhead fallback, clip Ctrl+Shift+C/V, solver"
               " segment rejection";
    return 0;
}

} // namespace

namespace pvf::tests {

void registerKeyframeClipboardTests(Registry& registry)
{
    registry.add(QStringLiteral("--keyframe-clipboard-test"),
                 [](QApplication& app) { return keyframeClipboardTest(app); });
}

} // namespace pvf::tests
