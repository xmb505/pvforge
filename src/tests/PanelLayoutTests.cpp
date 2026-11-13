#include "TestApp.h"
#include "TestRegistry.h"
#include "TestSupport.h"

#include "ui/panels/PanelGeometry.h"
#include "ui/panels/WorkbenchPanelHost.h"

#include <QGuiApplication>
#include <QPointer>
#include <QScreen>

namespace pvf::tests {

namespace {

QList<QRect> screenAreasForTest()
{
    QList<QRect> areas;
    for (QScreen* screen : QGuiApplication::screens())
        areas.append(screen->availableGeometry());
    return areas;
}

void checkGeometry(int* failures)
{
    const auto check = [failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "panel-layout-test:" << message;
            ++*failures;
        }
        return condition;
    };

    using namespace pvf::panels;
    const QSize minimum(420, 300);
    check(defaultDockRect(QPoint(10, 10), QSize(1000, 600), QSize(640, 560),
                          minimum)
              == QRect(10, 10, 640, 560),
          QStringLiteral("default dock rect changed"));
    check(clampToParent(QRect(-50, -50, 800, 700), minimum, QSize(1000, 600))
              == QRect(8, 8, 800, 600),
          QStringLiteral("parent clamp did not keep the panel inside"));
    check(clampToParent(QRect(10, 10, 200, 200), minimum, QSize(1000, 600))
              == QRect(10, 10, 420, 300),
          QStringLiteral("parent clamp did not enforce the minimum size"));

    const QList<QRect> singleScreen = {QRect(0, 0, 1920, 1080)};
    check(clampToScreens(QRect(5000, 5000, 640, 560), minimum, singleScreen,
                         singleScreen.first())
              == QRect(640, 260, 640, 560),
          QStringLiteral("off-screen window was not centred on the primary"));
    check(clampToScreens(QRect(1900, 1000, 640, 560), minimum, singleScreen,
                         singleScreen.first())
              == QRect(1280, 520, 640, 560),
          QStringLiteral("partially visible window was not clamped"));
    check(clampToScreens(QRect(100, 100, 200, 200), minimum, singleScreen,
                         singleScreen.first())
              == QRect(100, 100, 420, 300),
          QStringLiteral("small window did not grow to the minimum"));
    check(intersectsAnyScreen(QRect(10, 10, 100, 100), singleScreen),
          QStringLiteral("screen intersection helper failed"));
    check(!intersectsAnyScreen(QRect(3000, 3000, 100, 100), singleScreen),
          QStringLiteral("off-screen rectangle reported as visible"));
}

} // namespace

int panelLayoutTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    checkGeometry(&failures);

    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "panel-layout-test:" << message;
            ++failures;
        }
        return condition;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("panel-layout.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Panel layout");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "panel-layout-test: create failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* viewport = window->viewportForTest();
    auto* panel = window->workbenchForTest();
    auto* host = window->workbenchHostForTest();
    auto* workbenchButton =
        window->findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    auto* header =
        panel ? panel->findChild<QWidget*>(QStringLiteral("workbenchHeader"))
              : nullptr;
    if (!check(live && viewport && panel && host && workbenchButton && header,
               QStringLiteral("window parts missing")))
    {
        return 1;
    }

    // 1. Default state: hidden, docked child of the viewport.
    check(!panel->isVisible(), QStringLiteral("workbench starts visible"));
    check(panel->parentWidget() == viewport,
          QStringLiteral("workbench is not docked in the viewport"));
    check(!panel->isWindow(), QStringLiteral("docked workbench is a window"));
    check(host->mode() == pvf::WorkbenchPanelHost::Mode::Docked,
          QStringLiteral("initial mode is not docked"));
    check(header->height() == 22, QStringLiteral("header is not 22 px"));

    auto commitCamera = [&](const QString& name) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 30;
        entity.row = 0;
        entity.content.kind = pvf::ContentKind::Camera;
        live->document()->mutableProject().entities.insert(entity.uuid, entity);
        QString commitError;
        const bool committed = live->document()->commitEntity(
            entity.uuid, QStringLiteral("pvf: panel layout test"), &commitError);
        if (!committed)
            qCritical() << "panel-layout-test: commit failed" << commitError;
        return committed;
    };

    // 2. Open docked: visible, inside the viewport, same object.
    workbenchButton->click();
    QApplication::processEvents();
    check(panel->isVisible(), QStringLiteral("workbench did not open"));
    check(panel->parentWidget() == viewport,
          QStringLiteral("opened workbench left the viewport"));
    check(viewport->rect().intersects(panel->geometry()),
          QStringLiteral("docked geometry does not intersect the viewport"));
    check(host->isPanelVisible(), QStringLiteral("host missed the open state"));

    // 3. Clicking the cube again hides the docked panel.
    workbenchButton->click();
    QApplication::processEvents();
    check(!panel->isVisible(), QStringLiteral("docked workbench did not hide"));

    // 4. Float via the host: same widget becomes a native window.
    check(commitCamera(QStringLiteral("float playback")),
          QStringLiteral("camera commit before floating failed"));
    workbenchButton->click();
    QApplication::processEvents();
    host->setFloating(true);
    QApplication::processEvents();
    QApplication::processEvents();
    check(host->isFloating(), QStringLiteral("host mode did not become floating"));
    check(panel->isWindow(), QStringLiteral("floating workbench is not a window"));
    check(panel->parentWidget() == window,
          QStringLiteral("floating workbench is not owned by the main window"));
    check(pvf::panels::intersectsAnyScreen(panel->geometry(),
                                           screenAreasForTest()),
          QStringLiteral("floating geometry is off every screen"));
    check(panel->minimumWidth() == 420 && panel->minimumHeight() == 300,
          QStringLiteral("floating minimum size changed"));
    auto* floatAction =
        window->findChild<QAction*>(QStringLiteral("workbenchFloatAction"));
    auto* showAction =
        window->findChild<QAction*>(QStringLiteral("workbenchAction"));
    check(floatAction && floatAction->isChecked(),
          QStringLiteral("float menu action did not follow the mode"));
    check(floatAction
              && floatAction->shortcut()
                     == QKeySequence(QStringLiteral("Ctrl+Shift+D")),
          QStringLiteral("float shortcut changed"));
    check(showAction
              && showAction->shortcut()
                     == QKeySequence(QStringLiteral("Ctrl+Shift+W")),
          QStringLiteral("show shortcut changed"));

    // 5. Toggling while floating only raises; it must not hide the window.
    host->toggle();
    QApplication::processEvents();
    check(panel->isVisible(), QStringLiteral("floating toggle hid the window"));
    check(host->isFloating(), QStringLiteral("floating toggle changed the mode"));

    // The session-level wiring must survive the reparent: auto-key and
    // playback still react while the panel is a native window.
    auto* autoKeyAction =
        window->findChild<QAction*>(QStringLiteral("autoKeyAction"));
    auto* playAction = window->findChild<QAction*>(QStringLiteral("playAction"));
    if (check(autoKeyAction && playAction && live->playback(),
               QStringLiteral("auto-key/play actions missing")))
    {
        const bool autoKeyBefore = viewport->autoKeyEnabled();
        autoKeyAction->trigger();
        QApplication::processEvents();
        check(viewport->autoKeyEnabled() != autoKeyBefore,
              QStringLiteral("auto-key did not toggle while floating"));
        autoKeyAction->trigger();
        QApplication::processEvents();

        const bool playingBefore = live->playback()->isPlaying();
        playAction->trigger();
        QApplication::processEvents();
        check(live->playback()->isPlaying() != playingBefore,
              QStringLiteral("playback did not toggle while floating"));
        playAction->trigger();
        QApplication::processEvents();
        check(live->playback()->isPlaying() == playingBefore,
              QStringLiteral("playback did not return to its prior state"));
    }

    // 6. Hide/reopen keeps floating mode and geometry.
    const QRect floatedGeometry = panel->geometry();
    host->hidePanel();
    QApplication::processEvents();
    check(!panel->isVisible(), QStringLiteral("floating hide failed"));
    check(showAction && !showAction->isChecked(),
          QStringLiteral("show action stayed checked after hiding"));
    host->showPanel();
    QApplication::processEvents();
    QApplication::processEvents();
    check(panel->isVisible() && panel->isWindow(),
          QStringLiteral("floating mode was not restored"));
    const QRect reopened = panel->geometry();
    check((reopened.topLeft() - floatedGeometry.topLeft()).manhattanLength() <= 2
              && std::abs(reopened.width() - floatedGeometry.width()) <= 2
              && std::abs(reopened.height() - floatedGeometry.height()) <= 2,
          QStringLiteral("floating geometry was not restored"));

    // 7. Dock back: parent viewport, geometry inside it, mode docked.
    host->setFloating(false);
    QApplication::processEvents();
    check(!panel->isWindow(), QStringLiteral("docked workbench is still a window"));
    check(panel->parentWidget() == viewport,
          QStringLiteral("docked workbench did not return to the viewport"));
    check(viewport->rect().intersects(panel->geometry()),
          QStringLiteral("docked geometry left the viewport"));
    check(host->mode() == pvf::WorkbenchPanelHost::Mode::Docked,
          QStringLiteral("host mode did not return to docked"));

    // 8. The AI lock is a property of the panel; floating/docking keeps it.
    panel->setEnabled(false);
    host->setFloating(true);
    QApplication::processEvents();
    check(!panel->isEnabled(),
          QStringLiteral("floating cleared the AI lock"));
    host->setFloating(false);
    QApplication::processEvents();
    check(!panel->isEnabled(), QStringLiteral("docking cleared the AI lock"));
    panel->setEnabled(true);

    // 9. Closing the floating window only hides it; the session still commits.
    host->setFloating(true);
    QApplication::processEvents();
    QPointer<pvf::WorkbenchPanel> guard(panel);
    panel->close();
    QApplication::processEvents();
    check(!guard.isNull(), QStringLiteral("closing the float destroyed the panel"));
    check(!panel->isVisible(), QStringLiteral("closing the float did not hide it"));
    check(commitCamera(QStringLiteral("after close")),
          QStringLiteral("session could not commit after closing the float"));
    check(window->findChildren<pvf::WorkbenchPanel*>().size() == 1,
          QStringLiteral("workbench is no longer single-instance"));

    // 10. Persist the floating geometry, reopen the project, restore it.
    host->showPanel();
    QApplication::processEvents();
    QApplication::processEvents();
    const QRect remembered = panel->geometry();
    host->hidePanel();
    QApplication::processEvents();
    window->close();
    delete window;
    QApplication::processEvents();

    auto reopenedSession =
        pvf::EditorSession::openProject(projectDir, &error, false);
    if (!check(reopenedSession != nullptr,
               QStringLiteral("reopen failed: %1").arg(error)))
        return 1;
    auto* second = new pvf::MainWindow(reopenedSession.release());
    second->setAttribute(Qt::WA_DeleteOnClose);
    second->resize(1440, 900);
    second->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* secondPanel = second->workbenchForTest();
    auto* secondHost = second->workbenchHostForTest();
    if (!check(secondPanel && secondHost,
               QStringLiteral("reopened window has no workbench")))
        return 1;
    check(secondHost->mode() == pvf::WorkbenchPanelHost::Mode::Floating,
          QStringLiteral("floating mode was not persisted"));
    check(!secondPanel->isVisible(),
          QStringLiteral("hidden workbench was restored visible"));
    secondHost->showPanel();
    QApplication::processEvents();
    QApplication::processEvents();
    check(secondPanel->isVisible() && secondPanel->isWindow(),
          QStringLiteral("persisted floating mode did not reopen as a window"));
    check(pvf::panels::intersectsAnyScreen(secondPanel->geometry(),
                                           screenAreasForTest()),
          QStringLiteral("restored floating geometry is off every screen"));
    const QRect restored = secondPanel->geometry();
    check((restored.topLeft() - remembered.topLeft()).manhattanLength() <= 4,
          QStringLiteral("restored floating position drifted"));
    check(std::abs(restored.width() - remembered.width()) <= 4
              && std::abs(restored.height() - remembered.height()) <= 4,
          QStringLiteral("restored floating size drifted"));

    second->close();
    delete second;
    QApplication::processEvents();

    pvf::Settings::setWorkbenchVisible(false);
    pvf::Settings::setWorkbenchMode(QStringLiteral("docked"));

    if (failures == 0)
        qInfo() << "panel-layout-test OK: open/repeat/float/dock/hide/geometry/"
                   "shortcuts/lock/persistence";
    return failures == 0 ? 0 : 1;
}

void registerPanelLayoutTests(Registry& registry)
{
    registry.add(QStringLiteral("--panel-layout-test"),
                 [](QApplication& app) { return panelLayoutTest(app); });
}

} // namespace pvf::tests
