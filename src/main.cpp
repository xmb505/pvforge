#include "app/AppIcon.h"
#include "app/EditorSession.h"
#include "app/MainWindow.h"
#include "app/TestMode.h"
#include "app/WelcomeWindow.h"
#include "core/Lang.h"
#include "core/Settings.h"

#ifdef PVFORGE_SELF_TESTS
#include "tests/TestRegistry.h"
#endif

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

int main(int argc, char* argv[])
{
    // The current media pipeline decodes video first frames and audio with
    // FFmpeg software decoders. Keep Qt Multimedia's video backends from
    // probing VDPAU/VAAPI until real video playback is implemented; this also
    // avoids misleading driver warnings on machines without NVIDIA support.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray(","));

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PVForge"));
    app.setApplicationDisplayName(QStringLiteral("PVForge"));
    app.setOrganizationName(QStringLiteral("PVForge"));
    app.setWindowIcon(pvf::makeCubeIcon());

    // Keep self-tests independent from the user's saved layout, monitor
    // geometry and last project list. A fresh config per test process makes
    // pixel/geometry checks reproducible.
#ifdef PVFORGE_SELF_TESTS
    // Test mode is decided from the exact switch names registered in
    // src/tests/TestRegistry.cpp -- no `-test` suffix guessing, so
    // `--open /tmp/project-test` still uses the user's real config. Release
    // builds compile no registry, so this whole block is absent there.
    if (pvf::tests::isTestRun(app.arguments()))
    {
        pvf::setTestMode(true);
        // Per-process config so parallel test runs in different worktrees do
        // not delete each other's settings mid-test.
        const QString configDir =
            QDir::tempPath() + QStringLiteral("/pvforge-test-config-")
            + QString::number(QCoreApplication::applicationPid());
        QDir(configDir).removeRecursively();
        QDir().mkpath(configDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, configDir);
        // MCP's endpoint/token file also lives under XDG_CONFIG_HOME; keep
        // tests away from the user's real credentials and port file.
        qputenv("XDG_CONFIG_HOME", configDir.toUtf8());
    }
#endif

    pvf::Lang::load(pvf::Settings::language());

#ifdef PVFORGE_SELF_TESTS
    // Test builds answer the `--*-test` switches through the registry in
    // src/tests/. Release builds do not compile that code at all, so the same
    // main() is the shipping entry point.
    if (const auto testResult = pvf::tests::dispatch(app))
        return *testResult;
#endif

    // P1: `--open <project-dir>` opens a `.pvproj` directory directly.  The
    // legacy single-file format is rejected by EditorSession with a hint.
    QString openPath;
    QString shotPath;
    const QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i)
    {
        if (arguments.at(i) == QStringLiteral("--open")
            && i + 1 < arguments.size())
        {
            openPath = arguments.at(++i);
        }
        else if (arguments.at(i).startsWith(QStringLiteral("--open=")))
        {
            openPath = arguments.at(i).mid(QStringLiteral("--open=").size());
        }
        else if (arguments.at(i) == QStringLiteral("--shot")
                 && i + 1 < arguments.size())
        {
            shotPath = arguments.at(++i);
        }
        else if (arguments.at(i).startsWith(QStringLiteral("--shot=")))
        {
            shotPath = arguments.at(i).mid(QStringLiteral("--shot=").size());
        }
    }
    if (!openPath.isEmpty())
    {
        QString error;
        auto session = pvf::EditorSession::openProject(openPath, &error);
        if (!session)
        {
            qCritical().noquote() << "open failed:" << error;
            return 1;
        }
        auto* window = new pvf::MainWindow(session.release());
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->resize(1440, 900);
        window->show();
        if (!shotPath.isEmpty())
        {
            const QString target = shotPath;
            QTimer::singleShot(800, window, [window, target, &app] {
                QDir().mkpath(QFileInfo(target).absolutePath());
                if (!window->grab().save(target))
                    qCritical().noquote() << "screenshot failed:" << target;
                else
                    qInfo().noquote() << "screenshot:" << target;
                app.quit();
            });
        }
        return app.exec();
    }

    pvf::WelcomeWindow welcome;
    welcome.show();
    return app.exec();
}
