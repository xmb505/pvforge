// Viewport-side "等比缩放" (scale link) tests.
//
// The properties panel owns the toggle; MainWindow mirrors it onto the
// viewport.  While the link is on, centre and corner scale gestures must use
// one factor for both axes and keep the entity's current scaleX : scaleY
// ratio, including multi-key relative deltas, with one undo per gesture.

#include "TestApp.h"
#include "TestRegistry.h"

namespace {

struct MouseDriver
{
    QWidget* widget = nullptr;

    void send(QEvent::Type type, const QPointF& pos, Qt::MouseButton button,
              Qt::MouseButtons buttons,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(type, pos, widget->mapToGlobal(pos.toPoint()), button,
                          buttons, modifiers);
        QApplication::sendEvent(widget, &event);
    }

    void drag(const QPointF& from, const QPointF& to,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton,
             modifiers);
        send(QEvent::MouseMove, (from + to) / 2.0, Qt::NoButton, Qt::LeftButton,
             modifiers);
        send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
        send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton,
             modifiers);
    }
};

bool ratioMatches(double scaleX, double scaleY, double expected)
{
    return std::abs(scaleY) > 1e-9
           && std::abs(scaleX / scaleY - expected) < 1e-6;
}

bool nearly(double value, double expected, double tolerance = 1e-6)
{
    return std::abs(value - expected) <= tolerance;
}

int scaleLinkTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool ok, const char* label) {
        if (!ok)
        {
            qCritical() << "scale-link-test:" << label;
            ++failures;
        }
        return ok;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("scale-link.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Scale Link");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "scale-link-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("方块");
    entity.start = 0;
    entity.end = 60;
    entity.row = 0;
    entity.content.kind = pvf::ContentKind::Vector;
    entity.content.vector.width = 200.0;
    entity.content.vector.height = 100.0;
    entity.content.vector.fillColor = QStringLiteral("#ff0000");
    entity.transform.scaleX = 1.6;
    entity.transform.scaleY = 0.9;
    const pvf::Uuid uuid = entity.uuid;
    project.entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("pvf: init"),
                                          &error))
    {
        qCritical() << "scale-link-test: init commit failed" << error;
        return 1;
    }

    pvf::MainWindow window(session.release());
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* sessionPtr = window.editorSessionForTest();
    auto* viewport = window.viewportForTest();
    if (!sessionPtr || !viewport)
    {
        qCritical() << "scale-link-test: no session viewport";
        return 1;
    }
    auto* document = sessionPtr->document();
    const auto flushSession = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        return flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };
    const auto selectEntity = [&] {
        document->setSelection(uuid);
        sessionPtr->setPlayhead(0);
        QApplication::processEvents();
        viewport->repaint();
        QApplication::processEvents();
    };
    selectEntity();
    // The first undo entry must snapshot the committed project, not the empty
    // store baseline: force the init edit onto disk before any drag.
    flushSession();

    MouseDriver mouse{viewport};
    constexpr double kRatio = 1.6 / 0.9;

    auto* link = window.findChild<QCheckBox*>(QStringLiteral("transformLink"));
    if (!check(link != nullptr, "transformLink checkbox missing"))
        return 1;
    if (!check(link->text() == QStringLiteral("等比缩放"),
               "transformLink label is not the Chinese name"))
        return 1;
    if (!check(!viewport->scaleLinkEnabledForTest(),
               "viewport scale link must default off"))
        return 1;
    link->setChecked(true);
    QApplication::processEvents();
    if (!check(viewport->scaleLinkEnabledForTest(),
               "properties toggle did not reach the viewport"))
        return 1;

    // 1) Centre scale: dragging the centre handle to twice its offset is a
    //    factor of 2, so 1.6 : 0.9 must become 3.2 : 1.8, not 3.2 : 3.2.
    {
        const QPointF pivot = viewport->sessionPivotForTest();
        const QPointF start =
            viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::CenterScale);
        const QPointF target = pivot + (start - pivot) * 2.0;
        const int depthBefore = undoDepth();
        mouse.drag(start, target, Qt::ControlModifier);
        QApplication::processEvents();
        flushSession();

        const pvf::gizmo::Transform after =
            viewport->gizmoTransformForTest(uuid);
        check(ratioMatches(after.scaleX, after.scaleY, kRatio),
              "centre scale broke the 16:9 ratio");
        check(nearly(after.scaleX, 3.2, 1e-3) && nearly(after.scaleY, 1.8, 1e-3),
              "centre scale did not apply factor 2 to both axes");
        check(undoDepth() == depthBefore + 1,
              "centre scale was not one commit");

        document->undo();
        QApplication::processEvents();
        const pvf::gizmo::Transform undone =
            viewport->gizmoTransformForTest(uuid);
        if (!(nearly(undone.scaleX, 1.6) && nearly(undone.scaleY, 0.9)))
        {
            qCritical() << "scale-link-test: centre undo left" << undone.scaleX
                        << undone.scaleY << "depth" << undoDepth();
            ++failures;
        }
        selectEntity();
    }

    // 2) Corner drag: the uniform factor may differ from 2, but both axes must
    //    share it so the ratio survives, and the opposite corner stays fixed.
    {
        const QPointF start =
            viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::ScaleTopLeft);
        const QPointF opposite = viewport->gizmoCornerForTest(uuid, 2);
        const QPointF fixedBefore = opposite;
        const QPointF target = opposite + (start - opposite) * 1.5;
        const int depthBefore = undoDepth();
        mouse.drag(start, target, Qt::ControlModifier);
        QApplication::processEvents();
        flushSession();

        const pvf::gizmo::Transform after =
            viewport->gizmoTransformForTest(uuid);
        if (!ratioMatches(after.scaleX, after.scaleY, kRatio))
        {
            qCritical() << "scale-link-test: corner scale broke 16:9 ->"
                        << after.scaleX << after.scaleY << "start" << start
                        << "target" << target << "depth" << depthBefore
                        << undoDepth();
            ++failures;
        }
        check(!nearly(after.scaleX, 1.6, 1e-3),
              "corner scale did not change the scale");
        if (undoDepth() != depthBefore + 1)
        {
            qCritical() << "scale-link-test: corner commits" << depthBefore
                        << undoDepth();
            ++failures;
        }
        const QPointF fixedAfter = viewport->gizmoCornerForTest(uuid, 2);
        check(std::hypot(fixedAfter.x() - fixedBefore.x(),
                         fixedAfter.y() - fixedBefore.y())
                  < 0.5,
              "corner scale moved the opposite corner");

        document->undo();
        QApplication::processEvents();
        const pvf::gizmo::Transform undone =
            viewport->gizmoTransformForTest(uuid);
        check(nearly(undone.scaleX, 1.6) && nearly(undone.scaleY, 0.9),
              "one undo did not restore both corner-scaled axes");
        selectEntity();
    }

    // 3) Multi-key relative delta: every selected whole-frame key scales by
    //    the same linked factor and keeps its own ratio, one undo total.
    {
        auto& keyed = sessionPtr->mutableProject().entities[uuid];
        pvf::model::Keyframe key0;
        key0.frame = 0;
        key0.scaleX = 1.6;
        key0.scaleY = 0.9;
        pvf::model::Keyframe key10;
        key10.frame = 10;
        key10.scaleX = 0.8;
        key10.scaleY = 0.45;
        keyed.keyframes = {key0, key10};
        keyed.transform.scaleX = 1.0;
        keyed.transform.scaleY = 1.0;
        if (!document->commitEntity(uuid, QStringLiteral("pvf: scale keys"),
                                    &error))
        {
            qCritical() << "scale-link-test: key setup failed" << error;
            return 1;
        }
        flushSession();
        document->setSelection(uuid);
        sessionPtr->setPlayhead(0);
        viewport->setSelectedKeyframes(
            {pvf::KeyRef{uuid, 0}, pvf::KeyRef{uuid, 10}});
        QApplication::processEvents();
        viewport->repaint();
        QApplication::processEvents();

        const QPointF pivot = viewport->sessionPivotForTest();
        const QPointF start =
            viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::CenterScale);
        const QPointF target = pivot + (start - pivot) * 1.5;
        const int depthBefore = undoDepth();
        mouse.drag(start, target, Qt::ControlModifier);
        QApplication::processEvents();
        flushSession();

        const auto& after = sessionPtr->project().entities[uuid];
        const pvf::model::Keyframe* after0 =
            pvf::model::keyframeAt(after.keyframes, 0);
        const pvf::model::Keyframe* after10 =
            pvf::model::keyframeAt(after.keyframes, 10);
        check(after0 && after10, "multi-key scale lost a key");
        if (after0 && after10)
        {
            check(nearly(after0->scaleX, 2.4, 1e-3)
                      && nearly(after0->scaleY, 1.35, 1e-3),
                  "multi-key scale did not apply factor 1.5 to key 0");
            check(nearly(after10->scaleX, 1.2, 1e-3)
                      && nearly(after10->scaleY, 0.675, 1e-3),
                  "multi-key scale did not apply factor 1.5 to key 10");
            check(ratioMatches(after0->scaleX, after0->scaleY, kRatio)
                      && ratioMatches(after10->scaleX, after10->scaleY, kRatio),
                  "multi-key scale broke a per-key ratio");
        }
        check(undoDepth() == depthBefore + 1,
              "multi-key scale was not one commit");

        document->undo();
        QApplication::processEvents();
        const auto& undone = sessionPtr->project().entities[uuid];
        const pvf::model::Keyframe* undone0 =
            pvf::model::keyframeAt(undone.keyframes, 0);
        const pvf::model::Keyframe* undone10 =
            pvf::model::keyframeAt(undone.keyframes, 10);
        check(undone0 && undone10
                  && nearly(undone0->scaleX, 1.6) && nearly(undone0->scaleY, 0.9)
                  && nearly(undone10->scaleX, 0.8)
                  && nearly(undone10->scaleY, 0.45),
              "one undo did not restore both multi-key scales");
        viewport->setSelectedKeyframes({});
        QApplication::processEvents();
    }

    // 4) Turning the toggle off releases the lock again.
    link->setChecked(false);
    QApplication::processEvents();
    check(!viewport->scaleLinkEnabledForTest(),
          "properties toggle did not turn the viewport link off");

    if (failures == 0)
        qInfo() << "scale-link-test: ok";
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerViewportScaleLinkTests(Registry& registry)
{
    registry.add(QStringLiteral("--scale-link-test"),
                 [](QApplication& app) { return scaleLinkTest(app); });
}

} // namespace pvf::tests
