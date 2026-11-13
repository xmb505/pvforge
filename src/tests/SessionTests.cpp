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
        if (!live || !dialog || !dialog->appSettingsMode())
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

    {
        pvf::SettingsDialog dialog;
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvf_settings.png"));

        auto* autosave = dialog.findChild<QCheckBox*>(QStringLiteral("autosaveEnabled"));
        auto* autosaveMinutes =
            dialog.findChild<QSpinBox*>(QStringLiteral("autosaveMinutes"));
        if (!autosave || !autosave->isChecked() || !autosaveMinutes
            || autosaveMinutes->value() != 5)
        {
            qCritical() << "settings-test: autosave default is not on / 5 minutes"
                        << (autosave ? autosave->isChecked() : false)
                        << (autosaveMinutes ? autosaveMinutes->value() : -1);
            return 1;
        }

        auto* categories =
            dialog.findChild<QListWidget*>(QStringLiteral("settingsCategories"));
        if (categories)
            categories->setCurrentRow(5);
        QApplication::processEvents();
        auto* videoValue =
            dialog.findChild<QLabel*>(QStringLiteral("decoderVideoValue"));
        auto* modeValue =
            dialog.findChild<QLabel*>(QStringLiteral("decoderModeValue"));
        if (!videoValue || !videoValue->text().contains(QStringLiteral("软件解码"))
            || videoValue->width() < 120 || !modeValue
            || !modeValue->text().contains(QStringLiteral("软件解码")))
        {
            qCritical() << "settings-test: decoder value not visible"
                        << (videoValue ? videoValue->text() : QString())
                        << (videoValue ? videoValue->width() : -1);
            return 1;
        }
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvf_settings_decode.png"));

        dialog.showAboutPage();
        QApplication::processEvents();
        auto* about = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aboutText"));
        const QString text = about ? about->toPlainText() : QString();
        if (!about || !text.contains(QStringLiteral("处理器"))
            || !text.contains(QStringLiteral("FFmpeg"))
            || !text.contains(QStringLiteral("Qt "))
            || !text.contains(QStringLiteral("PVForge")))
        {
            qCritical() << "settings-test: about page incomplete";
            return 1;
        }
        auto* copy = dialog.findChild<QPushButton*>(QStringLiteral("aboutCopy"));
        if (!copy)
        {
            qCritical() << "settings-test: about copy button missing";
            return 1;
        }
        if (!categories || !categories->currentItem()
            || categories->currentItem()->text() != QStringLiteral("关于"))
        {
            qCritical() << "settings-test: about category not selected";
            return 1;
        }
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvf_settings_about.png"));
        dialog.close();
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

        // The settings dialog builds and can be shown.
        pvf::SettingsDialog dialog(window);
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_settings.png"));

        qInfo() << "settings-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int checkpointTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "checkpoint-test:" << message;
        ++failures;
    };
    const auto expect = [&fail](bool condition, const QString& message) {
        if (!condition)
            fail(message);
        return condition;
    };
    const auto sha256 = [](const QByteArray& data) {
        return QString::fromLatin1(
            QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    };
    const auto readAll = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const auto isObjectId = [](const QString& value) {
        if (value.size() != 40 && value.size() != 64)
            return false;
        for (const QChar character : value)
        {
            const ushort code = character.unicode();
            if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')))
                return false;
        }
        return true;
    };
    const auto ownerOnly = [](const QString& path, bool directory) {
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        const QFileDevice::Permissions group =
            QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup;
        const QFileDevice::Permissions other =
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        bool ok = permissions.testFlag(QFileDevice::ReadOwner)
                  && permissions.testFlag(QFileDevice::WriteOwner)
                  && (permissions & (group | other)) == QFileDevice::Permissions();
        if (directory)
            ok = ok && permissions.testFlag(QFileDevice::ExeOwner);
        return ok;
    };
    const auto runGit = [](const QString& directory, const QStringList& arguments,
                           QByteArray* output, int* status) {
        QProcess process;
        process.setWorkingDirectory(directory);
        process.start(QStringLiteral("git"), arguments);
        if (!process.waitForStarted(5000))
            return false;
        process.closeWriteChannel();
        if (!process.waitForFinished(20000))
        {
            process.kill();
            process.waitForFinished(2000);
            return false;
        }
        if (output)
            *output = process.readAllStandardOutput();
        if (status)
            *status = process.exitCode();
        return process.exitStatus() == QProcess::NormalExit
               && process.exitCode() == 0;
    };

    QTemporaryDir projectTemp(QDir::tempPath()
                              + QStringLiteral("/pvforge-checkpoint-project-XXXXXX"));
    QTemporaryDir cacheTemp(QDir::tempPath()
                            + QStringLiteral("/pvforge-checkpoint-cache-XXXXXX"));
    QTemporaryDir corruptCache(QDir::tempPath()
                               + QStringLiteral("/pvforge-checkpoint-corrupt-XXXXXX"));
    QTemporaryDir fallbackCache(QDir::tempPath()
                                + QStringLiteral("/pvforge-checkpoint-fallback-XXXXXX"));
    if (!projectTemp.isValid() || !cacheTemp.isValid() || !corruptCache.isValid()
        || !fallbackCache.isValid())
    {
        fail(QStringLiteral("could not create temporary directories"));
        return 1;
    }

    const QString projectDirectory = projectTemp.path();
    const QString projectPath = projectDirectory + QStringLiteral("/checkpoint.pvproj");
    const QString imagePath = projectDirectory + QStringLiteral("/still.png");
    const QString missingPath = projectDirectory + QStringLiteral("/missing.png");

    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(imagePath))
    {
        fail(QStringLiteral("could not write test image"));
        return 1;
    }

    // CheckpointManager stores the project JSON payload and extracts the
    // fields it lists (scenes, entity source references). The fixture mirrors
    // those fields so the test stays independent of the editor model.
    QString error;
    QByteArray json1 = QByteArrayLiteral("{\n"
                                         "  \"version\": 4,\n"
                                         "  \"name\": \"checkpoint fixture\",\n"
                                         "  \"width\": 640,\n"
                                         "  \"height\": 360,\n"
                                         "  \"fps\": 30,\n"
                                         "  \"scenes\": [{\"id\": \"intro\", \"name\": \"Intro\"}],\n"
                                         "  \"entities\": [\n"
                                         "    {\"id\": 1, \"type\": \"Image\", \"name\": \"still\", "
                                         "\"sourcePath\": \"");
    json1 += imagePath.toUtf8();
    json1 += QByteArrayLiteral("\"}\n  ]\n}\n");
    {
        QFile file(projectPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(json1) != json1.size())
        {
            fail(QStringLiteral("could not write the project fixture"));
            return 1;
        }
    }

    QJsonObject root1 = QJsonDocument::fromJson(json1).object();
    if (root1.isEmpty())
    {
        fail(QStringLiteral("saved project JSON is invalid"));
        return 1;
    }
    QJsonObject root2 = root1;
    root2[QStringLiteral("name")] = QStringLiteral("second");
    const QByteArray json2 = QJsonDocument(root2).toJson(QJsonDocument::Indented);
    QJsonObject root3 = root1;
    root3[QStringLiteral("name")] = QStringLiteral("third");
    QJsonArray entities = root3.value(QStringLiteral("entities")).toArray();
    if (!entities.isEmpty())
    {
        QJsonObject first = entities.at(0).toObject();
        first[QStringLiteral("sourcePath")] = missingPath;
        entities.replace(0, first);
        root3[QStringLiteral("entities")] = entities;
    }
    const QByteArray json3 = QJsonDocument(root3).toJson(QJsonDocument::Indented);

    // A real repository in the project folder proves that checkpoints never
    // touch or pollute the user's own git state.
    const bool systemGit =
        !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
    QByteArray projectRepoStatus;
    QByteArray projectRepoHead;
    bool projectRepoReady = false;
    if (systemGit
        && runGit(projectDirectory,
                  {QStringLiteral("init"), QStringLiteral("-q")}, nullptr, nullptr)
        && runGit(projectDirectory,
                  {QStringLiteral("add"), QStringLiteral("-A")}, nullptr, nullptr)
        && runGit(projectDirectory,
                  {QStringLiteral("-c"), QStringLiteral("user.name=Checkpoint Test"),
                   QStringLiteral("-c"),
                   QStringLiteral("user.email=test@example.invalid"),
                   QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false"),
                   QStringLiteral("commit"), QStringLiteral("-q"),
                   QStringLiteral("-m"), QStringLiteral("baseline")},
                  nullptr, nullptr))
    {
        projectRepoReady = true;
        runGit(projectDirectory, {QStringLiteral("status"), QStringLiteral("--porcelain")},
               &projectRepoStatus, nullptr);
        runGit(projectDirectory, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
               &projectRepoHead, nullptr);
    }
    const QByteArray projectFileBefore = readAll(projectPath);

    pvf::CheckpointManager manager(cacheTemp.path());
    expect(manager.isAvailable(), QStringLiteral("checkpoint manager is not available"));
    const bool gitBackend = manager.gitAvailable();
    qInfo() << "checkpoint-test:" << (gitBackend ? "git" : "snapshot") << "backend";
    if (!systemGit)
        expect(!gitBackend, QStringLiteral("git should be unavailable without git"));

    QString id1;
    if (!manager.createCheckpoint(projectPath, json1, QStringLiteral("first"),
                                  &error, &id1))
    {
        fail(QStringLiteral("create first checkpoint failed: %1").arg(error));
        return 1;
    }
    expect(!id1.isEmpty(), QStringLiteral("first checkpoint id is empty"));
    expect(ownerOnly(cacheTemp.path(), true),
           QStringLiteral("cache root permissions are not 0700"));

    QVector<pvf::CheckpointInfo> listed = manager.listCheckpoints(projectPath);
    bool usedGitBackend = false;
    QString hookMarker;
    if (!expect(listed.size() == 1, QStringLiteral("expected 1 checkpoint after first")))
        qCritical() << "checkpoint-test: listed" << listed.size();
    else
    {
        usedGitBackend = listed.first().gitBacked;
        expect(listed.first().id == id1, QStringLiteral("listed id differs"));
        expect(listed.first().summary == QStringLiteral("first"),
               QStringLiteral("summary was not stored"));
        expect(listed.first().fileSha256 == sha256(json1),
               QStringLiteral("sidecar SHA-256 differs"));
        expect(listed.first().fileSize == json1.size(),
               QStringLiteral("sidecar size differs"));
        expect(!listed.first().projectUuid.isEmpty(),
               QStringLiteral("project UUID is empty"));
        expect(listed.first().scenes.contains(QStringLiteral("Intro")),
               QStringLiteral("scene list was not stored"));
        expect(listed.first().assets.size() == 1,
               QStringLiteral("asset reference list size is %1")
                   .arg(listed.first().assets.size()));
        expect(listed.first().missingAssets.isEmpty(),
               QStringLiteral("existing asset was flagged missing"));
        if (usedGitBackend)
        {
            const QString repo = cacheTemp.path() + QLatin1Char('/')
                                 + pvf::CheckpointManager::projectPathHash(projectPath)
                                 + QStringLiteral(".git");
            expect(ownerOnly(repo, true),
                   QStringLiteral("checkpoint repo permissions are not 0700"));
        }
        else
        {
            const QString directory = cacheTemp.path() + QLatin1Char('/')
                                      + pvf::CheckpointManager::projectPathHash(projectPath)
                                      + QStringLiteral(".snapshots");
            expect(ownerOnly(directory, true),
                   QStringLiteral("snapshot directory permissions are not 0700"));
            expect(ownerOnly(directory + QLatin1Char('/') + id1
                                 + QStringLiteral(".json"),
                             false),
                   QStringLiteral("snapshot sidecar permissions are not 0600"));
        }

        // A hostile hook placed in the bare repository must never run.
        if (usedGitBackend)
        {
            const QString repo = cacheTemp.path() + QLatin1Char('/')
                                 + pvf::CheckpointManager::projectPathHash(projectPath)
                                 + QStringLiteral(".git");
            const QString hooks = repo + QStringLiteral("/hooks");
            QDir().mkpath(hooks);
            hookMarker = cacheTemp.path() + QStringLiteral("/hook-ran");
            QFile hook(hooks + QStringLiteral("/pre-commit"));
            if (hook.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                hook.write(
                    QStringLiteral("#!/bin/sh\ntouch '%1'\n").arg(hookMarker).toUtf8());
                hook.close();
                QFile::setPermissions(hooks + QStringLiteral("/pre-commit"),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner);
            }
        }
    }

    QString id2;
    if (!manager.createCheckpoint(projectPath, json2, QStringLiteral("second"),
                                  &error, &id2))
    {
        fail(QStringLiteral("create second checkpoint failed: %1").arg(error));
        return 1;
    }
    if (usedGitBackend)
        expect(!QFileInfo::exists(hookMarker),
               QStringLiteral("git hook executed during checkpoint"));
    QString id3;
    if (!manager.createCheckpoint(projectPath, json3, QStringLiteral("third"),
                                  &error, &id3))
    {
        fail(QStringLiteral("create third checkpoint failed: %1").arg(error));
        return 1;
    }
    expect(id1 != id2 && id2 != id3 && id1 != id3,
           QStringLiteral("checkpoint ids are not unique"));

    listed = manager.listCheckpoints(projectPath);
    expect(listed.size() == 3,
           QStringLiteral("expected 3 checkpoints, got %1").arg(listed.size()));
    if (!listed.isEmpty())
    {
        const pvf::CheckpointInfo& newest = listed.first();
        expect(newest.id == id3, QStringLiteral("newest checkpoint is not the third"));
        expect(newest.missingAssets.contains(missingPath),
               QStringLiteral("missing asset was not recorded"));
    }

    QByteArray restored;
    if (!expect(manager.restoreCheckpoint(projectPath, id2, &restored, &error),
                QStringLiteral("restore middle checkpoint failed: %1").arg(error)))
        qCritical() << "checkpoint-test: restore error" << error;
    else
        expect(restored == json2,
               QStringLiteral("middle checkpoint content differs"));

    restored.clear();
    if (expect(manager.restoreCheckpoint(projectPath, id1, &restored, &error),
               QStringLiteral("restore first checkpoint failed: %1").arg(error)))
        expect(restored == json1, QStringLiteral("first checkpoint content differs"));

    // Keep the newest two; the oldest disappears from the list.
    manager.prune(projectPath, 2);
    listed = manager.listCheckpoints(projectPath);
    expect(listed.size() == 2,
           QStringLiteral("after prune expected 2 checkpoints, got %1")
               .arg(listed.size()));
    bool hasSecond = false;
    bool hasThird = false;
    for (const pvf::CheckpointInfo& info : listed)
    {
        hasSecond = hasSecond || info.id == id2;
        hasThird = hasThird || info.id == id3;
    }
    expect(hasSecond && hasThird,
           QStringLiteral("prune removed the wrong checkpoints"));

    // Corrupting or deleting the repository must not crash listing, and a
    // damaged payload must be refused by restore.
    pvf::CheckpointManager corruptManager(corruptCache.path());
    QString corruptId;
    if (!corruptManager.createCheckpoint(projectPath, json1,
                                         QStringLiteral("corrupt me"), &error,
                                         &corruptId))
    {
        fail(QStringLiteral("corrupt-backend checkpoint failed: %1").arg(error));
    }
    else
    {
        QVector<pvf::CheckpointInfo> corruptList =
            corruptManager.listCheckpoints(projectPath);
        const bool usedGit = !corruptList.isEmpty() && corruptList.first().gitBacked;
        if (usedGit)
        {
            const QString repo = corruptCache.path() + QLatin1Char('/')
                                 + pvf::CheckpointManager::projectPathHash(projectPath)
                                 + QStringLiteral(".git");
            QByteArray blob;
            if (runGit(corruptCache.path(),
                       {QStringLiteral("--git-dir=") + repo,
                        QStringLiteral("rev-parse"), corruptId
                            + QStringLiteral(":project.pvproj")},
                       &blob, nullptr))
            {
                const QString hash = QString::fromLatin1(blob).trimmed();
                if (isObjectId(hash))
                {
                    const QString object =
                        repo + QStringLiteral("/objects/") + hash.left(2)
                        + QLatin1Char('/') + hash.mid(2);
                    expect(QFile::remove(object),
                           QStringLiteral("could not corrupt git object"));
                    QByteArray broken;
                    expect(!corruptManager.restoreCheckpoint(projectPath, corruptId,
                                                             &broken, &error),
                           QStringLiteral("restore accepted a deleted git object"));
                }
            }
            QDir(repo).removeRecursively();
        }
        else
        {
            const QString directory = corruptCache.path() + QLatin1Char('/')
                                      + pvf::CheckpointManager::projectPathHash(projectPath)
                                      + QStringLiteral(".snapshots");
            QFile payload(directory + QLatin1Char('/') + corruptId
                          + QStringLiteral(".pvproj"));
            if (payload.open(QIODevice::ReadWrite))
            {
                payload.seek(0);
                payload.write("X");
                payload.close();
            }
            QByteArray broken;
            expect(!corruptManager.restoreCheckpoint(projectPath, corruptId, &broken,
                                                     &error),
                   QStringLiteral("restore accepted a tampered snapshot"));
            QDir(directory).removeRecursively();
        }

        corruptList = corruptManager.listCheckpoints(projectPath);
        expect(corruptList.isEmpty(),
               QStringLiteral("listing a deleted store returned %1 entries")
                   .arg(corruptList.size()));
        QString newId;
        expect(corruptManager.createCheckpoint(projectPath, json2,
                                               QStringLiteral("after damage"),
                                               &error, &newId),
               QStringLiteral("checkpoint after damage failed: %1").arg(error));
        expect(corruptManager.listCheckpoints(projectPath).size() == 1,
               QStringLiteral("store did not recover after damage"));
    }

    // Missing git must transparently use the snapshot backend.
    pvf::CheckpointManager fallbackManager(fallbackCache.path(),
                                           QStringLiteral("/nonexistent/pvforge-git"));
    expect(!fallbackManager.gitAvailable(),
           QStringLiteral("fake git path reported git available"));
    expect(fallbackManager.isAvailable(),
           QStringLiteral("snapshot backend reported unavailable"));
    QString snapshotId;
    if (!expect(fallbackManager.createCheckpoint(projectPath, json1,
                                                 QStringLiteral("snapshot"),
                                                 &error, &snapshotId),
                QStringLiteral("snapshot fallback create failed: %1").arg(error)))
        qCritical() << "checkpoint-test: snapshot error" << error;
    else
    {
        const QVector<pvf::CheckpointInfo> snapshots =
            fallbackManager.listCheckpoints(projectPath);
        expect(snapshots.size() == 1,
               QStringLiteral("snapshot fallback list size %1").arg(snapshots.size()));
        if (!snapshots.isEmpty())
            expect(!snapshots.first().gitBacked,
                   QStringLiteral("fallback checkpoint claims git backend"));
        QByteArray snapshotJson;
        if (expect(fallbackManager.restoreCheckpoint(projectPath, snapshotId,
                                                     &snapshotJson, &error),
                   QStringLiteral("snapshot restore failed: %1").arg(error)))
            expect(snapshotJson == json1,
                   QStringLiteral("snapshot fallback content differs"));
        expect(QDir(fallbackCache.path() + QLatin1Char('/')
                    + pvf::CheckpointManager::projectPathHash(projectPath)
                    + QStringLiteral(".snapshots"))
                   .exists(),
               QStringLiteral("snapshot directory does not exist"));
    }

    if (gitBackend)
    {
        pvf::CheckpointManager mixedManager(fallbackCache.path());
        QString mixedId;
        expect(mixedManager.createCheckpoint(projectPath, json2,
                                             QStringLiteral("git and snapshot"),
                                             &error, &mixedId),
               QStringLiteral("mixed backend create failed: %1").arg(error));
        expect(mixedManager.listCheckpoints(projectPath).size() == 2,
               QStringLiteral("list did not merge git and snapshot checkpoints"));
    }

    // The user's own repository and project file must be byte-identical.
    if (projectRepoReady)
    {
        QByteArray statusAfter;
        QByteArray headAfter;
        runGit(projectDirectory, {QStringLiteral("status"), QStringLiteral("--porcelain")},
               &statusAfter, nullptr);
        runGit(projectDirectory, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
               &headAfter, nullptr);
        expect(statusAfter == projectRepoStatus,
               QStringLiteral("user repository status changed"));
        expect(headAfter == projectRepoHead,
               QStringLiteral("user repository HEAD changed"));
        expect(readAll(projectPath) == projectFileBefore,
               QStringLiteral("project file was modified by checkpointing"));
    }

    qInfo() << "checkpoint-test failures" << failures;
    return failures == 0 ? 0 : 1;
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
        return failures == 0 ? 0 : 1;
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
    registry.add(QStringLiteral("--checkpoint-test"),
                 [](QApplication&) { return checkpointTest(); });
    registry.add(QStringLiteral("--git-test"),
                 [](QApplication& app) { return gitSessionTest(app); });
}

} // namespace pvf::tests
