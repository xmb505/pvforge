#include "TestApp.h"
#include "TestRegistry.h"

#include <QMouseEvent>
#include <QTemporaryDir>

namespace pvf::tests {

namespace {

// Double-clicking a marker flag in the ruler renames that marker through the
// panel's prompt seam, commits exactly once, and a cancelled prompt (or an
// emptied title) writes nothing.
int timelineMarkerRenameTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("marker-rename.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("标记改名");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-marker-rename-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    const auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-marker-rename-test:" << message;
        return condition;
    };

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("片段");
    entity.start = 0;
    entity.end = 120;
    entity.content.kind = pvf::ContentKind::Empty;
    session->mutableProject().entities.insert(entity.uuid, entity);
    pvf::model::Marker marker;
    marker.id = QStringLiteral("m1");
    marker.frame = 30;
    marker.title = QStringLiteral("开场");
    marker.color = QStringLiteral("#ffbe3c");
    session->mutableProject().markers = {marker};
    if (!document->commitEntities({entity.uuid},
                                  QStringLiteral("marker rename setup"), &error)
        || !session->commitProject(QStringLiteral("marker rename setup"),
                                   &error))
    {
        qCritical() << "timeline-marker-rename-test: setup failed" << error;
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
    if (!check(live && timeline, QStringLiteral("window/timeline missing")))
        return 1;

    const QRect flag = timeline->markerRectForTest(0);
    if (!check(!flag.isEmpty(), QStringLiteral("marker flag rect missing")))
        return 1;
    const QPoint at = flag.center();
    const auto doubleClick = [&] {
        QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(at),
                          QPointF(timeline->mapToGlobal(at)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &event);
        QApplication::processEvents();
    };

    const int beforeDepth = int(document->store()->undoDepth());
    bool prompted = false;
    timeline->setMarkerRenamePromptForTest([&prompted](QString* title) {
        prompted = true;
        *title = QStringLiteral("重命名后");
        return true;
    });
    doubleClick();
    live->flush();
    if (!check(prompted, QStringLiteral("double-click did not prompt")))
        return 1;
    if (!check(live->project().markers.size() == 1
                   && live->project().markers.first().title
                          == QStringLiteral("重命名后"),
               QStringLiteral("marker title was not renamed")))
        return 1;
    if (!check(int(document->store()->undoDepth()) == beforeDepth + 1,
               QStringLiteral("marker rename did not commit exactly once")))
        return 1;

    // Cancelling keeps the title and does not add a commit.
    timeline->setMarkerRenamePromptForTest([](QString* title) {
        *title = QStringLiteral("不应写入");
        return false;
    });
    doubleClick();
    if (!check(live->project().markers.first().title
                   == QStringLiteral("重命名后"),
               QStringLiteral("cancelled rename wrote the title")))
        return 1;
    if (!check(int(document->store()->undoDepth()) == beforeDepth + 1,
               QStringLiteral("cancelled rename added a commit")))
        return 1;

    // An emptied (or whitespace-only) title is rejected, not committed.
    timeline->setMarkerRenamePromptForTest([](QString* title) {
        *title = QStringLiteral("   ");
        return true;
    });
    doubleClick();
    if (!check(live->project().markers.first().title
                   == QStringLiteral("重命名后"),
               QStringLiteral("empty rename wrote the title")))
        return 1;
    if (!check(int(document->store()->undoDepth()) == beforeDepth + 1,
               QStringLiteral("empty rename added a commit")))
        return 1;

    // A double-click on the ruler but away from any flag keeps the no-op path.
    timeline->setMarkerRenamePromptForTest([&prompted](QString* title) {
        prompted = true;
        *title = QStringLiteral("空白处");
        return true;
    });
    prompted = false;
    const QPoint far(timeline->frameXForTest(100), 4);
    QMouseEvent empty(QEvent::MouseButtonDblClick, QPointF(far),
                      QPointF(timeline->mapToGlobal(far)), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &empty);
    QApplication::processEvents();
    if (!check(!prompted, QStringLiteral("blank ruler double-click prompted")))
        return 1;
    timeline->setMarkerRenamePromptForTest(nullptr);

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-marker-rename-test OK: prompt, commit, cancel, empty";
    return 0;
}

} // namespace

void registerTimelineMarkerRenameTests(Registry& registry)
{
    registry.add(QStringLiteral("--timeline-marker-rename-test"),
                 [](QApplication& app) {
                     return timelineMarkerRenameTest(app);
                 });
}

} // namespace pvf::tests
