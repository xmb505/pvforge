#include "TestApp.h"
#include "TestRegistry.h"
#include "TestSupport.h"

#include "ui/panels/PanelGeometry.h"

#include <QGuiApplication>
#include <QScreen>

namespace pvf::tests {

namespace {

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
    auto* workbenchButton =
        window->findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    auto* header =
        panel ? panel->findChild<QWidget*>(QStringLiteral("workbenchHeader"))
              : nullptr;
    if (!check(live && viewport && panel && workbenchButton && header,
               QStringLiteral("window parts missing")))
    {
        return 1;
    }

    // 1. Default state: hidden, docked child of the viewport (stage).
    check(!panel->isVisible(), QStringLiteral("workbench starts visible"));
    check(panel->parentWidget() == viewport,
          QStringLiteral("workbench is not docked in the viewport"));
    check(!panel->isWindow(), QStringLiteral("docked workbench is a window"));
    check(header->height() == 22, QStringLiteral("header is not 22 px"));

    // 2. Open docked: visible, inside the viewport, bound to the stage.
    workbenchButton->click();
    QApplication::processEvents();
    check(panel->isVisible(), QStringLiteral("workbench did not open"));
    check(panel->parentWidget() == viewport,
          QStringLiteral("opened workbench left the viewport"));
    check(!panel->isWindow(), QStringLiteral("workbench floated unexpectedly"));
    check(viewport->rect().intersects(panel->geometry()),
          QStringLiteral("docked geometry does not intersect the viewport"));

    // 3. Clicking the button again hides the docked panel.
    workbenchButton->click();
    QApplication::processEvents();
    check(!panel->isVisible(), QStringLiteral("docked workbench did not hide"));

    window->close();
    delete window;
    QApplication::processEvents();

    if (failures == 0)
        qInfo() << "panel-layout-test OK: geometry/docked-workbench";
    return failures == 0 ? 0 : 1;
}

void registerPanelLayoutTests(Registry& registry)
{
    registry.add(QStringLiteral("--panel-layout-test"),
                 [](QApplication& app) { return panelLayoutTest(app); });
}

} // namespace pvf::tests
