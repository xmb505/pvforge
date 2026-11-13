#include "WorkbenchPanelHost.h"

#include "core/Settings.h"
#include "ui/WorkbenchPanel.h"

#include "PanelGeometry.h"

#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace pvf {

namespace {
constexpr int kMinimumWidth = 420;
constexpr int kMinimumHeight = 300;
constexpr int kPreferredWidth = 640;
constexpr int kPreferredHeight = 560;
constexpr int kSaveDelayMs = 500;
} // namespace

WorkbenchPanelHost::WorkbenchPanelHost(WorkbenchPanel* panel,
                                       QWidget* dockParent, QWidget* windowParent,
                                       AnchorProvider anchor, QObject* parent)
    : QObject(parent)
    , m_panel(panel)
    , m_dockParent(dockParent)
    , m_windowParent(windowParent)
    , m_anchor(std::move(anchor))
{
    m_mode = Settings::workbenchMode() == QStringLiteral("floating")
                 ? Mode::Floating
                 : Mode::Docked;

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this,
            [this] { saveGeometryForMode(); });

    if (m_panel)
    {
        m_panel->installEventFilter(this);
        connect(m_panel, &WorkbenchPanel::floatToggleRequested, this,
                &WorkbenchPanelHost::toggleFloating);
        connect(m_panel, &WorkbenchPanel::hideRequested, this,
                &WorkbenchPanelHost::hidePanel);
        connect(m_panel, &WorkbenchPanel::dockRequested, this,
                [this] { setFloating(false); });
        connect(m_panel, &WorkbenchPanel::visibilityChanged, this,
                [this](bool visible) {
                    if (!visible)
                        saveGeometryForMode();
                    persistVisible();
                    emit visibleChanged(visible);
                });
    }
    if (m_dockParent)
        m_dockParent->installEventFilter(this);

    for (QScreen* screen : QGuiApplication::screens())
        watchScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this,
            [this](QScreen* screen) {
                watchScreen(screen);
                reclampFloating();
            });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this,
            [this](QScreen*) { reclampFloating(); });
}

bool WorkbenchPanelHost::isPanelVisible() const
{
    return m_panel && m_panel->isVisible();
}

QList<QRect> WorkbenchPanelHost::screenAreas() const
{
    QList<QRect> areas;
    for (QScreen* screen : QGuiApplication::screens())
        areas.append(screen->availableGeometry());
    return areas;
}

QRect WorkbenchPanelHost::primaryScreenArea() const
{
    const QScreen* primary = QGuiApplication::primaryScreen();
    return primary ? primary->availableGeometry() : QRect();
}

QRect WorkbenchPanelHost::defaultDockGeometry() const
{
    const QSize parentSize = m_dockParent ? m_dockParent->size() : QSize();
    const QPoint anchor = m_anchor ? m_anchor() : QPoint(48, 48);
    return panels::defaultDockRect(anchor, parentSize,
                                   QSize(kPreferredWidth, kPreferredHeight),
                                   QSize(kMinimumWidth, kMinimumHeight));
}

QRect WorkbenchPanelHost::dockGeometry() const
{
    QRect desired = Settings::workbenchDockGeometry();
    if (desired.x() < 0 || desired.y() < 0 || desired.width() < kMinimumWidth
        || desired.height() < kMinimumHeight)
    {
        desired = defaultDockGeometry();
    }
    const QSize parentSize = m_dockParent ? m_dockParent->size() : QSize();
    return panels::clampToParent(
        desired, QSize(kMinimumWidth, kMinimumHeight), parentSize);
}

QRect WorkbenchPanelHost::clampedWindowGeometry(const QRect& desired) const
{
    QRect rect = desired;
    if (rect.x() < 0 || rect.y() < 0 || rect.width() < kMinimumWidth
        || rect.height() < kMinimumHeight)
    {
        const QPoint origin =
            m_dockParent ? m_dockParent->mapToGlobal(QPoint(0, 0)) : QPoint(0, 0);
        rect = QRect(origin + QPoint(48, 48),
                     QSize(kPreferredWidth, kPreferredHeight));
    }
    return panels::clampToScreens(rect,
                                  QSize(kMinimumWidth, kMinimumHeight),
                                  screenAreas(), primaryScreenArea());
}

void WorkbenchPanelHost::showPanel()
{
    if (!m_panel)
        return;

    if (m_mode == Mode::Floating)
    {
        applyFloatingWindow();
        return;
    }

    if (m_panel->isWindow() || m_panel->parentWidget() != m_dockParent)
        applyDockParent();
    else
    {
        m_panel->showDocked(dockGeometry());
        m_dockGeometryValid = true;
    }
}

void WorkbenchPanelHost::hidePanel()
{
    if (!m_panel)
        return;
    if (m_panel->isVisible())
    {
        saveGeometryForMode();
        m_panel->hidePanel();
    }
    persistVisible();
}

void WorkbenchPanelHost::toggle()
{
    if (!m_panel)
        return;
    if (!m_panel->isVisible())
    {
        showPanel();
        return;
    }
    if (m_mode == Mode::Floating)
    {
        // OBS semantics: clicking the workbench cube while the floating window
        // is visible raises it instead of hiding it.
        m_panel->raise();
        m_panel->activateWindow();
        return;
    }
    hidePanel();
}

void WorkbenchPanelHost::setFloating(bool floating)
{
    if (!m_panel)
        return;
    if ((m_mode == Mode::Floating) == floating)
    {
        if (floating && m_panel->isVisible())
        {
            m_panel->raise();
            m_panel->activateWindow();
        }
        return;
    }

    const bool wasVisible = m_panel->isVisible();
    saveGeometryForMode();

    m_switching = true;
    if (m_panel->isVisible())
        m_panel->hidePanel();
    m_switching = false;

    m_mode = floating ? Mode::Floating : Mode::Docked;
    persistMode();
    if (wasVisible)
        showPanel();
    else if (floating)
        m_panel->setFloatingChrome(true);
    else
        m_panel->setFloatingChrome(false);

    emit floatingChanged(floating);
}

void WorkbenchPanelHost::toggleFloating()
{
    setFloating(!isFloating());
}

void WorkbenchPanelHost::applyDockParent()
{
    if (!m_panel)
        return;
    m_panel->hidePanel();
    m_panel->setWindowFlags(Qt::Widget);
    m_panel->setParent(m_dockParent);
    m_panel->setFloatingChrome(false);
    m_panel->showDocked(dockGeometry());
    m_dockGeometryValid = true;
}

void WorkbenchPanelHost::applyFloatingWindow()
{
    if (!m_panel)
        return;

    if (!m_panel->isWindow() || m_panel->parentWidget() != m_windowParent)
    {
        m_panel->hidePanel();
        m_panel->setParent(m_windowParent);
        m_panel->setWindowFlags(Qt::Window);
        m_panel->setFloatingChrome(true);
    }
    m_panel->setGeometry(clampedWindowGeometry(
        Settings::workbenchWindowGeometry()));
    m_panel->show();
    m_panel->raise();
    m_panel->activateWindow();
}

void WorkbenchPanelHost::saveGeometryForMode()
{
    if (!m_panel)
        return;
    if (m_mode == Mode::Floating)
    {
        if (m_panel->isWindow())
            Settings::setWorkbenchWindowGeometry(m_panel->geometry());
        return;
    }
    if (m_dockGeometryValid && !m_panel->isWindow() && m_dockParent
        && m_panel->parentWidget() == m_dockParent)
    {
        Settings::setWorkbenchDockGeometry(m_panel->geometry());
    }
}

void WorkbenchPanelHost::persistMode()
{
    Settings::setWorkbenchMode(m_mode == Mode::Floating
                                   ? QStringLiteral("floating")
                                   : QStringLiteral("docked"));
}

void WorkbenchPanelHost::persistVisible()
{
    if (!m_switching)
        Settings::setWorkbenchVisible(isPanelVisible());
}

void WorkbenchPanelHost::scheduleGeometrySave()
{
    if (!m_panel || !m_panel->isVisible())
        return;
    m_saveTimer->start();
}

void WorkbenchPanelHost::reclampFloating()
{
    if (m_mode != Mode::Floating || !m_panel || !m_panel->isWindow()
        || !m_panel->isVisible())
        return;
    const QRect clamped = clampedWindowGeometry(m_panel->geometry());
    if (clamped != m_panel->geometry())
        m_panel->setGeometry(clamped);
}

void WorkbenchPanelHost::watchScreen(QScreen* screen)
{
    if (!screen)
        return;
    connect(screen, &QScreen::availableGeometryChanged, this,
            [this](const QRect&) { reclampFloating(); });
}

void WorkbenchPanelHost::restoreOnStartup()
{
    if (!Settings::workbenchVisible())
        return;
    QTimer::singleShot(0, this, [this] {
        if (Settings::workbenchVisible() && m_panel && !m_panel->isVisible())
            showPanel();
    });
}

bool WorkbenchPanelHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_panel
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
    {
        scheduleGeometrySave();
    }
    else if (watched == m_dockParent && event->type() == QEvent::Resize
             && m_mode == Mode::Docked && m_panel && m_panel->isVisible()
             && !m_panel->isWindow() && m_dockParent)
    {
        const QRect clamped = panels::clampToParent(
            m_panel->geometry(), QSize(kMinimumWidth, kMinimumHeight),
            m_dockParent->size());
        if (clamped != m_panel->geometry())
            m_panel->setGeometry(clamped);
    }
    return QObject::eventFilter(watched, event);
}

} // namespace pvf
