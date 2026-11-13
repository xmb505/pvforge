#include "TestApp.h"
#include "TestRegistry.h"

namespace {

int exportDialogTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "export-dialog-test:" << message;
            ++failures;
        }
        return condition;
    };

    pvf::model::Project project;
    project.settings.name = QStringLiteral("dialog");
    project.settings.width = 640;
    project.settings.height = 360;
    project.settings.fps = 30;
    auto addCamera = [&project](const QString& name, int start, int end) {
        pvf::model::Entity camera;
        camera.uuid = pvf::model::newUuid();
        camera.name = name;
        camera.start = start;
        camera.end = end;
        camera.content.kind = pvf::ContentKind::Camera;
        project.entities.insert(camera.uuid, camera);
        return camera.uuid;
    };
    const pvf::Uuid cameraA = addCamera(QStringLiteral("A 机位"), 0, 60);
    const pvf::Uuid cameraB = addCamera(QStringLiteral("B 机位"), 10, 70);

    pvf::ExportDialog dialog(project, cameraB);
    dialog.show();
    QApplication::processEvents();

    auto* cameras = dialog.cameraComboBoxForTest();
    check(cameras && cameras->count() == 2,
          QStringLiteral("camera combo count is not 2"));
    check(dialog.cameraUuid() == cameraB,
          QStringLiteral("preferred camera was not selected"));
    check(dialog.startFrame() == 10 && dialog.endFrame() == 70,
          QStringLiteral("camera range getters are wrong"));
    auto* range = dialog.findChild<QLabel*>(QStringLiteral("exportRangeLabel"));
    check(range && range->text().contains(QStringLiteral("10 .. 70")),
          QStringLiteral("range label does not show the camera range"));

    auto* resolutions = dialog.resolutionComboBoxForTest();
    check(resolutions && resolutions->count() == 3,
          QStringLiteral("resolution combo does not have 3 choices"));
    check(resolutions && resolutions->itemData(0).toInt() == 1
              && resolutions->itemData(1).toInt() == 2
              && resolutions->itemData(2).toInt() == 4,
          QStringLiteral("resolution scale data is wrong"));
    check(resolutions && resolutions->itemText(0).contains(QStringLiteral("640 × 360"))
              && resolutions->itemText(1).contains(QStringLiteral("1280 × 720"))
              && resolutions->itemText(2).contains(QStringLiteral("2560 × 1440")),
          QStringLiteral("resolution combo does not show actual sizes"));

    auto* fps = dialog.fpsComboBoxForTest();
    check(fps && fps->count() == 3 && fps->itemData(0).toInt() == 1
              && fps->itemData(1).toInt() == 2 && fps->itemData(2).toInt() == 4,
          QStringLiteral("fps scale choices are wrong"));
    check(fps && fps->itemText(0).contains(QStringLiteral("30"))
              && fps->itemText(1).contains(QStringLiteral("60"))
              && fps->itemText(2).contains(QStringLiteral("120")),
          QStringLiteral("fps combo does not show actual frame rates"));

    auto* crf = dialog.crfSpinBoxForTest();
    check(crf && crf->minimum() == 0 && crf->maximum() == 51,
          QStringLiteral("CRF range is not 0..51"));
    check(dialog.crf() == 18, QStringLiteral("CRF default is not 18"));

    auto* encoder = dialog.encoderComboBoxForTest();
    check(encoder && encoder->findData(QStringLiteral("libx264")) >= 0
              && encoder->findData(QStringLiteral("h264_vaapi")) >= 0,
          QStringLiteral("encoder choices are missing"));
    check(dialog.encoder() == QStringLiteral("libx264"),
          QStringLiteral("default encoder is not libx264"));

    auto* preset = dialog.presetComboBoxForTest();
    check(preset && preset->findData(QStringLiteral("medium")) >= 0
              && preset->findData(QStringLiteral("ultrafast")) >= 0
              && preset->findData(QStringLiteral("veryslow")) >= 0,
          QStringLiteral("preset choices are missing"));
    check(dialog.preset() == QStringLiteral("medium"),
          QStringLiteral("default preset is not medium"));

    auto* path = dialog.findChild<QLineEdit*>(QStringLiteral("exportOutputPath"));
    auto* start = dialog.startButtonForTest();
    check(path && start && start->isEnabled(),
          QStringLiteral("start button is not enabled for a valid camera/path"));
    if (path)
    {
        path->setText(QString());
        check(!start->isEnabled(),
              QStringLiteral("start button stays enabled with an empty path"));
        path->setText(QStringLiteral("/tmp/pvf-dialog-output"));
        check(start->isEnabled(),
              QStringLiteral("start button did not re-enable after a path"));
        check(dialog.outputPath().endsWith(QStringLiteral(".mp4")),
              QStringLiteral("output path did not gain the .mp4 suffix"));
    }

    // Overwrite confirmation: No keeps the dialog open, Yes accepts.
    QTemporaryDir overwriteDir;
    const QString existing =
        QDir(overwriteDir.path()).filePath(QStringLiteral("existing.mp4"));
    {
        QFile file(existing);
        check(file.open(QIODevice::WriteOnly), QStringLiteral("cannot create existing file"));
        file.write("old");
    }
    dialog.setConfirmLargeExportForTest(true);
    dialog.setOverwriteDecisionForTest(false);
    if (path)
        path->setText(existing);
    dialog.accept();
    check(dialog.result() != QDialog::Accepted && !dialog.overwriteExisting(),
          QStringLiteral("declining overwrite still accepted"));
    dialog.setOverwriteDecisionForTest(true);
    dialog.accept();
    check(dialog.result() == QDialog::Accepted && dialog.overwriteExisting(),
          QStringLiteral("confirming overwrite did not accept"));

    // 4x larger than 4096 on either axis asks for confirmation.
    pvf::model::Project large;
    large.settings.width = 1920;
    large.settings.height = 1080;
    large.settings.fps = 30;
    pvf::model::Entity largeCamera;
    largeCamera.uuid = pvf::model::newUuid();
    largeCamera.name = QStringLiteral("Camera");
    largeCamera.start = 0;
    largeCamera.end = 30;
    largeCamera.content.kind = pvf::ContentKind::Camera;
    large.entities.insert(largeCamera.uuid, largeCamera);
    pvf::ExportDialog largeDialog(large, largeCamera.uuid);
    auto* largePath =
        largeDialog.findChild<QLineEdit*>(QStringLiteral("exportOutputPath"));
    if (largePath)
        largePath->setText(QDir(overwriteDir.path()).filePath(QStringLiteral("large.mp4")));
    largeDialog.resolutionComboBoxForTest()->setCurrentIndex(
        largeDialog.resolutionComboBoxForTest()->findData(4));
    largeDialog.setOverwriteDecisionForTest(true);
    largeDialog.setLargeExportDecisionForTest(false);
    largeDialog.accept();
    check(largeDialog.result() != QDialog::Accepted,
          QStringLiteral("4x >4096 accepted without confirmation"));
    largeDialog.setLargeExportDecisionForTest(true);
    largeDialog.accept();
    check(largeDialog.result() == QDialog::Accepted,
          QStringLiteral("4x >4096 did not accept after confirmation"));

    // No camera: no choices and the start button is disabled.
    pvf::model::Project empty;
    empty.settings.width = 640;
    empty.settings.height = 360;
    empty.settings.fps = 30;
    pvf::ExportDialog emptyDialog(empty);
    auto* emptyCameras = emptyDialog.cameraComboBoxForTest();
    check(emptyCameras && emptyCameras->count() == 1
              && emptyCameras->currentData().toString().isEmpty(),
          QStringLiteral("empty project camera combo is wrong"));
    check(emptyDialog.cameraUuid().isEmpty(),
          QStringLiteral("empty project returned a camera"));
    check(!emptyDialog.startButtonForTest()->isEnabled(),
          QStringLiteral("start button is enabled without a camera"));

    check(pvf::friendlyExportError(QStringLiteral("output_exists"))
              .contains(QStringLiteral("已存在")),
          QStringLiteral("output_exists message is not friendly"));
    check(pvf::friendlyExportError(QStringLiteral("encoder_unavailable"))
              .contains(QStringLiteral("VAAPI")),
          QStringLiteral("encoder_unavailable message is not friendly"));
    check(pvf::friendlyExportError(QStringLiteral("invalid_crf"))
              .contains(QStringLiteral("0–51")),
          QStringLiteral("invalid_crf message is not friendly"));

    dialog.hide();
    largeDialog.hide();
    emptyDialog.hide();

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    pvf::ExportDialog shot(project, cameraA);
    if (auto* shotPath =
            shot.findChild<QLineEdit*>(QStringLiteral("exportOutputPath")))
        shotPath->setText(QStringLiteral("/tmp/pvforge-export.mp4"));
    shot.resolutionComboBoxForTest()->setCurrentIndex(0);
    shot.fpsComboBoxForTest()->setCurrentIndex(0);
    shot.crfSpinBoxForTest()->setValue(18);
    shot.show();
    QApplication::processEvents();
    shot.grab().toImage().save(
        QStringLiteral("/tmp/opencode/pvforge-p7a-export-dialog.png"));
    shot.hide();

    qInfo() << "export-dialog-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int exportTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "export-test:" << message;
            ++failures;
        }
        return condition;
    };
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffprobe.isEmpty() || ffmpeg.isEmpty())
    {
        qInfo() << "export-test: ffmpeg/ffprobe missing, skipped";
        return 0;
    }

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString configPath =
        QDir(temp.path()).filePath(QStringLiteral("mcp.json"));
    {
        QFile config(configPath);
        if (!config.open(QIODevice::WriteOnly))
            return 1;
        config.write("{\"port\":0}");
    }
    pvf::EditorSession::setMcpConfigPathForTest(configPath);
    pvf::EditorSession::setMcpPortOverrideForTest(0);

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p7a-export.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P7a Export");
    settings.width = 320;
    settings.height = 180;
    settings.fps = 24;
    QString error;
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error, false);
    if (!session)
    {
        qCritical() << "export-test: create failed" << error;
        return 1;
    }

    const QString wavPath =
        QDir(temp.path()).filePath(QStringLiteral("tone.wav"));
    {
        QProcess generator;
        generator.start(ffmpeg,
                        {QStringLiteral("-y"), QStringLiteral("-v"),
                         QStringLiteral("error"), QStringLiteral("-f"),
                         QStringLiteral("lavfi"), QStringLiteral("-i"),
                         QStringLiteral("sine=frequency=440:duration=1"),
                         QStringLiteral("-ac"), QStringLiteral("2"),
                         QStringLiteral("-ar"), QStringLiteral("48000"),
                         wavPath});
        if (!generator.waitForFinished(30000) || generator.exitCode() != 0)
        {
            qCritical() << "export-test: cannot generate wav"
                        << generator.readAllStandardError();
            return 1;
        }
    }
    const auto imported = session->assets().importFile(wavPath, &error);
    if (!imported)
    {
        qCritical() << "export-test: audio import failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity camera;
    camera.uuid = pvf::model::newUuid();
    camera.name = QStringLiteral("Camera");
    camera.start = 0;
    camera.end = 24;
    camera.content.kind = pvf::ContentKind::Camera;
    project.entities.insert(camera.uuid, camera);

    pvf::model::Entity vector;
    vector.uuid = pvf::model::newUuid();
    vector.name = QStringLiteral("红色方块");
    vector.start = 0;
    vector.end = 24;
    vector.content.kind = pvf::ContentKind::Vector;
    vector.content.vector.width = 120;
    vector.content.vector.height = 120;
    vector.content.vector.fillColor = QStringLiteral("#e62828");
    project.entities.insert(vector.uuid, vector);

    pvf::model::Entity audio;
    audio.uuid = pvf::model::newUuid();
    audio.name = QStringLiteral("Tone");
    audio.start = 0;
    audio.end = 24;
    audio.content.kind = pvf::ContentKind::Asset;
    audio.content.asset.type = pvf::AssetType::Audio;
    audio.content.asset.hash = imported->hash;
    project.entities.insert(audio.uuid, audio);

    const QVector<pvf::Uuid> setup = {camera.uuid, vector.uuid, audio.uuid};
    if (!session->document()->commitEntities(setup, QStringLiteral("p7a setup"),
                                             &error))
    {
        qCritical() << "export-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->resize(1280, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    if (!live)
    {
        qCritical() << "export-test: no live session";
        return 1;
    }
    window->setSuppressExportCompletionDialogForTest(true);

    const QString firstPath =
        QDir(temp.path()).filePath(QStringLiteral("first.mp4"));
    pvf::export2::Options captured;
    bool capturedValid = false;
    window->setExportOptionsProbeForTest([&](const pvf::export2::Options& options) {
        captured = options;
        capturedValid = true;
    });

    auto runWindowExport = [&](const QString& path, bool audioEnabled,
                               int fpsScale, int resScale, int crf,
                               const QString& encoderName,
                               const QString& presetName, bool overwrite,
                               bool* okOut, QString* errorOut,
                               bool* lockObserved = nullptr) {
        bool finished = false;
        *okOut = false;
        errorOut->clear();
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        window->setExportFinishedHandlerForTest(
            [&](bool success, const QString& message) {
                *okOut = success;
                *errorOut = message;
                finished = true;
                loop.quit();
            });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(180000);
        window->startSessionExportForTest(camera.uuid, 0, 24, path,
                                          audioEnabled, 24 * fpsScale, resScale,
                                          crf, encoderName, presetName,
                                          overwrite);
        if (window->exportRunningForTest())
        {
            if (lockObserved)
                *lockObserved = !window->humanEditingEnabledForTest();
            loop.exec();
        }
        return finished;
    };

    bool ok = false;
    QString exportError;
    bool lockedDuringExport = false;
    check(runWindowExport(firstPath, true, 2, 1, 23, QStringLiteral("libx264"),
                          QStringLiteral("fast"), false, &ok, &exportError,
                          &lockedDuringExport),
          QStringLiteral("window export did not finish"));
    check(ok, QStringLiteral("window export failed: %1").arg(exportError));
    check(lockedDuringExport,
          QStringLiteral("human editing was not locked during export"));
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("human editing stayed locked after export"));
    auto* exportAction = window->findChild<QAction*>(QStringLiteral("exportAction"));
    check(exportAction
              && exportAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+E")),
          QStringLiteral("export action/Ctrl+E is missing"));
    check(capturedValid, QStringLiteral("export options probe was not called"));
    check(captured.crf == 23 && captured.encoder == QStringLiteral("libx264")
              && captured.preset == QStringLiteral("fast")
              && captured.outputFps == 48 && captured.resolutionScale == 1
              && captured.includeAudio && !captured.overwrite
              && captured.cameraUuid == camera.uuid,
          QStringLiteral("options were not passed through to export2"));
    check(QFileInfo::exists(firstPath), QStringLiteral("first export file missing"));

    auto probe = [&](const QString& path, QJsonObject* object) {
        QProcess process;
        process.start(ffprobe,
                      {QStringLiteral("-v"), QStringLiteral("error"),
                       QStringLiteral("-print_format"), QStringLiteral("json"),
                       QStringLiteral("-show_streams"),
                       QStringLiteral("-show_format"), path});
        if (!process.waitForFinished(30000) || process.exitCode() != 0)
        {
            qCritical() << "export-test: ffprobe failed"
                        << process.readAllStandardError();
            return false;
        }
        *object = QJsonDocument::fromJson(process.readAllStandardOutput())
                      .object();
        return true;
    };
    auto rational = [](const QString& value) {
        const QStringList parts = value.split(QLatin1Char('/'));
        if (parts.size() == 2 && parts[1].toDouble() != 0.0)
            return parts[0].toDouble() / parts[1].toDouble();
        return value.toDouble();
    };
    auto hasStream = [](const QJsonObject& object, const QString& type) {
        for (const QJsonValue& value :
             object.value(QStringLiteral("streams")).toArray())
        {
            if (value.toObject().value(QStringLiteral("codec_type")).toString()
                == type)
                return true;
        }
        return false;
    };

    QJsonObject firstProbe;
    if (check(probe(firstPath, &firstProbe), QStringLiteral("cannot probe first.mp4")))
    {
        QJsonObject videoStream;
        for (const QJsonValue& value :
             firstProbe.value(QStringLiteral("streams")).toArray())
        {
            if (value.toObject().value(QStringLiteral("codec_type")).toString()
                == QStringLiteral("video"))
                videoStream = value.toObject();
        }
        const double duration =
            firstProbe.value(QStringLiteral("format"))
                .toObject()
                .value(QStringLiteral("duration"))
                .toString()
                .toDouble();
        check(videoStream.value(QStringLiteral("width")).toInt() == 320
                  && videoStream.value(QStringLiteral("height")).toInt() == 180,
              QStringLiteral("exported resolution is wrong"));
        const double frameRate =
            rational(videoStream.value(QStringLiteral("r_frame_rate")).toString());
        check(std::abs(frameRate - 48.0) < 0.01,
              QStringLiteral("exported fps is not 48 (avg %1, r %2)")
                  .arg(videoStream.value(QStringLiteral("avg_frame_rate")).toString(),
                       videoStream.value(QStringLiteral("r_frame_rate")).toString()));
        check(duration > 0.9 && duration < 1.1,
              QStringLiteral("exported duration is not ~1s: %1").arg(duration));
        check(hasStream(firstProbe, QStringLiteral("audio")),
              QStringLiteral("exported file has no audio track"));
    }

    // Audio switch: a video-only export must have no audio stream.
    const QString silentPath =
        QDir(temp.path()).filePath(QStringLiteral("silent.mp4"));
    check(runWindowExport(silentPath, false, 1, 1, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          false, &ok, &exportError)
              && ok,
          QStringLiteral("video-only export failed: %1").arg(exportError));
    QJsonObject silentProbe;
    if (check(probe(silentPath, &silentProbe),
              QStringLiteral("cannot probe silent.mp4")))
    {
        check(!hasStream(silentProbe, QStringLiteral("audio")),
              QStringLiteral("includeAudio=false still produced audio"));
    }

    // Resolution scale: 2x must produce 640×360 for a 320×180 project.
    const QString scaledPath =
        QDir(temp.path()).filePath(QStringLiteral("scaled.mp4"));
    check(runWindowExport(scaledPath, false, 1, 2, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          false, &ok, &exportError)
              && ok,
          QStringLiteral("2x resolution export failed: %1").arg(exportError));
    QJsonObject scaledProbe;
    if (check(probe(scaledPath, &scaledProbe),
              QStringLiteral("cannot probe scaled.mp4")))
    {
        QJsonObject videoStream;
        for (const QJsonValue& value :
             scaledProbe.value(QStringLiteral("streams")).toArray())
        {
            if (value.toObject().value(QStringLiteral("codec_type")).toString()
                == QStringLiteral("video"))
                videoStream = value.toObject();
        }
        check(videoStream.value(QStringLiteral("width")).toInt() == 640
                  && videoStream.value(QStringLiteral("height")).toInt() == 360,
              QStringLiteral("2x resolution output is not 640x360"));
    }

    // Existing file: refused without overwrite, accepted with overwrite.
    check(runWindowExport(firstPath, true, 1, 1, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          false, &ok, &exportError),
          QStringLiteral("overwrite probe export did not finish"));
    check(!ok && exportError == QStringLiteral("output_exists"),
          QStringLiteral("existing output was not refused: %1").arg(exportError));
    check(QFileInfo::exists(firstPath),
          QStringLiteral("refused overwrite removed the existing file"));
    check(runWindowExport(firstPath, true, 1, 1, 18,
                          QStringLiteral("libx264"), QStringLiteral("medium"),
                          true, &ok, &exportError)
              && ok,
          QStringLiteral("overwrite export failed: %1").arg(exportError));

    // Cancellation: export2 must report "cancelled" and leave no file.
    const QString cancelPath =
        QDir(temp.path()).filePath(QStringLiteral("cancelled.mp4"));
    pvf::export2::Options cancelOptions = captured;
    cancelOptions.outputPath = cancelPath;
    cancelOptions.includeAudio = false;
    cancelOptions.overwrite = false;
    cancelOptions.progress = [](int frame, int, double) {
        return frame < 3;
    };
    const pvf::export2::Result cancelResult = pvf::export2::exportVideo(
        live->project(), live->assets(), cancelOptions);
    check(!cancelResult.ok && cancelResult.error == QStringLiteral("cancelled"),
          QStringLiteral("cancel result is wrong: %1").arg(cancelResult.error));
    check(!QFileInfo::exists(cancelPath)
              && !QFileInfo::exists(cancelPath + QStringLiteral(".part.mp4")),
          QStringLiteral("cancel left a partial file"));

    // Invalid CRF / encoder are rejected before encoding.
    pvf::export2::Options invalid = captured;
    invalid.outputPath =
        QDir(temp.path()).filePath(QStringLiteral("invalid.mp4"));
    invalid.crf = 52;
    invalid.progress = {};
    const pvf::export2::Result invalidCrf =
        pvf::export2::exportVideo(live->project(), live->assets(), invalid);
    check(invalidCrf.error == QStringLiteral("invalid_crf"),
          QStringLiteral("invalid CRF was not rejected"));
    invalid.crf = 18;
    invalid.encoder = QStringLiteral("no-such-encoder");
    const pvf::export2::Result invalidEncoder =
        pvf::export2::exportVideo(live->project(), live->assets(), invalid);
    check(invalidEncoder.error == QStringLiteral("unknown_encoder"),
          QStringLiteral("unknown encoder was not rejected"));
    check(!QFileInfo::exists(invalid.outputPath),
          QStringLiteral("invalid options left an output file"));

    // Progress dialog: frame/percent/ETA text, preview and cancel signal.
    QString decodeError;
    const QImage firstFrame = pvf::decodeVideoFirstFrame(firstPath, &decodeError);
    check(!firstFrame.isNull(), QStringLiteral("cannot decode first frame: %1").arg(decodeError));
    {
        pvf::ExportProgressDialog progressDialog;
        progressDialog.resize(640, 460);
        progressDialog.setPreview(firstFrame);
        progressDialog.setProgress(12, 48, 3.4);
        progressDialog.show();
        QApplication::processEvents();
        auto* status =
            progressDialog.findChild<QLabel*>(QStringLiteral("exportStatus"));
        check(status && status->text().contains(QStringLiteral("12 / 48"))
                  && status->text().contains(QStringLiteral("25%"))
                  && status->text().contains(QStringLiteral("剩余")),
              QStringLiteral("progress dialog text is wrong: %1")
                  .arg(status ? status->text() : QString()));
        QDir().mkpath(QStringLiteral("/tmp/opencode"));
        check(progressDialog.grab().toImage().save(
                  QStringLiteral("/tmp/opencode/pvforge-p7a-export.png")),
              QStringLiteral("progress screenshot failed"));
        bool cancelRequested = false;
        QObject::connect(&progressDialog,
                         &pvf::ExportProgressDialog::cancelRequested,
                         [&cancelRequested] { cancelRequested = true; });
        if (auto* cancelButton = progressDialog.findChild<QPushButton*>(
                QStringLiteral("exportCancelButton")))
            cancelButton->click();
        check(cancelRequested,
              QStringLiteral("progress cancel button did not emit"));
        progressDialog.finish();
    }

    delete window;
    QApplication::processEvents();
    QFile::remove(wavPath);
    qInfo() << "export-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerRenderMediaTests(Registry& registry)
{
    registry.add(QStringLiteral("--export-dialog-test"),
                 [](QApplication& app) { return exportDialogTest(app); });
    registry.add(QStringLiteral("--export-test"),
                 [](QApplication& app) { return exportTest(app); });
}

} // namespace pvf::tests
