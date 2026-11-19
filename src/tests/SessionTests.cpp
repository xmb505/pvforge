#include "TestApp.h"
#include "TestRegistry.h"
#include "app/TestMode.h"

namespace {

int selfTest()
{
    // Issue #96: test mode comes from exact registered switch names, not a
    // `-test` suffix. A project path that merely ends in `-test` must keep the
    // user's real QSettings/XDG config.
    const QStringList projectNotTest{QStringLiteral("pvforge"),
                                     QStringLiteral("--open"),
                                     QStringLiteral("/tmp/project-test")};
    if (pvf::tests::isTestRun(projectNotTest))
    {
        qCritical() << "self-test: --open /tmp/project-test entered test mode";
        return 1;
    }
    const QStringList sessionTestArgs{QStringLiteral("pvforge"),
                                      QStringLiteral("--session-test")};
    if (!pvf::tests::isTestRun(sessionTestArgs) || !pvf::testMode())
    {
        qCritical() << "self-test: --session-test did not enter test mode";
        return 1;
    }

    // Issue #111: test processes must not register an input context with the
    // desktop IME. main() opts out before QApplication; assert the environment
    // actually seen by the running process.
    if (qEnvironmentVariable("QT_IM_MODULE") != QStringLiteral("none")
        || qEnvironmentVariableIsSet("GTK_IM_MODULE")
        || qEnvironmentVariableIsSet("XMODIFIERS"))
    {
        qCritical() << "self-test: test process still exposes the session IME"
                    << qEnvironmentVariable("QT_IM_MODULE")
                    << qEnvironmentVariable("GTK_IM_MODULE")
                    << qEnvironmentVariable("XMODIFIERS");
        return 1;
    }

    qInfo() << "self-test OK: test mode comes from exact switches";
    return 0;
}

int sessionTest()
{
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir = QDir(temp.path()).filePath(QStringLiteral("demo.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P1 Demo");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    settings.background = QStringLiteral("#1b2430");

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "session-test: create failed" << error;
        return 1;
    }
    if (session->settings().width != 640 || session->settings().height != 360
        || session->settings().fps != 30)
    {
        qCritical() << "session-test: created settings mismatch";
        return 1;
    }
    if (!QFileInfo::exists(QDir(projectDir).filePath(QStringLiteral("project.json"))))
    {
        qCritical() << "session-test: project.json missing";
        return 1;
    }

    // Rename + background go through commitProject and must survive reopen.
    session->mutableProject().settings.name = QStringLiteral("P1 Demo 改名");
    session->mutableProject().settings.background = QStringLiteral("#223344");
    if (!session->commitProject(QStringLiteral("session-test rename"), &error))
    {
        qCritical() << "session-test: commitProject failed" << error;
        return 1;
    }

    session->setPlayhead(42);
    session->flush(&error);
    if (!error.isEmpty())
    {
        qCritical() << "session-test: flush failed" << error;
        return 1;
    }
    const QString playheadPath =
        QDir(projectDir).filePath(QStringLiteral(".pvforge/playhead.json"));
    if (!QFileInfo::exists(playheadPath))
    {
        qCritical() << "session-test: playhead.json missing";
        return 1;
    }
    session.reset();

    auto reopened = pvf::EditorSession::openProject(projectDir, &error);
    if (!reopened)
    {
        qCritical() << "session-test: reopen failed" << error;
        return 1;
    }
    if (reopened->settings().name != QStringLiteral("P1 Demo 改名")
        || reopened->settings().background != QStringLiteral("#223344")
        || reopened->playhead() != 42)
    {
        qCritical() << "session-test: reopen did not preserve settings/playhead"
                    << reopened->settings().name << reopened->settings().background
                    << reopened->playhead();
        return 1;
    }

    // P1 shell: title, project-properties wiring and Ctrl+S flush.
    auto* window = new pvf::MainWindow(reopened.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    QApplication::processEvents();
    if (window->windowTitle() != QStringLiteral("PVForge — P1 Demo 改名"))
    {
        qCritical() << "session-test: window title mismatch"
                    << window->windowTitle();
        return 1;
    }
    auto* nameEdit =
        window->findChild<QLineEdit*>(QStringLiteral("projectName"));
    auto* saveAction =
        window->findChild<QAction*>(QStringLiteral("saveAction"));
    auto* backgroundButton =
        window->findChild<QPushButton*>(QStringLiteral("backgroundButton"));
    if (!nameEdit || !saveAction || !backgroundButton)
    {
        qCritical() << "session-test: project properties/save action missing";
        return 1;
    }
    if (backgroundButton->text() != QStringLiteral("#223344"))
    {
        qCritical() << "session-test: background panel shows"
                    << backgroundButton->text();
        return 1;
    }

    QMetaObject::invokeMethod(nameEdit, "textEdited", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("P1 UI 改名")));
    QApplication::processEvents();
    if (!window->windowTitle().contains(QStringLiteral("P1 UI 改名"))
        || !window->windowTitle().endsWith(QLatin1Char('*')))
    {
        qCritical() << "session-test: pending commit marker missing"
                    << window->windowTitle();
        return 1;
    }
    saveAction->trigger();
    QApplication::processEvents();
    if (window->windowTitle().endsWith(QLatin1Char('*')))
    {
        qCritical() << "session-test: Ctrl+S did not clear pending marker"
                    << window->windowTitle();
        return 1;
    }
    window->close();
    QApplication::processEvents();

    QFile savedProject(QDir(projectDir).filePath(QStringLiteral("project.json")));
    if (!savedProject.open(QIODevice::ReadOnly))
        return 1;
    const QJsonObject savedObject =
        QJsonDocument::fromJson(savedProject.readAll()).object();
    if (savedObject.value(QStringLiteral("name")).toString()
        != QStringLiteral("P1 UI 改名"))
    {
        qCritical() << "session-test: panel rename was not saved";
        return 1;
    }

    // Legacy single-file projects are rejected with a migration hint.
    const QString legacyPath =
        QDir(temp.path()).filePath(QStringLiteral("legacy.pvproj"));
    QFile legacy(legacyPath);
    if (!legacy.open(QIODevice::WriteOnly))
        return 1;
    legacy.write("{}");
    legacy.close();
    error.clear();
    if (pvf::EditorSession::openProject(legacyPath, &error) != nullptr
        || !error.contains(QStringLiteral("废弃")))
    {
        qCritical() << "session-test: legacy file was not rejected" << error;
        return 1;
    }

    // A session-less MainWindow must show the placeholder instead of crashing.
    {
        pvf::MainWindow emptyWindow(static_cast<pvf::EditorSession*>(nullptr));
        emptyWindow.show();
        QApplication::processEvents();
        emptyWindow.close();
        QApplication::processEvents();
    }

    qInfo() << "session-test OK: 640x360@30 create/open/rename/playhead";
    return 0;
}

int welcomeTest(QApplication& app)
{
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectPath =
        QDir(temp.path()).filePath(QStringLiteral("pvforge_welcome_test.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("欢迎页测试");
    settings.width = 1280;
    settings.height = 720;
    settings.fps = 30;
    QString error;
    auto session = pvf::EditorSession::createProject(projectPath, settings, &error,
                                                     false);
    if (!session)
    {
        qCritical() << "welcome-test: create failed" << error;
        return 1;
    }
    session->flush();

    QSettings qsettings;
    const QVariant oldRecent = qsettings.value(QStringLiteral("recentProjects"));
    qsettings.setValue(QStringLiteral("recentProjects"), QStringList{projectPath});

    pvf::WelcomeWindow welcome;
    welcome.show();

    QTimer::singleShot(400, &welcome, [&app, &welcome, projectPath, oldRecent] {
        welcome.grab().toImage().save(QStringLiteral("/tmp/pvforge_welcome.png"));

        auto* dialog = new pvf::NewProjectDialog(&welcome);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();

        QTimer::singleShot(300, dialog, [&app, dialog, oldRecent] {
            dialog->grab().toImage().save(QStringLiteral("/tmp/pvforge_newproject.png"));
            qInfo() << "welcome-test OK";

            QSettings settings;
            if (oldRecent.isValid())
                settings.setValue(QStringLiteral("recentProjects"), oldRecent);
            else
                settings.remove(QStringLiteral("recentProjects"));
            app.exit(0);
        });
    });
    return app.exec();
}

int settingsTest(QApplication& app)
{
    // ---- P6: legacy settings dialog on AppSettings ----------------------
    {
        QTemporaryDir temp;
        if (!temp.isValid())
            return 1;

        pvf::next::SettingsStore store;
        pvf::next::AppSettings initial;
        initial.onboardingDone = true;
        initial.gitEnabled = false;
        store.save(initial);

        const QString projectDir =
            QDir(temp.path()).filePath(QStringLiteral("p6-settings.pvproj"));
        pvf::model::ProjectSettings projectSettings;
        projectSettings.name = QStringLiteral("P6 设置");
        projectSettings.width = 640;
        projectSettings.height = 360;
        projectSettings.fps = 30;
        QString error;
        auto session = pvf::EditorSession::createProject(
            projectDir, projectSettings, &error, false);
        if (!session)
        {
            qCritical() << "settings-test: session create failed" << error;
            return 1;
        }
        auto* window = new pvf::MainWindow(session.release());
        window->resize(1440, 900);
        window->show();
        QApplication::processEvents();

        auto* live = window->editorSessionForTest();
        auto* dialog = window->openSessionSettingsDialogForTest();
        if (!live || !dialog)
        {
            qCritical() << "settings-test: session settings dialog missing";
            return 1;
        }

        int failures = 0;
        const auto expect = [&failures](bool condition, const QString& message) {
            if (!condition)
            {
                qCritical() << "settings-test:" << message;
                ++failures;
            }
        };

        auto* autoSave =
            dialog->findChild<QSpinBox*>(QStringLiteral("autoSaveMinutes"));
        auto* commitDelay =
            dialog->findChild<QSpinBox*>(QStringLiteral("humanCommitDelayMs"));
        auto* snap = dialog->findChild<QSpinBox*>(QStringLiteral("snapPixels"));
        auto* snapCenter =
            dialog->findChild<QCheckBox*>(QStringLiteral("snapCanvasCenter"));
        auto* htmlScale =
            dialog->findChild<QComboBox*>(QStringLiteral("htmlScale"));
        auto* htmlMemory =
            dialog->findChild<QSpinBox*>(QStringLiteral("htmlMemoryMb"));
        auto* watchdog =
            dialog->findChild<QSpinBox*>(QStringLiteral("watchdogMs"));
        auto* videoWidth =
            dialog->findChild<QComboBox*>(QStringLiteral("videoProxyWidth"));
        auto* workbenchScale =
            dialog->findChild<QComboBox*>(QStringLiteral("workbenchScale"));
        auto* workbenchExpand =
            dialog->findChild<QSpinBox*>(QStringLiteral("workbenchExpandPercent"));
        auto* workbenchMemory =
            dialog->findChild<QSpinBox*>(QStringLiteral("workbenchMemoryMb"));
        auto* workbenchWriter = dialog->findChild<QComboBox*>(
            QStringLiteral("workbenchWriterThreads"));
        auto* workbenchStats =
            dialog->findChild<QLabel*>(QStringLiteral("workbenchCacheStats"));
        auto* clearWorkbench =
            dialog->findChild<QPushButton*>(QStringLiteral("clearWorkbenchCache"));
        auto* openCache =
            dialog->findChild<QPushButton*>(QStringLiteral("openCacheFolder"));
        auto* htmlStats =
            dialog->findChild<QLabel*>(QStringLiteral("htmlCacheStats"));
        auto* openHtmlCache =
            dialog->findChild<QPushButton*>(QStringLiteral("openHtmlCacheFolder"));
        auto* solveInfo =
            dialog->findChild<QLabel*>(QStringLiteral("solveCacheInfo"));
        auto* rebuildSolve =
            dialog->findChild<QPushButton*>(QStringLiteral("rebuildSolveCache"));
        auto* cachePage =
            dialog->findChild<QWidget*>(QStringLiteral("cachePage"));
        auto* categories =
            dialog->findChild<QListWidget*>(QStringLiteral("settingsCategories"));
        auto* mcpPort = dialog->findChild<QSpinBox*>(QStringLiteral("mcpPort"));
        auto* mcpToken = dialog->findChild<QLineEdit*>(QStringLiteral("mcpToken"));
        auto* gitEnabled =
            dialog->findChild<QCheckBox*>(QStringLiteral("gitEnabled"));
        auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("applyButton"));
        expect(autoSave && commitDelay && snap && snapCenter && htmlScale
                   && htmlMemory && watchdog && videoWidth && workbenchScale
                   && workbenchExpand && workbenchMemory && workbenchWriter
                   && workbenchStats
                   && clearWorkbench && openCache && htmlStats && openHtmlCache
                   && solveInfo && rebuildSolve && cachePage && categories
                   && mcpPort && mcpToken && gitEnabled && apply,
               QStringLiteral("app settings widgets missing"));
        expect(categories && categories->count() == 7
                   && categories->item(2)->text() == QStringLiteral("缓存")
                   && categories->item(3)->text() == QStringLiteral("HTML"),
               QStringLiteral("settings page order changed"));
        if (failures)
            return 1;

        expect(autoSave->value() == 5 && commitDelay->value() == 1000
                   && snap->value() == 6
                   && htmlScale->currentData().toDouble() == 0.5
                   && videoWidth->currentData().toInt() == 960
                   && workbenchScale->currentData().toInt() == 50
                   && workbenchMemory->value() == 256
                   && workbenchWriter->currentData().toInt() == 2
                   && !workbenchStats->text().isEmpty()
                   && !htmlStats->text().isEmpty()
                   && mcpPort->value() == 47821 && !gitEnabled->isChecked(),
               QStringLiteral("app settings defaults mismatch"));
        expect(window->sessionCacheFolderPathForTest().endsWith(
                   QStringLiteral("/.cache"))
                   && window->sessionCacheFolderPathForTest(
                          QStringLiteral("workbench"))
                          .endsWith(QStringLiteral("/.cache/workbench")),
               QStringLiteral("cache folder path is wrong"));

        dialog->grab().toImage().save(
            QStringLiteral("/tmp/opencode/pvforge-p6-settings.png"));
        categories->setCurrentRow(2);
        QApplication::processEvents();
        dialog->grab().toImage().save(
            QStringLiteral("/tmp/opencode/pvforge-p6-cache.png"));

        const quint16 newPort = quint16(
            40000 + (QCoreApplication::applicationPid() % 20000));
        autoSave->setValue(3);
        commitDelay->setValue(700);
        snap->setValue(12);
        snapCenter->setChecked(false);
        htmlScale->setCurrentIndex(htmlScale->findData(1.0));
        htmlMemory->setValue(512);
        watchdog->setValue(5000);
        videoWidth->setCurrentIndex(videoWidth->findData(1280));
        workbenchScale->setCurrentIndex(workbenchScale->findData(75));
        workbenchExpand->setValue(75);
        workbenchMemory->setValue(512);
        workbenchWriter->setCurrentIndex(workbenchWriter->findData(1));
        mcpPort->setValue(newPort);
        const bool gitAvailable =
            pvf::store::ProjectStore::gitBinaryAvailable();
        gitEnabled->setChecked(gitAvailable);

        apply->click();
        QApplication::processEvents();
        QApplication::processEvents();

        const pvf::next::AppSettings saved = store.load();
        expect(saved.autoSaveMinutes == 3, QStringLiteral("autosave not saved"));
        expect(saved.humanCommitDelayMs == 700,
               QStringLiteral("commit debounce not saved"));
        expect(saved.snapPixels == 12 && !saved.snapCanvasCenter,
               QStringLiteral("snap settings not saved"));
        expect(saved.htmlScale == 1.0 && saved.htmlMemoryBytes == 512LL * 1024 * 1024
                   && saved.watchdogMs == 5000,
               QStringLiteral("HTML settings not saved"));
        expect(saved.videoProxyWidth == 1280,
               QStringLiteral("video proxy width not saved"));
        expect(saved.workbenchCacheScalePercent == 75
                   && saved.workbenchCacheExpandPercent == 75
                   && saved.workbenchCacheMemoryBytes == 512LL * 1024 * 1024
                   && saved.workbenchCacheWriterThreads == 1,
               QStringLiteral("workbench settings not saved"));
        expect(saved.mcpPort == newPort, QStringLiteral("MCP port not saved"));
        expect(saved.gitEnabled == (gitAvailable
                                        && live->document()->store()->gitEnabled()),
               QStringLiteral("Git setting was not persisted truthfully"));

        expect(live->appSettings().watchdogMs == 5000,
               QStringLiteral("session settings snapshot not updated"));
        expect(live->playback()->watchdogMs() == 5000,
               QStringLiteral("playback watchdog not rebuilt"));
        expect(live->document()->humanCommitDelayMs() == 700,
               QStringLiteral("document commit debounce not applied"));
        auto* afterWorkbench = live->workbench();
        expect(afterWorkbench != nullptr,
               QStringLiteral("workbench service is missing after apply"));
        expect(afterWorkbench && afterWorkbench->scalePercent() == 75
                   && afterWorkbench->expandPercent() == 75
                   && afterWorkbench->options().memoryBudgetBytes
                          == 512LL * 1024 * 1024
                   && afterWorkbench->writerThreads() == 1,
               QStringLiteral("workbench service kept the old settings"));

        // Changing only the writer count retunes the live pool: the service
        // instance (and every frame it still has queued) survives the apply.
        workbenchWriter->setCurrentIndex(workbenchWriter->findData(4));
        apply->click();
        QApplication::processEvents();
        QApplication::processEvents();
        expect(store.load().workbenchCacheWriterThreads == 4,
               QStringLiteral("writer thread count not saved"));
        expect(live->workbench() == afterWorkbench
                   && afterWorkbench->writerThreads() == 4,
               QStringLiteral("writer thread change rebuilt the workbench "
                              "service instead of retuning its pool"));
        expect(window->autosaveIntervalForTest() == 3 * 60 * 1000,
               QStringLiteral("autosave timer interval not rebuilt"));
        expect(live->mcpServer() && live->mcpServer()->isRunning()
                   && live->mcpServer()->port() == newPort,
               QStringLiteral("MCP server did not rebind to the new port"));
        expect(!mcpToken->text().isEmpty(),
               QStringLiteral("settings dialog does not show the MCP token"));
        if (gitAvailable)
            expect(live->document()->store()->gitEnabled(),
                   QStringLiteral("Git was not enabled on the store"));

        // Exercise the dialog -> MainWindow clear path without the modal
        // confirmation: drop a fake frame into the workbench cache root, emit
        // the signal the button would emit, and check the root and label.
        const QString cacheRoot =
            window->sessionCacheFolderPathForTest(QStringLiteral("workbench"));
        // Force a fresh, not-yet-opened cache service so clearing has to fall
        // back to removing the directory rather than a loaded index.
        live->invalidateWorkbench();
        live->workbench();
        QDir().mkpath(cacheRoot);
        const QString probePath = QDir(cacheRoot).filePath(
            QStringLiteral("999999.qoi"));
        QFile probe(probePath);
        if (probe.open(QIODevice::WriteOnly))
            probe.write(QByteArray(4096, '\0'));
        probe.close();
        dialog->clearWorkbenchCacheRequested();
        QApplication::processEvents();
        expect(!QFileInfo::exists(probePath),
               QStringLiteral("workbench probe frame was not cleared"));
        expect(workbenchStats->text().contains(QStringLiteral("帧数 0")),
               QStringLiteral("workbench stats were not refreshed: %1")
                   .arg(workbenchStats->text()));

        // The About page is the same widget Help -> About opens.
        dialog->showAboutPage();
        QApplication::processEvents();
        auto* about =
            dialog->findChild<QPlainTextEdit*>(QStringLiteral("aboutText"));
        const QString aboutInfo = about ? about->toPlainText() : QString();
        expect(about && aboutInfo.contains(QStringLiteral("处理器"))
                   && aboutInfo.contains(QStringLiteral("FFmpeg"))
                   && aboutInfo.contains(QStringLiteral("Qt "))
                   && aboutInfo.contains(QStringLiteral("PVForge")),
               QStringLiteral("about page incomplete"));
        expect(dialog->findChild<QPushButton*>(QStringLiteral("aboutCopy"))
                   != nullptr,
               QStringLiteral("about copy button missing"));

        dialog->reject();
        window->close();
        QApplication::processEvents();
        delete window;
        QApplication::processEvents();

        if (failures)
            return 1;
    }

    const int oldSnap = pvf::Settings::snapPixels();
    const bool oldEdgeCentre = pvf::Settings::edgeCenterSnap();
    const QString oldLanguage = pvf::Settings::language();

    pvf::Settings::setSnapPixels(12);
    if (pvf::Settings::snapPixels() != 12)
    {
        qCritical() << "settings-test: snap setting not persisted";
        return 1;
    }
    pvf::Settings::setSnapPixels(oldSnap);

    const pvf::DecoderInfo decoder = pvf::decoderInfo();
    if (decoder.ffmpegVersion.isEmpty() || decoder.mode.isEmpty()
        || decoder.video.isEmpty() || decoder.audioOutput.isEmpty())
    {
        qCritical() << "settings-test: decoder info incomplete";
        return 1;
    }

    const pvf::SystemInfo system = pvf::collectSystemInfo();
    if (system.cpuModel.isEmpty() || system.cpuModel == QStringLiteral("未知处理器")
        || system.physicalCores <= 0 || system.memoryBytes == 0
        || system.osName.isEmpty() || system.qtVersion.isEmpty()
        || system.gpus.isEmpty())
    {
        qCritical() << "settings-test: system info incomplete" << system.cpuModel
                    << system.physicalCores << system.memoryBytes << system.osName
                    << system.gpus;
        return 1;
    }

    pvf::Settings::setEdgeCenterSnap(true);
    if (!pvf::Settings::edgeCenterSnap())
    {
        qCritical() << "settings-test: edge snap setting not persisted";
        return 1;
    }
    pvf::Settings::setEdgeCenterSnap(oldEdgeCentre);

    pvf::Lang::load(QStringLiteral("en_US"));
    if (pvf::Lang::t(QStringLiteral("menu.file")) != QStringLiteral("File"))
    {
        qCritical() << "settings-test: english strings not loaded";
        return 1;
    }
    pvf::Lang::load(QStringLiteral("zh_CN"));
    if (pvf::Lang::t(QStringLiteral("menu.file")) != QStringLiteral("文件"))
    {
        qCritical() << "settings-test: chinese strings not loaded";
        return 1;
    }

    // Settings through the session shell: the window builds, the viewport
    // stays editable and the settings dialog can be shown.
    QTemporaryDir windowTemp;
    if (!windowTemp.isValid())
        return 1;
    QString error;
    auto settingsSession = pvf::EditorSession::createProject(
        QDir(windowTemp.path()).filePath(QStringLiteral("settings.pvproj")),
        pvf::model::ProjectSettings{}, &error, false);
    if (!settingsSession)
    {
        qCritical() << "settings-test: session create failed" << error;
        return 1;
    }
    auto* window = new pvf::MainWindow(settingsSession.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();

    QTimer::singleShot(400, &app, [&app, window] {
        int failures = 0;
        auto* viewport = window->findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "settings-test: viewport missing";
            app.exit(1);
            return;
        }
        if (!viewport->isEnabled())
        {
            qCritical() << "settings-test: viewport is disabled";
            ++failures;
        }

        qInfo() << "settings-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int gitSessionTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "git-test:" << message;
            ++failures;
        }
        return condition;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    // Default is off.
    {
        pvf::next::SettingsStore store(
            QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
        check(!store.load().gitEnabled,
              QStringLiteral("Git is enabled by default"));
    }

    // Missing git falls back to a plain text store.
    {
        const QByteArray oldPath = qgetenv("PATH");
        const QString emptyPath =
            QDir(temp.path()).filePath(QStringLiteral("empty-bin"));
        QDir().mkpath(emptyPath);
        qputenv("PATH", emptyPath.toUtf8());
        QString error;
        pvf::model::ProjectSettings settings;
        settings.name = QStringLiteral("无 Git");
        auto store = pvf::store::ProjectStore::create(
            QDir(temp.path()).filePath(QStringLiteral("no-git.pvproj")),
            settings, &error, true);
        check(store.has_value(),
              QStringLiteral("project creation failed without git: %1").arg(error));
        check(store && !store->gitEnabled(),
              QStringLiteral("store claims Git without a binary"));
        qputenv("PATH", oldPath);
    }

    const bool gitAvailable = pvf::store::ProjectStore::gitBinaryAvailable();
    if (!gitAvailable)
    {
        qInfo() << "git-test: git binary unavailable, skipping write/restore checks";
        qInfo() << "git-test failures" << failures;
        // Report the skip instead of a green run that exercised no git path.
        return failures == 0 ? 77 : 1;
    }

    const auto prepareDirty = [&temp](const QString& name,
                                      const QString& headName,
                                      const QString& dirtyName,
                                      QString* error) -> QString {
        const QString dir =
            QDir(temp.path()).filePath(name + QStringLiteral(".pvproj"));
        pvf::model::ProjectSettings settings;
        settings.name = headName;
        settings.width = 640;
        settings.height = 360;
        settings.fps = 30;
        auto session = pvf::EditorSession::createProject(dir, settings, error, false);
        if (!session)
            return QString();
        if (!session->document()->setGitEnabled(true, error))
            return QString();
        session->mutableProject().settings.name = headName;
        if (!session->commitProject(QStringLiteral("pvf: head"), error))
            return QString();
        session->flush(error);
        session.reset();

        QFile file(QDir(dir).filePath(QStringLiteral("project.json")));
        if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                *error = QStringLiteral("cannot read project.json");
            return QString();
        }
        QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        object.insert(QStringLiteral("name"), dirtyName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            if (error)
                *error = QStringLiteral("cannot write project.json");
            return QString();
        }
        file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
        return dir;
    };

    // Commit + history restore.
    {
        const QString dir =
            QDir(temp.path()).filePath(QStringLiteral("restore.pvproj"));
        pvf::model::ProjectSettings settings;
        settings.name = QStringLiteral("初始");
        settings.width = 640;
        settings.height = 360;
        settings.fps = 30;
        QString error;
        auto session =
            pvf::EditorSession::createProject(dir, settings, &error, false);
        if (!check(session != nullptr,
                   QStringLiteral("create failed: %1").arg(error)))
            return 1;
        check(session->document()->setGitEnabled(true, &error),
              QStringLiteral("enable Git failed: %1").arg(error));
        const QVector<pvf::store::GitCommit> initHistory =
            session->document()->store()->history(10, &error);
        check(!initHistory.isEmpty()
                  && initHistory.first().message == QStringLiteral("pvf: init"),
              QStringLiteral("initial commit missing"));
        const QString initHash =
            initHistory.isEmpty() ? QString() : initHistory.first().hash;

        session->mutableProject().settings.name = QStringLiteral("修改后");
        check(session->commitProject(QStringLiteral("pvf: change"), &error),
              QStringLiteral("change commit failed: %1").arg(error));
        session->flush(&error);
        check(session->document()->store()->history(10, &error).size() >= 2,
              QStringLiteral("second commit missing"));

        error.clear();
        const bool restored =
            session->document()->store()->restoreCommit(initHash, &error);
        check(restored, QStringLiteral("restoreCommit failed: %1").arg(error));
        session->document()->reloadAfterHistory(&error);
        check(session->project().settings.name == QStringLiteral("初始"),
              QStringLiteral("restore did not bring back the initial name"));

        // The history dock uses the same restore path: it must list commits
        // and restore through MainWindow, not only through the store.
        auto* window = new pvf::MainWindow(session.release());
        window->resize(1200, 800);
        window->show();
        QApplication::processEvents();
        auto* panel = window->historyPanelForTest();
        check(panel && panel->rowCountForTest() >= 2,
              QStringLiteral("history panel did not list commits"));
        QString uiRestoreError;
        const bool uiRestored =
            window->restoreSessionCommitForTest(initHash, &uiRestoreError);
        check(uiRestored,
              QStringLiteral("history panel restore failed: %1")
                  .arg(uiRestoreError));
        check(window->editorSessionForTest()
                  && window->editorSessionForTest()->project().settings.name
                         == QStringLiteral("初始"),
              QStringLiteral("history panel restore did not change the project"));
        check(panel && panel->rowCountForTest() >= 3,
              QStringLiteral("history panel did not show the restore commit"));
        delete window;
        QApplication::processEvents();
    }

    // Crash recovery branches (injectable handler).
    {
        QString error;
        const QString dir = prepareDirty(
            QStringLiteral("crash-restore"), QStringLiteral("恢复头"),
            QStringLiteral("恢复脏"), &error);
        if (!check(!dir.isEmpty(),
                   QStringLiteral("prepare restore failed: %1").arg(error)))
            return 1;
        auto session = pvf::openSessionProjectWithRecovery(
            dir, true,
            [](const QVector<pvf::store::DirtyText>&) {
                return pvf::SessionCrashRecoveryChoice::Restore;
            },
            nullptr, &error);
        check(session != nullptr,
              QStringLiteral("restore open failed: %1").arg(error));
        check(session
                  && session->project().settings.name
                         == QStringLiteral("恢复脏"),
              QStringLiteral("restore did not keep the dirty text"));
    }
    {
        QString error;
        const QString dir = prepareDirty(
            QStringLiteral("crash-discard"), QStringLiteral("丢弃头"),
            QStringLiteral("丢弃脏"), &error);
        if (!check(!dir.isEmpty(),
                   QStringLiteral("prepare discard failed: %1").arg(error)))
            return 1;
        auto session = pvf::openSessionProjectWithRecovery(
            dir, true,
            [](const QVector<pvf::store::DirtyText>&) {
                return pvf::SessionCrashRecoveryChoice::Discard;
            },
            nullptr, &error);
        check(session != nullptr,
              QStringLiteral("discard open failed: %1").arg(error));
        check(session
                  && session->project().settings.name
                         == QStringLiteral("丢弃头"),
              QStringLiteral("discard did not restore HEAD text"));
        check(session
                  && session->document()->store()->dirtyText().isEmpty(),
              QStringLiteral("discard left dirty text"));
    }
    {
        QString error;
        const QString dir = prepareDirty(
            QStringLiteral("crash-commit"), QStringLiteral("提交头"),
            QStringLiteral("提交脏"), &error);
        if (!check(!dir.isEmpty(),
                   QStringLiteral("prepare commit failed: %1").arg(error)))
            return 1;
        auto session = pvf::openSessionProjectWithRecovery(
            dir, true,
            [](const QVector<pvf::store::DirtyText>&) {
                return pvf::SessionCrashRecoveryChoice::Commit;
            },
            nullptr, &error);
        check(session != nullptr,
              QStringLiteral("commit open failed: %1").arg(error));
        check(session
                  && session->document()->store()->dirtyText().isEmpty(),
              QStringLiteral("commit left dirty text"));
        const QVector<pvf::store::GitCommit> history =
            session ? session->document()->store()->history(1, &error)
                    : QVector<pvf::store::GitCommit>();
        check(!history.isEmpty()
                  && history.first().message.contains(QStringLiteral("recover")),
              QStringLiteral("commit recovery did not create pvf: recover"));
    }
    {
        QString error;
        const QString dir = prepareDirty(
            QStringLiteral("crash-cancel"), QStringLiteral("取消头"),
            QStringLiteral("取消脏"), &error);
        if (!check(!dir.isEmpty(),
                   QStringLiteral("prepare cancel failed: %1").arg(error)))
            return 1;
        error.clear();
        auto session = pvf::openSessionProjectWithRecovery(
            dir, true,
            [](const QVector<pvf::store::DirtyText>&) {
                return pvf::SessionCrashRecoveryChoice::Cancel;
            },
            nullptr, &error);
        check(!session && error.isEmpty(),
              QStringLiteral("cancel did not abort cleanly"));
    }

    // History panel says "Git 未启用" without a repository.
    {
        const QString dir =
            QDir(temp.path()).filePath(QStringLiteral("no-history.pvproj"));
        pvf::model::ProjectSettings settings;
        settings.name = QStringLiteral("无历史");
        QString error;
        auto session =
            pvf::EditorSession::createProject(dir, settings, &error, false);
        if (!check(session != nullptr,
                   QStringLiteral("no-history create failed: %1").arg(error)))
            return 1;
        auto* window = new pvf::MainWindow(session.release());
        window->resize(1200, 800);
        window->show();
        QApplication::processEvents();
        auto* panel = window->historyPanelForTest();
        auto* status = panel ? panel->findChild<QLabel*>(
                                   QStringLiteral("historyStatusLabel"))
                             : nullptr;
        check(panel && status
                  && status->text().contains(QStringLiteral("Git 未启用")),
              QStringLiteral("history panel does not report Git disabled"));
        delete window;
        QApplication::processEvents();
    }

    qInfo() << "git-test failures" << failures;
    return failures == 0 ? 0 : 1;
}


} // namespace

namespace pvf::tests {

void registerSessionTests(Registry& registry)
{
    registry.add(QStringLiteral("--self-test"),
                 [](QApplication&) { return selfTest(); });
    registry.add(QStringLiteral("--session-test"),
                 [](QApplication&) { return sessionTest(); });
    registry.add(QStringLiteral("--welcome-test"),
                 [](QApplication& app) { return welcomeTest(app); });
    registry.add(QStringLiteral("--settings-test"),
                 [](QApplication& app) { return settingsTest(app); });
    registry.add(QStringLiteral("--git-test"),
                 [](QApplication& app) { return gitSessionTest(app); });
}

} // namespace pvf::tests
