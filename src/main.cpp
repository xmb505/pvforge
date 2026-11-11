#include "app/AppIcon.h"
#include "core/Commands.h"
#include "core/Camera.h"
#include "core/History.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "media/MediaImporter.h"
#include "media/AudioDecoder.h"
#include "media/VideoPlayer.h"
#include "media/VideoProxy.h"
#include "media/DecoderInfo.h"
#include "render/CameraRenderer.h"
#include "render/SceneRenderer.h"
#include "audio/AudioEngine.h"
#include "core/Settings.h"
#include "app/MainWindow.h"
#include "app/EditorSession.h"
#include "app/EditActions.h"
#include "app/NewProjectDialog.h"
#include "app/ExportDialog.h"
#include "app/ExportProgressDialog.h"
#include "app/SettingsDialog.h"
#include "app/SystemInfo.h"
#include "app/WelcomeWindow.h"
#include "anim/Anim.h"
#include "assets/AssetStore.h"
#include "assets/AssetTypeDetect.h"
#include "gizmo/Gizmo.h"
#include "html/HtmlCaptureTest.h"
#include "html/HtmlLayerTest.h"
#include "core/Project.h"
#include "export2/Export.h"
#include "mcp2/Service.h"
#include "mcp2/HttpServer.h"
#include "model/Project.h"
#include "model/Uuid.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "next/history/HistoryPanel.h"
#include "next/properties/SolverPreviewWidget.h"
#include "next/settings/SettingsStore.h"
#include "core/CheckpointManager.h"
#include "core/ComicBox.h"
#include "core/PerfMonitor.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "ui/BrandWallDialog.h"
#include "ui/PropertiesPanel.h"
#include "ui/TextLayoutDialog.h"
#include "ui/WorkbenchPanel.h"
#include "media/VideoDecoder.h"
#include "mcp/McpServer.h"
#include "mcp/StdioBridgeTest.h"
#include "mcp/McpCommandService.h"
#include "ui/BezierEditorDialog.h"
#include "ui/CameraMonitor.h"
#include "ui/EasingPicker.h"
#include "ui/CollapsibleSection.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CurveEditor.h"
#include "ui/McpLogPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WaveformView.h"
#include "ui/WorkbenchPanel.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QClipboard>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QCheckBox>
#include <QFocusEvent>
#include <QDataStream>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFont>
#include <QHostAddress>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QJsonDocument>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QEventLoop>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QWidgetAction>
#include <QWheelEvent>
#include <cmath>
#include <functional>
#include <memory>

namespace {

int selfTest()
{
    pvf::Project project;

    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString path = QDir::tempPath() + QStringLiteral("/pvforge_selftest.png");
    if (!img.save(path))
    {
        qCritical() << "self-test: failed to write" << path;
        return 1;
    }

    pvf::Entity* e = project.addImage(path);
    if (!e)
    {
        qCritical() << "self-test: addImage failed";
        return 1;
    }
    const pvf::EntityId id = e->id;

    project.setValue(id, QStringLiteral("x"), 0, 0, true);
    project.setValue(id, QStringLiteral("x"), 30, 600, true);

    const double mid = project.value(id, QStringLiteral("x"), 15);
    if (std::abs(mid - 300.0) > 1e-6)
    {
        qCritical() << "self-test: interpolation expected 300, got" << mid;
        return 1;
    }
    if (std::abs(project.value(id, QStringLiteral("x"), 0) - 0.0) > 1e-6
        || std::abs(project.value(id, QStringLiteral("x"), 30) - 600.0) > 1e-6)
    {
        qCritical() << "self-test: keyframe endpoints wrong";
        return 1;
    }
    if (std::abs(project.value(id, QStringLiteral("opacity"), 0) - 1.0) > 1e-6)
    {
        qCritical() << "self-test: opacity default wrong";
        return 1;
    }

    QFile::remove(path);
    qInfo() << "self-test OK: keyframe interpolation x(15) =" << mid;
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

int propertiesTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p4a-properties.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P4a 属性");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "properties-test: create failed" << error;
        return 1;
    }

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

    const pvf::Uuid target =
        makeEntity(QStringLiteral("地面"), 0, 120, 0, pvf::ContentKind::Empty);
    const pvf::Uuid camera =
        makeEntity(QStringLiteral("摄像机"), 0, 120, 1, pvf::ContentKind::Camera);
    project.entities[camera].transform.scaleX = 0.5;
    project.entities[camera].transform.scaleY = 0.5;

    const pvf::Uuid text =
        makeEntity(QStringLiteral("标题"), 0, 90, 2, pvf::ContentKind::Text);
    project.entities[text].transform.x = 10.0;
    project.entities[text].transform.y = 20.0;
    project.entities[text].transform.rotation = 5.0;
    project.entities[text].transform.opacity = 0.8;
    project.entities[text].content.text.text = QStringLiteral("PVForge");
    project.entities[text].content.text.pixelSize = 96;

    const pvf::Uuid vector =
        makeEntity(QStringLiteral("矢量"), 10, 50, 3, pvf::ContentKind::Vector);
    project.entities[vector].content.vector.width = 400.0;
    project.entities[vector].content.vector.height = 300.0;
    project.entities[vector].content.vector.cornerRadius = 40.0;

    const pvf::Uuid image =
        makeEntity(QStringLiteral("图片"), 0, 60, 4, pvf::ContentKind::Asset);
    project.entities[image].content.asset.type = pvf::AssetType::Image;
    project.entities[image].content.asset.hash = QStringLiteral("imagehash");
    project.entities[image].content.asset.sourceIn = 2;
    project.entities[image].content.asset.volume = 0.75;

    const pvf::Uuid solverRoot =
        makeEntity(QStringLiteral("求解物体"), 0, 60, 5, pvf::ContentKind::Empty);
    pvf::model::Track solverX;
    solverX.keys = {{0, 0.0, {}, {}}};
    project.entities[solverRoot].tracks.insert(QStringLiteral("x"), solverX);
    pvf::model::Track solverY;
    solverY.keys = {{0, 0.0, {}, {}}};
    project.entities[solverRoot].tracks.insert(QStringLiteral("y"), solverY);

    const QVector<pvf::Uuid> all = {target, camera, text, vector, image,
                                    solverRoot};
    if (!session->document()->commitEntities(all, QStringLiteral("p4a setup"),
                                             &error))
    {
        qCritical() << "properties-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    if (!live || !document || !panel || !panel->sessionMode())
    {
        qCritical() << "properties-test: session window/panel missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "properties-test: panel edit failed"
                                     << message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "properties-test:" << message;
        return condition;
    };
    auto entity = [live](const pvf::Uuid& uuid) {
        return live->project().entities.value(uuid);
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "properties-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };

    panel->setAutoKey(false);
    document->setSelection(text);
    QApplication::processEvents();
    QApplication::processEvents();

    auto* nameEdit = panel->findChild<QLineEdit*>(QStringLiteral("entityName"));
    auto* startSpin = panel->findChild<QSpinBox*>(QStringLiteral("entityStart"));
    auto* endSpin = panel->findChild<QSpinBox*>(QStringLiteral("entityEnd"));
    auto* rowSpin = panel->findChild<QSpinBox*>(QStringLiteral("entityRow"));
    auto* xSpin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_x"));
    auto* opacitySpin =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_opacity"));
    if (!check(nameEdit && startSpin && endSpin && rowSpin && xSpin
                   && opacitySpin,
               QStringLiteral("basic/transform widgets missing")))
        return 1;
    if (!check(nameEdit->text() == QStringLiteral("标题")
                   && startSpin->value() == 0 && endSpin->value() == 90
                   && rowSpin->value() == 2,
               QStringLiteral("basic fields do not show the entity")))
        return 1;
    if (!check(std::abs(xSpin->value() - 10.0) < 1e-6
                   && std::abs(opacitySpin->value() - 80.0) < 1e-6,
               QStringLiteral("transform fields do not show the entity")))
        return 1;

    int before = depth();
    nameEdit->setText(QStringLiteral("文字改名"));
    QMetaObject::invokeMethod(nameEdit, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(entity(text).name == QStringLiteral("文字改名"),
               QStringLiteral("name edit did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("name edit was not one commit")))
        return 1;

    before = depth();
    xSpin->setValue(120.0);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(text).transform.x - 120.0) < 1e-6,
               QStringLiteral("x edit did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("x edit was not one commit")))
        return 1;

    auto* scaleXSpin =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_scaleX"));
    if (!check(scaleXSpin, QStringLiteral("scale X row missing")))
        return 1;
    scaleXSpin->setValue(-1.5);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(text).transform.scaleX + 1.5) < 1e-6,
               QStringLiteral("negative scale was rejected")))
        return 1;

    auto* pivotXSpin =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("pivotX"));
    if (!check(pivotXSpin, QStringLiteral("pivot X row missing")))
        return 1;
    pivotXSpin->setValue(25.0);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(text).transform.pivotX - 25.0) < 1e-6,
               QStringLiteral("pivot X edit did not reach the model")))
        return 1;

    // Locking disables the other fields but the lock itself stays togglable.
    auto* locked = panel->findChild<QCheckBox*>(QStringLiteral("entityLocked"));
    if (!check(locked, QStringLiteral("locked checkbox missing")))
        return 1;
    locked->setChecked(true);
    QApplication::processEvents();
    flush();
    if (!check(entity(text).locked, QStringLiteral("lock did not reach the model"))
        || !check(!xSpin->isEnabled(),
                  QStringLiteral("locked entity fields stayed enabled")))
        return 1;
    locked->setChecked(false);
    QApplication::processEvents();
    flush();
    if (!check(!entity(text).locked,
               QStringLiteral("unlock did not reach the model")))
        return 1;

    // Camera framing scale: 5%..100%, stored as 0.05..1.0 absolute.
    document->setSelection(camera);
    QApplication::processEvents();
    auto* cameraScale =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_scaleX"));
    auto* cameraScaleY =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_scaleY"));
    if (!check(cameraScale && cameraScale->minimum() == 5.0
                   && cameraScale->maximum() == 100.0
                   && cameraScale->suffix() == QStringLiteral("%"),
               QStringLiteral("camera scale range/suffix wrong")))
        return 1;
    if (!check(cameraScale->toolTip().contains(QStringLiteral("0.05")),
               QStringLiteral("camera scale limit hint missing")))
        return 1;
    if (!check(cameraScaleY && !cameraScaleY->isVisible(),
               QStringLiteral("camera scale Y should be hidden")))
        return 1;
    cameraScale->setValue(50.0);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(camera).transform.scaleX - 0.5) < 1e-6
                   && std::abs(entity(camera).transform.scaleY - 0.5) < 1e-6,
               QStringLiteral("camera scale did not store 0.5")))
        return 1;
    cameraScale->setValue(100.0);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(camera).transform.scaleX - 1.0) < 1e-6,
               QStringLiteral("camera scale upper limit wrong")))
        return 1;
    cameraScale->setValue(5.0);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(camera).transform.scaleX - 0.05) < 1e-6,
               QStringLiteral("camera scale lower limit wrong")))
        return 1;
    auto* cameraInfo = panel->findChild<QLabel*>(QStringLiteral("cameraInfo"));
    if (!check(cameraInfo && cameraInfo->isVisible()
                   && cameraInfo->text().contains(QStringLiteral("只读")),
               QStringLiteral("camera read-only hint missing")))
        return 1;

    // Text content fields.
    document->setSelection(text);
    QApplication::processEvents();
    panel->setAutoKey(false);
    auto* textContent = panel->findChild<QLineEdit*>(QStringLiteral("textContent"));
    auto* textSize = panel->findChild<QSpinBox*>(QStringLiteral("textSize"));
    auto* lineHeight =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("textLineHeight"));
    auto* letterSpacing =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("textLetterSpacing"));
    auto* padding = panel->findChild<QDoubleSpinBox*>(QStringLiteral("textPadding"));
    auto* boxWidth = panel->findChild<QDoubleSpinBox*>(QStringLiteral("textBoxWidth"));
    auto* boxHeight =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("textBoxHeight"));
    auto* textBold = panel->findChild<QCheckBox*>(QStringLiteral("textBold"));
    auto* textAlign = panel->findChild<QComboBox*>(QStringLiteral("textAlign"));
    auto* textDirection =
        panel->findChild<QComboBox*>(QStringLiteral("textDirection"));
    auto* strokeWidth =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("textStrokeWidth"));
    if (!check(textContent && textSize && lineHeight && letterSpacing && padding
                   && boxWidth && boxHeight && textBold && textAlign
                   && textDirection && strokeWidth,
               QStringLiteral("text widgets missing")))
        return 1;
    QMetaObject::invokeMethod(textContent, "textEdited", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("改过的文字")));
    QApplication::processEvents();
    textSize->setValue(120);
    lineHeight->setValue(1.5);
    letterSpacing->setValue(2.5);
    padding->setValue(12.0);
    boxWidth->setValue(500.0);
    boxHeight->setValue(240.0);
    textBold->setChecked(true);
    textAlign->setCurrentIndex(
        textAlign->findData(int(Qt::AlignLeft | Qt::AlignVCenter)));
    textDirection->setCurrentIndex(1);
    strokeWidth->setValue(3.5);
    QApplication::processEvents();
    flush();
    {
        const pvf::model::TextContent content = entity(text).content.text;
        if (!check(content.text == QStringLiteral("改过的文字")
                       && content.pixelSize == 120
                       && std::abs(content.lineHeight - 1.5) < 1e-6
                       && std::abs(content.letterSpacing - 2.5) < 1e-6
                       && std::abs(content.padding - 12.0) < 1e-6
                       && std::abs(content.boxWidth - 500.0) < 1e-6
                       && std::abs(content.boxHeight - 240.0) < 1e-6
                       && content.bold && content.vertical && content.verticalRtl
                       && content.align == int(Qt::AlignLeft | Qt::AlignVCenter)
                       && std::abs(content.strokeWidth - 3.5) < 1e-6,
                   QStringLiteral("text fields did not reach the model")))
            return 1;
    }

    // Vector content fields.
    document->setSelection(vector);
    QApplication::processEvents();
    auto* vectorShape =
        panel->findChild<QComboBox*>(QStringLiteral("vectorShape"));
    auto* vectorWidth =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("vectorWidth"));
    auto* vectorHeight =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("vectorHeight"));
    auto* vectorCorner =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("vectorCorner"));
    auto* vectorSides = panel->findChild<QSpinBox*>(QStringLiteral("vectorSides"));
    auto* vectorInner =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("vectorInner"));
    auto* vectorStroke =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidth"));
    if (!check(vectorShape && vectorWidth && vectorHeight && vectorCorner
                   && vectorSides && vectorInner && vectorStroke,
               QStringLiteral("vector widgets missing")))
        return 1;
    if (!check(vectorShape->currentData().toInt() == 0
                   && std::abs(vectorWidth->value() - 400.0) < 1e-6,
               QStringLiteral("vector fields do not show the entity")))
        return 1;
    vectorWidth->setValue(500.0);
    vectorHeight->setValue(250.0);
    vectorCorner->setValue(12.0);
    vectorSides->setValue(7);
    vectorInner->setValue(0.6);
    vectorStroke->setValue(4.0);
    vectorShape->setCurrentIndex(vectorShape->findData(3));
    QApplication::processEvents();
    flush();
    {
        const pvf::model::VectorContent content = entity(vector).content.vector;
        if (!check(std::abs(content.width - 500.0) < 1e-6
                       && std::abs(content.height - 250.0) < 1e-6
                       && std::abs(content.cornerRadius - 12.0) < 1e-6
                       && content.sides == 7
                       && std::abs(content.innerRatio - 0.6) < 1e-6
                       && std::abs(content.strokeWidth - 4.0) < 1e-6
                       && content.shape == 3,
                   QStringLiteral("vector fields did not reach the model")))
            return 1;
    }

    // Asset fields: hash/type read-only, sourceIn/volume editable.
    document->setSelection(image);
    QApplication::processEvents();
    auto* assetHash = panel->findChild<QLineEdit*>(QStringLiteral("assetHash"));
    auto* assetType = panel->findChild<QLabel*>(QStringLiteral("assetType"));
    auto* assetSourceIn =
        panel->findChild<QSpinBox*>(QStringLiteral("assetSourceIn"));
    auto* assetVolume =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("assetVolume"));
    if (!check(assetHash && assetType && assetSourceIn && assetVolume,
               QStringLiteral("asset widgets missing")))
        return 1;
    if (!check(assetHash->isReadOnly()
                   && assetHash->text() == QStringLiteral("imagehash")
                   && assetType->text() == QStringLiteral("图片")
                   && assetSourceIn->value() == 2
                   && std::abs(assetVolume->value() - 0.75) < 1e-6,
               QStringLiteral("asset fields do not show the entity")))
        return 1;
    assetSourceIn->setValue(7);
    assetVolume->setValue(1.25);
    QApplication::processEvents();
    flush();
    if (!check(entity(image).content.asset.sourceIn == 7
                   && std::abs(entity(image).content.asset.volume - 1.25) < 1e-6,
               QStringLiteral("asset fields did not reach the model")))
        return 1;
    assetVolume->setValue(3.0);
    QApplication::processEvents();
    flush();
    if (!check(entity(image).content.asset.volume <= 2.0,
               QStringLiteral("asset volume range not enforced")))
        return 1;

    // Keyframes: per-track diamond plus one group easing control (33 entries).
    document->setSelection(text);
    live->setPlayhead(0);
    QApplication::processEvents();
    auto* diamond =
        panel->findChild<QToolButton*>(QStringLiteral("trackDiamond_fontSize"));
    auto* easing =
        panel->findChild<QComboBox*>(QStringLiteral("keyframeEasingGroup"));
    if (!check(diamond && easing, QStringLiteral("keyframe track row missing")))
        return 1;
    if (!check(easing->count() == 33, QStringLiteral("easing combo has %1 items")
                                          .arg(easing->count())))
        return 1;
    if (!check(easing->findData(QStringLiteral("bezier")) >= 0,
               QStringLiteral("bezier easing entry missing")))
        return 1;
    if (!check(!panel->findChild<QComboBox*>(QStringLiteral("trackEasing_fontSize")),
               QStringLiteral("per-track easing combo still present")))
        return 1;
    before = depth();
    diamond->click();
    QApplication::processEvents();
    flush();
    if (!check(depth() == before + 1,
               QStringLiteral("add first key was not one commit")))
        return 1;

    live->setPlayhead(10);
    QApplication::processEvents();
    before = depth();
    diamond->click();
    QApplication::processEvents();
    flush();
    {
        const auto track =
            entity(text).tracks.value(QStringLiteral("fontSize"));
        bool found = false;
        for (const pvf::model::Keyframe& key : track.keys)
            if (key.frame == 10)
                found = true;
        if (!check(found, QStringLiteral("diamond did not add a key at frame 10"))
            || !check(depth() == before + 1,
                      QStringLiteral("add key was not one commit")))
            return 1;
    }
    before = depth();
    easing->setCurrentIndex(easing->findData(QStringLiteral("easeinoutcubic")));
    QApplication::processEvents();
    flush();
    {
        bool found = false;
        const auto track = entity(text).tracks.value(QStringLiteral("fontSize"));
        for (const pvf::model::Keyframe& key : track.keys)
            if (key.frame == 10)
                found = key.easing.type == QStringLiteral("easeinoutcubic");
        if (!check(found, QStringLiteral("easing preset not written"))
            || !check(depth() == before + 1,
                      QStringLiteral("group easing was not one commit")))
            return 1;
    }
    before = depth();
    diamond->click();
    QApplication::processEvents();
    flush();
    {
        bool found = false;
        const auto track = entity(text).tracks.value(QStringLiteral("fontSize"));
        for (const pvf::model::Keyframe& key : track.keys)
            if (key.frame == 10)
                found = true;
        if (!check(!found, QStringLiteral("diamond did not delete the key"))
            || !check(depth() == before + 1,
                      QStringLiteral("delete key was not one commit")))
            return 1;
    }

    // Solvers: create from the playhead, edit the range, protect driven keys,
    // then delete.
    document->setSelection(solverRoot);
    live->setPlayhead(0);
    QApplication::processEvents();
    auto* solverEffect =
        panel->findChild<QComboBox*>(QStringLiteral("solverEffectCombo"));
    auto* solverTarget =
        panel->findChild<QComboBox*>(QStringLiteral("solverTargetCombo"));
    auto* solverCreateEnd =
        panel->findChild<QSpinBox*>(QStringLiteral("solverCreateEnd"));
    auto* solverCreate =
        panel->findChild<QPushButton*>(QStringLiteral("solverCreateButton"));
    if (!check(solverEffect && solverTarget && solverCreateEnd && solverCreate,
               QStringLiteral("solver create widgets missing")))
        return 1;
    solverEffect->setCurrentIndex(
        solverEffect->findData(int(pvf::SolverEffect::Gravity)));
    solverTarget->setCurrentIndex(solverTarget->findData(target));
    solverCreateEnd->setValue(-1);
    before = depth();
    solverCreate->click();
    QApplication::processEvents();
    flush();
    if (!check(entity(solverRoot).solvers.size() == 1,
               QStringLiteral("solver segment was not created")))
        return 1;
    if (!check(entity(solverRoot).solvers.first().effect
                       == pvf::SolverEffect::Gravity
                   && entity(solverRoot).solvers.first().startFrame == 0
                   && entity(solverRoot).solvers.first().endFrame == -1
                   && entity(solverRoot)
                          .solvers.first()
                          .params.value(QStringLiteral("targetUuid"))
                          .toString()
                       == target,
               QStringLiteral("solver defaults wrong")))
        return 1;
    if (!check(depth() == before + 1,
               QStringLiteral("solver create was not one commit")))
        return 1;

    auto* solverEnd = panel->findChild<QSpinBox*>(QStringLiteral("solverEnd0"));
    auto* solverDelete =
        panel->findChild<QPushButton*>(QStringLiteral("solverDelete0"));
    if (!check(solverEnd && solverDelete,
               QStringLiteral("solver card widgets missing")))
        return 1;
    solverEnd->setValue(30);
    QMetaObject::invokeMethod(solverEnd, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(entity(solverRoot).solvers.first().endFrame == 30,
               QStringLiteral("solver end edit did not reach the model")))
        return 1;

    live->setPlayhead(20);
    QApplication::processEvents();
    auto* xDiamond =
        panel->findChild<QToolButton*>(QStringLiteral("trackDiamond_x"));
    if (!check(xDiamond && !xDiamond->isEnabled(),
               QStringLiteral("driven key diamond is not protected")))
        return 1;
    const int keysBefore =
        entity(solverRoot).tracks.value(QStringLiteral("x")).keys.size();
    xDiamond->click();
    QApplication::processEvents();
    if (!check(entity(solverRoot).tracks.value(QStringLiteral("x")).keys.size()
                   == keysBefore,
               QStringLiteral("protected diamond added a key")))
        return 1;
    error.clear();
    if (!check(!live->editActions()->addKeyframe(solverRoot, QStringLiteral("x"),
                                                 20, &error),
               QStringLiteral("EditActions allowed a key inside a solver")))
        return 1;

    before = depth();
    solverDelete->click();
    QApplication::processEvents();
    flush();
    if (!check(entity(solverRoot).solvers.isEmpty(),
               QStringLiteral("solver delete did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("solver delete was not one commit")))
        return 1;

    // Auto-key is a single switch shared by the menu action and the panel.
    auto* autoKeyCheck =
        panel->findChild<QCheckBox*>(QStringLiteral("autoKeyCheck"));
    auto* autoKeyAction =
        window->findChild<QAction*>(QStringLiteral("autoKeyAction"));
    if (!check(autoKeyCheck && autoKeyAction,
               QStringLiteral("auto-key controls missing")))
        return 1;
    autoKeyAction->setChecked(false);
    QApplication::processEvents();
    const bool actionBefore = autoKeyAction->isChecked();
    autoKeyCheck->click();
    QApplication::processEvents();
    if (!check(autoKeyAction->isChecked() != actionBefore
                   && panel->autoKey() == autoKeyAction->isChecked(),
               QStringLiteral("panel auto-key did not reach the action")))
        return 1;
    autoKeyAction->setChecked(actionBefore);
    QApplication::processEvents();
    if (!check(panel->autoKey() == actionBefore,
               QStringLiteral("action auto-key did not reach the panel")))
        return 1;

    // Screenshot with a text entity selected and the properties tab visible.
    document->setSelection(text);
    live->setPlayhead(15);
    QApplication::processEvents();
    QApplication::processEvents();
    if (auto* objectTab = window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
        objectTab->verticalScrollBar()->setValue(300);
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-p4a-properties.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "properties-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "properties-test OK: basic/transform/content/keyframes/33 easing/"
               "solvers, screenshot"
            << screenshot;
    return 0;
}

int solverUiTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver UI");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-ui.pvproj"));
    auto session = pvf::EditorSession::createProject(projectDir, settings,
                                                     &error);
    if (!session)
    {
        qCritical() << "solver-ui-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("地面"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 600.0;
    project.entities[ground].content.vector.height = 60.0;
    project.entities[ground].content.vector.fillColor =
        QStringLiteral("#6a707a");
    project.entities[ground].physicsInteract = true;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("落球"), 1, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    project.entities[ball].content.vector.fillColor =
        QStringLiteral("#ffd166");
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    const pvf::Uuid spinner =
        makeEntity(QStringLiteral("自转物体"), 2, pvf::ContentKind::Vector);
    project.entities[spinner].transform.x = 150.0;
    project.entities[spinner].content.vector.width = 60.0;
    project.entities[spinner].content.vector.height = 60.0;
    pvf::model::Track spinRotation;
    spinRotation.keys = {{0, 0.0, {}, {}}};
    project.entities[spinner].tracks.insert(QStringLiteral("rotation"),
                                            spinRotation);

    const pvf::Uuid orbiter =
        makeEntity(QStringLiteral("公转物体"), 3, pvf::ContentKind::Vector);
    project.entities[orbiter].transform.x = -150.0;
    project.entities[orbiter].transform.y = 80.0;
    project.entities[orbiter].content.vector.width = 40.0;
    project.entities[orbiter].content.vector.height = 40.0;
    pvf::model::Track orbitX;
    orbitX.keys = {{0, -150.0, {}, {}}};
    project.entities[orbiter].tracks.insert(QStringLiteral("x"), orbitX);
    pvf::model::Track orbitY;
    orbitY.keys = {{0, 80.0, {}, {}}};
    project.entities[orbiter].tracks.insert(QStringLiteral("y"), orbitY);

    const QVector<pvf::Uuid> setup = {ground, ball, spinner, orbiter};
    if (!session->document()->commitEntities(setup,
                                             QStringLiteral("solver-ui setup"),
                                             &error))
    {
        qCritical() << "solver-ui-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* viewport = window->viewportForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !panel || !viewport || !timeline)
    {
        qCritical() << "solver-ui-test: window parts missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "solver-ui-test: panel edit failed"
                                     << message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-ui-test:" << message;
        return condition;
    };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "solver-ui-test: flush failed" << flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };

    auto* solverSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("solverSection"));
    if (!check(solverSection != nullptr, QStringLiteral("solver section missing")))
        return 1;
    solverSection->setExpanded(true);
    QApplication::processEvents();

    auto* effectCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverEffectCombo"));
    auto* targetCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverTargetCombo"));
    auto* createEnd =
        panel->findChild<QSpinBox*>(QStringLiteral("solverCreateEnd"));
    auto* createManual =
        panel->findChild<QCheckBox*>(QStringLiteral("solverCreateManualEnd"));
    auto* createAuto =
        panel->findChild<QLabel*>(QStringLiteral("solverCreateEndAuto"));
    auto* createButton =
        panel->findChild<QPushButton*>(QStringLiteral("solverCreateButton"));
    if (!check(effectCombo && targetCombo && createEnd && createManual
                   && createAuto && createButton,
               QStringLiteral("solver create widgets missing")))
        return 1;

    // Default creation is automatic: no end-frame spin is shown, only the
    // read-only automatic explanation.
    if (!check(!createEnd->isVisible(),
               QStringLiteral("create end spin is visible by default"))
        || !check(!createManual->isChecked(),
                  QStringLiteral("manual end is checked by default"))
        || !check(createAuto->text().contains(QStringLiteral("自动")),
                  QStringLiteral("automatic end label missing")))
        return 1;

    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    effectCombo->setCurrentIndex(
        effectCombo->findData(int(pvf::SolverEffect::Gravity)));
    targetCombo->setCurrentIndex(targetCombo->findData(ground));
    const int beforeCreate = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();

    const auto& ballEntity = live->project().entities.value(ball);
    if (!check(ballEntity.solvers.size() == 1,
               QStringLiteral("gravity solver was not created"))
        || !check(ballEntity.solvers.first().endFrame == -1,
                  QStringLiteral("new solver is not automatic"))
        || !check(undoDepth() == beforeCreate + 1,
                  QStringLiteral("solver creation was not one commit")))
        return 1;
    const pvf::Uuid gravityId = ballEntity.solvers.first().id;

    // Tighten the collision parameters through the card widgets; every edit
    // goes through EditActions and keeps the automatic end at -1.
    auto* strength =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_strength"));
    auto* elasticity =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_elasticity"));
    auto* friction =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_friction"));
    auto* threshold =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_stopThreshold"));
    if (!check(strength && elasticity && friction && threshold,
               QStringLiteral("gravity parameter widgets missing")))
        return 1;
    const auto setParam = [](QDoubleSpinBox* spin, double value) {
        spin->setValue(value);
        QMetaObject::invokeMethod(spin, "editingFinished",
                                  Qt::DirectConnection);
        QApplication::processEvents();
    };
    setParam(strength, 500.0);
    setParam(elasticity, 0.0);
    setParam(friction, 0.5);
    setParam(threshold, 10.0);
    flush();

    const pvf::model::Entity tunedBall =
        live->project().entities.value(ball);
    const pvf::SolverPreviewEval expected = pvf::evaluateSolverPreview(
        live->project(), live->assets(), tunedBall,
        tunedBall.solvers.first());
    if (!check(expected.isValid(), QStringLiteral("preview failed: %1")
                                       .arg(expected.error))
        || !check(expected.result.stopFrame > 0
                      && expected.result.stopFrame < 59,
                  QStringLiteral("gravity did not stop inside the clip: "
                                 "stop=%1 reason=%2")
                      .arg(expected.result.stopFrame)
                      .arg(pvf::solver::stopReasonName(
                          expected.result.reason)))
        || !check(expected.result.reason
                      == pvf::solver::StopReason::Collision,
                  QStringLiteral("gravity stop reason is not collision")))
        return 1;
    const int stopFrame = expected.result.stopFrame;

    auto* endInfo =
        panel->findChild<QLabel*>(QStringLiteral("solverEndInfo0"));
    auto* manualEnd =
        panel->findChild<QCheckBox*>(QStringLiteral("solverManualEnd0"));
    auto* solverEnd =
        panel->findChild<QSpinBox*>(QStringLiteral("solverEnd0"));
    if (!check(endInfo && manualEnd && solverEnd,
               QStringLiteral("solver card end widgets missing"))
        || !check(endInfo->text().contains(QString::number(stopFrame))
                      && endInfo->text().contains(QStringLiteral("碰撞")),
                  QStringLiteral("card does not show stop frame/reason: %1")
                      .arg(endInfo ? endInfo->text() : QString()))
        || !check(!solverEnd->isVisible(),
                  QStringLiteral("card end spin is visible in automatic mode")))
        return 1;

    // Advanced override: toggling the checkbox compacts one explicit end,
    // editing the spin updates it, and unticking returns to automatic.
    manualEnd->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.first().endFrame
                   == stopFrame,
               QStringLiteral("manual end toggle did not write stop frame"))
        || !check(manualEnd->isChecked() && solverEnd->isVisible(),
                  QStringLiteral("manual end spin did not appear")))
        return 1;
    solverEnd->setValue(12);
    QMetaObject::invokeMethod(solverEnd, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.first().endFrame
                   == 12,
               QStringLiteral("explicit end edit did not reach the model"))
        || !check(endInfo->text().contains(QStringLiteral("显式覆盖")),
                  QStringLiteral("explicit end label missing")))
        return 1;
    manualEnd->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.first().endFrame
                   == -1,
               QStringLiteral("manual end toggle did not restore automatic")))
        return 1;

    // The trajectory now lives in the properties panel's mini window; the
    // viewport no longer draws a solver overlay or a velocity handle.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    auto* mini = panel->findChild<pvf::next::SolverPreviewWidget*>(
        QStringLiteral("solverPreviewMini"));
    if (!check(!viewport->sessionSolverPreviewVisibleForTest(),
               QStringLiteral("viewport still draws the solver overlay"))
        || !check(mini != nullptr, QStringLiteral("mini solver preview missing")))
        return 1;
    if (!check(mini->entityUuid() == ball && mini->solverId() == gravityId,
               QStringLiteral("mini preview is not bound to the gravity solver"))
        || !check(mini->hasTarget() && mini->hasPath() && mini->hasArrow(),
                  QStringLiteral("mini preview misses target/path/arrow: %1")
                      .arg(mini->errorText()))
        || !check(mini->sampleCount() >= 2 && !mini->isTruncated(),
                  QStringLiteral("mini preview sample count wrong: %1")
                      .arg(mini->sampleCount())))
        return 1;

    // Playhead changes repaint the mini window without a commit.
    live->setPlayhead(5);
    QApplication::processEvents();
    if (!check(!mini->currentPoint().isNull(),
               QStringLiteral("mini preview current point missing")))
        return 1;
    live->setPlayhead(0);
    QApplication::processEvents();

    // Orbit / spin segments have no velocity handle.
    pvf::model::SolverSegment spinSolver;
    spinSolver.id = pvf::model::newUuid();
    spinSolver.effect = pvf::SolverEffect::Spin;
    spinSolver.startFrame = 0;
    spinSolver.endFrame = -1;
    spinSolver.params.insert(QStringLiteral("angularVelocity"), 6.0);
    spinSolver.params.insert(QStringLiteral("direction"), 1);
    live->mutableProject().entities[spinner].solvers.append(spinSolver);
    live->mutableProject().entities[spinner].tracks[
        QStringLiteral("rotation")].keys = {{0, 0.0, {}, {}}};
    if (!document->commitEntity(spinner, QStringLiteral("solver-ui spin"),
                                &error))
    {
        qCritical() << "solver-ui-test: spin commit failed" << error;
        return 1;
    }

    pvf::model::SolverSegment orbitSolver;
    orbitSolver.id = pvf::model::newUuid();
    orbitSolver.effect = pvf::SolverEffect::Orbit;
    orbitSolver.startFrame = 0;
    orbitSolver.endFrame = -1;
    orbitSolver.params.insert(QStringLiteral("targetUuid"), ground);
    orbitSolver.params.insert(QStringLiteral("aspect"), 1.0);
    orbitSolver.params.insert(QStringLiteral("angle"), 0.0);
    orbitSolver.params.insert(QStringLiteral("direction"), 1);
    orbitSolver.params.insert(QStringLiteral("periodFrames"), 60.0);
    live->mutableProject().entities[orbiter].solvers.append(orbitSolver);
    if (!document->commitEntity(orbiter, QStringLiteral("solver-ui orbit"),
                                &error))
    {
        qCritical() << "solver-ui-test: orbit commit failed" << error;
        return 1;
    }
    flush();
    for (const pvf::Uuid& uuid : {spinner, orbiter})
    {
        document->setSelection(uuid);
        live->setPlayhead(0);
        QApplication::processEvents();
        if (!check(!viewport->sessionSolverPreviewVisibleForTest(),
                   QStringLiteral("orbit/spin still draws a viewport overlay"))
            || !check(mini->entityUuid() == uuid && mini->hasPath(),
                      QStringLiteral("orbit/spin mini preview is not visible"))
            || !check(!mini->hasArrow(),
                      QStringLiteral("orbit/spin shows a velocity arrow")))
            return 1;
    }

    // Timeline band ends at Result::stopFrame, not the clip end.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    const QRect band =
        timeline->solverBandRectForTest(ball, gravityId);
    const int bandStop =
        timeline->solverBandStopFrameForTest(ball, gravityId);
    if (!check(!band.isEmpty(), QStringLiteral("timeline solver band missing"))
        || !check(bandStop == stopFrame,
                  QStringLiteral("timeline band stop frame wrong"))
        || !check(std::abs(band.right()
                           - (timeline->frameXForTest(stopFrame) - 1))
                  <= 2,
                  QStringLiteral("timeline band does not end at stopFrame")))
        return 1;

    // Screenshot with the solver card and the mini trajectory window visible.
    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
        objectTab->verticalScrollBar()->setValue(
            objectTab->verticalScrollBar()->maximum());
    live->setPlayhead(5);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-solver-preview.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "solver-ui-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "solver-ui-test OK: automatic end, stop reason/frame, explicit"
               " override, mini trajectory window, orbit/spin, timeline band,"
               " screenshot"
            << screenshot;
    return 0;
}

int solverMiniPreviewTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "solver-mini-preview-test: no temp dir";
        return 1;
    }

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver Mini");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-mini.pvproj"));
    auto session = pvf::EditorSession::createProject(projectDir, settings,
                                                     &error);
    if (!session)
    {
        qCritical() << "solver-mini-preview-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("黑洞"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 80.0;
    project.entities[ground].content.vector.height = 80.0;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("落球"), 1, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    const pvf::Uuid moon =
        makeEntity(QStringLiteral("卫星"), 2, pvf::ContentKind::Vector);
    project.entities[moon].transform.x = -150.0;
    project.entities[moon].transform.y = 60.0;
    project.entities[moon].content.vector.width = 40.0;
    project.entities[moon].content.vector.height = 40.0;

    // Long clip whose black hole target sits exactly on the launch point.
    // The capture happens long before the 2000-frame preview cap, so the
    // preview must not claim truncation and must still name the hole.
    const pvf::Uuid holeBall =
        makeEntity(QStringLiteral("黑洞球"), 3, pvf::ContentKind::Vector);
    project.entities[holeBall].end = 3000;
    project.entities[holeBall].transform.x = 0.0;
    project.entities[holeBall].transform.y = -100.0;
    project.entities[holeBall].content.vector.width = 20.0;
    project.entities[holeBall].content.vector.height = 20.0;

    pvf::model::SolverSegment gravity;
    gravity.id = pvf::model::newUuid();
    gravity.effect = pvf::SolverEffect::Gravity;
    gravity.startFrame = 0;
    gravity.endFrame = -1;
    gravity.params.insert(QStringLiteral("targetUuid"), ground);
    gravity.params.insert(QStringLiteral("strength"), 500.0);
    gravity.params.insert(QStringLiteral("elasticity"), 0.0);
    gravity.params.insert(QStringLiteral("friction"), 0.5);
    gravity.params.insert(QStringLiteral("stopThreshold"), 10.0);
    gravity.params.insert(QStringLiteral("softening"), 60.0);
    gravity.params.insert(QStringLiteral("maxSpeed"), 4000.0);
    project.entities[ball].solvers.append(gravity);

    pvf::model::SolverSegment orbit;
    orbit.id = pvf::model::newUuid();
    orbit.effect = pvf::SolverEffect::Orbit;
    orbit.startFrame = 0;
    orbit.endFrame = -1;
    orbit.params.insert(QStringLiteral("targetUuid"), ground);
    orbit.params.insert(QStringLiteral("aspect"), 1.0);
    orbit.params.insert(QStringLiteral("angle"), 0.0);
    orbit.params.insert(QStringLiteral("direction"), 1);
    orbit.params.insert(QStringLiteral("periodFrames"), 60.0);
    project.entities[moon].solvers.append(orbit);

    pvf::model::SolverSegment hole;
    hole.id = pvf::model::newUuid();
    hole.effect = pvf::SolverEffect::BlackHole;
    hole.startFrame = 0;
    hole.endFrame = -1;
    hole.initialVelocity = QPointF(80.0, 220.0);
    hole.params.insert(QStringLiteral("targetUuid"), ground);
    hole.params.insert(QStringLiteral("mass"), 5000.0);
    hole.params.insert(QStringLiteral("eventHorizonRadius"), 30.0);
    hole.params.insert(QStringLiteral("objectRadius"), 5.0);
    hole.params.insert(QStringLiteral("softening"), 30.0);
    hole.params.insert(QStringLiteral("maxSpeed"), 4000.0);
    project.entities[holeBall].solvers.append(hole);

    if (!session->document()->commitEntities({ground, ball, moon, holeBall},
                                             QStringLiteral("solver-mini setup"),
                                             &error))
    {
        qCritical() << "solver-mini-preview-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* viewport = window->viewportForTest();
    auto* solverSection = panel
        ? panel->findChild<pvf::CollapsibleSection*>(
              QStringLiteral("solverSection"))
        : nullptr;
    if (!live || !document || !panel || !viewport || !solverSection)
    {
        qCritical() << "solver-mini-preview-test: window parts missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "solver-mini-preview-test: panel edit"
                                        " failed" << message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-mini-preview-test:" << message;
        return condition;
    };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "solver-mini-preview-test: flush failed" << flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };

    solverSection->setExpanded(true);
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();

    auto* mini = panel->findChild<pvf::next::SolverPreviewWidget*>(
        QStringLiteral("solverPreviewMini"));
    if (!check(mini != nullptr, QStringLiteral("mini widget missing"))
        || !check(solverSection->isAncestorOf(mini),
                  QStringLiteral("mini widget is not inside the solver section"))
        || !check(mini->isVisible(), QStringLiteral("mini widget hidden")))
        return 1;

    // Size window: about 260x160 .. 300x180 (width follows the panel).
    if (!check(mini->width() >= 260 && mini->width() <= 440,
               QStringLiteral("mini width out of range: %1").arg(mini->width()))
        || !check(mini->height() >= 160 && mini->height() <= 180,
                  QStringLiteral("mini height out of range: %1")
                      .arg(mini->height())))
        return 1;

    if (!check(mini->entityUuid() == ball && mini->solverId() == gravity.id,
               QStringLiteral("mini widget is not bound to the gravity solver"))
        || !check(mini->errorText().isEmpty(),
                  QStringLiteral("mini widget error: %1").arg(mini->errorText()))
        || !check(mini->hasTarget() && mini->hasPath() && mini->hasArrow(),
                  QStringLiteral("mini widget misses target/path/arrow"))
        || !check(mini->sampleCount() >= 2,
                  QStringLiteral("mini widget has no samples"))
        || !check(mini->targetName() == QStringLiteral("黑洞"),
                  QStringLiteral("target is not labelled with its entity name: %1")
                      .arg(mini->targetName()))
        || !check(mini->entityName() == QStringLiteral("落球"),
                  QStringLiteral("start is not labelled with its entity name: %1")
                      .arg(mini->entityName()))
        || !check(!mini->isTruncated(),
                  QStringLiteral("short segment reported as truncated")))
        return 1;

    // Only points/lines: the four public anchors sit inside the window.
    const QRect windowRect = mini->rect();
    if (!check(windowRect.contains(mini->startPoint().toPoint())
                   && windowRect.contains(mini->targetPoint().toPoint())
                   && windowRect.contains(mini->arrowTip().toPoint()),
               QStringLiteral("mini anchors fall outside the window: rect=%1,%2 "
                              "%3x%4 start=%5,%6 target=%7,%8 arrow=%9,%10")
                   .arg(windowRect.x())
                   .arg(windowRect.y())
                   .arg(windowRect.width())
                   .arg(windowRect.height())
                   .arg(mini->startPoint().x())
                   .arg(mini->startPoint().y())
                   .arg(mini->targetPoint().x())
                   .arg(mini->targetPoint().y())
                   .arg(mini->arrowTip().x())
                   .arg(mini->arrowTip().y()))
        || !check(mini->startPoint() != mini->targetPoint(),
                  QStringLiteral("start and target collapse onto one point")))
        return 1;

    // Black hole target sitting exactly on the launch point, on a clip much
    // longer than the preview cap.  The physical capture happens early, so the
    // preview must still name both dots and must not report truncation.
    mini->setSolver(holeBall, hole.id);
    document->setSelection(holeBall);
    live->setPlayhead(0);
    QApplication::processEvents();
    mini->refreshPreview();
    QApplication::processEvents();
    if (!check(mini->errorText().isEmpty(),
               QStringLiteral("black hole preview error: %1")
                   .arg(mini->errorText()))
        || !check(mini->hasTarget() && mini->hasPath(),
                  QStringLiteral("black hole preview misses target/path"))
        || !check(mini->targetName() == QStringLiteral("黑洞"),
                  QStringLiteral("black hole target label: %1")
                      .arg(mini->targetName()))
        || !check(mini->entityName() == QStringLiteral("黑洞球"),
                  QStringLiteral("black hole entity label: %1")
                      .arg(mini->entityName()))
        || !check(mini->startPoint() == mini->targetPoint(),
                  QStringLiteral("black hole target should overlap the start"))
        || !check(!mini->isTruncated(),
                  QStringLiteral("early capture reported as truncated")))
        return 1;
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    mini->repaint();
    QApplication::processEvents();
    if (!mini->grab().save(
            QStringLiteral("/tmp/opencode/pvforge-solver-mini-overlap.png")))
    {
        qCritical() << "solver-mini-preview-test: overlap screenshot failed";
        return 1;
    }

    // Back to the gravity segment for the drag/undo assertions below.
    document->setSelection(ball);
    mini->setSolver(ball, gravity.id);
    live->setPlayhead(0);
    mini->refreshPreview();
    QApplication::processEvents();

    // Drag the arrow handle: live model write, exactly one commit.
    const auto sendMouseTo = [](QWidget* target, QEvent::Type type,
                                const QPointF& pos, Qt::MouseButton button,
                                Qt::MouseButtons buttons) {
        QMouseEvent event(type, pos, target->mapToGlobal(pos.toPoint()),
                          button, buttons, Qt::NoModifier);
        QApplication::sendEvent(target, &event);
    };
    const int beforeDrag = undoDepth();
    const QPointF tip = mini->arrowTip();
    const QPoint dragTo = (tip + QPointF(55.0, -35.0)).toPoint();
    sendMouseTo(mini, QEvent::MouseButtonPress, tip, Qt::LeftButton,
                Qt::LeftButton);
    sendMouseTo(mini, QEvent::MouseMove, dragTo, Qt::NoButton,
                Qt::LeftButton);
    // While the handle is held the tip tracks the pointer exactly (the fit is
    // frozen during the drag).
    const QPointF duringDragTip = mini->arrowTip();
    sendMouseTo(mini, QEvent::MouseButtonRelease, dragTo, Qt::LeftButton,
                Qt::NoButton);
    QApplication::processEvents();
    flush();

    const QPointF dragged =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    if (!check(std::abs(dragged.x()) > 1.0 && std::abs(dragged.y()) > 1.0,
               QStringLiteral("arrow drag did not write initialVelocity"))
        || !check(undoDepth() == beforeDrag + 1,
                  QStringLiteral("arrow drag was not exactly one commit"))
        || !check(!mini->isDragging(),
                  QStringLiteral("arrow drag did not finish"))
        || !check(QLineF(duringDragTip, QPointF(dragTo)).length() < 1.5,
                  QStringLiteral("arrow tip did not follow the drag: %1,%2")
                      .arg(duringDragTip.x())
                      .arg(duringDragTip.y()))
        || !check(QLineF(mini->arrowTip(), mini->startPoint()).length() > 1.0,
                  QStringLiteral("arrow tip stayed on the start point")))
        return 1;

    // Switching the selection rebinds the window; entities without solvers
    // clear it.
    document->setSelection(moon);
    live->setPlayhead(0);
    QApplication::processEvents();
    if (!check(mini->entityUuid() == moon && mini->solverId() == orbit.id,
               QStringLiteral("mini widget did not follow the selection"))
        || !check(mini->hasTarget() && !mini->hasArrow(),
                  QStringLiteral("orbit segment shows wrong mini content")))
        return 1;

    document->setSelection(ground);
    QApplication::processEvents();
    if (!check(!mini->isVisible() && mini->entityUuid().isEmpty(),
               QStringLiteral("mini widget did not clear on solverless entity")))
        return 1;

    // Esc cancels the drag: velocity and undo stack roll back.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    const QPointF beforeEscVelocity =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    const int beforeEsc = undoDepth();
    const QPointF escTip = mini->arrowTip();
    sendMouseTo(mini, QEvent::MouseButtonPress, escTip, Qt::LeftButton,
                Qt::LeftButton);
    sendMouseTo(mini, QEvent::MouseMove,
                (escTip + QPointF(50.0, 40.0)).toPoint(), Qt::NoButton,
                Qt::LeftButton);
    if (!check(mini->isDragging(), QStringLiteral("drag did not start")))
        return 1;
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(mini, &escape);
    QApplication::processEvents();
    const QPointF afterEscVelocity =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    if (!check(!mini->isDragging(), QStringLiteral("Esc did not end the drag"))
        || !check(QLineF(beforeEscVelocity, afterEscVelocity).length() < 1e-6,
                  QStringLiteral("Esc did not restore initialVelocity"))
        || !check(undoDepth() == beforeEsc,
                  QStringLiteral("Esc created an undo entry")))
        return 1;

    // The viewport no longer draws an overlay or grabs the velocity handle:
    // dragging the solver-driven ball writes nothing, and its velocity is
    // untouched.
    const QPointF pivot = viewport->projectToWidgetForTest(QPointF(0.0, 200.0));
    const auto sendMouse = [viewport](QEvent::Type type, const QPointF& pos,
                                      Qt::MouseButton button,
                                      Qt::MouseButtons buttons) {
        QMouseEvent event(type, pos, viewport->mapToGlobal(pos.toPoint()),
                          button, buttons, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    };
    const int beforeViewportDrag = undoDepth();
    sendMouse(QEvent::MouseButtonPress, pivot, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseMove, pivot + QPointF(60.0, -40.0), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, pivot + QPointF(60.0, -40.0),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    flush();
    const QPointF afterViewportDrag =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    if (!check(!viewport->sessionSolverPreviewVisibleForTest(),
               QStringLiteral("viewport still exposes a solver overlay"))
        || !check(QLineF(afterEscVelocity, afterViewportDrag).length() < 1e-6,
                  QStringLiteral("viewport drag wrote the solver velocity"))
        || !check(undoDepth() == beforeViewportDrag,
                  QStringLiteral("viewport wrote a solver-driven transform")))
        return 1;

    // The entity gizmo still works on an entity the solver does not drive.
    document->setSelection(ground);
    live->setPlayhead(0);
    QApplication::processEvents();
    const QPointF groundCenter =
        viewport->projectToWidgetForTest(QPointF(0.0, -100.0));
    sendMouse(QEvent::MouseButtonPress, groundCenter, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseMove, groundCenter + QPointF(40.0, 25.0),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, groundCenter + QPointF(40.0, 25.0),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    flush();
    if (!check(undoDepth() == beforeViewportDrag + 1,
               QStringLiteral("viewport gizmo no longer commits moves")))
        return 1;

    // Screenshot: mini window inside the expanded solver section.
    // Screenshot state: a readable arc so the target, the solver point, the
    // path and the velocity arrow are all visible at once.
    {
        pvf::model::Project& shotProject = live->mutableProject();
        auto shotEntity = shotProject.entities.find(ball);
        if (shotEntity != shotProject.entities.end()
            && !shotEntity->solvers.isEmpty())
        {
            shotEntity->solvers.first().initialVelocity = QPointF(150.0, -200.0);
            shotEntity->solvers.first().params.insert(
                QStringLiteral("strength"), 160.0);
        }
        document->commitEntity(ball, QStringLiteral("solver-mini screenshot"),
                               &error);
    }
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
    {
        // Find a scroll offset that shows the whole mini window.
        if (auto* scroll = objectTab->verticalScrollBar())
        {
            const QWidget* view = objectTab->viewport();
            for (int value = 0; value <= scroll->maximum(); value += 8)
            {
                scroll->setValue(value);
                QApplication::processEvents();
                const QRect box(mini->mapTo(view, QPoint(0, 0)), mini->size());
                if (view->rect().contains(box))
                    break;
            }
        }
    }
    QApplication::processEvents();
    mini->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-solver-mini.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "solver-mini-preview-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "solver-mini-preview-test OK: solver-section widget"
            << mini->width() << "x" << mini->height()
            << "target/path/arrow only, arrow drag one commit, Esc cancel,"
               " selection rebind, no viewport overlay, screenshot"
            << screenshot;
    return 0;
}

int solverConvertTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver Convert");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-convert.pvproj"));
    auto session = pvf::EditorSession::createProject(projectDir, settings,
                                                     &error);
    if (!session)
    {
        qCritical() << "solver-convert-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("地面"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 600.0;
    project.entities[ground].content.vector.height = 60.0;
    project.entities[ground].physicsInteract = true;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("落球"), 1, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    // A last key on a non-driven track (opacity) exercises conversion without
    // requiring an x/y key at the solver start frame.
    const pvf::Uuid marker =
        makeEntity(QStringLiteral("淡出块"), 2, pvf::ContentKind::Vector);
    pvf::model::Track markerOpacity;
    markerOpacity.keys = {{10, 0.5, {}, {}}, {20, 1.0, {}, {}}};
    project.entities[marker].tracks.insert(QStringLiteral("opacity"),
                                           markerOpacity);

    const QVector<pvf::Uuid> setup = {ground, ball, marker};
    if (!session->document()->commitEntities(setup,
                                             QStringLiteral("solver-convert setup"),
                                             &error))
    {
        qCritical() << "solver-convert-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !panel || !timeline)
    {
        qCritical() << "solver-convert-test: window parts missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "solver-convert-test: panel edit failed"
                                     << message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-convert-test:" << message;
        return condition;
    };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "solver-convert-test: flush failed" << flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };

    auto* solverSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("solverSection"));
    if (solverSection)
        solverSection->setExpanded(true);
    auto* keySection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("keySection"));
    if (keySection)
        keySection->setExpanded(true);
    QApplication::processEvents();

    auto* convertButton =
        panel->findChild<QPushButton*>(QStringLiteral("solverConvertButton"));
    auto* convertHint =
        panel->findChild<QLabel*>(QStringLiteral("solverConvertHint"));
    auto* effectCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverEffectCombo"));
    auto* targetCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverTargetCombo"));
    auto* createEnd =
        panel->findChild<QSpinBox*>(QStringLiteral("solverCreateEnd"));
    auto* createManual =
        panel->findChild<QCheckBox*>(QStringLiteral("solverCreateManualEnd"));
    auto* createAuto =
        panel->findChild<QLabel*>(QStringLiteral("solverCreateEndAuto"));
    auto* createButton =
        panel->findChild<QPushButton*>(QStringLiteral("solverCreateButton"));
    if (!check(convertButton && convertHint && effectCombo && targetCombo
                   && createEnd && createManual && createAuto && createButton,
               QStringLiteral("convert/create widgets missing")))
        return 1;

    // Property no longer requires an end frame.
    if (!check(!createEnd->isVisible(),
               QStringLiteral("create end spin is visible by default"))
        || !check(!createManual->isChecked(),
                  QStringLiteral("manual end is checked by default"))
        || !check(createAuto->text().contains(QStringLiteral("自动")),
                  QStringLiteral("automatic end explanation missing")))
        return 1;

    // Non-last key: conversion is disabled with the expected hint.
    document->setSelection(marker);
    live->setPlayhead(10);
    QApplication::processEvents();
    if (!check(!convertButton->isEnabled(),
               QStringLiteral("non-last key conversion is enabled"))
        || !check(convertHint->text().contains(QStringLiteral("后面还有关键帧")),
                  QStringLiteral("non-last hint missing: %1")
                      .arg(convertHint->text()))
        || !check(!createButton->isEnabled(),
                  QStringLiteral("solver create button ignores non-last key")))
        return 1;

    // The same entity's last key (opacity only) can be converted.
    live->setPlayhead(20);
    QApplication::processEvents();
    if (!check(convertButton->isEnabled(),
               QStringLiteral("last key conversion is disabled")))
        return 1;

    // Gravity conversion at frame 0: one commit, automatic end.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    effectCombo->setCurrentIndex(
        effectCombo->findData(int(pvf::SolverEffect::Gravity)));
    targetCombo->setCurrentIndex(targetCombo->findData(ground));
    const int beforeCreate = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();

    const auto& ballAfterCreate = live->project().entities.value(ball);
    if (!check(ballAfterCreate.solvers.size() == 1,
               QStringLiteral("gravity solver was not created"))
        || !check(ballAfterCreate.solvers.first().effect
                      == pvf::SolverEffect::Gravity
                  && ballAfterCreate.solvers.first().startFrame == 0
                  && ballAfterCreate.solvers.first().endFrame == -1,
                  QStringLiteral("converted solver defaults wrong"))
        || !check(ballAfterCreate.solvers.first()
                          .params.value(QStringLiteral("targetUuid"))
                          .toString()
                      == ground,
                  QStringLiteral("converted solver target wrong"))
        || !check(undoDepth() == beforeCreate + 1,
                  QStringLiteral("conversion was not one commit")))
        return 1;
    const pvf::Uuid gravityId = ballAfterCreate.solvers.first().id;
    if (!check(pvf::model::solverStructuralEnd(
                   ballAfterCreate, ballAfterCreate.solvers.first())
                   == 59,
               QStringLiteral("automatic structural end is not the clip end")))
        return 1;

    auto* endInfo =
        panel->findChild<QLabel*>(QStringLiteral("solverEndInfo0"));
    if (!check(endInfo && endInfo->text().contains(QStringLiteral("自动")),
               QStringLiteral("solver card does not show automatic end: %1")
                   .arg(endInfo ? endInfo->text() : QString())))
        return 1;

    // One commit is also one undo step.
    if (!document->undo(&error))
    {
        qCritical() << "solver-convert-test: undo failed" << error;
        return 1;
    }
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.isEmpty(),
               QStringLiteral("undo did not remove the solver")))
        return 1;
    if (!document->redo(&error))
    {
        qCritical() << "solver-convert-test: redo failed" << error;
        return 1;
    }
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.size() == 1,
               QStringLiteral("redo did not restore the solver")))
        return 1;

    // The first key created after the solver start is the end marker: the
    // structural end becomes N-1 and the key at N is ordinary animation.
    if (!check(live->editActions()->addKeyframe(ball, QStringLiteral("x"), 30,
                                                &error),
               QStringLiteral("could not add the end-marker key: %1").arg(error)))
        return 1;
    flush();
    {
        const auto& entity = live->project().entities.value(ball);
        if (!check(pvf::model::solverStructuralEnd(entity,
                                                   entity.solvers.first())
                       == 29,
                   QStringLiteral("end-marker key did not derive end N-1"))
            || !check(pvf::anim::solverAt(entity, 29) != nullptr,
                      QStringLiteral("solver is not active at N-1"))
            || !check(pvf::anim::solverAt(entity, 30) == nullptr,
                      QStringLiteral("solver still active at the end key"))
            || !check(pvf::model::validateEntity(entity, &error),
                      QStringLiteral("entity invalid after end key: %1")
                          .arg(error)))
            return 1;
    }

    // Deleting the end marker restores the automatic end.
    if (!check(live->editActions()->deleteKeyframes(
                   {pvf::KeyRef{ball, QStringLiteral("x"), 30}}, &error),
               QStringLiteral("could not delete the end-marker key: %1")
                   .arg(error)))
        return 1;
    flush();
    {
        const auto& entity = live->project().entities.value(ball);
        if (!check(pvf::model::solverStructuralEnd(entity,
                                                   entity.solvers.first())
                       == 59,
                   QStringLiteral("deleting the end key did not restore auto"))
            || !check(pvf::anim::solverAt(entity, 59) != nullptr,
                      QStringLiteral("restored auto solver does not reach clip end")))
            return 1;
    }

    // A later last key can become a second segment in the same clip.
    if (!check(live->editActions()->addKeyframe(ball, QStringLiteral("x"), 40,
                                                &error),
               QStringLiteral("could not add the second start key: %1")
                   .arg(error)))
        return 1;
    live->setPlayhead(40);
    QApplication::processEvents();
    if (!check(convertButton->isEnabled(),
               QStringLiteral("second segment conversion is disabled")))
        return 1;
    const int beforeSecond = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();
    {
        const auto& entity = live->project().entities.value(ball);
        if (!check(entity.solvers.size() == 2,
                   QStringLiteral("second solver was not created"))
            || !check(entity.solvers.at(1).startFrame == 40
                      && entity.solvers.at(1).endFrame == -1,
                      QStringLiteral("second solver range wrong"))
            || !check(pvf::model::solverStructuralEnd(entity,
                                                       entity.solvers.at(0))
                          == 39,
                      QStringLiteral("first segment did not end before second"))
            || !check(pvf::model::solverStructuralEnd(entity,
                                                       entity.solvers.at(1))
                          == 59,
                      QStringLiteral("second segment structural end wrong"))
            || !check(undoDepth() == beforeSecond + 1,
                      QStringLiteral("second conversion was not one commit")))
            return 1;
    }

    // Any track's last key works: opacity-only key converted to spin.
    document->setSelection(marker);
    live->setPlayhead(20);
    QApplication::processEvents();
    effectCombo->setCurrentIndex(
        effectCombo->findData(int(pvf::SolverEffect::Spin)));
    const int beforeMarker = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();
    {
        const auto& entity = live->project().entities.value(marker);
        if (!check(entity.solvers.size() == 1,
                   QStringLiteral("non-driven-track conversion failed"))
            || !check(entity.solvers.first().effect == pvf::SolverEffect::Spin
                      && entity.solvers.first().startFrame == 20
                      && entity.solvers.first().endFrame == -1,
                      QStringLiteral("non-driven-track solver range wrong"))
            || !check(undoDepth() == beforeMarker + 1,
                      QStringLiteral("non-driven-track conversion was not one commit"))
            || !check(!createEnd->isVisible(),
                      QStringLiteral("conversion still exposes an end-frame spin")))
            return 1;
    }

    // Timeline band uses the physical stop clipped by the derived end.
    const int bandStop = timeline->solverBandStopFrameForTest(ball, gravityId);
    if (!check(bandStop >= 0 && bandStop <= 39,
               QStringLiteral("timeline band stop is not clipped: %1")
                   .arg(bandStop)))
        return 1;

    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
        objectTab->verticalScrollBar()->setValue(
            objectTab->verticalScrollBar()->maximum());
    document->setSelection(ball);
    live->setPlayhead(40);
    QApplication::processEvents();
    window->viewportForTest()->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-solver-convert.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "solver-convert-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "solver-convert-test OK: last/non-last key, any track,"
               "N-1 end marker, delete restores auto, multi-segment,"
               "one commit/undo, automatic end UI, screenshot"
            << screenshot;
    return 0;
}

int keyframeEasingGroupTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir = QDir(temp.path()).filePath(
        QStringLiteral("keyframe-easing-group.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("整体缓动");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "keyframe-easing-group-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid text =
        makeEntity(QStringLiteral("整体缓动"), 0, pvf::ContentKind::Text);
    project.entities[text].content.text.text = QStringLiteral("PVForge");
    project.entities[text].content.text.pixelSize = 96;
    for (const QString& track : {QStringLiteral("x"), QStringLiteral("y"),
                                 QStringLiteral("rotation")})
    {
        pvf::model::Track data;
        data.keys = {{0, 0.0, {}, {}}, {10, 20.0, {}, {}}};
        project.entities[text].tracks.insert(track, data);
    }

    const pvf::Uuid vector =
        makeEntity(QStringLiteral("矢量"), 1, pvf::ContentKind::Vector);
    project.entities[vector].content.vector.width = 400.0;
    project.entities[vector].content.vector.height = 300.0;

    const pvf::Uuid camera =
        makeEntity(QStringLiteral("摄像机"), 2, pvf::ContentKind::Camera);
    project.entities[camera].transform.scaleX = 0.5;
    project.entities[camera].transform.scaleY = 0.5;

    if (!session->document()->commitEntities({text, vector, camera},
                                             QStringLiteral("easing group setup"),
                                             &error))
    {
        qCritical() << "keyframe-easing-group-test: setup commit failed" << error;
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
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* objectTab = window->findChild<QScrollArea*>(QStringLiteral("objectTab"));
    if (!live || !document || !panel || !objectTab)
    {
        qCritical() << "keyframe-easing-group-test: session window/panel missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "keyframe-easing-group-test: panel edit failed"
                                     << message;
                     });

    bool failed = false;
    auto check = [&failed](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "keyframe-easing-group-test:" << message;
            failed = true;
        }
        return condition;
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "keyframe-easing-group-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto trackEasing = [&](const QString& track) {
        const pvf::model::Entity entity =
            live->project().entities.value(text);
        const pvf::model::Track data = entity.tracks.value(track);
        for (const pvf::model::Keyframe& key : data.keys)
            if (key.frame == 10)
                return key;
        return pvf::model::Keyframe();
    };
    auto onFirstScreen = [objectTab](QWidget* widget) {
        if (!objectTab || !widget || !widget->isVisible())
            return false;
        const QPoint pos = widget->mapTo(objectTab->viewport(), QPoint(0, 0));
        return objectTab->viewport()->rect().contains(
            QRect(pos, widget->size()));
    };
    auto setKeySection = [panel](bool expanded) {
        for (auto* section : panel->findChildren<pvf::CollapsibleSection*>())
            if (section->title() == QStringLiteral("关键帧"))
                section->setExpanded(expanded);
        QApplication::processEvents();
    };

    document->setSelection(text);
    live->setPlayhead(10);
    QApplication::processEvents();
    objectTab->verticalScrollBar()->setValue(0);
    QApplication::processEvents();

    // #50: the text content page must be reachable without scrolling in an
    // 800px window; the entity/keyframe sections start collapsed.
    auto* textContent =
        panel->findChild<QLineEdit*>(QStringLiteral("textContent"));
    auto* textSize = panel->findChild<QSpinBox*>(QStringLiteral("textSize"));
    check(textContent && textSize && onFirstScreen(textContent)
              && onFirstScreen(textSize),
          QStringLiteral("text content/font/size are not on the first screen"));

    setKeySection(true);
    auto* easing =
        panel->findChild<QComboBox*>(QStringLiteral("keyframeEasingGroup"));
    if (!check(easing, QStringLiteral("group easing combo missing")))
        return 1;
    if (!check(easing->count() == 33,
               QStringLiteral("group easing combo has %1 items")
                   .arg(easing->count())))
        return 1;
    if (!check(easing->findData(QStringLiteral("bezier")) >= 0,
               QStringLiteral("custom bezier entry missing")))
        return 1;
    if (!check(easing->isVisible(),
               QStringLiteral("group easing combo is not visible")))
        return 1;
    bool perTrackEasing = false;
    for (auto* combo : panel->findChildren<QComboBox*>())
        if (combo->objectName().startsWith(QStringLiteral("trackEasing_")))
            perTrackEasing = true;
    if (!check(!perTrackEasing,
               QStringLiteral("per-track easing combo still exists")))
        return 1;

    // One change applies to x, y and rotation at the same frame.
    const int before = depth();
    easing->setCurrentIndex(
        easing->findData(QStringLiteral("easeinoutcubic")));
    QApplication::processEvents();
    flush();
    if (!check(trackEasing(QStringLiteral("x")).easing.type
                       == QStringLiteral("easeinoutcubic")
                   && trackEasing(QStringLiteral("y")).easing.type
                          == QStringLiteral("easeinoutcubic")
                   && trackEasing(QStringLiteral("rotation")).easing.type
                          == QStringLiteral("easeinoutcubic"),
               QStringLiteral("group easing did not reach every track")))
        return 1;
    if (!check(depth() == before + 1,
               QStringLiteral("group easing was not one commit")))
        return 1;
    if (!check(trackEasing(QStringLiteral("x")).easing.type
                   == QStringLiteral("easeinoutcubic"),
               QStringLiteral("group easing was not written at frame 10")))
        return 1;
    {
        const pvf::model::Entity entity =
            live->project().entities.value(text);
        for (const pvf::model::Keyframe& key :
             entity.tracks.value(QStringLiteral("x")).keys)
            if (key.frame == 0)
                check(key.easing.type == QStringLiteral("linear"),
                      QStringLiteral("group easing touched frame 0"));
    }

    // Custom bezier still goes through the same merged group.
    const int bezierBefore = depth();
    QTimer dialogTimer;
    dialogTimer.setInterval(10);
    QObject::connect(&dialogTimer, &QTimer::timeout, [&dialogTimer] {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog && dialog->isVisible())
            {
                dialog->accept();
                dialogTimer.stop();
                return;
            }
        }
    });
    QTimer::singleShot(5000, &dialogTimer, [&dialogTimer] {
        dialogTimer.stop();
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget);
                dialog && dialog->isVisible())
                dialog->reject();
    });
    dialogTimer.start();
    easing->setCurrentIndex(easing->findData(QStringLiteral("bezier")));
    dialogTimer.stop();
    QApplication::processEvents();
    flush();
    if (!check(trackEasing(QStringLiteral("x")).easing.type
                       == QStringLiteral("bezier")
                   && trackEasing(QStringLiteral("y")).easing.type
                          == QStringLiteral("bezier")
                   && trackEasing(QStringLiteral("rotation")).easing.type
                          == QStringLiteral("bezier"),
               QStringLiteral("custom bezier did not reach every track")))
        return 1;
    if (!check(depth() == bezierBefore + 1,
               QStringLiteral("custom bezier was not one commit")))
        return 1;

    // The first keyframe owns no entering segment, matching the old button.
    live->setPlayhead(0);
    QApplication::processEvents();
    if (!check(!easing->isEnabled() && easing->currentIndex() == -1,
               QStringLiteral("first frame should not own an entering segment")))
        return 1;
    live->setPlayhead(10);
    QApplication::processEvents();
    if (!check(easing->isEnabled(),
               QStringLiteral("group easing stayed disabled at frame 10")))
        return 1;

    // #50: vector content and the camera page have no dangling row diamonds.
    setKeySection(false);
    document->setSelection(vector);
    QApplication::processEvents();
    objectTab->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    auto* vectorShape =
        panel->findChild<QComboBox*>(QStringLiteral("vectorShape"));
    auto* vectorCorner =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("vectorCorner"));
    check(vectorShape && vectorCorner && onFirstScreen(vectorShape)
              && onFirstScreen(vectorCorner),
          QStringLiteral("vector content is not on the first screen"));

    document->setSelection(camera);
    QApplication::processEvents();
    auto* keyScaleY =
        panel->findChild<QToolButton*>(QStringLiteral("key_scaleY"));
    auto* keyOpacity =
        panel->findChild<QToolButton*>(QStringLiteral("key_opacity"));
    check(keyScaleY && keyOpacity && !keyScaleY->isVisible()
              && !keyOpacity->isVisible(),
          QStringLiteral("camera still has dangling key diamonds"));

    // Screenshot: text selected, group easing visible in the keyframe section.
    document->setSelection(text);
    live->setPlayhead(10);
    QApplication::processEvents();
    setKeySection(true);
    easing->setCurrentIndex(
        easing->findData(QStringLiteral("easeinoutcubic")));
    QApplication::processEvents();
    objectTab->ensureWidgetVisible(easing, 20, 80);
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-easing-group.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "keyframe-easing-group-test: screenshot failed"
                    << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    if (failed)
        return 1;
    qInfo() << "keyframe-easing-group-test OK: one group easing, 33 entries, "
               "bezier, one commit, screenshot"
            << screenshot;
    return 0;
}

int propertiesSpecialTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p4b-special.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P4b 特殊内容");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "properties-special-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 120;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid comic =
        makeEntity(QStringLiteral("漫画文字"), 0, pvf::ContentKind::ComicText);
    project.entities[comic].content.comic.text = QStringLiteral("台词");
    project.entities[comic].content.comic.pixelSize = 72;
    project.entities[comic].content.comic.boxWidth = 480.0;
    project.entities[comic].content.comic.boxHeight = 260.0;

    const pvf::Uuid radial =
        makeEntity(QStringLiteral("径向线"), 1, pvf::ContentKind::RadialLines);
    project.entities[radial].content.radial.count = 56;
    project.entities[radial].content.radial.radius = 420.0;

    const pvf::Uuid flow =
        makeEntity(QStringLiteral("流线"), 2, pvf::ContentKind::FlowLines);
    project.entities[flow].content.flow.count = 26;
    project.entities[flow].content.flow.length = 420.0;

    const pvf::Uuid halftone =
        makeEntity(QStringLiteral("点阵"), 3, pvf::ContentKind::HalftoneDots);
    project.entities[halftone].content.halftone.layout = QStringLiteral("grid");
    project.entities[halftone].content.halftone.area = 900.0;

    const pvf::Uuid wall =
        makeEntity(QStringLiteral("品牌墙"), 4, pvf::ContentKind::BrandWall);
    project.entities[wall].content.brandWall.logos = {QStringLiteral("A"),
                                                      QStringLiteral("B")};

    const pvf::Uuid text =
        makeEntity(QStringLiteral("文字"), 5, pvf::ContentKind::Text);
    project.entities[text].content.text.text = QStringLiteral("PVForge");

    const QVector<pvf::Uuid> all = {comic, radial, flow, halftone, wall, text};
    if (!session->document()->commitEntities(all, QStringLiteral("p4b setup"),
                                             &error))
    {
        qCritical() << "properties-special-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    if (!live || !document || !panel || !panel->sessionMode())
    {
        qCritical() << "properties-special-test: session window/panel missing";
        return 1;
    }

    QString lastError;
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [&lastError](const QString& message) {
                         lastError = message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "properties-special-test:" << message;
        return condition;
    };
    auto entity = [live](const pvf::Uuid& uuid) {
        return live->project().entities.value(uuid);
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "properties-special-test: flush failed" << flushError;
    };
    auto select = [&](const pvf::Uuid& uuid) {
        document->setSelection(uuid);
        QApplication::processEvents();
        QApplication::processEvents();
    };

    panel->setAutoKey(false);

    // Comic text page.
    select(comic);
    auto* comicPage = panel->findChild<QWidget*>(QStringLiteral("comicTextPage"));
    auto* comicSize =
        panel->findChild<QSpinBox*>(QStringLiteral("comicPixelSizeSpin"));
    auto* comicShape =
        panel->findChild<QComboBox*>(QStringLiteral("comicShapeCombo"));
    if (!check(comicPage && comicPage->isVisible(),
               QStringLiteral("comic page is not visible"))
        || !check(comicSize && comicSize->value() == 72,
                  QStringLiteral("comic page does not show pixelSize")))
        return 1;
    int before = depth();
    comicSize->setValue(88);
    QMetaObject::invokeMethod(comicSize, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(entity(comic).content.comic.pixelSize == 88,
               QStringLiteral("comic pixelSize did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("comic edit was not one commit")))
        return 1;
    before = depth();
    comicShape->setCurrentIndex(comicShape->findData(QStringLiteral("burst")));
    QApplication::processEvents();
    flush();
    if (!check(entity(comic).content.comic.shape == QStringLiteral("burst"),
               QStringLiteral("comic shape did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("comic shape edit was not one commit")))
        return 1;

    // Radial lines page.
    select(radial);
    auto* radialPage =
        panel->findChild<QWidget*>(QStringLiteral("radialLinesPage"));
    auto* radialCount =
        panel->findChild<QSpinBox*>(QStringLiteral("radialCountSpin"));
    if (!check(radialPage && radialPage->isVisible(),
               QStringLiteral("radial page is not visible"))
        || !check(radialCount && radialCount->value() == 56,
                  QStringLiteral("radial page does not show count")))
        return 1;
    before = depth();
    radialCount->setValue(12);
    QMetaObject::invokeMethod(radialCount, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(entity(radial).content.radial.count == 12,
               QStringLiteral("radial count did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("radial edit was not one commit")))
        return 1;

    // Flow lines page.
    select(flow);
    auto* flowPage = panel->findChild<QWidget*>(QStringLiteral("flowLinesPage"));
    auto* flowLength =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("flowLengthSpin"));
    if (!check(flowPage && flowPage->isVisible(),
               QStringLiteral("flow page is not visible"))
        || !check(flowLength && std::abs(flowLength->value() - 420.0) < 1e-6,
                  QStringLiteral("flow page does not show length")))
        return 1;
    before = depth();
    flowLength->setValue(500.0);
    QMetaObject::invokeMethod(flowLength, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(flow).content.flow.length - 500.0) < 1e-6,
               QStringLiteral("flow length did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("flow edit was not one commit")))
        return 1;

    // Halftone page.
    select(halftone);
    auto* halftonePage =
        panel->findChild<QWidget*>(QStringLiteral("halftoneDotsPage"));
    auto* halftoneLayout =
        panel->findChild<QComboBox*>(QStringLiteral("halftoneLayoutCombo"));
    auto* halftoneDot =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("halftoneDotSizeSpin"));
    if (!check(halftonePage && halftonePage->isVisible(),
               QStringLiteral("halftone page is not visible"))
        || !check(halftoneLayout && halftoneDot,
                  QStringLiteral("halftone widgets missing")))
        return 1;
    before = depth();
    halftoneLayout->setCurrentIndex(
        halftoneLayout->findData(QStringLiteral("radial")));
    QApplication::processEvents();
    flush();
    if (!check(entity(halftone).content.halftone.layout
                   == QStringLiteral("radial"),
               QStringLiteral("halftone layout did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("halftone layout was not one commit")))
        return 1;
    before = depth();
    halftoneDot->setValue(9.5);
    QMetaObject::invokeMethod(halftoneDot, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(std::abs(entity(halftone).content.halftone.dotSize - 9.5) < 1e-6,
               QStringLiteral("halftone dotSize did not reach the model")))
        return 1;

    // Brand wall page and empty-logo validation.
    select(wall);
    auto* wallPage =
        panel->findChild<QWidget*>(QStringLiteral("brandWallPage"));
    auto* wallLogos =
        panel->findChild<QPlainTextEdit*>(QStringLiteral("brandLogosEdit"));
    auto* wallColumns =
        panel->findChild<QSpinBox*>(QStringLiteral("brandColumnsSpin"));
    if (!check(wallPage && wallPage->isVisible(),
               QStringLiteral("brand wall page is not visible"))
        || !check(wallLogos && wallColumns,
                  QStringLiteral("brand wall widgets missing")))
        return 1;
    before = depth();
    wallLogos->setPlainText(QStringLiteral("ACME\nPVForge"));
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(wallLogos, &focusOut);
    QApplication::processEvents();
    flush();
    if (!check(entity(wall).content.brandWall.logos
                   == QStringList({QStringLiteral("ACME"),
                                   QStringLiteral("PVForge")}),
               QStringLiteral("brand wall labels did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("brand wall label edit was not one commit")))
        return 1;
    before = depth();
    wallColumns->setValue(4);
    QMetaObject::invokeMethod(wallColumns, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(entity(wall).content.brandWall.columns == 4,
               QStringLiteral("brand wall columns did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("brand wall columns edit was not one commit")))
        return 1;

    lastError.clear();
    before = depth();
    wallLogos->setPlainText(QString());
    QApplication::sendEvent(wallLogos, &focusOut);
    QApplication::processEvents();
    flush();
    if (!check(!entity(wall).content.brandWall.logos.isEmpty(),
               QStringLiteral("empty brand wall labels were committed"))
        || !check(depth() == before,
                  QStringLiteral("rejected brand wall edit added a commit"))
        || !check(lastError.contains(QStringLiteral("标签")),
                  QStringLiteral("brand wall rejection did not report an error")))
        return 1;

    // Range validation: a zero count must be rejected by the model.
    select(radial);
    lastError.clear();
    before = depth();
    const int radialBefore = entity(radial).content.radial.count;
    radialCount->setValue(0);
    QMetaObject::invokeMethod(radialCount, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(entity(radial).content.radial.count == radialBefore,
               QStringLiteral("out-of-range radial count was committed"))
        || !check(depth() == before,
                  QStringLiteral("rejected radial edit added a commit"))
        || !check(lastError.contains(QStringLiteral("count")),
                  QStringLiteral("radial rejection did not report an error")))
        return 1;

    // The special page only appears for the five special kinds.
    select(text);
    auto* specialSection =
        panel->findChild<QWidget*>(QStringLiteral("specialContentSection"));
    if (!check(specialSection && !specialSection->isVisible(),
               QStringLiteral("special section is visible for text")))
        return 1;

    // Restore labels for the screenshot, select the wall and show the page.
    select(wall);
    wallLogos->setPlainText(QStringLiteral("ACME\nPVForge"));
    QApplication::sendEvent(wallLogos, &focusOut);
    QApplication::processEvents();
    flush();
    live->setPlayhead(15);
    QApplication::processEvents();
    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
    {
        if (auto* section =
                panel->findChild<QWidget*>(QStringLiteral("specialContentSection")))
        {
            const int y = section->mapTo(panel, QPoint(0, 0)).y();
            objectTab->verticalScrollBar()->setValue(
                std::clamp(y - 4, 0, objectTab->verticalScrollBar()->maximum()));
        }
    }
    window->statusBar()->clearMessage();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-p4b-special.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "properties-special-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "properties-special-test OK: comic/radial/flow/halftone/brandWall,"
               " screenshot"
            << screenshot;
    return 0;
}

int htmlVarsTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    QTemporaryDir htmlSource;
    if (!temp.isValid() || !htmlSource.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p4b-html.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P4b HTML");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "html-vars-test: create failed" << error;
        return 1;
    }

    {
        QFile index(QDir(htmlSource.path()).filePath(QStringLiteral("index.html")));
        if (!index.open(QIODevice::WriteOnly))
            return 1;
        index.write("<!doctype html><html><body>PVForge</body></html>");
    }
    {
        QJsonObject variables;
        variables.insert(QStringLiteral("speed"),
                         QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                     {QStringLiteral("default"), 2.5},
                                     {QStringLiteral("min"), 0.0},
                                     {QStringLiteral("max"), 10.0}});
        variables.insert(QStringLiteral("count"),
                         QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                     {QStringLiteral("default"), 3.0},
                                     {QStringLiteral("min"), 0.0},
                                     {QStringLiteral("max"), 100.0},
                                     {QStringLiteral("integer"), true}});
        variables.insert(QStringLiteral("label"), QStringLiteral("hello"));
        variables.insert(QStringLiteral("flag"), true);
        QJsonObject manifest;
        manifest.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
        manifest.insert(QStringLiteral("logicalSize"), QJsonArray{800, 120});
        manifest.insert(QStringLiteral("timeDependent"), true);
        manifest.insert(QStringLiteral("variables"), variables);
        QFile file(
            QDir(htmlSource.path()).filePath(QStringLiteral("pvhtml.json")));
        if (!file.open(QIODevice::WriteOnly))
            return 1;
        file.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    }

    const auto imported =
        session->assets().importPvhtml(htmlSource.path(), &error);
    if (!imported)
    {
        qCritical() << "html-vars-test: import failed" << error;
        return 1;
    }

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("HTML");
    entity.start = 0;
    entity.end = 120;
    entity.content.kind = pvf::ContentKind::Asset;
    entity.content.asset.type = pvf::AssetType::Html;
    entity.content.asset.hash = imported->hash;
    const pvf::Uuid htmlUuid = entity.uuid;
    session->mutableProject().entities.insert(htmlUuid, entity);
    if (!session->document()->commitEntity(htmlUuid,
                                           QStringLiteral("p4b html setup"),
                                           &error))
    {
        qCritical() << "html-vars-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    if (!live || !document || !panel)
    {
        qCritical() << "html-vars-test: session window/panel missing";
        return 1;
    }

    QString lastError;
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [&lastError](const QString& message) {
                         lastError = message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "html-vars-test:" << message;
        return condition;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "html-vars-test: flush failed" << flushError;
    };

    document->setSelection(htmlUuid);
    live->setPlayhead(0);
    QApplication::processEvents();
    QApplication::processEvents();

    auto* htmlSection =
        panel->findChild<QWidget*>(QStringLiteral("sessionHtmlSection"));
    auto* htmlInfo =
        panel->findChild<QLabel*>(QStringLiteral("sessionHtmlInfo"));
    auto* speedSpin = panel->findChild<QDoubleSpinBox*>(
        QStringLiteral("sessionHtmlVarValue_var.speed"));
    auto* countSpin = panel->findChild<QDoubleSpinBox*>(
        QStringLiteral("sessionHtmlVarValue_var.count"));
    auto* speedKey = panel->findChild<QToolButton*>(
        QStringLiteral("sessionHtmlVarDiamond_var.speed"));
    auto* labelValue = panel->findChild<QLabel*>(
        QStringLiteral("sessionHtmlVarValue_var.label"));
    auto* flagValue = panel->findChild<QLabel*>(
        QStringLiteral("sessionHtmlVarValue_var.flag"));
    if (!check(htmlSection && htmlSection->isVisible(),
               QStringLiteral("HTML variables section is not visible"))
        || !check(htmlInfo && htmlInfo->text().contains(QStringLiteral("是")),
                  QStringLiteral("timeDependent state missing"))
        || !check(speedSpin && std::abs(speedSpin->value() - 2.5) < 1e-6,
                  QStringLiteral("numeric default is not shown"))
        || !check(countSpin && countSpin->decimals() == 0,
                  QStringLiteral("integer variable decimals wrong"))
        || !check(labelValue && labelValue->text() == QStringLiteral("hello"),
                  QStringLiteral("string variable is not read-only/default"))
        || !check(flagValue && flagValue->text() == QStringLiteral("true"),
                  QStringLiteral("boolean variable is not read-only/default"))
        || !check(!panel->findChild<QDoubleSpinBox*>(
                      QStringLiteral("sessionHtmlVarValue_var.label")),
                  QStringLiteral("string variable got a spin box"))
        || !check(speedKey, QStringLiteral("variable key diamond missing")))
        return 1;

    int before = depth();
    speedKey->click();
    QApplication::processEvents();
    flush();
    {
        const auto track = live->project()
                               .entities.value(htmlUuid)
                               .tracks.value(QStringLiteral("var.speed"));
        bool found = false;
        for (const pvf::model::Keyframe& key : track.keys)
            if (key.frame == 0 && std::abs(key.value - 2.5) < 1e-6)
                found = true;
        if (!check(found, QStringLiteral("diamond did not key the default"))
            || !check(depth() == before + 1,
                      QStringLiteral("variable key was not one commit")))
            return 1;
    }

    live->setPlayhead(10);
    QApplication::processEvents();
    before = depth();
    speedSpin->setValue(4.2);
    QMetaObject::invokeMethod(speedSpin, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    {
        const auto track = live->project()
                               .entities.value(htmlUuid)
                               .tracks.value(QStringLiteral("var.speed"));
        bool found = false;
        for (const pvf::model::Keyframe& key : track.keys)
            if (key.frame == 10 && std::abs(key.value - 4.2) < 1e-6)
                found = true;
        if (!check(found, QStringLiteral("variable edit did not key frame 10"))
            || !check(depth() == before + 1,
                      QStringLiteral("variable edit was not one commit")))
            return 1;
    }

    panel->setAutoKey(false);
    live->setPlayhead(20);
    QApplication::processEvents();
    lastError.clear();
    before = depth();
    countSpin->setValue(9.0);
    QMetaObject::invokeMethod(countSpin, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    {
        const auto track = live->project()
                               .entities.value(htmlUuid)
                               .tracks.value(QStringLiteral("var.count"));
        bool found = false;
        for (const pvf::model::Keyframe& key : track.keys)
            if (key.frame == 20)
                found = true;
        if (!check(!found, QStringLiteral("auto-key off still wrote a key"))
            || !check(depth() == before,
                      QStringLiteral("rejected variable edit added a commit"))
            || !check(lastError.contains(QStringLiteral("HTML 变量")),
                      QStringLiteral("auto-key rejection did not report")))
            return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "html-vars-test OK: defaults/read-only/timeDependent/var.* keys";
    return 0;
}

int textLayoutTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p4b-text-layout.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P4b 文字排版");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "text-layout-test: create failed" << error;
        return 1;
    }

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("标题");
    entity.start = 0;
    entity.end = 120;
    entity.content.kind = pvf::ContentKind::Text;
    entity.content.text.text = QStringLiteral("PVForge");
    entity.content.text.pixelSize = 96;
    const pvf::Uuid textUuid = entity.uuid;
    session->mutableProject().entities.insert(textUuid, entity);
    if (!session->document()->commitEntity(textUuid,
                                           QStringLiteral("p4b text setup"),
                                           &error))
    {
        qCritical() << "text-layout-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    if (!live || !document || !panel)
    {
        qCritical() << "text-layout-test: session window/panel missing";
        return 1;
    }

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "text-layout-test:" << message;
        return condition;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "text-layout-test: flush failed" << flushError;
    };

    document->setSelection(textUuid);
    QApplication::processEvents();
    QApplication::processEvents();

    auto* layoutButton =
        panel->findChild<QPushButton*>(QStringLiteral("textLayoutButton"));
    if (!check(layoutButton && layoutButton->isVisible(),
               QStringLiteral("text layout button missing")))
        return 1;

    auto findDialog = []() -> QDialog* {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog && dialog->isVisible()
                && dialog->windowTitle() == QStringLiteral("文字排版"))
                return dialog;
        }
        return nullptr;
    };

    // Accept: the dialog edits a copy and the panel commits it once.
    QTimer::singleShot(50, [&] {
        QDialog* dialog = findDialog();
        if (!dialog)
        {
            qCritical() << "text-layout-test: accept dialog not found";
            return;
        }
        auto* size = dialog->findChild<QSpinBox*>();
        auto* spacing = dialog->findChild<QDoubleSpinBox*>(
            QStringLiteral("layoutLetterSpacing"));
        auto* preview =
            dialog->findChild<QWidget*>(QStringLiteral("layoutPreview"));
        if (size)
            size->setValue(123);
        if (spacing)
            spacing->setValue(4.0);
        if (preview && !preview->isVisible())
            qCritical() << "text-layout-test: preview is not visible";
        dialog->accept();
    });
    QTimer::singleShot(2000, [&] {
        if (QDialog* dialog = findDialog())
            dialog->reject();
    });
    int before = depth();
    layoutButton->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project()
                       .entities.value(textUuid)
                       .content.text.pixelSize
                   == 123,
               QStringLiteral("dialog size did not reach the model"))
        || !check(std::abs(live->project()
                               .entities.value(textUuid)
                               .content.text.letterSpacing
                           - 4.0)
                      < 1e-6,
                  QStringLiteral("dialog letter spacing did not reach the model"))
        || !check(depth() == before + 1,
                  QStringLiteral("dialog accept was not one commit")))
        return 1;

    // Cancel: edits are discarded and no commit is added.
    QTimer::singleShot(50, [&] {
        QDialog* dialog = findDialog();
        if (!dialog)
        {
            qCritical() << "text-layout-test: cancel dialog not found";
            return;
        }
        if (auto* size = dialog->findChild<QSpinBox*>())
            size->setValue(200);
        dialog->reject();
    });
    QTimer::singleShot(2000, [&] {
        if (QDialog* dialog = findDialog())
            dialog->reject();
    });
    before = depth();
    layoutButton->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project()
                       .entities.value(textUuid)
                       .content.text.pixelSize
                   == 123,
               QStringLiteral("cancel changed the model"))
        || !check(depth() == before,
                  QStringLiteral("cancel added a commit")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "text-layout-test OK: dialog write-back and cancel";
    return 0;
}

int brandWallCreateTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p4b-brandwall.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P4b 品牌墙");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "brandwall-create-test: create failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* workbench = window->findChild<pvf::WorkbenchPanel*>();
    auto* workbenchButton =
        window->findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    if (!live || !document || !workbench || !workbenchButton)
    {
        qCritical() << "brandwall-create-test: session/workbench missing";
        return 1;
    }

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "brandwall-create-test:" << message;
        return condition;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "brandwall-create-test: flush failed" << flushError;
    };

    workbenchButton->click();
    QApplication::processEvents();
    if (!check(workbench->isVisible(), QStringLiteral("workbench did not open")))
        return 1;

    auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
    auto* editWall =
        workbench->findChild<QPushButton*>(QStringLiteral("wallEditButton"));
    auto* create =
        workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
    int specialRow = -1;
    if (types)
    {
        for (int row = 0; row < types->count(); ++row)
            if (types->item(row)->text() == QStringLiteral("特殊"))
            {
                specialRow = row;
                break;
            }
    }
    if (!check(types && specialRow >= 0,
               QStringLiteral("session workbench has no 特殊 page"))
        || !check(editWall && create,
                  QStringLiteral("session wall widgets missing")))
        return 1;
    types->setCurrentRow(specialRow);
    QApplication::processEvents();

    QTimer::singleShot(50, [&] {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || !dialog->isVisible()
                || dialog->windowTitle()
                       != QStringLiteral("品牌瓷砖墙 · 文字标签"))
                continue;
            auto* logos = dialog->findChild<QPlainTextEdit*>(
                QStringLiteral("brandWallContentLogos"));
            auto* columns = dialog->findChild<QSpinBox*>(
                QStringLiteral("brandWallContentColumns"));
            auto* rows = dialog->findChild<QSpinBox*>(
                QStringLiteral("brandWallContentRows"));
            auto* spacing = dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("brandWallContentSpacing"));
            auto* logoSize = dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("brandWallContentLogoSize"));
            auto* colors = dialog->findChild<QLineEdit*>(
                QStringLiteral("brandWallContentColors"));
            auto* alternate = dialog->findChild<QCheckBox*>(
                QStringLiteral("brandWallContentAlternate"));
            auto* angle = dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("brandWallContentAngle"));
            auto* background = dialog->findChild<QLineEdit*>(
                QStringLiteral("brandWallContentBackground"));
            auto* seed = dialog->findChild<QSpinBox*>(
                QStringLiteral("brandWallContentSeed"));
            auto* preview = dialog->findChild<QLabel*>(
                QStringLiteral("brandWallContentPreview"));
            if (logos)
                logos->setPlainText(QStringLiteral("ACME\nPVForge"));
            if (columns)
                columns->setValue(5);
            if (rows)
                rows->setValue(3);
            if (spacing)
                spacing->setValue(12.0);
            if (logoSize)
                logoSize->setValue(180.0);
            if (colors)
                colors->setText(QStringLiteral("#ff0000, #00ff00"));
            if (alternate)
                alternate->setChecked(false);
            if (angle)
                angle->setValue(15.0);
            if (background)
                background->setText(QStringLiteral("#202020"));
            if (seed)
                seed->setValue(7);
            if (preview && preview->pixmap().isNull())
                qCritical() << "brandwall-create-test: preview pixmap is empty";
            dialog->accept();
            return;
        }
        qCritical() << "brandwall-create-test: wall dialog not found";
    });
    QTimer::singleShot(2000, [&] {
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget);
                dialog && dialog->isVisible())
                dialog->reject();
    });
    editWall->click();
    QApplication::processEvents();

    if (!check(create->isEnabled(),
               QStringLiteral("create button disabled after configuring")))
        return 1;

    const int before = depth();
    create->click();
    QApplication::processEvents();
    flush();

    const auto& entities = live->project().entities;
    if (!check(entities.size() == 1,
               QStringLiteral("brand wall entity was not created")))
        return 1;
    const pvf::model::Entity created = entities.constBegin().value();
    if (!check(created.content.kind == pvf::ContentKind::BrandWall,
               QStringLiteral("created entity is not a brand wall"))
        || !check(created.content.brandWall.logos
                      == QStringList({QStringLiteral("ACME"),
                                      QStringLiteral("PVForge")}),
                  QStringLiteral("brand wall labels wrong"))
        || !check(created.content.brandWall.columns == 5
                      && created.content.brandWall.rows == 3,
                  QStringLiteral("brand wall grid wrong"))
        || !check(std::abs(created.content.brandWall.spacing - 12.0) < 1e-6
                      && std::abs(created.content.brandWall.logoSize - 180.0)
                             < 1e-6,
                  QStringLiteral("brand wall sizes wrong"))
        || !check(created.content.brandWall.colors
                      == QStringList({QStringLiteral("#ff0000"),
                                      QStringLiteral("#00ff00")}),
                  QStringLiteral("brand wall colors wrong"))
        || !check(!created.content.brandWall.alternate
                      && std::abs(created.content.brandWall.angle - 15.0) < 1e-6
                      && created.content.brandWall.background
                             == QStringLiteral("#202020")
                      && created.content.brandWall.seed == 7,
                  QStringLiteral("brand wall options wrong"))
        || !check(std::abs(created.transform.x - 320.0) < 1e-6
                      && std::abs(created.transform.y - 180.0) < 1e-6,
                  QStringLiteral("brand wall was not centred"))
        || !check(created.start == 0 && created.end == 30,
                  QStringLiteral("brand wall range wrong"))
        || !check(depth() == before + 1,
                  QStringLiteral("brand wall create was not one commit"))
        || !check(document->selection() == created.uuid,
                  QStringLiteral("new brand wall was not selected"))
        || !check(!workbench->isVisible(),
                  QStringLiteral("workbench stayed open after create")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "brandwall-create-test OK: workbench/BrandWallDialog ->"
               " BrandWallContent";
    return 0;
}

int timelineTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir = QDir(temp.path()).filePath(
        QStringLiteral("p2a-timeline.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2a 时间轴");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid camera = makeEntity(QStringLiteral("摄像机"), 0, 60, 0);
    project.entities[camera].content.kind = pvf::ContentKind::Camera;

    const pvf::Uuid title = makeEntity(QStringLiteral("标题"), 0, 90, 1);
    project.entities[title].content.kind = pvf::ContentKind::Text;
    project.entities[title].content.text.text = QStringLiteral("PVForge");

    const pvf::Uuid vector = makeEntity(QStringLiteral("矢量"), 10, 50, 2);
    project.entities[vector].content.kind = pvf::ContentKind::Vector;

    const pvf::Uuid image = makeEntity(QStringLiteral("图片"), 20, 40, 3);
    project.entities[image].content.kind = pvf::ContentKind::Asset;
    project.entities[image].content.asset.type = pvf::AssetType::Image;
    project.entities[image].content.asset.hash = QStringLiteral("imagehash");

    const pvf::Uuid video = makeEntity(QStringLiteral("视频"), 0, 60, 4);
    project.entities[video].content.kind = pvf::ContentKind::Asset;
    project.entities[video].content.asset.type = pvf::AssetType::Video;
    project.entities[video].content.asset.hash = QStringLiteral("videohash");

    const pvf::Uuid audio = makeEntity(QStringLiteral("音频"), 5, 65, 5);
    project.entities[audio].content.kind = pvf::ContentKind::Asset;
    project.entities[audio].content.asset.type = pvf::AssetType::Audio;
    project.entities[audio].content.asset.hash = QStringLiteral("audiohash");

    const pvf::Uuid html = makeEntity(QStringLiteral("HTML"), 15, 75, 6);
    project.entities[html].content.kind = pvf::ContentKind::Asset;
    project.entities[html].content.asset.type = pvf::AssetType::Html;
    project.entities[html].content.asset.hash = QStringLiteral("htmlhash");

    // A child of "标题" exercises the private timeline and depth reporting.
    pvf::model::Entity child;
    child.uuid = pvf::model::newUuid();
    child.name = QStringLiteral("标题子层");
    child.parentUuid = title;
    child.start = 10;
    child.end = 40;
    child.row = 0;
    child.content.kind = pvf::ContentKind::Text;
    project.entities.insert(child.uuid, child);

    // Keyframes are stored relative to entity.start and must be shown at
    // absolute frames by the read-only adapter.
    pvf::model::Track track;
    track.keys = {{0, 0.0, {}, {}}, {30, 1.0, {}, {}}};
    project.entities[title].tracks.insert(QStringLiteral("opacity"), track);

    pvf::model::Marker markerA;
    markerA.id = QStringLiteral("m1");
    markerA.frame = 20;
    markerA.title = QStringLiteral("开场");
    markerA.color = QStringLiteral("#ffbe3c");
    pvf::model::Marker markerB;
    markerB.id = QStringLiteral("m2");
    markerB.frame = 55;
    markerB.title = QStringLiteral("结尾");
    markerB.color = QStringLiteral("#ff5a5f");
    project.markers = {markerA, markerB};

    QVector<pvf::Uuid> uuids = {camera, title, vector, image, video, audio,
                                html, child.uuid};
    if (!session->document()->commitEntities(uuids,
                                             QStringLiteral("timeline-test"), &error)
        || !session->commitProject(QStringLiteral("timeline-test markers"),
                                   &error))
    {
        qCritical() << "timeline-test: commit failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "timeline-test: no timeline panel";
        return 1;
    }
    auto* sessionPtr = window->editorSessionForTest();
    if (!sessionPtr || !sessionPtr->document())
    {
        qCritical() << "timeline-test: window has no session";
        return 1;
    }
    if (timeline->readOnly())
    {
        qCritical() << "timeline-test: session timeline should be editable";
        return 1;
    }
    if (timeline->entityRowCountForTest() != 7)
    {
        qCritical() << "timeline-test: expected 7 rows, got"
                    << timeline->entityRowCountForTest()
                    << "rowCount" << timeline->rowCountForTest();
        return 1;
    }

    const QStringList expectedNames = {
        QStringLiteral("摄像机"), QStringLiteral("标题"), QStringLiteral("矢量"),
        QStringLiteral("图片"),   QStringLiteral("视频"), QStringLiteral("音频"),
        QStringLiteral("HTML")};
    for (int row = 0; row < expectedNames.size(); ++row)
    {
        if (timeline->rowNameForTest(row) != expectedNames.at(row))
        {
            qCritical() << "timeline-test: row" << row << "expected"
                        << expectedNames.at(row) << "got"
                        << timeline->rowNameForTest(row);
            return 1;
        }
        if (timeline->rowDepthForTest(row) != 0)
        {
            qCritical() << "timeline-test: root row" << row << "has depth"
                        << timeline->rowDepthForTest(row);
            return 1;
        }
    }
    if (timeline->rowStartForTest(3) != 20 || timeline->rowEndForTest(3) != 40)
    {
        qCritical() << "timeline-test: image range"
                    << timeline->rowStartForTest(3)
                    << timeline->rowEndForTest(3);
        return 1;
    }
    if (timeline->rowStartForTest(1) != 0 || timeline->rowEndForTest(1) != 90)
    {
        qCritical() << "timeline-test: title range"
                    << timeline->rowStartForTest(1)
                    << timeline->rowEndForTest(1);
        return 1;
    }
    if (!timeline->rowIsAudioForTest(5)
        || timeline->rowAssetHashForTest(5) != QStringLiteral("audiohash"))
    {
        qCritical() << "timeline-test: audio row metadata missing"
                    << timeline->rowAssetHashForTest(5);
        return 1;
    }
    if (timeline->markerCountForTest() != 2
        || timeline->markerFrameForTest(0) != 20
        || timeline->markerFrameForTest(1) != 55
        || timeline->markerRectForTest(0).isEmpty())
    {
        qCritical() << "timeline-test: markers"
                    << timeline->markerCountForTest()
                    << timeline->markerFrameForTest(0)
                    << timeline->markerFrameForTest(1);
        return 1;
    }

    // Playhead follows ProjectDocument and is drawn in the panel.
    // The window owns the session; drive the playhead through the panel signal
    // path by simulating a ruler click, then verify via the exposed frame.
    const QPoint rulerPoint(timeline->frameXForTest(20), 10);
    QMouseEvent rulerPress(QEvent::MouseButtonPress, QPointF(rulerPoint),
                           QPointF(timeline->mapToGlobal(rulerPoint)),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rulerRelease(QEvent::MouseButtonRelease, QPointF(rulerPoint),
                             QPointF(timeline->mapToGlobal(rulerPoint)),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &rulerPress);
    QApplication::sendEvent(timeline, &rulerRelease);
    QApplication::processEvents();
    if (timeline->currentFrameForTest() != 20
        || sessionPtr->playhead() != 20)
    {
        qCritical() << "timeline-test: ruler click frame"
                    << timeline->currentFrameForTest()
                    << "playhead" << sessionPtr->playhead();
        return 1;
    }

    // Clicking a clip selects it in the Document (read-only, no edit).
    const QPoint vectorPoint(timeline->frameXForTest(30),
                             timeline->rowCenterYForTest(2));
    QMouseEvent clipPress(QEvent::MouseButtonPress, QPointF(vectorPoint),
                          QPointF(timeline->mapToGlobal(vectorPoint)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent clipRelease(QEvent::MouseButtonRelease, QPointF(vectorPoint),
                            QPointF(timeline->mapToGlobal(vectorPoint)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &clipPress);
    QApplication::sendEvent(timeline, &clipRelease);
    QApplication::processEvents();
    if (timeline->uuidForEntityId(timeline->rowEntityForTest(2)) != vector)
    {
        qCritical() << "timeline-test: row id does not map back to uuid";
        return 1;
    }
    const pvf::Uuid selectionUuid =
        timeline->uuidForEntityId(timeline->entityIdForUuid(vector));
    if (selectionUuid != vector)
    {
        qCritical() << "timeline-test: selection mapping broken";
        return 1;
    }
    if (sessionPtr->document()->selection() != vector)
    {
        qCritical() << "timeline-test: clip click did not select in Document"
                    << sessionPtr->document()->selection();
        return 1;
    }

    // Private timeline: only the children of the owner, with depth 1.
    auto* popup = timeline->openPopup(timeline->entityIdForUuid(title));
    QApplication::processEvents();
    if (!popup)
    {
        qCritical() << "timeline-test: private timeline did not open";
        return 1;
    }
    if (popup->entityRowCountForTest() != 1
        || popup->rowNameForTest(0) != QStringLiteral("标题子层")
        || popup->rowDepthForTest(0) != 1
        || popup->rowStartForTest(0) != 10 || popup->rowEndForTest(0) != 40)
    {
        qCritical() << "timeline-test: private timeline rows"
                    << popup->entityRowCountForTest()
                    << popup->rowNameForTest(0) << popup->rowDepthForTest(0)
                    << popup->rowStartForTest(0) << popup->rowEndForTest(0);
        return 1;
    }
    timeline->closeAllPopups();
    QApplication::processEvents();

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!window->grab().save(
            QStringLiteral("/tmp/opencode/pvforge-p2a-timeline-test.png")))
    {
        qCritical() << "timeline-test: screenshot failed";
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-test OK: 7 rows, ranges, depth, markers, playhead,"
               " private timeline";
    return 0;
}

int viewportTest(QApplication& app, const QString& projectDir)
{
    Q_UNUSED(app);
    QString error;
    auto session = pvf::EditorSession::openProject(projectDir, &error);
    if (!session)
    {
        qCritical() << "viewport-test: open failed" << projectDir << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* sessionPtr = window->editorSessionForTest();
    auto* viewport = window->viewportForTest();
    if (!sessionPtr || !sessionPtr->document() || !viewport)
    {
        qCritical() << "viewport-test: window has no session viewport";
        return 1;
    }
    if (sessionPtr->playhead() != 0)
    {
        qCritical() << "viewport-test: open did not start at frame 0"
                    << sessionPtr->playhead();
        return 1;
    }
    if (viewport->grabProjectFrame().isNull())
    {
        qCritical() << "viewport-test: frame 0 render is empty";
        return 1;
    }

    auto waitFor = [](const std::function<bool()>& predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs)
        {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
        }
        return predicate();
    };

    sessionPtr->setPlayhead(30);
    QApplication::processEvents();

    auto* media = sessionPtr->media();
    if (media)
    {
        media->prepareHtmlPreview(3000);
        waitFor([media] { return !media->htmlPending(); }, 5000);
    }
    viewport->refreshSession();
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();

    const QImage grab = viewport->grab().toImage();
    if (grab.isNull() || grab.width() < 100 || grab.height() < 100)
    {
        qCritical() << "viewport-test: viewport grab is empty" << grab.size();
        return 1;
    }

    // The HTML layer is a solid #ff3b30 rectangle at project (0,0)-(320,150)
    // (the clip starts above the canvas).  A camera view maps it into the
    // upper-left quadrant of the viewport.
    int redPixels = 0;
    for (int y = 0; y < grab.height(); y += 2)
    {
        for (int x = 0; x < grab.width(); x += 2)
        {
            const QColor color = grab.pixelColor(x, y);
            if (color.red() > 180 && color.green() < 100 && color.blue() < 100)
                ++redPixels;
        }
    }
    if (redPixels < 200)
    {
        qCritical() << "viewport-test: HTML red area missing; red pixels"
                    << redPixels
                    << "htmlStartFailed" << (media && media->htmlStartFailed())
                    << (media ? media->htmlError() : QString());
        return 1;
    }

    // Sample a point that only the HTML layer covers: project (40,40) is inside
    // the 320x180 HTML frame, left of the video and image entities.
    const QPoint htmlPoint =
        viewport->projectToWidgetForTest(QPointF(40.0, 40.0)).toPoint();
    if (!grab.rect().contains(htmlPoint))
    {
        qCritical() << "viewport-test: HTML sample point outside viewport"
                    << htmlPoint;
        return 1;
    }
    const QColor htmlColor = grab.pixelColor(htmlPoint);
    if (htmlColor.red() < 180 || htmlColor.green() > 100
        || htmlColor.blue() > 100)
    {
        qCritical() << "viewport-test: HTML pixel is not red" << htmlPoint
                    << htmlColor;
        return 1;
    }

    // The title text sits on the canvas centre at frame 30.
    bool centreLit = false;
    const QPoint centre(grab.width() / 2, grab.height() / 2);
    for (int dy = -8; dy <= 8 && !centreLit; ++dy)
    {
        for (int dx = -8; dx <= 8; ++dx)
        {
            const QColor color = grab.pixelColor(centre.x() + dx, centre.y() + dy);
            if (color.lightness() > 150)
            {
                centreLit = true;
                break;
            }
        }
    }
    if (!centreLit)
    {
        qCritical() << "viewport-test: canvas centre has no rendered pixels";
        return 1;
    }

    auto* workbench = sessionPtr->workbench();
    const QString cacheDir = workbench ? workbench->cacheDirectory() : QString();
    if (cacheDir.isEmpty() || !QFileInfo(cacheDir).isDir())
    {
        qCritical() << "viewport-test: workbench cache directory missing" << cacheDir;
        return 1;
    }
    const pvf::workbench::CacheStats cacheStats =
        workbench ? workbench->stats() : pvf::workbench::CacheStats();
    if (cacheStats.entries <= 0)
    {
        qCritical() << "viewport-test: workbench cache has no entries" << cacheDir;
        return 1;
    }

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-p3a-viewport.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "viewport-test: screenshot failed" << screenshot;
        return 1;
    }

    // Compare the same frame with preview.render_frame (same render2 call and
    // the same MediaServices providers).
    pvf::mcp2::Service service(sessionPtr->document()->store(),
                               &sessionPtr->assets());
    if (media)
    {
        service.setVideoFrameProvider([media](const pvf::Uuid& uuid, double frame) {
            return media->videoFrame(uuid, frame);
        });
        service.setHtmlFrameProvider([media](const pvf::Uuid& uuid, double frame) {
            return media->htmlFrame(uuid, frame);
        });
    }
    QJsonObject args;
    args.insert(QStringLiteral("frame"), 30);
    const QJsonObject previewResult =
        service.execute(QStringLiteral("preview.render_frame"), args);
    if (!previewResult.value(QStringLiteral("ok")).toBool())
    {
        qCritical() << "viewport-test: preview.render_frame failed"
                    << previewResult;
        return 1;
    }
    const QString previewPath =
        previewResult.value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("path"))
            .toString();
    const QImage preview(previewPath);
    const QImage viewportFrame = viewport->grabProjectFrame();
    if (preview.isNull() || viewportFrame.isNull()
        || preview.size() != viewportFrame.size())
    {
        qCritical() << "viewport-test: preview/viewport size mismatch"
                    << preview.size() << viewportFrame.size() << previewPath;
        return 1;
    }
    QFile::remove(QStringLiteral("/tmp/opencode/pvforge-p3a-preview.png"));
    QFile::copy(previewPath,
                QStringLiteral("/tmp/opencode/pvforge-p3a-preview.png"));

    double colourDiff = 0.0;
    int samples = 0;
    for (int y = 0; y < preview.height(); y += 4)
    {
        for (int x = 0; x < preview.width(); x += 4)
        {
            const QColor a = preview.pixelColor(x, y);
            const QColor b = viewportFrame.pixelColor(x, y);
            colourDiff += std::abs(a.red() - b.red())
                          + std::abs(a.green() - b.green())
                          + std::abs(a.blue() - b.blue());
            ++samples;
        }
    }
    colourDiff /= std::max(1, samples);
    if (colourDiff > 12.0)
    {
        qCritical() << "viewport-test: preview colour mismatch" << colourDiff
                    << previewPath;
        return 1;
    }

    // Playback: the frame clock must advance the playhead by ~30 frames.
    auto* playback = sessionPtr->playback();
    if (!playback)
    {
        qCritical() << "viewport-test: no playback controller";
        return 1;
    }
    sessionPtr->setPlayhead(30);
    const int playStart = sessionPtr->playhead();
    playback->play();
    const bool advanced =
        waitFor([sessionPtr, playStart] {
            return sessionPtr->playhead() >= playStart + 30;
        }, 6000);
    playback->pause();
    const int playEnd = sessionPtr->playhead();
    if (!advanced || playEnd <= playStart)
    {
        qCritical() << "viewport-test: playback did not advance" << playStart
                    << playEnd;
        return 1;
    }

    // The old camera selection rule: selecting a camera entity switches
    // render2's cameraUuid to it; clearing the selection returns to canvas.
    pvf::Uuid cameraUuid;
    for (auto it = sessionPtr->project().entities.constBegin();
         it != sessionPtr->project().entities.constEnd(); ++it)
    {
        if (it->content.kind == pvf::ContentKind::Camera)
        {
            cameraUuid = it->uuid;
            break;
        }
    }
    if (!cameraUuid.isEmpty())
    {
        sessionPtr->document()->setSelection(cameraUuid);
        QApplication::processEvents();
        if (sessionPtr->activeCameraUuid() != cameraUuid
            || viewport->grabProjectFrame(cameraUuid).isNull())
        {
            qCritical() << "viewport-test: camera selection did not reach render2";
            return 1;
        }
        sessionPtr->document()->setSelection(pvf::Uuid());
        QApplication::processEvents();
        if (!sessionPtr->activeCameraUuid().isEmpty())
        {
            qCritical() << "viewport-test: canvas selection did not clear camera";
            return 1;
        }
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "viewport-test OK: frame 30 red pixels" << redPixels
            << "cache entries" << cacheStats.entries << "preview diff"
            << colourDiff << "playback" << playStart << "->" << playEnd
            << "screenshot" << screenshot;
    return 0;
}

int timelineEditTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b1-edit.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b1 时间轴编辑");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-edit-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    pvf::EditActions* actions = session->editActions();
    if (!actions)
    {
        qCritical() << "timeline-edit-test: no EditActions";
        return 1;
    }

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-edit-test:" << message;
        return condition;
    };

    const QString audioHash(64, QLatin1Char('a'));
    const QString videoHash(64, QLatin1Char('b'));
    auto writeFakeAsset = [&](const QString& hash, const QString& type, int frames) {
        const QString directory =
            projectDir + QStringLiteral("/assets/") + hash;
        if (!QDir().mkpath(directory))
            return false;
        QJsonObject metadata;
        metadata.insert(QStringLiteral("hash"), hash);
        metadata.insert(QStringLiteral("type"), type);
        metadata.insert(QStringLiteral("name"), type + QStringLiteral(".bin"));
        metadata.insert(QStringLiteral("durationFrames"), frames);
        metadata.insert(QStringLiteral("durationSeconds"), frames / 30.0);
        QFile metadataFile(directory + QStringLiteral("/asset.json"));
        if (!metadataFile.open(QIODevice::WriteOnly))
            return false;
        metadataFile.write(QJsonDocument(metadata).toJson());
        metadataFile.close();
        QFile content(directory + QStringLiteral("/content.bin"));
        if (!content.open(QIODevice::WriteOnly))
            return false;
        content.write("fake");
        return true;
    };
    if (!check(writeFakeAsset(audioHash, QStringLiteral("audio"), 60)
                   && writeFakeAsset(videoHash, QStringLiteral("video"), 40),
               QStringLiteral("fake asset write failed")))
        return 1;

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row,
                                 const pvf::Uuid& parent = pvf::Uuid()) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.parentUuid = parent;
        entity.content.kind = pvf::ContentKind::Empty;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    auto makeAsset = [&](const QString& name, int start, int end, int row,
                         pvf::AssetType type, const QString& hash,
                         int sourceIn) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Asset;
        entity.content.asset.type = type;
        entity.content.asset.hash = hash;
        entity.content.asset.sourceIn = sourceIn;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid parent = makeEntity(QStringLiteral("父级"), 0, 60, 0);
    const pvf::Uuid child = makeEntity(QStringLiteral("子级"), 10, 40, 0, parent);
    project.entities[child].content.kind = pvf::ContentKind::Text;
    pvf::model::Track childTrack;
    childTrack.keys = {{0, 0.0, {}, {}}, {29, 1.0, {}, {}}};
    project.entities[child].tracks.insert(QStringLiteral("opacity"), childTrack);

    const pvf::Uuid audio = makeAsset(QStringLiteral("音频"), 20, 50, 1,
                                      pvf::AssetType::Audio, audioHash, 0);
    const pvf::Uuid video = makeAsset(QStringLiteral("视频"), 0, 30, 2,
                                      pvf::AssetType::Video, videoHash, 5);
    const pvf::Uuid lockedRoot = makeEntity(QStringLiteral("锁定父级"), 0, 60, 3);
    project.entities[lockedRoot].locked = true;
    const pvf::Uuid lockedChild =
        makeEntity(QStringLiteral("锁定子级"), 10, 20, 0, lockedRoot);
    const pvf::Uuid target = makeEntity(QStringLiteral("目标父级"), 100, 200, 4);
    const pvf::Uuid worldSource =
        makeEntity(QStringLiteral("世界位置"), 120, 140, 5);
    const pvf::Uuid localSource =
        makeEntity(QStringLiteral("局部坐标"), 20, 40, 6);
    const pvf::Uuid solverRoot =
        makeEntity(QStringLiteral("求解段"), 0, 60, 7);
    pvf::model::SolverSegment segment;
    segment.id = pvf::model::newUuid();
    segment.effect = pvf::SolverEffect::Gravity;
    segment.startFrame = 0;
    segment.endFrame = -1;
    segment.params.insert(QStringLiteral("targetUuid"), parent);
    project.entities[solverRoot].solvers.append(segment);
    const pvf::Uuid trashRoot = makeEntity(QStringLiteral("待删除"), 200, 220, 8);
    const pvf::Uuid trashChild =
        makeEntity(QStringLiteral("待删除子级"), 205, 210, 0, trashRoot);

    const QVector<pvf::Uuid> all = {
        parent,      child,       audio,      video,      lockedRoot,
        lockedChild, target,      worldSource, localSource, solverRoot,
        trashRoot,   trashChild};
    if (!document->commitEntities(all, QStringLiteral("p2b1 setup"), &error))
    {
        qCritical() << "timeline-edit-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto flush = [&] {
        QString flushError;
        session->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-edit-test: flush failed" << flushError;
    };
    auto undo = [&] {
        flush();
        QString undoError;
        if (!document->undo(&undoError))
        {
            qCritical() << "timeline-edit-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto depth = [&] { return document->store()->undoDepth(); };
    pvf::EditorSession* sessionPtr = session.get();
    auto entity = [sessionPtr](const pvf::Uuid& uuid) {
        return sessionPtr->project().entities.value(uuid);
    };

    // Move a parent: the whole subtree keeps its local offsets.
    int beforeDepth = depth();
    pvf::EditPlan movePlan;
    if (!check(actions->planMove({parent}, 10, {}, &movePlan, &error),
               QStringLiteral("planMove failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).start == 0 && entity(child).start == 10,
               QStringLiteral("planMove mutated the document")))
        return 1;
    if (!check(actions->moveClips({parent}, 10, &error),
               QStringLiteral("moveClips failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).start == 10 && entity(parent).end == 70
                   && entity(child).start == 20 && entity(child).end == 50,
               QStringLiteral("parent move did not shift the child")))
        return 1;
    flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("moveClips did not commit exactly once")))
        return 1;
    if (!undo())
        return 1;

    // Trim start: keys shift/crop and the child is clipped by the parent.
    const int childTrackSize = entity(child).tracks.value(QStringLiteral("opacity")).keys.size();
    if (!check(childTrackSize == 2, QStringLiteral("setup child keys wrong")))
        return 1;
    if (!check(actions->trimClip(child, 15, 40, &error),
               QStringLiteral("trim start failed: %1").arg(error)))
        return 1;
    {
        const auto& track = entity(child).tracks.value(QStringLiteral("opacity"));
        if (!check(entity(child).start == 15 && entity(child).end == 40
                       && track.keys.size() == 1 && track.keys.first().frame == 24,
                   QStringLiteral("trim start did not shift/crop keys")))
            return 1;
    }
    if (!undo())
        return 1;

    // Trim end: out-of-range keys disappear.
    if (!check(actions->trimClip(child, 10, 30, &error),
               QStringLiteral("trim end failed: %1").arg(error)))
        return 1;
    {
        const auto& track = entity(child).tracks.value(QStringLiteral("opacity"));
        if (!check(entity(child).end == 30 && track.keys.size() == 1
                       && track.keys.first().frame == 0,
                   QStringLiteral("trim end did not drop the late key")))
            return 1;
    }
    if (!undo())
        return 1;

    // Audio sourceIn follows the start; crossing the material start fails.
    if (!check(actions->trimClip(audio, 25, 50, &error),
               QStringLiteral("audio trim start failed: %1").arg(error)))
        return 1;
    if (!check(entity(audio).start == 25
                   && entity(audio).content.asset.sourceIn == 5,
               QStringLiteral("audio sourceIn was not shifted")))
        return 1;
    if (!undo())
        return 1;
    if (!check(!actions->trimClip(audio, 10, 50, &error),
               QStringLiteral("audio trim below sourceIn should fail")))
        return 1;
    if (!check(entity(audio).start == 20
                   && entity(audio).content.asset.sourceIn == 0,
               QStringLiteral("failed audio trim changed the project")))
        return 1;
    if (!check(!actions->trimClip(audio, 20, 90, &error),
               QStringLiteral("audio trim past the material should fail")))
        return 1;

    // Video sourceIn shifts too.
    if (!check(actions->trimClip(video, 2, 30, &error),
               QStringLiteral("video trim start failed: %1").arg(error)))
        return 1;
    if (!check(entity(video).content.asset.sourceIn == 7,
               QStringLiteral("video sourceIn was not shifted")))
        return 1;
    if (!undo())
        return 1;

    // Locked ancestors reject moves, trims and deletes.
    if (!check(!actions->moveClips({lockedChild}, 5, &error),
               QStringLiteral("locked child move should fail")))
        return 1;
    if (!check(!actions->trimClip(lockedChild, 12, 20, &error),
               QStringLiteral("locked child trim should fail")))
        return 1;
    if (!check(!actions->deleteEntities({lockedRoot}, false, nullptr, &error),
               QStringLiteral("locked cascade delete should fail")))
        return 1;
    if (!check(entity(lockedChild).start == 10,
               QStringLiteral("locked edit changed the project")))
        return 1;

    // Trim end pins an open solver segment to the new last frame.
    if (!check(actions->trimClip(solverRoot, 0, 40, &error),
               QStringLiteral("solver trim failed: %1").arg(error)))
        return 1;
    if (!check(entity(solverRoot).solvers.size() == 1
                   && entity(solverRoot).solvers.first().endFrame == 39,
               QStringLiteral("open solver segment was not pinned")))
        return 1;
    if (!undo())
        return 1;

    // Reparent keeps either world time or parent-local time.
    if (!check(actions->reparent(worldSource, target, true, &error),
               QStringLiteral("keepWorld reparent failed: %1").arg(error)))
        return 1;
    if (!check(entity(worldSource).parentUuid == target
                   && entity(worldSource).start == 120
                   && entity(worldSource).end == 140,
               QStringLiteral("keepWorld changed the absolute range")))
        return 1;
    if (!undo())
        return 1;
    if (!check(actions->reparent(localSource, target, false, &error),
               QStringLiteral("keepLocal reparent failed: %1").arg(error)))
        return 1;
    if (!check(entity(localSource).parentUuid == target
                   && entity(localSource).start == 120
                   && entity(localSource).end == 140,
               QStringLiteral("keepLocal did not add the parent offset")))
        return 1;
    if (!undo())
        return 1;
    if (!check(!actions->reparent(localSource, target, true, &error),
               QStringLiteral("keepWorld outside the new parent should fail")))
        return 1;

    // Delete cascades and undo restores the subtree.
    const int entityCount = session->project().entities.size();
    if (!check(actions->deleteEntities({trashRoot}, false, nullptr, &error),
               QStringLiteral("cascade delete failed: %1").arg(error)))
        return 1;
    if (!check(!session->project().entities.contains(trashRoot)
                   && !session->project().entities.contains(trashChild)
                   && session->project().entities.size() == entityCount - 2,
               QStringLiteral("cascade delete left children behind")))
        return 1;
    if (!undo())
        return 1;
    if (!check(session->project().entities.contains(trashChild),
               QStringLiteral("undo did not restore the deleted child")))
        return 1;

    // Panel gesture: one drag = one changed()/commit/undo step.  Esc cancels.
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!check(live && timeline, QStringLiteral("session window/timeline missing")))
        return 1;
    auto rowForName = [&](const QString& name) {
        for (int row = 0; row < timeline->entityRowCountForTest(); ++row)
            if (timeline->rowNameForTest(row) == name)
                return row;
        return -1;
    };
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         qCritical() << "timeline-edit-test: panel edit failed"
                                     << message;
                     });

    const int playheadBefore = live->playhead();
    const int panelRow = rowForName(QStringLiteral("父级"));
    if (!check(panelRow >= 0, QStringLiteral("parent row missing")))
        return 1;
    const QPoint startPoint(timeline->frameXForTest(20),
                            timeline->rowCenterYForTest(panelRow));
    const QPoint movePoint(timeline->frameXForTest(35),
                           timeline->rowCenterYForTest(panelRow));
    int changedSignals = 0;
    QObject::connect(live->document(), &pvf::next::Document::changed,
                     [&changedSignals] { ++changedSignals; });
    int panelDepth = live->document()->store()->undoDepth();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(startPoint),
                      QPointF(timeline->mapToGlobal(startPoint)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(movePoint),
                     QPointF(timeline->mapToGlobal(movePoint)), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(movePoint),
                        QPointF(timeline->mapToGlobal(movePoint)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &press);
    QApplication::sendEvent(timeline, &move);
    QApplication::sendEvent(timeline, &release);
    QApplication::processEvents();
    if (!check(changedSignals == 1,
               QStringLiteral("one drag emitted %1 changed signals")
                   .arg(changedSignals)))
        return 1;
    live->flush();
    if (!check(live->document()->store()->undoDepth() == panelDepth + 1,
               QStringLiteral("one drag did not create exactly one undo step")))
        return 1;
    if (!check(entity(parent).start == 15 && entity(parent).end == 75
                   && entity(child).start == 25 && entity(child).end == 55,
               QStringLiteral("panel drag did not move parent and child")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("panel drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).start == 0 && entity(child).start == 10,
               QStringLiteral("panel undo did not restore the subtree")))
        return 1;
    if (!check(live->playhead() == playheadBefore,
               QStringLiteral("panel drag changed the playhead")))
        return 1;

    // Esc during a drag restores the preview and creates no commit.
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    QMouseEvent cancelPress(QEvent::MouseButtonPress, QPointF(startPoint),
                            QPointF(timeline->mapToGlobal(startPoint)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent cancelMove(QEvent::MouseMove,
                           QPointF(timeline->frameXForTest(30),
                                   timeline->rowCenterYForTest(panelRow)),
                           QPointF(timeline->mapToGlobal(
                               QPoint(timeline->frameXForTest(30),
                                      timeline->rowCenterYForTest(panelRow)))),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &cancelPress);
    QApplication::sendEvent(timeline, &cancelMove);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(timeline, &escape);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 0, QStringLiteral("Esc emitted changed"))
        || !check(live->document()->store()->undoDepth() == panelDepth,
                  QStringLiteral("Esc created an undo step"))
        || !check(entity(parent).start == 0,
                  QStringLiteral("Esc did not restore the drag preview")))
        return 1;

    // Vertical drag between occupied rows; the row change is part of the same
    // single commit and swaps the two row slots.
    const int videoRow = rowForName(QStringLiteral("视频"));
    const int parentRow = rowForName(QStringLiteral("父级"));
    if (!check(videoRow >= 0 && parentRow >= 0,
               QStringLiteral("row reorder rows missing")))
        return 1;
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint rowFrom(timeline->frameXForTest(30),
                         timeline->rowCenterYForTest(parentRow));
    const QPoint rowTo(timeline->frameXForTest(30),
                       timeline->rowCenterYForTest(videoRow));
    QMouseEvent rowPress(QEvent::MouseButtonPress, QPointF(rowFrom),
                         QPointF(timeline->mapToGlobal(rowFrom)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rowMove(QEvent::MouseMove, QPointF(rowTo),
                        QPointF(timeline->mapToGlobal(rowTo)), Qt::NoButton,
                        Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rowRelease(QEvent::MouseButtonRelease, QPointF(rowTo),
                           QPointF(timeline->mapToGlobal(rowTo)),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &rowPress);
    QApplication::sendEvent(timeline, &rowMove);
    QApplication::sendEvent(timeline, &rowRelease);
    QApplication::processEvents();
    live->flush();
    // Row slots are fixed: dropping on the video clip swaps the two slots, so
    // the dragged clip lands exactly on the row under the pointer.
    if (!check(changedSignals == 1,
               QStringLiteral("row drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("row drag did not create one undo step"))
        || !check(timeline->rowForEntityForTest(
                      timeline->entityIdForUuid(parent))
                      == videoRow,
                  QStringLiteral("row drag did not land on the dropped row (row %1, expected %2; model %3; rows %4/%5/%6)")
                      .arg(timeline->rowForEntityForTest(
                          timeline->entityIdForUuid(parent)))
                      .arg(videoRow)
                      .arg(entity(parent).row)
                      .arg(timeline->rowNameForTest(0))
                      .arg(timeline->rowNameForTest(1))
                      .arg(timeline->rowNameForTest(2)))
        || !check(timeline->rowForEntityForTest(
                      timeline->entityIdForUuid(video))
                      == parentRow,
                  QStringLiteral("row drag did not move the occupant to the vacated row")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("row drag undo failed: %1").arg(error)))
        return 1;
    if (!check(timeline->rowForEntityForTest(
                   timeline->entityIdForUuid(parent))
                   == parentRow,
               QStringLiteral("row drag undo did not restore rows")))
        return 1;

    // Right-edge drag trims the parent and clips the child in one commit.
    const int trimRow = rowForName(QStringLiteral("父级"));
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint trimFrom(timeline->frameXForTest(60),
                          timeline->rowCenterYForTest(trimRow));
    const QPoint trimTo(timeline->frameXForTest(30),
                        timeline->rowCenterYForTest(trimRow));
    QMouseEvent trimPress(QEvent::MouseButtonPress, QPointF(trimFrom),
                          QPointF(timeline->mapToGlobal(trimFrom)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent trimMove(QEvent::MouseMove, QPointF(trimTo),
                         QPointF(timeline->mapToGlobal(trimTo)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
    QMouseEvent trimRelease(QEvent::MouseButtonRelease, QPointF(trimTo),
                            QPointF(timeline->mapToGlobal(trimTo)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &trimPress);
    QApplication::sendEvent(timeline, &trimMove);
    QApplication::sendEvent(timeline, &trimRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("trim drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("trim drag did not create one undo step"))
        || !check(entity(parent).end == 30 && entity(child).end == 30,
                  QStringLiteral("panel trim did not cascade to the child")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("trim drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(parent).end == 60 && entity(child).end == 40,
               QStringLiteral("trim drag undo did not restore the range")))
        return 1;

    // The private timeline overlay edits the same model and commits once.
    auto* popup = timeline->openPopup(timeline->entityIdForUuid(parent));
    QApplication::processEvents();
    if (!check(popup && popup->entityRowCountForTest() == 1,
               QStringLiteral("private timeline did not open for editing")))
        return 1;
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint popupFrom(popup->frameXForTest(20),
                           popup->rowCenterYForTest(0));
    const QPoint popupTo(popup->frameXForTest(25),
                         popup->rowCenterYForTest(0));
    QMouseEvent popupPress(QEvent::MouseButtonPress, QPointF(popupFrom),
                           QPointF(popup->mapToGlobal(popupFrom)),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent popupMove(QEvent::MouseMove, QPointF(popupTo),
                          QPointF(popup->mapToGlobal(popupTo)), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
    QMouseEvent popupRelease(QEvent::MouseButtonRelease, QPointF(popupTo),
                             QPointF(popup->mapToGlobal(popupTo)),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(popup, &popupPress);
    QApplication::sendEvent(popup, &popupMove);
    QApplication::sendEvent(popup, &popupRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("popup drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("popup drag did not create one undo step"))
        || !check(entity(child).start == 15 && entity(child).end == 45,
                  QStringLiteral("popup drag did not move the child")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("popup drag undo failed: %1").arg(error)))
        return 1;
    timeline->closeAllPopups();
    QApplication::processEvents();

    // Dragging onto a folder clip asks world/local and commits the choice.
    const int reparentSourceRow = rowForName(QStringLiteral("世界位置"));
    const int reparentTargetRow = rowForName(QStringLiteral("目标父级"));
    if (!check(reparentSourceRow >= 0 && reparentTargetRow >= 0,
               QStringLiteral("reparent rows missing")))
        return 1;
    panelDepth = live->document()->store()->undoDepth();
    changedSignals = 0;
    const QPoint reparentFrom(timeline->frameXForTest(130),
                              timeline->rowCenterYForTest(reparentSourceRow));
    const QPoint reparentTo(timeline->frameXForTest(130),
                            timeline->rowCenterYForTest(reparentTargetRow));
    QTimer::singleShot(0, [] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (QAbstractButton* button : box->buttons())
        {
            if (button->text().contains(QStringLiteral("保持世界位置")))
            {
                button->click();
                return;
            }
        }
        box->reject();
    });
    QTimer::singleShot(2000, [] {
        if (auto* box =
                qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->reject();
    });
    QMouseEvent reparentPress(QEvent::MouseButtonPress, QPointF(reparentFrom),
                              QPointF(timeline->mapToGlobal(reparentFrom)),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent reparentMove(QEvent::MouseMove, QPointF(reparentTo),
                             QPointF(timeline->mapToGlobal(reparentTo)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent reparentRelease(
        QEvent::MouseButtonRelease, QPointF(reparentTo),
        QPointF(timeline->mapToGlobal(reparentTo)), Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(timeline, &reparentPress);
    QApplication::sendEvent(timeline, &reparentMove);
    QApplication::sendEvent(timeline, &reparentRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(changedSignals == 1,
               QStringLiteral("reparent drag emitted %1 changed signals")
                   .arg(changedSignals))
        || !check(live->document()->store()->undoDepth() == panelDepth + 1,
                  QStringLiteral("reparent drag did not create one undo step"))
        || !check(entity(worldSource).parentUuid == target
                      && entity(worldSource).start == 120
                      && entity(worldSource).end == 140,
                  QStringLiteral("drag reparent keepWorld result wrong")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("reparent drag undo failed: %1").arg(error)))
        return 1;
    if (!check(entity(worldSource).parentUuid.isEmpty()
                   && rowForName(QStringLiteral("世界位置")) >= 0,
               QStringLiteral("reparent undo did not restore the root row")))
        return 1;

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!check(window->grab().save(
                   QStringLiteral("/tmp/opencode/pvforge-p2b1-edit.png")),
               QStringLiteral("screenshot failed")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-edit-test OK: subtree move, trim cascade/keys/sourceIn,"
               " solver pin, locks, row/trim/popup gestures, one gesture one"
               " undo, drag reparent world/local, cascade delete";
    return 0;
}

int timelineBottomUpTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p3-bottomup.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P3 时间轴自下而上");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-bottomup-test: create failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !timeline)
    {
        qCritical() << "timeline-bottomup-test: session window/timeline missing";
        return 1;
    }

    int failures = 0;
    auto expect = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "timeline-bottomup-test:" << message;
            ++failures;
        }
        return condition;
    };
    int changedSignals = 0;
    QObject::connect(document, &pvf::next::Document::changed,
                     [&changedSignals] { ++changedSignals; });
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         qCritical() << "timeline-bottomup-test: edit failed"
                                     << message;
                     });

    const int rowHeight = 30;
    // Empty project: row 0 is the bottom row and nothing occupies it.
    expect(timeline->entityRowCountForTest() == 0,
           QStringLiteral("empty project should have no entity rows"));
    expect(timeline->rowTopForTest(0) + rowHeight
               == timeline->bodyBottomForTest() + 1,
           QStringLiteral("empty row 0 is not bottom-aligned"));
    expect(timeline->rowTopForTest(1) + rowHeight == timeline->rowTopForTest(0),
           QStringLiteral("row 1 is not stacked above row 0"));
    expect(timeline->dropRowForTest(timeline->rowCenterYForTest(0)) == 0,
           QStringLiteral("empty drop below did not resolve to row 0"));
    expect(timeline->dropRowForTest(timeline->rowCenterYForTest(6)) == 0,
           QStringLiteral("empty drop above did not clamp to row 0"));
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    window->grab().save(
        QStringLiteral("/tmp/opencode/pvforge-timeline-bottomup-empty.png"));

    auto makeEntity = [&live](const QString& name, int start, int end,
                              int row) {
        auto& project = live->mutableProject();
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Empty;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid bottom = makeEntity(QStringLiteral("底"), 0, 60, 0);
    const pvf::Uuid middle = makeEntity(QStringLiteral("中"), 10, 40, 1);
    const pvf::Uuid top = makeEntity(QStringLiteral("顶"), 20, 50, 2);
    // A non-empty, childless clip is a reorder target, not a parent target.
    live->mutableProject().entities[top].content.kind = pvf::ContentKind::Text;
    pvf::model::Track track;
    track.keys = {{0, 0.0, {}, {}}, {10, 100.0, {}, {}}};
    live->mutableProject().entities[middle].tracks.insert(QStringLiteral("x"),
                                                          track);
    if (!expect(document->commitEntities({bottom, middle, top},
                                         QStringLiteral("bottom-up setup"),
                                         &error),
                QStringLiteral("setup commit failed: %1").arg(error)))
        return 1;
    QString flushError;
    live->flush(&flushError);
    QApplication::processEvents();

    auto rowFor = [&](const pvf::Uuid& uuid) {
        return timeline->rowForEntityForTest(timeline->entityIdForUuid(uuid));
    };
    auto idFor = [&](const pvf::Uuid& uuid) {
        return timeline->entityIdForUuid(uuid);
    };
    auto sendDrag = [&](const QPoint& from, const QPoint& to) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          QPointF(timeline->mapToGlobal(from)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         QPointF(timeline->mapToGlobal(to)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &move);
        QApplication::processEvents();
    };
    auto sendRelease = [&](const QPoint& at) {
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at),
                            QPointF(timeline->mapToGlobal(at)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
    };
    auto undo = [&] {
        live->flush(&flushError);
        if (!document->undo(&error))
        {
            qCritical() << "timeline-bottomup-test: undo failed" << error;
            return false;
        }
        QApplication::processEvents();
        return true;
    };
    auto depthNow = [&] {
        // Human commits are debounced; flush before reading the undo stack.
        live->flush(&flushError);
        return document->store()->undoDepth();
    };

    // Three rows from the bottom up: row 0 is drawn below row 1, row 2 on top.
    expect(timeline->entityRowCountForTest() == 3,
           QStringLiteral("expected three entity rows"));
    expect(timeline->rowEntityForTest(0) == idFor(bottom)
               && timeline->rowEntityForTest(1) == idFor(middle)
               && timeline->rowEntityForTest(2) == idFor(top),
           QStringLiteral("rows did not map model row 0/1/2 to the bottom"));
    expect(timeline->rowCenterYForTest(0) > timeline->rowCenterYForTest(1)
               && timeline->rowCenterYForTest(1)
                      > timeline->rowCenterYForTest(2),
           QStringLiteral("rows are not painted bottom-up"));
    expect(timeline->clipRectFor(idFor(middle)).top()
                   + timeline->clipRectFor(idFor(middle)).height() / 2
               == timeline->rowCenterYForTest(1),
           QStringLiteral("clip rect does not follow the bottom-up row"));
    expect(timeline->dropRowForTest(timeline->rowCenterYForTest(3)) == 3,
           QStringLiteral("drop above the top clip did not resolve to highest+1"));
    expect(timeline->dropRowForTest(timeline->bodyBottomForTest() + 20) == 0,
           QStringLiteral("drop below the body did not resolve to row 0"));

    // 1. Middle clip -> topmost blank row.
    QPoint dragFrom(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(1));
    QPoint dragTo(timeline->frameXForTest(30),
                  timeline->rowCenterYForTest(3));
    int depthBefore = depthNow();
    changedSignals = 0;
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1,
           QStringLiteral("top blank drop emitted %1 changed signals")
               .arg(changedSignals));
    expect(depthNow() == depthBefore + 1,
           QStringLiteral("top blank drop did not create one undo step"));
    expect(rowFor(middle) == 3,
           QStringLiteral("middle clip did not land on the top blank row (%1)")
               .arg(rowFor(middle)));
    expect(rowFor(bottom) == 0 && rowFor(top) == 2,
           QStringLiteral("top blank drop moved an unrelated slot"));
    expect(undo() && rowFor(middle) == 1,
           QStringLiteral("undo did not restore the middle row"));

    // 2. Bottom blank row: vacate row 0 by dragging the bottom clip up, then
    // drop the middle clip onto the now-blank bottom row.
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(0));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    depthBefore = depthNow();
    changedSignals = 0;
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("partial drag / reorder steps do not match"));
    expect(rowFor(bottom) == 3 && rowFor(middle) == 1,
           QStringLiteral("bottom clip did not move to the blank top row"));
    depthBefore = depthNow();
    changedSignals = 0;
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(0));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("bottom blank drop did not commit once"));
    expect(rowFor(middle) == 0,
           QStringLiteral("middle clip did not land on the blank bottom row (%1)")
               .arg(rowFor(middle)));
    expect(rowFor(top) == 2 && rowFor(bottom) == 3,
           QStringLiteral("bottom blank drop compacted unrelated rows"));
    expect(undo() && rowFor(middle) == 1 && undo() && rowFor(bottom) == 0,
           QStringLiteral("undo did not restore the bottom blank drags"));

    // 3. Drag preview follows the pointer Y and Esc cancels without a commit.
    changedSignals = 0;
    const int depthAtDragStart = document->store()->undoDepth();
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    const QPoint previewBottom(timeline->frameXForTest(30),
                               timeline->rowCenterYForTest(0));
    sendDrag(dragFrom, previewBottom);
    expect(rowFor(middle) == 0,
           QStringLiteral("drag preview did not follow the pointer to row 0 (%1)")
               .arg(rowFor(middle)));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    QMouseEvent previewMove(QEvent::MouseMove, QPointF(dragTo),
                            QPointF(timeline->mapToGlobal(dragTo)), Qt::NoButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &previewMove);
    QApplication::processEvents();
    expect(rowFor(middle) == 3,
           QStringLiteral("drag preview did not follow the pointer to row 3 (%1)")
               .arg(rowFor(middle)));
    expect(changedSignals == 0
               && document->store()->undoDepth() == depthAtDragStart,
           QStringLiteral("preview wrote a commit before release"));
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    window->grab().save(
        QStringLiteral("/tmp/opencode/pvforge-timeline-bottomup-drag.png"));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(timeline, &escape);
    QApplication::processEvents();
    expect(rowFor(middle) == 1,
           QStringLiteral("Esc did not restore the preview row (%1)")
               .arg(rowFor(middle)));
    expect(changedSignals == 0 && depthNow() == depthAtDragStart,
           QStringLiteral("Esc created a commit"));

    // 4. Dropping on an occupied clip lands on that row and swaps slots.
    depthBefore = depthNow();
    changedSignals = 0;
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(2));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("occupied-slot drop did not commit once"));
    expect(rowFor(middle) == 2 && rowFor(top) == 1 && rowFor(bottom) == 0,
           QStringLiteral("occupied-slot drop did not swap rows (%1/%2)")
               .arg(rowFor(middle))
               .arg(rowFor(top)));
    expect(undo() && rowFor(middle) == 1 && rowFor(top) == 2,
           QStringLiteral("undo did not restore swapped rows"));

    // 4b. A blank gap row is a valid target and stays a gap for the others.
    depthBefore = depthNow();
    changedSignals = 0;
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(2));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(rowFor(top) == 3 && rowFor(bottom) == 0 && rowFor(middle) == 1,
           QStringLiteral("top drop above did not keep the other slots"));
    expect(depthNow() == depthBefore + 1,
           QStringLiteral("top gap drop did not commit once"));
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(2));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(rowFor(middle) == 2 && rowFor(top) == 3 && rowFor(bottom) == 0,
           QStringLiteral("gap drop into a middle blank row was compacted"));
    expect(depthNow() == depthBefore + 2,
           QStringLiteral("two gap drops did not create two undo steps"));
    const bool gapUndo1 = undo();
    const int gapMiddle1 = rowFor(middle);
    const int gapTop1 = rowFor(top);
    const bool gapUndo2 = undo();
    expect(gapUndo1 && gapUndo2 && rowFor(middle) == 1 && rowFor(top) == 2,
           QStringLiteral("undo did not restore the gap drops (%1/%2 -> %3/%4, %5/%6)")
               .arg(gapUndo1)
               .arg(gapUndo2)
               .arg(gapMiddle1)
               .arg(gapTop1)
               .arg(rowFor(middle))
               .arg(rowFor(top)));

    // 5. Wheel zoom/scroll keeps the bottom anchor and moves rows as a block.
    const int topBefore = timeline->rowTopForTest(0);
    QWheelEvent wheel(QPointF(timeline->frameXForTest(20),
                              timeline->rowCenterYForTest(1)),
                      QPointF(timeline->mapToGlobal(
                          QPoint(timeline->frameXForTest(20),
                                 timeline->rowCenterYForTest(1)))),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(timeline, &wheel);
    QApplication::processEvents();
    expect(timeline->verticalScrollForTest() > 0,
           QStringLiteral("wheel up did not scroll towards higher rows"));
    expect(timeline->rowTopForTest(0) > topBefore,
           QStringLiteral("scrolled rows did not move as a block"));
    QWheelEvent wheelBack(QPointF(timeline->frameXForTest(20),
                                  timeline->rowCenterYForTest(1)),
                          QPointF(timeline->mapToGlobal(
                              QPoint(timeline->frameXForTest(20),
                                     timeline->rowCenterYForTest(1)))),
                          QPoint(), QPoint(0, -120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(timeline, &wheelBack);
    QApplication::processEvents();
    expect(timeline->verticalScrollForTest() == 0,
           QStringLiteral("wheel down did not return to the bottom anchor"));

    // 6. Keyframe hit needs the diamond band, not only the X position.
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Keyframes);
    QApplication::processEvents();
    const QPoint keyPoint(timeline->frameXForTest(20),
                          timeline->rowCenterYForTest(1));
    QMouseEvent keyPress(QEvent::MouseButtonPress, QPointF(keyPoint),
                         QPointF(timeline->mapToGlobal(keyPoint)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &keyPress);
    QApplication::processEvents();
    expect(timeline->selectedKeyframeCount() == 1,
           QStringLiteral("keyframe diamond was not hit at its Y"));
    QMouseEvent keyRelease(QEvent::MouseButtonRelease, QPointF(keyPoint),
                           QPointF(timeline->mapToGlobal(keyPoint)),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &keyRelease);
    QApplication::processEvents();
    const QRect middleClip = timeline->clipRectFor(idFor(middle));
    expect(timeline->keyframeHitForTest(1, timeline->frameXForTest(20),
                                        timeline->rowCenterYForTest(1))
               == 20,
           QStringLiteral("keyframe lookup missed the diamond centre"));
    const QPoint offBand(keyPoint.x(), middleClip.top() + 1);
    expect(timeline->keyframeHitForTest(1, offBand.x(), offBand.y()) == -1,
           QStringLiteral("same X outside the diamond band still hit a keyframe"));
    QMouseEvent offPress(QEvent::MouseButtonPress, QPointF(offBand),
                         QPointF(timeline->mapToGlobal(offBand)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &offPress);
    QApplication::processEvents();
    QMouseEvent offRelease(QEvent::MouseButtonRelease, QPointF(offBand),
                           QPointF(timeline->mapToGlobal(offBand)),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &offRelease);
    QApplication::processEvents();
    expect(timeline->selectedKeyframeCount() == 0,
           QStringLiteral("clip body click selected a keyframe"));
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Clips);
    QApplication::processEvents();

    // 7. Cross-parent drop: a folder row asks for reparent and lands on the
    // new parent's next free row.
    const pvf::Uuid folder = makeEntity(QStringLiteral("文件夹"), 0, 60, 3);
    if (!expect(document->commitEntities({folder},
                                         QStringLiteral("folder setup"), &error),
                QStringLiteral("folder commit failed: %1").arg(error)))
        return 1;
    live->flush(&flushError);
    QApplication::processEvents();
    expect(rowFor(folder) == 3, QStringLiteral("folder row missing"));
    depthBefore = depthNow();
    changedSignals = 0;
    QTimer::singleShot(50, [] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (QAbstractButton* button : box->buttons())
        {
            if (button->text().contains(QStringLiteral("保持世界位置")))
            {
                button->click();
                return;
            }
        }
        box->reject();
    });
    QTimer::singleShot(2000, [] {
        if (auto* box =
                qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->reject();
    });
    dragFrom = QPoint(timeline->frameXForTest(30),
                      timeline->rowCenterYForTest(1));
    dragTo = QPoint(timeline->frameXForTest(30),
                    timeline->rowCenterYForTest(3));
    sendDrag(dragFrom, dragTo);
    sendRelease(dragTo);
    expect(changedSignals == 1 && depthNow() == depthBefore + 1,
           QStringLiteral("cross-parent drop did not commit once"));
    expect(live->project().entities.value(middle).parentUuid == folder,
           QStringLiteral("cross-parent drop did not reparent"));
    expect(rowFor(middle) == 0 && rowFor(folder) == 3,
           QStringLiteral("cross-parent drop rows wrong (%1/%2)")
               .arg(rowFor(middle))
               .arg(rowFor(folder)));
    {
        auto* popup = timeline->openPopup(idFor(folder));
        QApplication::processEvents();
        expect(popup && popup->entityRowCountForTest() == 1,
               QStringLiteral("private timeline did not open the new child"));
        if (popup)
        {
            expect(popup->rowCenterYForTest(0) > popup->rowCenterYForTest(1),
                   QStringLiteral("private timeline is not bottom-up"));
            expect(popup->rowEntityForTest(0) == idFor(middle),
                   QStringLiteral("private timeline row 0 is not the child"));
            // Vertical drag inside the private timeline also lands on a blank
            // row and commits once.
            const int popupDepth = depthNow();
            const QPoint popupFrom(popup->frameXForTest(30),
                                   popup->rowCenterYForTest(0));
            const QPoint popupTo(popup->frameXForTest(30),
                                 popup->rowCenterYForTest(2));
            QMouseEvent popupPress(QEvent::MouseButtonPress, QPointF(popupFrom),
                                   QPointF(popup->mapToGlobal(popupFrom)),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent popupMove(QEvent::MouseMove, QPointF(popupTo),
                                  QPointF(popup->mapToGlobal(popupTo)),
                                  Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent popupRelease(
                QEvent::MouseButtonRelease, QPointF(popupTo),
                QPointF(popup->mapToGlobal(popupTo)), Qt::LeftButton,
                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(popup, &popupPress);
            QApplication::sendEvent(popup, &popupMove);
            QApplication::sendEvent(popup, &popupRelease);
            QApplication::processEvents();
            expect(rowFor(middle) == 1,
                   QStringLiteral("private timeline blank-row drag did not land (%1)")
                       .arg(rowFor(middle)));
            expect(depthNow() == popupDepth + 1,
                   QStringLiteral("private timeline drag did not commit once"));
            expect(undo() && rowFor(middle) == 0,
                   QStringLiteral("private timeline drag undo did not restore row 0"));
        }
        timeline->closeAllPopups();
        QApplication::processEvents();
    }
    expect(undo() && rowFor(middle) == 1,
           QStringLiteral("cross-parent undo did not restore the root row"));

    // 8. Deleting a clip leaves the other row slots untouched.
    const int rowsBefore = timeline->entityRowCountForTest();
    if (!expect(live->editActions()->deleteEntities({top}, false, nullptr,
                                                    &error),
                QStringLiteral("delete failed: %1").arg(error)))
        return 1;
    live->flush(&flushError);
    QApplication::processEvents();
    expect(timeline->entityRowCountForTest() == rowsBefore - 1,
           QStringLiteral("delete did not remove one row"));
    expect(rowFor(bottom) == 0 && rowFor(middle) == 1,
           QStringLiteral("delete compacted the remaining rows"));
    expect(undo() && rowFor(top) == 2,
           QStringLiteral("delete undo did not restore the top row"));

    window->grab().save(
        QStringLiteral("/tmp/opencode/pvforge-timeline-bottomup.png"));
    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-bottomup-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int timelineUiTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p3-timeline-ui.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P3 时间轴回归");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-ui-test: create failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !timeline)
    {
        qCritical() << "timeline-ui-test: session window/timeline missing";
        return 1;
    }

    int failures = 0;
    auto expect = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "timeline-ui-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        QApplication::processEvents();
        return document->store()->undoDepth();
    };

    auto makeEntity = [&live](const QString& name, int start, int end, int row) {
        auto& project = live->mutableProject();
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Text;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid hero = makeEntity(QStringLiteral("角色A"), 0, 120, 0);
    const pvf::Uuid background = makeEntity(QStringLiteral("背景B"), 20, 90, 1);
    const pvf::Uuid prop = makeEntity(QStringLiteral("道具C"), 40, 80, 2);
    pvf::model::Track xTrack;
    xTrack.keys = {{0, 0.0, {}, {}}, {30, 100.0, {}, {}},
                   {60, 200.0, {}, {}}};
    live->mutableProject().entities[hero].tracks.insert(QStringLiteral("x"),
                                                        xTrack);
    if (!document->commitEntities({hero, background, prop},
                                  QStringLiteral("timeline ui setup"), &error))
    {
        qCritical() << "timeline-ui-test: setup commit failed" << error;
        return 1;
    }
    flush();

    auto rowFor = [&](const pvf::Uuid& uuid) {
        return timeline->rowForEntityForTest(timeline->entityIdForUuid(uuid));
    };
    auto idFor = [&](const pvf::Uuid& uuid) {
        return timeline->entityIdForUuid(uuid);
    };
    auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                Qt::MouseButtons buttons,
                                Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(type, QPointF(pos),
                          QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                          buttons, modifiers);
        QApplication::sendEvent(timeline, &event);
    };
    auto click = [&](const QPoint& pos) {
        sendMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, pos, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
    };

    // 1. Clip-first: no left name column and no timeline-side entity search.
    expect(timeline->nameColumnWidthForTest() == 0,
           QStringLiteral("timeline still reserves a name column"));
    expect(timeline->findChild<QLineEdit*>(
               QStringLiteral("timelineSearchEdit"))
               == nullptr,
           QStringLiteral("timeline-side entity search still exists"));
    expect(timeline->entityRowCountForTest() == 3,
           QStringLiteral("entity row count is wrong"));
    expect(timeline->rowNameForTest(rowFor(background))
               == QStringLiteral("背景B"),
           QStringLiteral("clip name is not the entity name"));
    expect(!timeline->clipNameTextForTest(idFor(background)).isEmpty(),
           QStringLiteral("clip does not draw the entity name"));

    // 2. Ruler density table and minor ticks.
    auto checkSteps = [&](double pixels, int tick, int label) {
        timeline->setPixelsPerFrameForTest(pixels);
        expect(timeline->rulerTickStepForTest() == tick
                   && timeline->rulerLabelStepForTest() == label,
               QStringLiteral("ruler steps at %1 px are %2/%3, expected %4/%5")
                   .arg(pixels)
                   .arg(timeline->rulerTickStepForTest())
                   .arg(timeline->rulerLabelStepForTest())
                   .arg(tick)
                   .arg(label));
    };
    checkSteps(8.0, 1, 5);
    checkSteps(4.0, 5, 10);
    checkSteps(2.0, 10, 50);
    checkSteps(0.8, 20, 100);
    checkSteps(0.4, 50, 200);
    timeline->setPixelsPerFrameForTest(6.0);
    QApplication::processEvents();

    // 3. Clip appearance is square and flat, not rounded/gradient.
    expect(timeline->clipCornerRadiusForTest() == 0,
           QStringLiteral("clip corner radius is not zero"));
    const QRect propClip = timeline->clipRectFor(idFor(prop));
    expect(!propClip.isEmpty(), QStringLiteral("clip rect is missing"));
    if (!propClip.isEmpty())
    {
        const QImage shot = timeline->grab().toImage();
        const QColor corner =
            shot.pixelColor(propClip.left() + 2, propClip.top() + 2);
        const QColor center = shot.pixelColor(propClip.center());
        const int distance = std::abs(corner.red() - center.red())
                             + std::abs(corner.green() - center.green())
                             + std::abs(corner.blue() - center.blue());
        expect(distance < 24,
               QStringLiteral("clip corner is not flat (corner %1, center %2)")
                   .arg(corner.name(), center.name()));
    }

    // 4. Default clip mode: clicking a diamond selects it, seeks, and drags.
    expect(timeline->editMode() == pvf::TimelinePanel::EditMode::Clips,
           QStringLiteral("timeline does not start in clip mode"));
    const int heroRow = rowFor(hero);
    const QPoint key30(timeline->frameXForTest(30),
                       timeline->rowCenterYForTest(heroRow));
    click(key30);
    expect(timeline->selectedKeyframeCount() == 1,
           QStringLiteral("clicking a diamond in clip mode did not select it"));
    expect(window->currentFrameForTest() == 30,
           QStringLiteral("clicking a diamond did not seek (%1)")
               .arg(window->currentFrameForTest()));

    const int depthBeforeKeyDrag = flush();
    const QPoint key45(timeline->frameXForTest(45),
                       timeline->rowCenterYForTest(heroRow));
    sendMouse(QEvent::MouseButtonPress, key30, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, key45, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, key45, Qt::NoButton, Qt::NoModifier);
    QApplication::processEvents();
    flush();
    const pvf::model::Entity& heroAfter = live->project().entities.value(hero);
    bool has30 = false;
    bool has45 = false;
    for (const pvf::model::Keyframe& key : heroAfter.tracks.value(QStringLiteral("x")).keys)
    {
        if (key.frame == 30)
            has30 = true;
        if (key.frame == 45)
            has45 = true;
    }
    expect(has45 && !has30,
           QStringLiteral("dragging the diamond did not move the key"));
    expect(window->currentFrameForTest() == 45,
           QStringLiteral("playhead did not follow the dragged key (%1)")
               .arg(window->currentFrameForTest()));
    expect(flush() == depthBeforeKeyDrag + 1,
           QStringLiteral("key drag did not create exactly one commit"));

    // 5. Eye / lock buttons commit through EditActions exactly once.
    auto entityFlag = [&](const pvf::Uuid& uuid, bool hidden) {
        const auto it = live->project().entities.constFind(uuid);
        if (it == live->project().entities.constEnd())
            return false;
        return hidden ? it->hidden : it->locked;
    };
    auto clickRowButton = [&](const pvf::Uuid& uuid, int index) {
        const int row = rowFor(uuid);
        click(timeline->rowButtonRect(row, index).center());
        QApplication::processEvents();
        flush();
    };
    int depthBeforeFlag = flush();
    clickRowButton(background, 0);
    expect(entityFlag(background, true),
           QStringLiteral("eye button did not hide the entity"));
    expect(flush() == depthBeforeFlag + 1,
           QStringLiteral("eye button did not commit once"));
    depthBeforeFlag = flush();
    clickRowButton(hero, 1);
    expect(entityFlag(hero, false),
           QStringLiteral("lock button did not lock the entity"));
    expect(flush() == depthBeforeFlag + 1,
           QStringLiteral("lock button did not commit once"));
    // Unlock so the screenshot shows an editable clip again.
    clickRowButton(hero, 1);
    expect(!entityFlag(hero, false), QStringLiteral("lock button did not unlock"));

    // 6. Screenshot in frame mode so the 1/5/10 tick density is visible.
    timeline->setPixelsPerFrameForTest(6.0);
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Clips);
    timeline->setDisplayFrames(true);
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    expect(window->grab().save(
               QStringLiteral("/tmp/opencode/pvforge-timeline-ui.png")),
           QStringLiteral("screenshot failed"));
    timeline->setDisplayFrames(false);

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-ui-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int timelineAssetsTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("timeline-assets.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("时间轴素材栏");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-assets-test: create failed" << error;
        return 1;
    }

    const QString imagePath =
        QDir(temp.path()).filePath(QStringLiteral("短素材.png"));
    QImage image(96, 64, QImage::Format_ARGB32);
    image.fill(QColor(60, 150, 220));
    {
        QPainter painter(&image);
        painter.setPen(Qt::white);
        painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("ASSET"));
    }
    if (!image.save(imagePath))
    {
        qCritical() << "timeline-assets-test: cannot write test image";
        return 1;
    }
    const auto asset = session->assets().importFile(imagePath, &error);
    if (!asset)
    {
        qCritical() << "timeline-assets-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    auto* timeline = window->timelineForTest();
    auto* split =
        window->findChild<QSplitter*>(QStringLiteral("timelineAssetSplit"));
    if (!live || !panel || !timeline || !split)
    {
        qCritical() << "timeline-assets-test: session window widgets missing";
        return 1;
    }

    int failures = 0;
    auto expect = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "timeline-assets-test:" << message;
            ++failures;
        }
        return condition;
    };

    // 1. Clip-first geometry: no left name column and no timeline search.
    expect(timeline->nameColumnWidthForTest() == 0,
           QStringLiteral("timeline still has a name column"));
    expect(timeline->findChild<QLineEdit*>(
               QStringLiteral("timelineSearchEdit"))
               == nullptr,
           QStringLiteral("timeline-side entity search still exists"));

    // 2. Asset bin sits left of the timeline, is compact, and shows data.
    auto* panelWidget =
        window->findChild<QWidget*>(QStringLiteral("timelineAssetPanel"));
    auto* assetList =
        window->findChild<QListWidget*>(QStringLiteral("assetList"));
    auto* importButton =
        window->findChild<QToolButton*>(QStringLiteral("assetImport"));
    auto* tagFilter =
        window->findChild<QComboBox*>(QStringLiteral("assetTagFilter"));
    expect(panelWidget && split->isAncestorOf(panelWidget),
           QStringLiteral("timeline asset bar is missing"));
    expect(assetList && importButton && tagFilter,
           QStringLiteral("asset bar list/import/tag widgets missing"));
    if (panelWidget && assetList)
    {
        const QPoint panelPos = panelWidget->mapTo(window, QPoint(0, 0));
        const QPoint timelinePos = timeline->mapTo(window, QPoint(0, 0));
        expect(panelWidget->width() >= 140 && panelWidget->width() <= 260,
               QStringLiteral("asset bar width is not 160-200 px (%1)")
                   .arg(panelWidget->width()));
        expect(panelPos.x() < timelinePos.x(),
               QStringLiteral("asset bar is not left of the timeline"));
        expect(assetList->count() >= 1,
               QStringLiteral("asset bar list is empty"));
        expect(importButton->isVisible(),
               QStringLiteral("asset import button is hidden"));
        expect(tagFilter->isVisible(),
               QStringLiteral("asset tag filter is hidden"));
    }
    expect(panel->assetCount() >= 1, QStringLiteral("asset entry missing"));
    QString tagError;
    expect(panel->renameAsset(asset->hash, QStringLiteral("短素材"), &tagError),
           QStringLiteral("renameAsset failed: %1").arg(tagError));
    panel->refresh();
    expect(panel->assetNameAt(0) == QStringLiteral("短素材"),
           QStringLiteral("asset name is wrong: %1").arg(panel->assetNameAt(0)));
    expect(!panel->assetThumbnailAt(0).isNull(),
           QStringLiteral("asset thumbnail is null"));
    expect(!panel->assetMissingAt(0),
           QStringLiteral("freshly imported asset is marked missing"));

    // Search belongs to the asset bin, not the timeline.
    panel->setSearchText(QStringLiteral("短素材"));
    expect(panel->assetCount() == 1,
           QStringLiteral("asset search did not filter: %1").arg(panel->assetCount()));
    panel->setSearchText(QString());
    expect(panel->assetCount() >= 1,
           QStringLiteral("clearing asset search did not restore"));

    // Tags stay available in the compact bar.
    expect(panel->setAssetTags(asset->hash, {QStringLiteral("intro")}, &tagError),
           QStringLiteral("setAssetTags failed: %1").arg(tagError));
    expect(panel->tagFilters().contains(QStringLiteral("intro")),
           QStringLiteral("tag filter missing after tagging"));

    // 3. Drag an asset onto the timeline through the real MIME payload.
    const qsizetype beforeDrop = live->project().entities.size();
    QMimeData* mime = panel->assetMimeData(asset->hash);
    const pvf::EntityId dropped =
        timeline->dropAssetForTest(*mime, 20, 0);
    delete mime;
    QApplication::processEvents();
    expect(dropped != pvf::kInvalidEntity
               && live->project().entities.size() == beforeDrop + 1,
           QStringLiteral("asset drag did not create an entity"));
    const int row = timeline->rowForEntityForTest(dropped);
    expect(row >= 0, QStringLiteral("dropped clip has no row"));
    expect(timeline->rowNameForTest(row) == QStringLiteral("短素材"),
           QStringLiteral("clip name is not the entity name: %1")
               .arg(timeline->rowNameForTest(row)));
    expect(timeline->clipNameTextForTest(dropped) == QStringLiteral("短素材"),
           QStringLiteral("wide clip does not draw the full entity name: %1")
               .arg(timeline->clipNameTextForTest(dropped)));

    // 4. Bottom-up rows and free vertical drop rows still work.
    const int bottom = timeline->bodyBottomForTest();
    expect(timeline->rowTopForTest(0) >= bottom - 31
               && timeline->rowTopForTest(0) <= bottom,
           QStringLiteral("row 0 is not at the bottom"));
    expect(timeline->dropRowForTest(bottom - 5) == 0,
           QStringLiteral("drop row at the bottom is not row 0"));

    // 5. Eye / lock stay reachable inside the clip and commit through edits.
    auto click = [timeline](const QPoint& pos) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos),
                          QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos),
                            QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
    };
    const pvf::Uuid droppedUuid = timeline->uuidForEntityId(dropped);
    click(timeline->rowButtonRect(row, 0).center());
    expect(live->project().entities.value(droppedUuid).hidden,
           QStringLiteral("eye button did not hide the clip"));
    click(timeline->rowButtonRect(row, 0).center());
    expect(!live->project().entities.value(droppedUuid).hidden,
           QStringLiteral("eye button did not show the clip again"));
    click(timeline->rowButtonRect(row, 1).center());
    expect(live->project().entities.value(droppedUuid).locked,
           QStringLiteral("lock button did not lock the clip"));
    click(timeline->rowButtonRect(row, 1).center());
    expect(!live->project().entities.value(droppedUuid).locked,
           QStringLiteral("lock button did not unlock the clip"));

    // 6. Short clip: name elides and the tooltip carries the full identity.
    auto& entity = live->mutableProject().entities[droppedUuid];
    entity.end = entity.start + 5;
    expect(live->document()->commitEntities({droppedUuid},
                                            QStringLiteral("short clip"), &error),
           QStringLiteral("short clip commit failed: %1").arg(error));
    QApplication::processEvents();
    expect(timeline->clipNameElidedForTest(dropped),
           QStringLiteral("short clip name is not elided: %1")
               .arg(timeline->clipNameTextForTest(dropped)));
    const QPoint shortPoint = timeline->clipRectFor(dropped).center();
    QMouseEvent move(QEvent::MouseMove, QPointF(shortPoint),
                     QPointF(timeline->mapToGlobal(shortPoint)), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &move);
    QApplication::processEvents();
    const QString tip = timeline->toolTip();
    expect(tip.contains(QStringLiteral("短素材")),
           QStringLiteral("short clip tooltip lost the entity name: %1").arg(tip));
    expect(tip.contains(QStringLiteral("时长")),
           QStringLiteral("short clip tooltip lost the duration: %1").arg(tip));

    // 7. Missing asset state remains visible in the same compact bar.
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    expect(window->grab().save(
               QStringLiteral("/tmp/opencode/pvforge-timeline-assets.png")),
           QStringLiteral("screenshot failed"));

    expect(panel->deleteAsset(asset->hash, &tagError),
           QStringLiteral("deleteAsset failed: %1").arg(tagError));
    panel->refresh();
    int missingRow = -1;
    for (int i = 0; i < panel->assetCount(); ++i)
        if (panel->assetHashAt(i) == asset->hash)
            missingRow = i;
    expect(missingRow >= 0 && panel->assetMissingAt(missingRow),
           QStringLiteral("missing asset is not marked in the bar"));
    expect(!panel->assetMissingReasonAt(missingRow).isEmpty(),
           QStringLiteral("missing asset has no reason"));

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-assets-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int timelineKeysTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b2-keys.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b2 关键帧");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-keys-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto* actions = session->editActions();
    if (!actions)
    {
        qCritical() << "timeline-keys-test: no EditActions";
        return 1;
    }
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-keys-test:" << message;
        return condition;
    };

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.content.kind = pvf::ContentKind::Text;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid clip = makeEntity(QStringLiteral("关键帧片段"), 0, 60, 0);
    const pvf::Uuid other = makeEntity(QStringLiteral("其他片段"), 100, 160, 1);
    const pvf::Uuid solver = makeEntity(QStringLiteral("求解片段"), 0, 60, 2);
    pvf::model::SolverSegment segment;
    segment.id = pvf::model::newUuid();
    segment.effect = pvf::SolverEffect::Gravity;
    segment.startFrame = 10;
    segment.endFrame = 40;
    segment.params.insert(QStringLiteral("targetUuid"), other);
    project.entities[solver].solvers.append(segment);
    pvf::model::Track solverX;
    solverX.keys = {{10, 0.0, {}, {}}};
    project.entities[solver].tracks.insert(QStringLiteral("x"), solverX);

    if (!document->commitEntities({clip, other, solver},
                                  QStringLiteral("p2b2 keys setup"), &error))
    {
        qCritical() << "timeline-keys-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    pvf::EditorSession* sessionPtr = session.get();
    auto flush = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-keys-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto undo = [document, &flush] {
        flush();
        QString undoError;
        if (!document->undo(&undoError))
        {
            qCritical() << "timeline-keys-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto keyAt = [sessionPtr](const pvf::Uuid& uuid, const QString& track,
                              int frame) {
        const auto entity = sessionPtr->project().entities.constFind(uuid);
        if (entity == sessionPtr->project().entities.constEnd())
            return false;
        const auto keys = entity.value().tracks.constFind(track);
        if (keys == entity.value().tracks.constEnd())
            return false;
        for (const auto& key : keys.value().keys)
            if (key.frame == frame)
                return true;
        return false;
    };
    auto keyCount = [sessionPtr](const pvf::Uuid& uuid, const QString& track) {
        const auto entity = sessionPtr->project().entities.constFind(uuid);
        if (entity == sessionPtr->project().entities.constEnd())
            return 0;
        const auto keys = entity.value().tracks.constFind(track);
        return keys == entity.value().tracks.constEnd()
                   ? 0
                   : int(keys.value().keys.size());
    };

    // EditActions: insert, one commit, undo.
    int beforeDepth = depth();
    if (!check(actions->addKeyframe(clip, QStringLiteral("x"), 15, &error),
               QStringLiteral("addKeyframe failed: %1").arg(error)))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 15),
               QStringLiteral("addKeyframe did not write the key")))
        return 1;
    flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("addKeyframe did not commit once")))
        return 1;
    if (!undo())
        return 1;

    // Panel: double-click inserts on the selected track.
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
    auto rowForName = [&](const QString& name) {
        for (int row = 0; row < timeline->entityRowCountForTest(); ++row)
            if (timeline->rowNameForTest(row) == name)
                return row;
        return -1;
    };
    QObject::connect(timeline, &pvf::TimelinePanel::editFailed,
                     [](const QString& message) {
                         qCritical() << "timeline-keys-test: panel edit failed"
                                     << message;
                     });

    const int clipRow = rowForName(QStringLiteral("关键帧片段"));
    if (!check(clipRow >= 0, QStringLiteral("clip row missing")))
        return 1;
    timeline->setSelectedTrack(QStringLiteral("x"));
    beforeDepth = depth();
    const QPoint dblPoint(timeline->frameXForTest(20),
                          timeline->rowCenterYForTest(clipRow));
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(dblPoint),
                            QPointF(timeline->mapToGlobal(dblPoint)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &doubleClick);
    QApplication::processEvents();
    if (!check(keyAt(clip, QStringLiteral("x"), 20),
               QStringLiteral("double-click did not insert a key")))
        return 1;
    live->flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("double-click did not commit once")))
        return 1;
    if (!undo())
        return 1;

    // Two tracks at one frame paint one merged diamond; dragging moves both.
    if (!check(actions->addKeyframe(clip, QStringLiteral("x"), 10, &error)
                   && actions->addKeyframe(clip, QStringLiteral("y"), 10, &error),
               QStringLiteral("merged key setup failed: %1").arg(error)))
        return 1;
    live->flush();
    timeline->setEditMode(pvf::TimelinePanel::EditMode::Keyframes);
    timeline->setSelectedTrack(QStringLiteral("x"));
    beforeDepth = depth();
    const QPoint mergeFrom(timeline->frameXForTest(10),
                           timeline->rowCenterYForTest(clipRow));
    const QPoint mergeTo(timeline->frameXForTest(15),
                         timeline->rowCenterYForTest(clipRow));
    QMouseEvent mergePress(QEvent::MouseButtonPress, QPointF(mergeFrom),
                           QPointF(timeline->mapToGlobal(mergeFrom)),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent mergeMove(QEvent::MouseMove, QPointF(mergeTo),
                          QPointF(timeline->mapToGlobal(mergeTo)), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
    QMouseEvent mergeRelease(QEvent::MouseButtonRelease, QPointF(mergeTo),
                             QPointF(timeline->mapToGlobal(mergeTo)),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &mergePress);
    QApplication::sendEvent(timeline, &mergeMove);
    QApplication::sendEvent(timeline, &mergeRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("merged drag did not commit once")))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 15)
                   && keyAt(clip, QStringLiteral("y"), 15)
                   && !keyAt(clip, QStringLiteral("x"), 10)
                   && !keyAt(clip, QStringLiteral("y"), 10),
               QStringLiteral("merged drag did not move both tracks")))
        return 1;
    if (!undo())
        return 1;

    // Esc cancels a key drag without committing.
    beforeDepth = depth();
    const QPoint cancelFrom(timeline->frameXForTest(10),
                            timeline->rowCenterYForTest(clipRow));
    const QPoint cancelTo(timeline->frameXForTest(20),
                          timeline->rowCenterYForTest(clipRow));
    QMouseEvent cancelPress(QEvent::MouseButtonPress, QPointF(cancelFrom),
                            QPointF(timeline->mapToGlobal(cancelFrom)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent cancelMove(QEvent::MouseMove, QPointF(cancelTo),
                           QPointF(timeline->mapToGlobal(cancelTo)), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &cancelPress);
    QApplication::sendEvent(timeline, &cancelMove);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(timeline, &escape);
    QApplication::processEvents();
    live->flush();
    if (!check(depth() == beforeDepth,
               QStringLiteral("Esc created an undo step")))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 10)
                   && keyAt(clip, QStringLiteral("y"), 10)
                   && !keyAt(clip, QStringLiteral("x"), 20),
               QStringLiteral("Esc did not restore the key preview")))
        return 1;

    // Alt-drag moves only the selected track.
    timeline->setSelectedTrack(QStringLiteral("x"));
    beforeDepth = depth();
    const QPoint singleFrom(timeline->frameXForTest(10),
                            timeline->rowCenterYForTest(clipRow));
    const QPoint singleTo(timeline->frameXForTest(12),
                          timeline->rowCenterYForTest(clipRow));
    QMouseEvent singlePress(QEvent::MouseButtonPress, QPointF(singleFrom),
                            QPointF(timeline->mapToGlobal(singleFrom)),
                            Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    QMouseEvent singleMove(QEvent::MouseMove, QPointF(singleTo),
                           QPointF(timeline->mapToGlobal(singleTo)), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QMouseEvent singleRelease(QEvent::MouseButtonRelease, QPointF(singleTo),
                              QPointF(timeline->mapToGlobal(singleTo)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &singlePress);
    QApplication::sendEvent(timeline, &singleMove);
    QApplication::sendEvent(timeline, &singleRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("single-track drag did not commit once")))
        return 1;
    if (!check(keyAt(clip, QStringLiteral("x"), 12)
                   && keyAt(clip, QStringLiteral("y"), 10)
                   && !keyAt(clip, QStringLiteral("x"), 10),
               QStringLiteral("single-track drag moved the wrong tracks")))
        return 1;
    if (!undo())
        return 1;

    // Click selects the merged diamond, Delete removes every track at the frame.
    const QPoint selectPoint(timeline->frameXForTest(10),
                             timeline->rowCenterYForTest(clipRow));
    QMouseEvent selectPress(QEvent::MouseButtonPress, QPointF(selectPoint),
                            QPointF(timeline->mapToGlobal(selectPoint)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent selectRelease(QEvent::MouseButtonRelease, QPointF(selectPoint),
                              QPointF(timeline->mapToGlobal(selectPoint)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &selectPress);
    QApplication::sendEvent(timeline, &selectRelease);
    QApplication::processEvents();
    if (!check(timeline->selectedKeyframeCount() == 1,
               QStringLiteral("merged diamond was not selected")))
        return 1;
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(timeline, &del);
    QApplication::processEvents();
    live->flush();
    if (!check(keyCount(clip, QStringLiteral("x")) == 0
                   && keyCount(clip, QStringLiteral("y")) == 0,
               QStringLiteral("Delete did not remove the merged keys")))
        return 1;
    if (!undo())
        return 1;

    // Ctrl+C / Ctrl+V pastes at the playhead.
    QApplication::sendEvent(timeline, &selectPress);
    QApplication::sendEvent(timeline, &selectRelease);
    QApplication::processEvents();
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(timeline, &copy);
    live->setPlayhead(40);
    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(timeline, &paste);
    QApplication::processEvents();
    live->flush();
    if (!check(keyAt(clip, QStringLiteral("x"), 40)
                   && keyAt(clip, QStringLiteral("y"), 40),
               QStringLiteral("paste did not land on the playhead")))
        return 1;
    if (!undo())
        return 1;

    // Easing write path; the timeline opens EasingChooser for a key.
    pvf::model::EasingSpec easing;
    easing.type = QStringLiteral("easeinoutcubic");
    error.clear();
    if (!check(actions->setKeyframeEasing(
                   QVector<pvf::KeyRef>{{clip, QStringLiteral("x"), 10}}, easing,
                   &error),
               QStringLiteral("setKeyframeEasing failed: %1").arg(error)))
        return 1;
    {
        bool eased = false;
        const auto track = sessionPtr->project()
                               .entities.value(clip)
                               .tracks.value(QStringLiteral("x"));
        for (const auto& key : track.keys)
            if (key.frame == 10
                && key.easing.type == QStringLiteral("easeinoutcubic"))
                eased = true;
        if (!check(eased, QStringLiteral("key easing was not written")))
            return 1;
    }
    if (!undo())
        return 1;

    // Solver protection: the segment start key cannot be deleted or moved,
    // and normal keys cannot be written inside the segment.
    error.clear();
    if (!check(!actions->deleteKeyframes(
                   QVector<pvf::KeyRef>{{solver, QStringLiteral("x"), 10}},
                   &error),
               QStringLiteral("solver start key delete should fail")))
        return 1;
    error.clear();
    if (!check(!actions->moveKeyframes(
                   QVector<pvf::KeyRef>{{solver, QStringLiteral("x"), 10}}, 1,
                   &error),
               QStringLiteral("solver start key move should fail")))
        return 1;
    error.clear();
    if (!check(!actions->addKeyframe(solver, QStringLiteral("x"), 20, &error),
               QStringLiteral("key inside a solver segment should fail")))
        return 1;
    if (!check(keyAt(solver, QStringLiteral("x"), 10)
                   && keyCount(solver, QStringLiteral("x")) == 1,
               QStringLiteral("failed solver edits changed the keys")))
        return 1;

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!check(window->grab().save(
                   QStringLiteral("/tmp/opencode/pvforge-p2b2-keys.png")),
               QStringLiteral("screenshot failed")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-keys-test OK: double-click add, merged/single-track drag,"
               " Delete, copy/paste, solver start-key protection";
    return 0;
}

int timelineSplitTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b2-split.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b2 切分");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-split-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto* actions = session->editActions();
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-split-test:" << message;
        return condition;
    };

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int start, int end, int row,
                                 const pvf::Uuid& parent = pvf::Uuid()) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.row = row;
        entity.parentUuid = parent;
        entity.content.kind = pvf::ContentKind::Empty;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };
    const pvf::Uuid parent = makeEntity(QStringLiteral("父级"), 0, 60, 0);
    const pvf::Uuid child = makeEntity(QStringLiteral("子级"), 10, 50, 0, parent);
    pvf::model::Track parentX;
    parentX.keys = {{0, 0.0, {}, {}}, {10, 1.0, {}, {}},
                    {30, 2.0, {}, {}}, {50, 3.0, {}, {}}};
    project.entities[parent].tracks.insert(QStringLiteral("x"), parentX);

    pvf::model::Entity video;
    video.uuid = pvf::model::newUuid();
    video.name = QStringLiteral("视频");
    video.start = 0;
    video.end = 40;
    video.row = 1;
    video.content.kind = pvf::ContentKind::Asset;
    video.content.asset.type = pvf::AssetType::Video;
    video.content.asset.hash = QString(64, QLatin1Char('c'));
    video.content.asset.sourceIn = 5;
    project.entities.insert(video.uuid, video);

    const pvf::Uuid locked = makeEntity(QStringLiteral("锁定"), 0, 60, 2);
    project.entities[locked].locked = true;

    if (!document->commitEntities({parent, child, video.uuid, locked},
                                  QStringLiteral("p2b2 split setup"), &error))
    {
        qCritical() << "timeline-split-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    pvf::EditorSession* sessionPtr = session.get();
    auto flush = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-split-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };
    auto undo = [document, &flush] {
        flush();
        QString undoError;
        if (!document->undo(&undoError))
        {
            qCritical() << "timeline-split-test: undo failed" << undoError;
            return false;
        }
        return true;
    };
    auto findByName = [](const pvf::model::Project& p, const QString& name,
                         int start, const pvf::Uuid& exclude) {
        for (auto it = p.entities.constBegin(); it != p.entities.constEnd(); ++it)
            if (it.key() != exclude && it.value().name == name
                && it.value().start == start)
                return it.key();
        return pvf::Uuid();
    };
    auto keyFrames = [](const pvf::model::Project& p, const pvf::Uuid& uuid,
                        const QString& track) {
        QVector<int> frames;
        const auto entity = p.entities.constFind(uuid);
        if (entity == p.entities.constEnd())
            return frames;
        const auto keys = entity.value().tracks.constFind(track);
        if (keys == entity.value().tracks.constEnd())
            return frames;
        for (const auto& key : keys.value().keys)
            frames.append(key.frame);
        std::sort(frames.begin(), frames.end());
        return frames;
    };

    // Parent + child split: the right halves get fresh UUIDs and shifted keys.
    int beforeDepth = depth();
    pvf::EditPlan plan;
    if (!check(actions->planSplit(parent, 30, &plan, &error),
               QStringLiteral("planSplit failed: %1").arg(error)))
        return 1;
    const pvf::Uuid rightParent =
        findByName(plan.project, QStringLiteral("父级"), 30, parent);
    if (!check(!rightParent.isEmpty(),
               QStringLiteral("right parent missing from the plan")))
        return 1;
    const pvf::Uuid rightChild =
        findByName(plan.project, QStringLiteral("子级"), 30, child);
    if (!check(!rightChild.isEmpty()
                   && plan.project.entities.value(rightChild).parentUuid
                          == rightParent,
               QStringLiteral("child was not split under the right parent")))
        return 1;
    if (!check(plan.project.entities.value(parent).end == 30
                   && plan.project.entities.value(rightParent).end == 60
                   && plan.project.entities.value(child).end == 30
                   && plan.project.entities.value(rightChild).end == 50,
               QStringLiteral("split ranges are wrong")))
        return 1;
    if (!check(keyFrames(plan.project, parent, QStringLiteral("x"))
                       == QVector<int>({0, 10})
                   && keyFrames(plan.project, rightParent, QStringLiteral("x"))
                          == QVector<int>({0, 20}),
               QStringLiteral("split keys were not cropped/shifted")))
        return 1;
    if (!check(actions->apply(plan, &error),
               QStringLiteral("split apply failed: %1").arg(error)))
        return 1;
    flush();
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("split did not commit once")))
        return 1;
    if (!undo())
        return 1;

    // Video sourceIn shifts on the right half.
    beforeDepth = depth();
    if (!check(actions->splitClip(video.uuid, 20, &error),
               QStringLiteral("video split failed: %1").arg(error)))
        return 1;
    const pvf::Uuid rightVideo = [&] {
        for (auto it = session->project().entities.constBegin();
             it != session->project().entities.constEnd(); ++it)
            if (it.key() != video.uuid && it.value().name == QStringLiteral("视频"))
                return it.key();
        return pvf::Uuid();
    }();
    if (!check(!rightVideo.isEmpty()
                   && session->project().entities.value(video.uuid).content.asset.sourceIn
                          == 5
                   && session->project().entities.value(rightVideo).content.asset.sourceIn
                          == 25,
               QStringLiteral("video sourceIn was not shifted")))
        return 1;
    if (!undo())
        return 1;

    // Boundaries and locks reject without changing anything.
    const int entityCount = session->project().entities.size();
    if (!check(!actions->splitClip(video.uuid, 0, &error),
               QStringLiteral("split at the start should fail"))
        || !check(!actions->splitClip(video.uuid, 40, &error),
                  QStringLiteral("split at the end should fail"))
        || !check(!actions->splitClip(locked, 20, &error),
                  QStringLiteral("split of a locked entity should fail"))
        || !check(session->project().entities.size() == entityCount,
                  QStringLiteral("failed splits changed the project")))
        return 1;

    // S in the panel splits the selected clip at the playhead.
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
    const pvf::EntityId parentId = timeline->entityIdForUuid(parent);
    timeline->setSelectedEntity(parentId);
    live->setPlayhead(15);
    beforeDepth = depth();
    QKeyEvent splitKey(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
    QApplication::sendEvent(timeline, &splitKey);
    QApplication::processEvents();
    live->flush();
    const pvf::Uuid rightByS = [&] {
        for (auto it = live->project().entities.constBegin();
             it != live->project().entities.constEnd(); ++it)
            if (it.key() != parent && it.value().name == QStringLiteral("父级")
                && it.value().start == 15)
                return it.key();
        return pvf::Uuid();
    }();
    if (!check(!rightByS.isEmpty()
                   && live->project().entities.value(parent).end == 15,
               QStringLiteral("S did not split at the playhead")))
        return 1;
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("S split did not commit once")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("S split undo failed: %1").arg(error)))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-split-test OK: parent/child, video sourceIn,"
               " boundary and lock rejection, S shortcut";
    return 0;
}

int timelineMarkerTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p2b2-marker.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P2b2 标记");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "timeline-marker-test: create failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto* actions = session->editActions();
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "timeline-marker-test:" << message;
        return condition;
    };

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("片段");
    entity.start = 0;
    entity.end = 120;
    entity.content.kind = pvf::ContentKind::Empty;
    session->mutableProject().entities.insert(entity.uuid, entity);
    if (!document->commitEntities({entity.uuid}, QStringLiteral("p2b2 marker setup"),
                                  &error))
    {
        qCritical() << "timeline-marker-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    pvf::EditorSession* sessionPtr = session.get();
    auto flush = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "timeline-marker-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };

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

    // M adds a marker at the playhead.
    live->setPlayhead(30);
    int beforeDepth = depth();
    QKeyEvent markerKey(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier);
    QApplication::sendEvent(timeline, &markerKey);
    QApplication::processEvents();
    live->flush();
    if (!check(live->project().markers.size() == 1
                   && live->project().markers.first().frame == 30,
               QStringLiteral("M did not add a marker at the playhead")))
        return 1;
    if (!check(depth() == beforeDepth + 1,
               QStringLiteral("marker add did not commit once")))
        return 1;

    // Dragging the marker changes its frame in one commit.
    const QRect markerRect = timeline->markerRectForTest(0);
    if (!check(!markerRect.isEmpty(), QStringLiteral("marker rect missing")))
        return 1;
    const QPoint markerFrom = markerRect.center();
    const QPoint markerTo(timeline->frameXForTest(45), markerRect.center().y());
    QMouseEvent markerPress(QEvent::MouseButtonPress, QPointF(markerFrom),
                            QPointF(timeline->mapToGlobal(markerFrom)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent markerMove(QEvent::MouseMove, QPointF(markerTo),
                           QPointF(timeline->mapToGlobal(markerTo)), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QMouseEvent markerRelease(QEvent::MouseButtonRelease, QPointF(markerTo),
                              QPointF(timeline->mapToGlobal(markerTo)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(timeline, &markerPress);
    QApplication::sendEvent(timeline, &markerMove);
    QApplication::sendEvent(timeline, &markerRelease);
    QApplication::processEvents();
    live->flush();
    if (!check(live->project().markers.first().frame == 45,
               QStringLiteral("marker drag did not change the frame")))
        return 1;
    if (!check(depth() == beforeDepth + 2,
               QStringLiteral("marker drag did not commit once")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("marker drag undo failed: %1").arg(error)))
        return 1;
    if (!check(live->project().markers.first().frame == 30,
               QStringLiteral("marker drag undo did not restore the frame")))
        return 1;

    // Edit title/color/note/tags; empty titles are rejected.
    pvf::model::Marker marker = live->project().markers.first();
    marker.title = QStringLiteral("高潮");
    marker.color = QStringLiteral("#ff3355");
    marker.note = QStringLiteral("副歌开始");
    marker.tags = {QStringLiteral("音乐"), QStringLiteral("段落")};
    if (!check(actions->updateMarker(marker, &error),
               QStringLiteral("updateMarker failed: %1").arg(error)))
        return 1;
    const auto updated = live->project().markers.first();
    if (!check(updated.title == QStringLiteral("高潮")
                   && updated.color == QStringLiteral("#ff3355")
                   && updated.note == QStringLiteral("副歌开始")
                   && updated.tags.size() == 2,
               QStringLiteral("marker fields were not saved")))
        return 1;
    live->flush();
    pvf::model::Marker invalid = updated;
    invalid.title.clear();
    error.clear();
    if (!check(!actions->updateMarker(invalid, &error),
               QStringLiteral("empty marker title should fail")))
        return 1;
    if (!check(live->document()->undo(&error),
               QStringLiteral("marker edit undo failed: %1").arg(error)))
        return 1;

    // Delete and undo.
    const QString markerId = live->project().markers.first().id;
    if (!check(actions->deleteMarker(markerId, &error),
               QStringLiteral("deleteMarker failed: %1").arg(error)))
        return 1;
    if (!check(live->project().markers.isEmpty(),
               QStringLiteral("marker was not deleted")))
        return 1;
    live->flush();
    if (!check(live->document()->undo(&error),
               QStringLiteral("marker delete undo failed: %1").arg(error)))
        return 1;
    if (!check(live->project().markers.size() == 1,
               QStringLiteral("marker delete undo did not restore it")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "timeline-marker-test OK: M add, drag frame, edit fields,"
               " validation, delete and undo";
    return 0;
}

int timelineEditE2eTest(QApplication& app, const QString& projectDir)
{
    QString error;
    auto session = pvf::EditorSession::openProject(projectDir, &error);
    if (!session)
    {
        qCritical() << "timeline-edit-e2e: open failed" << error;
        return 1;
    }
    auto* document = session->document();
    auto findUuid = [&](const QString& name) {
        for (auto it = session->project().entities.constBegin();
             it != session->project().entities.constEnd(); ++it)
            if (it.value().name == name)
                return it.key();
        return pvf::Uuid();
    };
    const pvf::Uuid audio = findUuid(QStringLiteral("音频"));
    const pvf::Uuid image = findUuid(QStringLiteral("图片"));
    if (audio.isEmpty() || image.isEmpty())
    {
        qCritical() << "timeline-edit-e2e: audio/image entity missing";
        return 1;
    }
    const int audioStartBefore = session->project().entities.value(audio).start;
    const int audioEndBefore = session->project().entities.value(audio).end;
    const int imageStartBefore = session->project().entities.value(image).start;
    const int imageEndBefore = session->project().entities.value(image).end;

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 820);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* live = window->editorSessionForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !timeline)
    {
        qCritical() << "timeline-edit-e2e: window has no session timeline";
        return 1;
    }
    auto rowForUuid = [&](const pvf::Uuid& uuid) {
        return timeline->rowForEntityForTest(timeline->entityIdForUuid(uuid));
    };
    auto dragClip = [&](const pvf::Uuid& uuid, int grabFrame, int delta) {
        const int row = rowForUuid(uuid);
        if (row < 0)
            return false;
        const QPoint from(timeline->frameXForTest(grabFrame),
                          timeline->rowCenterYForTest(row));
        const QPoint to(timeline->frameXForTest(grabFrame + delta),
                        timeline->rowCenterYForTest(row));
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          QPointF(timeline->mapToGlobal(from)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         QPointF(timeline->mapToGlobal(to)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to),
                            QPointF(timeline->mapToGlobal(to)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &move);
        QApplication::sendEvent(timeline, &release);
        QApplication::processEvents();
        return true;
    };
    if (!dragClip(audio, audioStartBefore + 10, 30)
        || !dragClip(image, imageStartBefore + 10, 20))
    {
        qCritical() << "timeline-edit-e2e: drag failed";
        return 1;
    }
    live->flush();

    auto readRange = [&](const pvf::Uuid& uuid, int* start, int* end) {
        QFile file(live->root() + QStringLiteral("/entities/") + uuid
                   + QStringLiteral(".json"));
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QJsonObject object =
            QJsonDocument::fromJson(file.readAll()).object();
        *start = object.value(QStringLiteral("start")).toInt();
        *end = object.value(QStringLiteral("end")).toInt();
        return true;
    };
    int audioStart = 0;
    int audioEnd = 0;
    int imageStart = 0;
    int imageEnd = 0;
    if (!readRange(audio, &audioStart, &audioEnd)
        || !readRange(image, &imageStart, &imageEnd))
    {
        qCritical() << "timeline-edit-e2e: cannot read entity JSON";
        return 1;
    }
    if (audioStart != audioStartBefore + 30
        || audioEnd != audioEndBefore + 30
        || imageStart != imageStartBefore + 20
        || imageEnd != imageEndBefore + 20)
    {
        qCritical() << "timeline-edit-e2e: ranges wrong"
                    << "audio" << audioStartBefore << audioEndBefore << "->"
                    << audioStart << audioEnd << "image" << imageStartBefore
                    << imageEndBefore << "->" << imageStart << imageEnd;
        return 1;
    }

    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!window->grab().save(
            QStringLiteral("/tmp/opencode/pvforge-p2b1-edit.png")))
    {
        qCritical() << "timeline-edit-e2e: screenshot failed";
        return 1;
    }
    qInfo() << "timeline-edit-e2e OK: audio"
            << audioStartBefore << "->" << audioStart << " image"
            << imageStartBefore << "->" << imageStart << projectDir;
    window->close();
    QApplication::processEvents();
    Q_UNUSED(document);
    Q_UNUSED(app);
    return 0;
}

int saveTest()
{
    pvf::Project empty;
    if (empty.timelineLength() != 0)
    {
        qCritical() << "save-test: empty project length should be 0, got"
                    << empty.timelineLength();
        return 1;
    }
    const QString imagePath = QDir::tempPath() + QStringLiteral("/pvforge_savetest.png");
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_savetest.pvproj");

    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::cyan);
    if (!img.save(imagePath))
        return 1;

    pvf::Project before;
    before.name = QStringLiteral("往返测试");
    before.author = QStringLiteral("tester");
    before.width = 1280;
    before.height = 720;
    before.fps = 30;

    pvf::Entity* image = before.addImage(imagePath);
    if (!image)
        return 1;
    const pvf::EntityId imageId = image->id;
    before.setValue(imageId, QStringLiteral("x"), 0, 0, true);
    before.setValue(imageId, QStringLiteral("x"), 90, 600, true);

    // Timeline spans the last keyframe (90), not a fixed project duration.
    if (before.timelineLength() != 90)
    {
        qCritical() << "save-test: expected length 90, got" << before.timelineLength();
        return 1;
    }

    QString error;
    if (!before.save(projectPath, &error))
    {
        qCritical() << "save-test: save failed" << error;
        return 1;
    }

    pvf::Project after;
    if (!after.load(projectPath, &error))
    {
        qCritical() << "save-test: load failed" << error;
        return 1;
    }

    if (after.name != before.name || after.fps != 30 || after.entities().size() != 1)
    {
        qCritical() << "save-test: metadata mismatch";
        return 1;
    }
    if (after.width != 1280 || after.height != 720)
    {
        qCritical() << "save-test: resolution mismatch";
        return 1;
    }
    if (after.templates().size() != 1)
    {
        qCritical() << "save-test: templates" << after.templates().size();
        return 1;
    }
    if (after.entities().first().templateId != after.templates().first().id)
    {
        qCritical() << "save-test: template association lost";
        return 1;
    }
    if (std::abs(after.value(imageId, QStringLiteral("x"), 45) - 300.0) > 1e-6)
    {
        qCritical() << "save-test: keyframe roundtrip mismatch";
        return 1;
    }
    if (after.timelineLength() != 90)
    {
        qCritical() << "save-test: length after load" << after.timelineLength();
        return 1;
    }
    if (after.entities().first().image.isNull())
    {
        qCritical() << "save-test: image not reloaded";
        return 1;
    }

    QFile::remove(projectPath);
    QFile::remove(imagePath);
    qInfo() << "save-test OK: roundtrip length" << after.timelineLength();
    return 0;
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJsonObject(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
}

bool rewriteProjectVersion(const QString& source, const QString& target, int version,
                           bool dropScenes)
{
    QJsonObject root = readJsonObject(source);
    if (root.isEmpty())
        return false;
    root[QStringLiteral("version")] = version;
    if (dropScenes)
    {
        root.remove(QStringLiteral("scenes"));
        root.remove(QStringLiteral("annotations"));
    }
    return writeJsonObject(target, root);
}

QVector<pvf::EntityId> drawableOrder(const pvf::Project& project)
{
    QVector<pvf::EntityId> order;
    for (int index : project.entityOrder())
    {
        const pvf::Entity& entity = project.entities()[index];
        if (!entity.isFolder())
            order.append(entity.id);
    }
    return order;
}

bool isIdentityFolder(const pvf::Project& project, pvf::EntityId id)
{
    const pvf::Entity* folder = project.entity(id);
    if (!folder || !folder->isFolder())
        return false;
    const auto zero = [&project, id](const QString& prop) {
        return std::abs(project.value(id, prop, 0)) < 1e-9;
    };
    return zero(QStringLiteral("x")) && zero(QStringLiteral("y"))
           && zero(QStringLiteral("rotation")) && zero(QStringLiteral("rotPointX"))
           && zero(QStringLiteral("rotPointY"))
           && std::abs(project.value(id, QStringLiteral("scaleX"), 0) - 1.0) < 1e-9
           && std::abs(project.value(id, QStringLiteral("scaleY"), 0) - 1.0) < 1e-9
           && std::abs(project.value(id, QStringLiteral("opacity"), 0) - 1.0) < 1e-9
           && folder->tracks.isEmpty();
}

bool transformNear(const QTransform& a, const QTransform& b)
{
    const QPointF points[] = {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1),
                              QPointF(37, -19)};
    for (const QPointF& point : points)
        if ((a.map(point) - b.map(point)).manhattanLength() > 1e-9)
            return false;
    return true;
}

int sceneTest()
{
    int failures = 0;
    const QString dir = QDir::tempPath();
    const QString basePath = dir + QStringLiteral("/pvforge_scene_base.pvproj");
    const QString v3Path = dir + QStringLiteral("/pvforge_scene_v3.pvproj");
    const QString v1Path = dir + QStringLiteral("/pvforge_scene_v1.pvproj");
    const QString v2Path = dir + QStringLiteral("/pvforge_scene_v2.pvproj");
    const QString tooNewPath = dir + QStringLiteral("/pvforge_scene_v5.pvproj");
    const QString emptyPath = dir + QStringLiteral("/pvforge_scene_empty.pvproj");
    const QString emptyV3Path = dir + QStringLiteral("/pvforge_scene_empty_v3.pvproj");
    QString error;

    // ---- Scene API, ranges and ordering --------------------------------
    pvf::Project project;
    project.fps = 30;
    pvf::Entity* a =
        project.addVector(0, 120, 90, QColor(230, 90, 90), Qt::white, 0);
    const pvf::EntityId aId = a->id;
    pvf::Entity* b =
        project.addVector(0, 120, 90, QColor(90, 140, 230), Qt::white, 0);
    const pvf::EntityId bId = b->id;
    pvf::Entity* camera = project.addCamera();
    const pvf::EntityId cameraId = camera->id;
    project.setValue(aId, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(aId, QStringLiteral("x"), 120, 300.0, true);
    // Outside the project-space clamp so migration must preserve clamping.
    project.setValue(cameraId, QStringLiteral("x"), 0, 400.0, false);

    if (project.timelineLength() != 120)
    {
        qCritical() << "scene-test: setup length" << project.timelineLength();
        return 1;
    }

    pvf::Scene* first =
        project.addScene(QStringLiteral("开场"), 0, 60, QColor(255, 80, 80),
                         QStringLiteral("第一段"), &error);
    if (!first)
    {
        qCritical() << "scene-test: add first scene" << error;
        return 1;
    }
    const QString firstId = first->id;
    const pvf::EntityId firstFolder = first->folderId;
    if (!isIdentityFolder(project, firstFolder))
    {
        qCritical() << "scene-test: first folder is not identity";
        ++failures;
    }
    if (!project.entity(firstFolder) || !project.entity(firstFolder)->isFolder())
    {
        qCritical() << "scene-test: scene folder missing";
        ++failures;
    }

    error.clear();
    if (project.addScene(QStringLiteral("重叠"), 50, 90, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: overlapping scene was accepted";
        return 1;
    }
    error.clear();
    if (project.addScene(QStringLiteral("负数"), -1, 10, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: negative start was accepted";
        return 1;
    }
    error.clear();
    if (project.addScene(QStringLiteral("倒序"), 20, 10, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: reversed range was accepted";
        return 1;
    }
    error.clear();
    if (project.addScene(QStringLiteral("越界"), 100, 130, QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: out-of-timeline range was accepted";
        return 1;
    }
    if (project.scenes().size() != 1)
    {
        qCritical() << "scene-test: rejected scenes leaked into the list"
                    << project.scenes().size();
        ++failures;
    }

    pvf::Scene* second =
        project.addScene(QStringLiteral("后段"), 70, 120, QColor(80, 200, 120),
                         QStringLiteral("第二段"), &error);
    if (!second)
    {
        qCritical() << "scene-test: add second scene" << error;
        return 1;
    }
    const QString secondId = second->id;
    const pvf::EntityId secondFolder = second->folderId;
    if (project.scenes().size() != 2
        || project.scenes()[0].id != firstId
        || project.scenes()[1].id != secondId)
    {
        qCritical() << "scene-test: scenes are not sorted by start frame";
        ++failures;
    }

    auto expectSceneAt = [&](int frame, const QString& expected) {
        const pvf::Scene* scene = project.sceneAt(frame);
        const QString actual = scene ? scene->id : QString();
        if (actual != expected)
        {
            qCritical() << "scene-test: sceneAt" << frame << actual << expected;
            ++failures;
        }
    };
    expectSceneAt(0, firstId);
    expectSceneAt(60, firstId);
    expectSceneAt(61, QString());
    expectSceneAt(69, QString());
    expectSceneAt(70, secondId);
    expectSceneAt(120, secondId);
    expectSceneAt(121, QString());
    expectSceneAt(-1, QString());
    if (project.sceneFolderAt(30) != firstFolder
        || project.sceneFolderAt(65) != pvf::kInvalidEntity
        || project.sceneFolderAt(80) != secondFolder)
    {
        qCritical() << "scene-test: sceneFolderAt boundary";
        ++failures;
    }
    if (!project.sceneById(firstId) || project.sceneById(QStringLiteral("missing")))
    {
        qCritical() << "scene-test: sceneById";
        ++failures;
    }

    if (!project.updateScene(firstId, QStringLiteral("开场改"), 0, 65,
                             QColor(10, 20, 30), QStringLiteral("改过"), &error))
    {
        qCritical() << "scene-test: update scene" << error;
        ++failures;
    }
    const pvf::Scene* updatedFirst = project.sceneById(firstId);
    if (!updatedFirst || updatedFirst->name != QStringLiteral("开场改")
        || updatedFirst->endFrame != 65
        || updatedFirst->color.name(QColor::HexArgb)
               != QColor(10, 20, 30).name(QColor::HexArgb)
        || updatedFirst->note != QStringLiteral("改过"))
    {
        qCritical() << "scene-test: updated fields mismatch";
        ++failures;
    }
    error.clear();
    if (project.updateScene(firstId, QStringLiteral("开场改"), 0, 75, QColor(),
                            QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: overlapping update was accepted";
        ++failures;
    }
    error.clear();
    if (project.updateScene(firstId, QStringLiteral("开场改"), 0, 130, QColor(),
                            QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: out-of-range update was accepted";
        ++failures;
    }
    error.clear();
    if (project.updateScene(QStringLiteral("missing"), QStringLiteral("x"), 0, 1,
                            QColor(), QString(), &error)
        || error.isEmpty())
    {
        qCritical() << "scene-test: missing scene update was accepted";
        ++failures;
    }

    // Removing a scene keeps its folder and children as ordinary data.
    if (!project.setEntityParent(bId, secondFolder)
        || !project.removeScene(secondId))
    {
        qCritical() << "scene-test: remove scene failed";
        ++failures;
    }
    const pvf::Entity* orphanedFolder = project.entity(secondFolder);
    const pvf::Entity* movedB = project.entity(bId);
    if (project.sceneById(secondId) || !orphanedFolder || !movedB
        || movedB->parent != secondFolder)
    {
        qCritical() << "scene-test: remove scene deleted entities";
        ++failures;
    }
    if (project.removeScene(secondId))
    {
        qCritical() << "scene-test: removing twice succeeded";
        ++failures;
    }

    if (!project.setKeyframeNote(aId, QStringLiteral("x"), 120.0,
                                 QStringLiteral("收尾")))
    {
        qCritical() << "scene-test: keyframe note set failed";
        ++failures;
    }
    if (project.keyframeNote(aId, QStringLiteral("x"), 120.0)
        != QStringLiteral("收尾"))
    {
        qCritical() << "scene-test: keyframe note query";
        ++failures;
    }

    // ---- v4 roundtrip --------------------------------------------------
    error.clear();
    if (!project.save(basePath, &error))
    {
        qCritical() << "scene-test: save v4" << error;
        return 1;
    }
    const QJsonObject savedRoot = readJsonObject(basePath);
    if (savedRoot.value(QStringLiteral("version")).toInt() != pvf::kProjectVersion
        || !savedRoot.contains(QStringLiteral("scenes"))
        || !savedRoot.contains(QStringLiteral("annotations"))
        || savedRoot.value(QStringLiteral("scenes")).toArray().size() != 1
        || !savedRoot.value(QStringLiteral("annotations")).toArray().isEmpty())
    {
        qCritical() << "scene-test: v4 json shape";
        ++failures;
    }
    bool noteAt120 = false;
    bool noteAt0 = true;
    for (const QJsonValue& value : savedRoot.value(QStringLiteral("entities")).toArray())
    {
        const QJsonObject entity = value.toObject();
        if (entity.value(QStringLiteral("id")).toInt() != aId)
            continue;
        const QJsonArray keys =
            entity.value(QStringLiteral("tracks")).toObject()
                .value(QStringLiteral("x")).toArray();
        for (const QJsonValue& keyValue : keys)
        {
            const QJsonObject key = keyValue.toObject();
            const double frame = key.value(QStringLiteral("f")).toDouble();
            if (std::abs(frame - 120.0) < 1e-6)
                noteAt120 = key.value(QStringLiteral("note")).toString()
                            == QStringLiteral("收尾");
            if (std::abs(frame) < 1e-6)
                noteAt0 = !key.contains(QStringLiteral("note"));
        }
    }
    if (!noteAt120 || !noteAt0)
    {
        qCritical() << "scene-test: keyframe note json" << noteAt120 << noteAt0;
        ++failures;
    }

    pvf::Project loaded;
    if (!loaded.load(basePath, &error))
    {
        qCritical() << "scene-test: load v4" << error;
        return 1;
    }
    if (loaded.scenes().size() != 1
        || loaded.scenes().first().id != firstId
        || loaded.scenes().first().folderId != firstFolder
        || loaded.scenes().first().endFrame != 65
        || loaded.timelineLength() != 120
        || !isIdentityFolder(loaded, firstFolder))
    {
        qCritical() << "scene-test: v4 scene roundtrip";
        ++failures;
    }
    if (const pvf::Entity* loadedB = loaded.entity(bId);
        !loadedB || loadedB->parent != secondFolder)
    {
        qCritical() << "scene-test: entity parent roundtrip";
        ++failures;
    }
    if (loaded.keyframeNote(aId, QStringLiteral("x"), 120.0)
        != QStringLiteral("收尾"))
    {
        qCritical() << "scene-test: keyframe note roundtrip";
        ++failures;
    }

    // ---- v3 migration: one default scene, identity folder --------------
    pvf::Project v3;
    v3.fps = 30;
    pvf::Entity* rootA =
        v3.addVector(0, 100, 80, QColor(200, 100, 100), Qt::white, 0);
    const pvf::EntityId rootAId = rootA->id;
    pvf::Entity* group = v3.addFolder(QStringLiteral("Group"));
    const pvf::EntityId groupId = group->id;
    pvf::Entity* nested =
        v3.addVector(0, 100, 80, QColor(100, 200, 100), Qt::white, 0);
    const pvf::EntityId nestedId = nested->id;
    pvf::Entity* v3Camera = v3.addCamera();
    const pvf::EntityId v3CameraId = v3Camera->id;
    pvf::Entity* rootB =
        v3.addVector(0, 100, 80, QColor(100, 100, 200), Qt::white, 0);
    const pvf::EntityId rootBId = rootB->id;
    v3.setEntityParent(nestedId, groupId);
    v3.setValue(rootAId, QStringLiteral("x"), 0, 0.0, true);
    v3.setValue(rootAId, QStringLiteral("x"), 45, 200.0, true);
    v3.setValue(rootAId, QStringLiteral("x"), 90, 600.0, true);
    v3.setValue(v3CameraId, QStringLiteral("x"), 0, 400.0, false);

    const int beforeLength = v3.timelineLength();
    if (beforeLength != 90)
    {
        qCritical() << "scene-test: migration setup length" << beforeLength;
        return 1;
    }
    const int frames[] = {0, 45, 90};
    const QVector<pvf::EntityId> migrationIds = {rootAId, groupId, nestedId,
                                                 v3CameraId, rootBId};
    QVector<QTransform> beforeTransforms;
    QVector<pvf::Camera::State> beforeCameras;
    for (int frame : frames)
    {
        for (pvf::EntityId id : migrationIds)
        {
            const pvf::Entity* entity = v3.entity(id);
            if (!entity)
            {
                qCritical() << "scene-test: migration entity missing" << id;
                return 1;
            }
            beforeTransforms.append(
                pvf::SceneRenderer::worldTransform(v3, *entity, frame));
        }
        beforeCameras.append(pvf::Camera::state(v3, v3CameraId, frame));
    }
    const QVector<pvf::EntityId> beforeOrder = drawableOrder(v3);

    error.clear();
    if (!v3.save(basePath, &error))
    {
        qCritical() << "scene-test: save migration source" << error;
        return 1;
    }
    if (!rewriteProjectVersion(basePath, v3Path, 3, true))
    {
        qCritical() << "scene-test: rewrite v3 json";
        return 1;
    }

    pvf::Project migrated;
    error.clear();
    if (!migrated.load(v3Path, &error))
    {
        qCritical() << "scene-test: load v3" << error;
        return 1;
    }
    if (migrated.scenes().size() != 1)
    {
        qCritical() << "scene-test: v3 default scene count"
                    << migrated.scenes().size();
        return 1;
    }
    const pvf::Scene& defaultScene = migrated.scenes().first();
    const pvf::EntityId sceneFolder = defaultScene.folderId;
    if (defaultScene.startFrame != 0 || defaultScene.endFrame != beforeLength
        || !isIdentityFolder(migrated, sceneFolder))
    {
        qCritical() << "scene-test: v3 default scene range/folder";
        ++failures;
    }
    if (migrated.timelineLength() != beforeLength
        || migrated.entities().size() != v3.entities().size() + 1)
    {
        qCritical() << "scene-test: v3 migration changed content"
                    << migrated.timelineLength() << migrated.entities().size();
        ++failures;
    }
    for (pvf::EntityId id : {rootAId, groupId, v3CameraId, rootBId})
    {
        const pvf::Entity* entity = migrated.entity(id);
        if (!entity || entity->parent != sceneFolder)
        {
            qCritical() << "scene-test: root not moved into default scene" << id;
            ++failures;
        }
    }
    if (const pvf::Entity* entity = migrated.entity(nestedId);
        !entity || entity->parent != groupId)
    {
        qCritical() << "scene-test: nested entity reparented";
        ++failures;
    }
    if (migrated.sceneFolderAt(45) != sceneFolder
        || migrated.sceneAt(90) != migrated.sceneById(defaultScene.id))
    {
        qCritical() << "scene-test: migrated scene lookup";
        ++failures;
    }

    int pose = 0;
    for (int frame : frames)
    {
        for (pvf::EntityId id : migrationIds)
        {
            const pvf::Entity* entity = migrated.entity(id);
            if (!entity
                || !transformNear(beforeTransforms[pose],
                                  pvf::SceneRenderer::worldTransform(migrated,
                                                                     *entity, frame)))
            {
                qCritical() << "scene-test: transform changed" << id << frame;
                ++failures;
            }
            ++pose;
        }
        const pvf::Camera::State state = pvf::Camera::state(migrated, v3CameraId, frame);
        const pvf::Camera::State before = beforeCameras[frame == 0 ? 0
                                                       : frame == 45    ? 1
                                                                        : 2];
        if ((state.center - before.center).manhattanLength() > 1e-9
            || std::abs(state.rotation - before.rotation) > 1e-9
            || std::abs(state.scale - before.scale) > 1e-9)
        {
            qCritical() << "scene-test: camera changed" << frame;
            ++failures;
        }
    }
    if (drawableOrder(migrated) != beforeOrder)
    {
        qCritical() << "scene-test: draw order changed";
        ++failures;
    }

    // v1/v2 files are accepted and migrated the same way.
    for (const auto& pair : {std::pair<int, QString>{1, v1Path},
                             std::pair<int, QString>{2, v2Path}})
    {
        if (!rewriteProjectVersion(basePath, pair.second, pair.first, true))
        {
            qCritical() << "scene-test: rewrite version" << pair.first;
            ++failures;
            continue;
        }
        pvf::Project old;
        error.clear();
        if (!old.load(pair.second, &error) || old.scenes().size() != 1
            || !isIdentityFolder(old, old.scenes().first().folderId))
        {
            qCritical() << "scene-test: load version" << pair.first << error;
            ++failures;
        }
    }

    // Future versions are rejected with a clear error.
    if (!rewriteProjectVersion(basePath, tooNewPath, 5, false))
    {
        qCritical() << "scene-test: rewrite v5";
        ++failures;
    }
    else
    {
        pvf::Project future;
        error.clear();
        if (future.load(tooNewPath, &error) || !error.contains(QStringLiteral("过新")))
        {
            qCritical() << "scene-test: version 5 not rejected" << error;
            ++failures;
        }
    }

    // Empty v3 project: a single frame 0..0 scene.
    pvf::Project empty;
    error.clear();
    if (!empty.save(emptyPath, &error)
        || !rewriteProjectVersion(emptyPath, emptyV3Path, 3, true))
    {
        qCritical() << "scene-test: empty migration setup" << error;
        ++failures;
    }
    else
    {
        pvf::Project emptyLoaded;
        error.clear();
        if (!emptyLoaded.load(emptyV3Path, &error)
            || emptyLoaded.scenes().size() != 1
            || emptyLoaded.scenes().first().startFrame != 0
            || emptyLoaded.scenes().first().endFrame != 0
            || emptyLoaded.timelineLength() != 0
            || emptyLoaded.entities().size() != 1
            || !isIdentityFolder(emptyLoaded,
                                 emptyLoaded.scenes().first().folderId))
        {
            qCritical() << "scene-test: empty v3 migration" << error;
            ++failures;
        }
    }

    for (const QString& path : {basePath, v3Path, v1Path, v2Path, tooNewPath,
                                emptyPath, emptyV3Path})
    {
        QFile::remove(path);
    }
    qInfo() << "scene-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int annotationTest()
{
    int failures = 0;
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_annotation.pvproj");
    const QString v3Path =
        QDir::tempPath() + QStringLiteral("/pvforge_annotation_v3.pvproj");
    QString error;

    pvf::Project project;
    project.fps = 30;
    pvf::Entity* entity =
        project.addVector(0, 100, 80, QColor(220, 120, 90), Qt::white, 0);
    const pvf::EntityId entityId = entity->id;
    project.setValue(entityId, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(entityId, QStringLiteral("x"), 30, 300.0, true);
    const int length = project.timelineLength();
    if (length != 30)
    {
        qCritical() << "annotation-test: setup length" << length;
        return 1;
    }

    pvf::Annotation* late =
        project.addAnnotation(30, QStringLiteral("副歌"), QStringLiteral("**Markdown**"),
                              QStringList{QStringLiteral("副歌"),
                                          QStringLiteral("镜头")},
                              QColor(255, 60, 60), &error);
    const QString lateId = late ? late->id : QString();
    pvf::Annotation* early =
        project.addAnnotation(10, QStringLiteral("开场"), QStringLiteral("first"),
                              QStringList{QStringLiteral("字幕")},
                              QColor(60, 220, 60), &error);
    const QString earlyId = early ? early->id : QString();
    pvf::Annotation* middle =
        project.addAnnotation(20, QStringLiteral("过渡"), QString(),
                              QStringList(), QColor(60, 60, 255), &error);
    const QString middleId = middle ? middle->id : QString();
    if (!late || !early || !middle)
    {
        qCritical() << "annotation-test: add" << error;
        return 1;
    }
    if (project.annotations().size() != 3
        || project.annotations()[0].id != earlyId
        || project.annotations()[1].id != middleId
        || project.annotations()[2].id != lateId)
    {
        qCritical() << "annotation-test: annotations are not sorted";
        ++failures;
    }
    const pvf::Annotation* earlyLoaded = project.annotationById(earlyId);
    if (!earlyLoaded || earlyLoaded->title != QStringLiteral("开场")
        || earlyLoaded->note != QStringLiteral("first")
        || earlyLoaded->tags != QStringList{QStringLiteral("字幕")}
        || earlyLoaded->color.name(QColor::HexArgb)
               != QColor(60, 220, 60).name(QColor::HexArgb))
    {
        qCritical() << "annotation-test: annotation fields";
        ++failures;
    }

    pvf::Annotation* far =
        project.addAnnotation(1000, QStringLiteral("远处"), QString(),
                              QStringList{QStringLiteral("场景")},
                              QColor(200, 200, 0), &error);
    if (!far)
    {
        qCritical() << "annotation-test: add far" << error;
        return 1;
    }
    const QString farId = far->id;
    if (project.timelineLength() != length)
    {
        qCritical() << "annotation-test: annotation extended timeline"
                    << project.timelineLength();
        ++failures;
    }
    if (project.annotations().size() != 4
        || project.annotations().last().id != farId)
    {
        qCritical() << "annotation-test: far annotation not last";
        ++failures;
    }

    pvf::Annotation changed = *project.annotationById(middleId);
    changed.frame = 5;
    changed.title = QStringLiteral("修改");
    changed.note = QStringLiteral("**changed**");
    changed.tags = QStringList{QStringLiteral("场景")};
    changed.color = QColor(1, 2, 3);
    if (!project.updateAnnotation(changed, &error))
    {
        qCritical() << "annotation-test: update" << error;
        ++failures;
    }
    if (project.annotations().size() != 4
        || project.annotations()[0].id != middleId
        || project.annotations()[0].frame != 5
        || project.annotations()[1].id != earlyId
        || project.annotations()[2].id != lateId)
    {
        qCritical() << "annotation-test: update did not re-sort";
        ++failures;
    }
    const pvf::Annotation* middleLoaded = project.annotationById(middleId);
    if (!middleLoaded || middleLoaded->title != QStringLiteral("修改")
        || middleLoaded->note != QStringLiteral("**changed**")
        || middleLoaded->tags != QStringList{QStringLiteral("场景")}
        || middleLoaded->color.name(QColor::HexArgb)
               != QColor(1, 2, 3).name(QColor::HexArgb))
    {
        qCritical() << "annotation-test: updated fields";
        ++failures;
    }

    changed.frame = -1;
    error.clear();
    if (project.updateAnnotation(changed, &error) || error.isEmpty())
    {
        qCritical() << "annotation-test: negative update accepted";
        ++failures;
    }
    if (project.annotationById(middleId)->frame != 5)
    {
        qCritical() << "annotation-test: failed update changed data";
        ++failures;
    }
    error.clear();
    if (project.addAnnotation(-1, QStringLiteral("bad"), QString(), QStringList(),
                              QColor(), &error)
        || error.isEmpty())
    {
        qCritical() << "annotation-test: negative add accepted";
        ++failures;
    }

    if (!project.removeAnnotation(earlyId) || project.annotationById(earlyId)
        || project.removeAnnotation(earlyId))
    {
        qCritical() << "annotation-test: remove";
        ++failures;
    }
    if (project.annotations().size() != 3)
    {
        qCritical() << "annotation-test: remove count"
                    << project.annotations().size();
        ++failures;
    }

    // ---- Keyframe notes ------------------------------------------------
    if (!project.setKeyframeNote(entityId, QStringLiteral("x"), 30.0,
                                 QStringLiteral("收尾"))
        || project.keyframeNote(entityId, QStringLiteral("x"), 30.0)
               != QStringLiteral("收尾"))
    {
        qCritical() << "annotation-test: set keyframe note";
        ++failures;
    }
    if (project.setKeyframeNote(entityId, QStringLiteral("x"), 99.0,
                                QStringLiteral("missing")))
    {
        qCritical() << "annotation-test: missing keyframe note accepted";
        ++failures;
    }
    // Updating a value at an existing keyframe keeps its note.
    project.setValue(entityId, QStringLiteral("x"), 30, 200.0, true);
    if (project.keyframeNote(entityId, QStringLiteral("x"), 30.0)
        != QStringLiteral("收尾"))
    {
        qCritical() << "annotation-test: value update cleared note";
        ++failures;
    }

    error.clear();
    if (!project.save(projectPath, &error))
    {
        qCritical() << "annotation-test: save" << error;
        return 1;
    }
    const QJsonObject root = readJsonObject(projectPath);
    if (root.value(QStringLiteral("version")).toInt() != pvf::kProjectVersion)
    {
        qCritical() << "annotation-test: version";
        ++failures;
    }
    const QJsonArray savedAnnotations =
        root.value(QStringLiteral("annotations")).toArray();
    if (savedAnnotations.size() != 3
        || savedAnnotations[0].toObject().value(QStringLiteral("id")).toString()
               != middleId
        || savedAnnotations[1].toObject().value(QStringLiteral("id")).toString()
               != lateId
        || savedAnnotations[2].toObject().value(QStringLiteral("id")).toString()
               != farId)
    {
        qCritical() << "annotation-test: annotation json order";
        ++failures;
    }
    const QJsonObject savedMiddle = savedAnnotations[0].toObject();
    if (savedMiddle.value(QStringLiteral("title")).toString()
            != QStringLiteral("修改")
        || savedMiddle.value(QStringLiteral("tags")).toArray().size() != 1
        || savedMiddle.value(QStringLiteral("tags")).toArray()[0].toString()
               != QStringLiteral("场景"))
    {
        qCritical() << "annotation-test: annotation json fields";
        ++failures;
    }
    bool noteAt30 = false;
    bool noteAt0 = true;
    for (const QJsonValue& value : root.value(QStringLiteral("entities")).toArray())
    {
        const QJsonObject savedEntity = value.toObject();
        if (savedEntity.value(QStringLiteral("id")).toInt() != entityId)
            continue;
        for (const QJsonValue& keyValue :
             savedEntity.value(QStringLiteral("tracks")).toObject()
                 .value(QStringLiteral("x")).toArray())
        {
            const QJsonObject key = keyValue.toObject();
            const double frame = key.value(QStringLiteral("f")).toDouble();
            if (std::abs(frame - 30.0) < 1e-6)
                noteAt30 = key.value(QStringLiteral("note")).toString()
                           == QStringLiteral("收尾");
            if (std::abs(frame) < 1e-6)
                noteAt0 = !key.contains(QStringLiteral("note"));
        }
    }
    if (!noteAt30 || !noteAt0)
    {
        qCritical() << "annotation-test: keyframe note json" << noteAt30
                    << noteAt0;
        ++failures;
    }

    pvf::Project loaded;
    error.clear();
    if (!loaded.load(projectPath, &error))
    {
        qCritical() << "annotation-test: load" << error;
        return 1;
    }
    if (loaded.annotations().size() != 3
        || loaded.annotations()[0].id != middleId
        || loaded.annotations()[1].id != lateId
        || loaded.annotations()[2].id != farId
        || loaded.annotations()[2].frame != 1000
        || loaded.timelineLength() != length)
    {
        qCritical() << "annotation-test: annotation roundtrip";
        ++failures;
    }
    if (const pvf::Annotation* loadedMiddle = loaded.annotationById(middleId);
        !loadedMiddle || loadedMiddle->frame != 5
        || loadedMiddle->tags != QStringList{QStringLiteral("场景")}
        || loadedMiddle->color.name(QColor::HexArgb)
               != QColor(1, 2, 3).name(QColor::HexArgb))
    {
        qCritical() << "annotation-test: annotation roundtrip fields";
        ++failures;
    }
    if (loaded.keyframeNote(entityId, QStringLiteral("x"), 30.0)
            != QStringLiteral("收尾")
        || !loaded.keyframeNote(entityId, QStringLiteral("x"), 0.0).isEmpty())
    {
        qCritical() << "annotation-test: keyframe note roundtrip";
        ++failures;
    }

    // v3 files get empty annotations.
    if (!rewriteProjectVersion(projectPath, v3Path, 3, true))
    {
        qCritical() << "annotation-test: rewrite v3";
        ++failures;
    }
    else
    {
        pvf::Project old;
        error.clear();
        if (!old.load(v3Path, &error) || !old.annotations().isEmpty()
            || old.scenes().size() != 1)
        {
            qCritical() << "annotation-test: v3 annotations" << error;
            ++failures;
        }
    }

    QFile::remove(projectPath);
    QFile::remove(v3Path);
    qInfo() << "annotation-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int sceneUiTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* content =
        project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId contentId = content->id;
    project->setValue(contentId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(contentId, QStringLiteral("x"), 120, 600.0, true);

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, contentId] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "scene-ui-test:" << text;
            ++failures;
        };

        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!timeline || !undo || !redo)
        {
            qCritical() << "scene-ui-test: timeline or undo actions missing";
            app.exit(1);
            return;
        }
        timeline->setPixelsPerFrameForTest(6.0);

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton
                                                               : Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        const QString firstId = window.addSceneForTest(
            QStringLiteral("开场"), 10, 40, QColor(200, 80, 80),
            QStringLiteral("第一段"));
        if (firstId.isEmpty() || project->scenes().size() != 1)
        {
            fail("scene creation through the UI path failed");
            qInfo() << "scene-ui-test failures" << failures;
            app.exit(1);
            return;
        }
        const pvf::Scene* scene = project->sceneById(firstId);
        const pvf::EntityId sceneFolder =
            scene ? scene->folderId : pvf::kInvalidEntity;
        if (!scene || !project->entity(sceneFolder))
            fail("scene folder missing");

        const QRect band = timeline->sceneBandRectForTest(firstId);
        if (!band.isValid() || band.width() <= 0)
            fail("scene band is not visible");

        // Clicking the band selects it and jumps the playhead to its start.
        const QPoint bandPoint(band.center().x(), timeline->sceneBandCenterYForTest());
        sendMouse(QEvent::MouseButtonPress, bandPoint);
        sendMouse(QEvent::MouseButtonRelease, bandPoint);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 10)
            fail("clicking the scene band did not jump to the start frame");
        if (timeline->selectedSceneForTest() != firstId)
            fail("clicking the scene band did not select the scene");
        if (!timeline->sceneActiveForTest(firstId))
            fail("the playhead scene is not marked active in the band");

        // A new folder with no explicit parent lands in the active scene.
        window.goToFrame(25);
        const pvf::EntityId created = window.createFolderForTest();
        const pvf::Entity* createdFolder = project->entity(created);
        if (!createdFolder || createdFolder->parent != sceneFolder)
            fail("new entity did not land in the active scene folder");

        const int beforeUndo = int(project->entities().size());
        undo->trigger();
        QApplication::processEvents();
        if (project->entity(created) || int(project->entities().size()) != beforeUndo - 1)
            fail("undo did not remove the auto-parented entity");
        redo->trigger();
        QApplication::processEvents();
        createdFolder = project->entity(created);
        if (!createdFolder || createdFolder->parent != sceneFolder)
            fail("redo did not restore the auto-parented entity");

        // Range/name edits and their undo/redo.
        QString error;
        if (!window.updateSceneForTest(firstId, QStringLiteral("开场改"), 5, 45,
                                       QColor(10, 20, 30), QStringLiteral("改过"),
                                       &error))
            fail("scene update failed");
        const pvf::Scene* updated = project->sceneById(firstId);
        if (!updated || updated->name != QStringLiteral("开场改")
            || updated->startFrame != 5 || updated->endFrame != 45
            || updated->note != QStringLiteral("改过"))
            fail("scene update fields are wrong");
        if (const pvf::Entity* renamed = project->entity(sceneFolder);
            !renamed || renamed->name != QStringLiteral("开场改"))
            fail("scene folder did not follow the scene name");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Scene* restored = project->sceneById(firstId);
        if (!restored || restored->name != QStringLiteral("开场")
            || restored->startFrame != 10 || restored->endFrame != 40)
            fail("undo did not restore the scene range/name");
        if (const pvf::Entity* renamed = project->entity(sceneFolder);
            !renamed || renamed->name != QStringLiteral("开场"))
            fail("undo did not restore the scene folder name");
        redo->trigger();
        QApplication::processEvents();
        updated = project->sceneById(firstId);
        if (!updated || updated->startFrame != 5 || updated->endFrame != 45)
            fail("redo did not reapply the scene edit");
        if (const pvf::Entity* renamed = project->entity(sceneFolder);
            !renamed || renamed->name != QStringLiteral("开场改"))
            fail("redo did not rename the scene folder again");

        const QString secondId = window.addSceneForTest(
            QStringLiteral("后段"), 70, 100, QColor(80, 200, 120),
            QStringLiteral("第二段"));
        if (secondId.isEmpty())
            fail("second scene creation failed");
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_scene_ui.png"));
        error.clear();
        if (window.updateSceneForTest(firstId, QStringLiteral("重叠"), 0, 100,
                                      QColor(), QString(), &error)
            || error.isEmpty())
            fail("overlapping scene range was accepted");

        // Retirement moves the folder under _retired, fades and locks it.
        if (!window.setSceneRetiredForTest(firstId, true))
            fail("retiring the scene failed");
        const pvf::Scene* retiredScene = project->sceneById(firstId);
        pvf::EntityId retiredRoot = pvf::kInvalidEntity;
        for (const pvf::Entity& entity : project->entities())
        {
            if (entity.parent == pvf::kInvalidEntity && entity.isFolder()
                && entity.name == QStringLiteral("_retired"))
                retiredRoot = entity.id;
        }
        const pvf::Entity* retiredFolder = project->entity(sceneFolder);
        if (!retiredScene || retiredRoot == pvf::kInvalidEntity || !retiredFolder
            || retiredFolder->parent != retiredRoot)
            fail("retired scene folder is not under _retired");
        if (!retiredFolder || !retiredFolder->locked
            || project->value(sceneFolder, QStringLiteral("opacity"), 0) > 1e-9)
            fail("retired folder is not transparent and locked");
        if (!pvf::sceneIsRetired(*project, *retiredScene))
            fail("scene record is not marked retired");

        undo->trigger();
        QApplication::processEvents();
        const pvf::Entity* restoredRetire = project->entity(sceneFolder);
        if (!restoredRetire || restoredRetire->parent != pvf::kInvalidEntity
            || restoredRetire->locked
            || project->value(sceneFolder, QStringLiteral("opacity"), 0) < 0.999)
            fail("undo did not restore the retired folder");
        const pvf::Scene* afterUndoRetire = project->sceneById(firstId);
        if (!afterUndoRetire || pvf::sceneIsRetired(*project, *afterUndoRetire))
            fail("undo did not clear the retired mark");
        redo->trigger();
        QApplication::processEvents();

        // Human restore is independent from undo.
        if (!window.setSceneRetiredForTest(firstId, false))
            fail("restoring the scene failed");
        restoredRetire = project->entity(sceneFolder);
        if (!restoredRetire || restoredRetire->parent != pvf::kInvalidEntity
            || restoredRetire->locked
            || std::abs(project->value(sceneFolder, QStringLiteral("opacity"), 0)
                        - 1.0) > 1e-6)
            fail("restore did not move the folder back and reset opacity/lock");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Scene* undoRestore = project->sceneById(firstId);
        if (!undoRestore || !pvf::sceneIsRetired(*project, *undoRestore))
            fail("undo did not return the scene to retired");
        redo->trigger();
        QApplication::processEvents();

        // Removing a scene record keeps the folder; undo rebinds the same id.
        if (!window.removeSceneForTest(firstId))
            fail("removing the scene failed");
        if (project->sceneById(firstId) || !project->entity(sceneFolder))
            fail("scene removal deleted the folder");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Scene* reAdded = project->sceneById(firstId);
        if (!reAdded || reAdded->folderId != sceneFolder
            || !project->entity(sceneFolder))
            fail("undo did not restore the scene record on the same folder");
        redo->trigger();
        QApplication::processEvents();
        if (project->sceneById(firstId))
            fail("redo did not remove the scene record again");

        qInfo() << "scene-ui-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int annotationUiTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity = project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    project->setValue(id, QStringLiteral("x"), 20, 200.0, true);
    project->setValue(id, QStringLiteral("y"), 10, 0.0, true);
    project->setValue(id, QStringLiteral("y"), 20, 50.0, true);
    const int lengthBefore = project->timelineLength();

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id, lengthBefore] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "annotation-ui-test:" << text;
            ++failures;
        };

        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!timeline || !undo || !redo)
        {
            qCritical() << "annotation-ui-test: timeline or undo actions missing";
            app.exit(1);
            return;
        }
        timeline->setPixelsPerFrameForTest(6.0);

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton
                                                               : Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        const QString later = window.addAnnotationForTest(
            30, QStringLiteral("副歌"), QStringLiteral("**副歌** 开始"),
            {QStringLiteral("副歌")}, QColor(255, 120, 60));
        const QString earlier = window.addAnnotationForTest(
            10, QStringLiteral("开场"), QStringLiteral("第一帧"),
            {QStringLiteral("镜头")}, QColor(90, 200, 255));
        if (later.isEmpty() || earlier.isEmpty() || project->annotations().size() != 2)
        {
            fail("annotation creation through the UI path failed");
            qInfo() << "annotation-ui-test failures" << failures;
            app.exit(1);
            return;
        }
        if (project->annotations()[0].id != earlier
            || project->annotations()[1].id != later)
            fail("annotations are not sorted by frame");
        if (project->timelineLength() != lengthBefore)
            fail("annotations changed the timeline length");

        const QRect marker = timeline->annotationRectForTest(later);
        if (!marker.isValid())
            fail("annotation marker is not visible");
        const QPoint markerPoint(marker.center().x(),
                                 timeline->annotationLaneCenterYForTest());
        sendMouse(QEvent::MouseButtonPress, markerPoint);
        sendMouse(QEvent::MouseButtonRelease, markerPoint);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 30)
            fail("clicking an annotation marker did not jump to its frame");

        // Editing moves the marker and re-sorts the list.
        QString error;
        if (!window.updateAnnotationForTest(earlier, 5, QStringLiteral("开场改"),
                                            QStringLiteral("改过的备注"),
                                            {QStringLiteral("镜头"),
                                             QStringLiteral("字幕")},
                                            QColor(20, 30, 40), &error))
            fail("annotation update failed");
        const pvf::Annotation* updated = project->annotationById(earlier);
        if (!updated || updated->frame != 5
            || updated->title != QStringLiteral("开场改")
            || updated->note != QStringLiteral("改过的备注")
            || updated->tags.size() != 2)
            fail("annotation update fields are wrong");
        if (project->annotations().first().id != earlier)
            fail("annotation update did not re-sort");
        undo->trigger();
        QApplication::processEvents();
        const pvf::Annotation* restored = project->annotationById(earlier);
        if (!restored || restored->frame != 10
            || restored->title != QStringLiteral("开场"))
            fail("undo did not restore the annotation");
        redo->trigger();
        QApplication::processEvents();
        updated = project->annotationById(earlier);
        if (!updated || updated->frame != 5)
            fail("redo did not reapply the annotation edit");

        if (!window.removeAnnotationForTest(earlier))
            fail("annotation removal failed");
        if (project->annotationById(earlier))
            fail("annotation was not removed");
        undo->trigger();
        QApplication::processEvents();
        if (!project->annotationById(earlier))
            fail("undo did not restore the annotation id");
        redo->trigger();
        QApplication::processEvents();
        if (project->annotationById(earlier))
            fail("redo did not remove the annotation again");

        // Keyframe notes cover every property at the frame and undo fully.
        if (!window.setKeyframeNoteForTest(id, 10, QStringLiteral("第一帧")))
            fail("setting the keyframe note failed");
        if (project->keyframeNote(id, QStringLiteral("x"), 10)
                != QStringLiteral("第一帧")
            || project->keyframeNote(id, QStringLiteral("y"), 10)
                   != QStringLiteral("第一帧"))
            fail("keyframe note was not applied to every property");
        if (!timeline->keyframeHasNoteForTest(id, 10))
            fail("keyframe note dot is not exposed");
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_annotation_ui.png"));
        undo->trigger();
        QApplication::processEvents();
        if (!project->keyframeNote(id, QStringLiteral("x"), 10).isEmpty()
            || !project->keyframeNote(id, QStringLiteral("y"), 10).isEmpty())
            fail("undo did not clear the keyframe note");
        redo->trigger();
        QApplication::processEvents();
        if (project->keyframeNote(id, QStringLiteral("x"), 10)
            != QStringLiteral("第一帧"))
            fail("redo did not restore the keyframe note");

        if (!window.setKeyframeNoteForTest(id, 10, QString()))
            fail("clearing the keyframe note failed");
        if (!project->keyframeNote(id, QStringLiteral("x"), 10).isEmpty())
            fail("keyframe note was not cleared");
        undo->trigger();
        QApplication::processEvents();
        if (project->keyframeNote(id, QStringLiteral("x"), 10)
            != QStringLiteral("第一帧"))
            fail("undo did not restore the cleared note");
        redo->trigger();
        QApplication::processEvents();
        if (!project->keyframeNote(id, QStringLiteral("x"), 10).isEmpty())
            fail("redo did not clear the note again");

        qInfo() << "annotation-ui-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int autoSaveTest()
{
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_autosave_test.pvproj");
    const QString imagePath =
        QDir::tempPath() + QStringLiteral("/pvforge_autosave_test.png");
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    if (!img.save(imagePath))
        return 1;

    auto* project = new pvf::Project;
    project->name = QStringLiteral("自动保存测试");
    project->addImage(imagePath);
    QString error;
    if (!project->save(projectPath, &error))
    {
        qCritical() << "autosave-test: initial save failed" << error;
        return 1;
    }

    int failures = 0;
    if (!pvf::Settings::autosaveEnabled()
        || pvf::Settings::autosaveMinutes() != 5)
    {
        qCritical() << "autosave-test: defaults are not enabled / 5 minutes"
                    << pvf::Settings::autosaveEnabled()
                    << pvf::Settings::autosaveMinutes();
        ++failures;
    }

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();

    if (!window.autosaveActiveForTest()
        || window.autosaveIntervalForTest() != 5 * 60 * 1000)
    {
        qCritical() << "autosave-test: timer not running at five minutes"
                    << window.autosaveActiveForTest()
                    << window.autosaveIntervalForTest();
        ++failures;
    }

    const int before = project->entities().size();
    project->addText(QStringLiteral("自动保存"), QFont().family(), 64, Qt::white,
                     int(Qt::AlignCenter));
    if (!project->modified)
    {
        qCritical() << "autosave-test: edit did not mark the project modified";
        ++failures;
    }
    window.autoSaveNowForTest();
    if (project->modified)
    {
        qCritical() << "autosave-test: autosave did not clear the modified flag";
        ++failures;
    }

    pvf::Project loaded;
    if (!loaded.load(projectPath, &error))
    {
        qCritical() << "autosave-test: reload failed" << error;
        ++failures;
    }
    else if (loaded.entities().size() != before + 1)
    {
        qCritical() << "autosave-test: autosaved entity missing"
                    << loaded.entities().size() << before + 1;
        ++failures;
    }

    pvf::Settings::setAutosaveEnabled(false);
    window.reloadAutosaveSettingsForTest();
    if (window.autosaveActiveForTest())
    {
        qCritical() << "autosave-test: timer still runs when disabled";
        ++failures;
    }
    project->addText(QStringLiteral("不要保存"), QFont().family(), 64,
                     Qt::white, int(Qt::AlignCenter));
    window.autoSaveNowForTest();
    if (!project->modified)
    {
        qCritical() << "autosave-test: disabled autosave still wrote the file";
        ++failures;
    }

    pvf::Settings::setAutosaveEnabled(true);
    pvf::Settings::setAutosaveMinutes(2);
    window.reloadAutosaveSettingsForTest();
    if (!window.autosaveActiveForTest()
        || window.autosaveIntervalForTest() != 2 * 60 * 1000)
    {
        qCritical() << "autosave-test: interval setting not applied"
                    << window.autosaveActiveForTest()
                    << window.autosaveIntervalForTest();
        ++failures;
    }

    QFile::remove(projectPath);
    QFile::remove(imagePath);
    qInfo() << "autosave-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int groupTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    QVector<pvf::EntityId> ids;
    for (int i = 0; i < 3; ++i)
    {
        pvf::Entity* entity =
            project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
        ids.append(entity->id);
    }

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, ids] {
        int failures = 0;
        window.groupEntitiesForTest(ids);
        QApplication::processEvents();

        pvf::EntityId folderId = pvf::kInvalidEntity;
        int folders = 0;
        int children = 0;
        for (const pvf::Entity& entity : project->entities())
        {
            if (entity.isFolder())
            {
                ++folders;
                folderId = entity.id;
            }
        }
        for (const pvf::Entity& entity : project->entities())
            if (entity.parent == folderId)
                ++children;

        if (project->entities().size() != 4 || folders != 1 || children != 3
            || window.selectedEntityForTest() != folderId)
        {
            qCritical() << "group-test: grouping failed" << project->entities().size()
                        << folders << children << window.selectedEntityForTest();
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            int foldersAfterUndo = 0;
            for (const pvf::Entity& entity : project->entities())
                if (entity.isFolder())
                    ++foldersAfterUndo;
            if (project->entities().size() != 3 || foldersAfterUndo != 0)
            {
                qCritical() << "group-test: undo failed"
                            << project->entities().size() << foldersAfterUndo;
                ++failures;
            }
            if (auto* redo =
                    window.findChild<QAction*>(QStringLiteral("redoAction")))
            {
                redo->trigger();
                QApplication::processEvents();
                if (project->entities().size() != 4)
                {
                    qCritical() << "group-test: redo failed"
                                << project->entities().size();
                    ++failures;
                }
            }
        }

        qInfo() << "group-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int opacityTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* transparent =
        project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId transparentId = transparent->id;
    pvf::Entity* behind = project->addVector(0, 80, 80, Qt::blue, Qt::white, 0);
    const pvf::EntityId behindId = behind->id;
    pvf::Entity* folder = project->addFolder(QStringLiteral("Fade"));
    const pvf::EntityId folderId = folder->id;
    project->setEntityParent(behindId, folderId);
    project->setValue(transparentId, QStringLiteral("opacity"), 0, 0.0, false);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window,
                       [&app, &window, project, transparentId, behindId, folderId] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "opacity-test: viewport missing";
            app.exit(1);
            return;
        }

        const QPointF centre =
            viewport->worldToScreenForTest().map(QPointF(0.0, 0.0));
        if (project->effectiveOpacity(transparentId, 0) > 1e-6)
        {
            qCritical() << "opacity-test: transparent opacity is not zero";
            ++failures;
        }
        if (viewport->hitTestForTest(centre) != behindId)
        {
            qCritical() << "opacity-test: transparent entity blocks the pick"
                        << viewport->hitTestForTest(centre);
            ++failures;
        }

        project->setValue(transparentId, QStringLiteral("opacity"), 0, 1.0, false);
        if (viewport->hitTestForTest(centre) != transparentId)
        {
            qCritical() << "opacity-test: visible entity is not picked";
            ++failures;
        }

        project->setValue(transparentId, QStringLiteral("opacity"), 0, 0.0, false);
        project->setValue(folderId, QStringLiteral("opacity"), 0, 0.0, false);
        if (project->effectiveOpacity(behindId, 0) > 1e-6
            || viewport->hitTestForTest(centre) != pvf::kInvalidEntity)
        {
            qCritical() << "opacity-test: zero opacity folder is still visible";
            ++failures;
        }

        project->setValue(folderId, QStringLiteral("opacity"), 0, 1.0, false);
        if (viewport->hitTestForTest(centre) != behindId)
        {
            qCritical() << "opacity-test: folder child is not picked after fading in";
            ++failures;
        }

        qInfo() << "opacity-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int multiDragTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* folder = project->addFolder(QStringLiteral("Target"));
    const pvf::EntityId folderId = folder->id;
    QVector<pvf::EntityId> ids;
    for (int i = 0; i < 3; ++i)
        ids.append(project->addVector(0, 80, 80, Qt::red, Qt::white, 0)->id);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, folderId, ids] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        if (!timeline)
        {
            qCritical() << "multidrag-test: timeline missing";
            app.exit(1);
            return;
        }

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons,
                                    Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, modifiers);
            QApplication::sendEvent(timeline, &event);
        };

        const QPoint first(10, timeline->rowCenterYForTest(1));
        const QPoint second(10, timeline->rowCenterYForTest(2));
        const QPoint folderRow(10, timeline->rowCenterYForTest(0));

        sendMouse(QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, first, Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, second, Qt::LeftButton,
                  Qt::ControlModifier);
        sendMouse(QEvent::MouseButtonRelease, second, Qt::NoButton,
                  Qt::ControlModifier);
        QApplication::processEvents();
        if (timeline->entitySelectionCountForTest() != 2)
        {
            qCritical() << "multidrag-test: ctrl selection"
                        << timeline->entitySelectionCountForTest();
            ++failures;
        }

        // Pressing a row that is already selected keeps the whole selection,
        // so dragging drops both rows into the folder as one block.
        sendMouse(QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, folderRow, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, folderRow, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();

        const pvf::Entity* a = project->entity(ids[0]);
        const pvf::Entity* b = project->entity(ids[1]);
        const pvf::Entity* c = project->entity(ids[2]);
        if (!a || !b || !c || a->parent != folderId || b->parent != folderId
            || c->parent != pvf::kInvalidEntity)
        {
            qCritical() << "multidrag-test: group drop"
                        << (a ? a->parent : -1) << (b ? b->parent : -1)
                        << (c ? c->parent : -1);
            ++failures;
        }
        else
        {
            QVector<pvf::EntityId> order;
            for (int index : project->entityOrder())
            {
                const pvf::Entity& entity = project->entities()[index];
                if (entity.parent == folderId)
                    order.append(entity.id);
            }
            if (order != QVector<pvf::EntityId>({ids[0], ids[1]}))
            {
                qCritical() << "multidrag-test: child order" << order;
                ++failures;
            }
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            const pvf::Entity* undoneA = project->entity(ids[0]);
            const pvf::Entity* undoneB = project->entity(ids[1]);
            if (!undoneA || !undoneB
                || undoneA->parent != pvf::kInvalidEntity
                || undoneB->parent != pvf::kInvalidEntity)
            {
                qCritical() << "multidrag-test: undo did not restore the rows";
                ++failures;
            }
        }

        qInfo() << "multidrag-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int keyframeDragTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity = project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    project->setValue(id, QStringLiteral("x"), 20, 150.0, true);
    project->setValue(id, QStringLiteral("y"), 10, 50.0, true);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        if (!timeline)
        {
            qCritical() << "keyframe-drag-test: timeline missing";
            app.exit(1);
            return;
        }

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons,
                                    Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, modifiers);
            QApplication::sendEvent(timeline, &event);
        };

        const int y = timeline->rowCenterYForTest(0);
        const QPoint ten(timeline->frameXForTest(10), y);
        const QPoint twenty(timeline->frameXForTest(20), y);

        sendMouse(QEvent::MouseButtonPress, ten, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, ten, Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, twenty, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(QEvent::MouseButtonRelease, twenty, Qt::NoButton, Qt::ShiftModifier);
        QApplication::processEvents();
        if (timeline->selectedKeyframes().size() != 2)
        {
            qCritical() << "keyframe-drag-test: selection"
                        << timeline->selectedKeyframes().size();
            ++failures;
        }
        // Selecting a keyframe moves the playhead there, so the property
        // panel shows the keyframe's values.
        if (window.currentFrameForTest() != 20)
        {
            qCritical() << "keyframe-drag-test: playhead did not follow the keyframe"
                        << window.currentFrameForTest();
            ++failures;
        }

        // Drag the selected keys ten frames to the right.
        sendMouse(QEvent::MouseButtonPress, ten, Qt::LeftButton, Qt::NoModifier);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 10)
        {
            qCritical() << "keyframe-drag-test: press did not focus the keyframe"
                        << window.currentFrameForTest();
            ++failures;
        }
        sendMouse(QEvent::MouseMove, twenty, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, twenty, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
        if (window.currentFrameForTest() != 20)
        {
            qCritical() << "keyframe-drag-test: playhead did not follow the move"
                        << window.currentFrameForTest();
            ++failures;
        }

        const pvf::Keyframe* x20 = project->keyframe(id, QStringLiteral("x"), 20);
        const pvf::Keyframe* x30 = project->keyframe(id, QStringLiteral("x"), 30);
        const pvf::Keyframe* y20 = project->keyframe(id, QStringLiteral("y"), 20);
        if (!x20 || !x30 || !y20 || std::abs(x20->value - 100.0) > 1e-6
            || std::abs(x30->value - 150.0) > 1e-6
            || std::abs(y20->value - 50.0) > 1e-6
            || project->keyframe(id, QStringLiteral("x"), 10)
            || project->keyframe(id, QStringLiteral("y"), 10))
        {
            qCritical() << "keyframe-drag-test: keys did not move"
                        << (x20 ? x20->value : -1) << (x30 ? x30->value : -1)
                        << (y20 ? y20->value : -1);
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            const pvf::Keyframe* x10 = project->keyframe(id, QStringLiteral("x"), 10);
            const pvf::Keyframe* x20 = project->keyframe(id, QStringLiteral("x"), 20);
            const pvf::Keyframe* y10 = project->keyframe(id, QStringLiteral("y"), 10);
            if (!x10 || !x20 || !y10 || std::abs(x10->value - 100.0) > 1e-6
                || std::abs(x20->value - 150.0) > 1e-6
                || std::abs(y10->value - 50.0) > 1e-6
                || project->keyframe(id, QStringLiteral("x"), 30)
                || project->keyframe(id, QStringLiteral("y"), 20))
            {
                qCritical() << "keyframe-drag-test: undo did not restore keys";
                ++failures;
            }
        }

        qInfo() << "keyframe-drag-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int curveEditorTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity = project->addVector(0, 80, 80, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    project->setValue(id, QStringLiteral("x"), 20, 200.0, true);
    project->setValue(id, QStringLiteral("y"), 10, 0.0, true);
    project->setValue(id, QStringLiteral("y"), 20, 50.0, true);

    pvf::EasingSpec bezier;
    bezier.easing = pvf::Easing::Bezier;
    bezier.inX = 0.8;
    bezier.inY = 0.0;
    bezier.outX = 0.2;
    bezier.outY = 1.0;
    project->setKeyframeEasing(id, QStringLiteral("x"), 20, bezier);

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "curve-editor-test:" << text;
            ++failures;
        };

        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("timelineTabs"));
        auto* curve = window.findChild<pvf::CurveEditor*>();
        if (!tabs || !curve || tabs->count() < 2)
        {
            qCritical() << "curve-editor-test: timeline tabs/curve editor missing";
            app.exit(1);
            return;
        }
        tabs->setCurrentWidget(curve);
        window.setSelectedEntityForTest(id);
        curve->setPixelsPerFrameForTest(10.0);
        curve->setCurveVisible(0, true);
        curve->setCurveVisible(1, true);
        QApplication::processEvents();

        auto sendMouse = [curve](QEvent::Type type, const QPointF& pos,
                                 Qt::MouseButtons buttons,
                                 Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, pos, curve->mapToGlobal(pos), Qt::LeftButton,
                              buttons, modifiers);
            QApplication::sendEvent(curve, &event);
        };
        auto sendKey = [curve](QEvent::Type type, int key,
                               Qt::KeyboardModifiers modifiers) {
            QKeyEvent event(type, key, modifiers);
            QApplication::sendEvent(curve, &event);
        };

        const QPointF p10 = curve->keyframePointForTest(QStringLiteral("x"), 10.0);
        const QPointF p20 = curve->keyframePointForTest(QStringLiteral("x"), 20.0);
        if (p10.isNull() || p20.isNull()
            || std::abs(p10.x() - curve->frameXForTest(10.0)) > 1.0)
        {
            fail("keyframe points not mapped to the time axis");
        }

        // Move the selected frame from 10 to 15. Every property at that frame
        // travels with it, exactly like a timeline group drag.
        sendMouse(QEvent::MouseButtonPress, p10, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, QPointF(curve->frameXForTest(15.0), p10.y()),
                  Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease,
                  QPointF(curve->frameXForTest(15.0), p10.y()), Qt::NoButton,
                  Qt::NoModifier);
        QApplication::processEvents();

        const pvf::Keyframe* movedX = project->keyframe(id, QStringLiteral("x"), 15.0);
        if (!movedX || std::abs(movedX->value - 100.0) > 1e-6
            || !project->keyframe(id, QStringLiteral("y"), 15.0)
            || project->keyframe(id, QStringLiteral("x"), 10.0)
            || project->keyframe(id, QStringLiteral("y"), 10.0))
        {
            fail("dragging a keyframe did not move the whole frame");
        }
        if (window.currentFrameForTest() != 15)
        {
            fail("playhead did not follow the dragged keyframe");
        }

        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!undo)
        {
            fail("undo action missing");
        }
        else
        {
            undo->trigger();
            QApplication::processEvents();
            if (!project->keyframe(id, QStringLiteral("x"), 10.0)
                || !project->keyframe(id, QStringLiteral("y"), 10.0)
                || project->keyframe(id, QStringLiteral("x"), 15.0))
            {
                fail("undo did not restore the dragged keyframes");
            }
        }

        // Dragging onto an occupied frame must stop one frame short instead of
        // overwriting the existing key.
        const QPointF again10 =
            curve->keyframePointForTest(QStringLiteral("x"), 10.0);
        sendMouse(QEvent::MouseButtonPress, again10, Qt::LeftButton,
                  Qt::NoModifier);
        sendMouse(QEvent::MouseMove, QPointF(curve->frameXForTest(20.0), again10.y()),
                  Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease,
                  QPointF(curve->frameXForTest(20.0), again10.y()), Qt::NoButton,
                  Qt::NoModifier);
        QApplication::processEvents();
        if (!project->keyframe(id, QStringLiteral("x"), 19.0)
            || !project->keyframe(id, QStringLiteral("x"), 20.0)
            || project->keyframe(id, QStringLiteral("x"), 10.0))
        {
            fail("keyframe drag overwrote a neighbour instead of clamping");
        }
        if (undo)
        {
            undo->trigger();
            QApplication::processEvents();
        }

        // Bezier handle drag: select the incoming key of the segment, grab the
        // in-handle and move it. The curve changes once, on release.
        const QPointF key20 = curve->keyframePointForTest(QStringLiteral("x"), 20.0);
        sendMouse(QEvent::MouseButtonPress, key20, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, key20, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
        if (curve->selectedKeyframeCount() != 1)
            fail("clicking a graph keyframe did not select it");

        const QPointF handle =
            curve->bezierHandlePointForTest(QStringLiteral("x"), 20.0, true);
        if (handle.isNull())
        {
            fail("bezier handle was not exposed for the selected keyframe");
        }
        else
        {
            sendMouse(QEvent::MouseButtonPress, handle, Qt::LeftButton,
                      Qt::NoModifier);
            sendMouse(QEvent::MouseMove, handle + QPointF(-15.0, 12.0),
                      Qt::LeftButton, Qt::NoModifier);
            sendMouse(QEvent::MouseButtonRelease, handle + QPointF(-15.0, 12.0),
                      Qt::NoButton, Qt::NoModifier);
            QApplication::processEvents();
            const pvf::Keyframe* eased =
                project->keyframe(id, QStringLiteral("x"), 20.0);
            if (!eased || eased->ease.easing != pvf::Easing::Bezier
                || (std::abs(eased->ease.inX - 0.8) < 1e-6
                    && std::abs(eased->ease.inY) < 1e-6))
            {
                fail("dragging the bezier handle did not change the easing");
            }
            if (undo)
            {
                undo->trigger();
                QApplication::processEvents();
                const pvf::Keyframe* restored =
                    project->keyframe(id, QStringLiteral("x"), 20.0);
                if (!restored || std::abs(restored->ease.inX - 0.8) > 1e-6
                    || std::abs(restored->ease.inY) > 1e-6)
                {
                    fail("undo did not restore the bezier handles");
                }
            }
        }

        // Rubber-band select both frames, then copy and paste them at frame 40.
        const QRect plot = curve->plotRectForTest();
        const QPointF boxStart(curve->frameXForTest(0.0), plot.bottom() - 2.0);
        const QPointF boxEnd(curve->frameXForTest(30.0), plot.top() + 2.0);
        sendMouse(QEvent::MouseButtonPress, boxStart, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, boxEnd, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, boxEnd, Qt::NoButton, Qt::NoModifier);
        QApplication::processEvents();
        if (curve->selectedKeyframeCount() < 2)
            fail("rubber-band selection did not select both keyframes");

        sendKey(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        window.goToFrame(40);
        QApplication::processEvents();
        sendKey(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::processEvents();
        const pvf::Keyframe* pastedX40 =
            project->keyframe(id, QStringLiteral("x"), 40.0);
        const pvf::Keyframe* pastedX50 =
            project->keyframe(id, QStringLiteral("x"), 50.0);
        if (!pastedX40 || !pastedX50
            || std::abs(pastedX40->value - 100.0) > 1e-6
            || std::abs(pastedX50->value - 200.0) > 1e-6
            || !project->keyframe(id, QStringLiteral("y"), 50.0))
        {
            fail("copy/paste did not recreate the selected keyframes");
        }
        if (undo)
        {
            undo->trigger();
            QApplication::processEvents();
            if (project->keyframe(id, QStringLiteral("x"), 40.0)
                || project->keyframe(id, QStringLiteral("x"), 50.0))
            {
                fail("undo did not remove pasted keyframes");
            }
        }

        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_curve_editor.png"));
        qInfo() << "curve-editor-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int audioUndoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    auto clip = std::make_shared<pvf::AudioClip>();
    clip->duration = 5.0;
    clip->peaks.fill(0.5f, 5000);
    pvf::Entity* audio = project->addAudio(QStringLiteral("/tmp/pvf_audio_undo.wav"),
                                           clip, QStringLiteral("测试音频"));
    const pvf::EntityId id = audio->id;
    // Extra lanes make the waveform page vertically scrollable.
    for (int i = 0; i < 2; ++i)
        project->addAudio(QStringLiteral("/tmp/pvf_audio_undo.wav"), clip,
                          QStringLiteral("测试音频 %1").arg(i + 2));

    pvf::MainWindow window(project);
    window.resize(1100, 800);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, id] {
        int failures = 0;
        auto fail = [&failures](const char* text) {
            qCritical() << "audio-undo-test:" << text;
            ++failures;
        };

        window.setSelectedEntityForTest(id);
        QApplication::processEvents();

        auto* volume = window.findChild<QSpinBox*>(QStringLiteral("audioVolume"));
        auto* trimStart =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioTrimStart"));
        auto* trimEnd =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioTrimEnd"));
        auto* fadeIn =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioFadeIn"));
        auto* fadeOut =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("audioFadeOut"));
        auto* start = window.findChild<QSpinBox*>(QStringLiteral("audioStartFrame"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!volume || !trimStart || !trimEnd || !fadeIn || !fadeOut || !start
            || !undo)
        {
            qCritical() << "audio-undo-test: audio controls missing";
            app.exit(1);
            return;
        }

        auto data = [project, id](const QString& key) {
            const pvf::Entity* entity = project->entity(id);
            return entity ? entity->data.value(key, 0.0).toDouble() : -1.0;
        };

        // A spin-box drag emits several valueChanged signals; they must fold
        // into one undo step.
        volume->setValue(80);
        volume->setValue(60);
        if (std::abs(data(QStringLiteral("volume")) - 0.6) > 1e-6)
            fail("volume edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("volume")) - 1.0) > 1e-6)
            fail("one undo did not revert the whole volume drag");

        trimStart->setValue(1.0);
        if (std::abs(data(QStringLiteral("trimStart")) - 1.0) > 1e-6)
            fail("trim start edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("trimStart"))) > 1e-6)
            fail("trim start undo failed");

        trimEnd->setValue(0.5);
        if (std::abs(data(QStringLiteral("trimEnd")) - 0.5) > 1e-6)
            fail("trim end edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("trimEnd"))) > 1e-6)
            fail("trim end undo failed");

        fadeIn->setValue(0.75);
        fadeOut->setValue(1.25);
        if (std::abs(data(QStringLiteral("fadeIn")) - 0.75) > 1e-6
            || std::abs(data(QStringLiteral("fadeOut")) - 1.25) > 1e-6)
            fail("fade edits were not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("fadeOut"))) > 1e-6)
            fail("fade out undo failed");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("fadeIn"))) > 1e-6)
            fail("fade in undo failed");

        start->setValue(30);
        if (std::abs(data(QStringLiteral("startFrame")) - 30.0) > 1e-6)
            fail("start frame edit was not applied");
        undo->trigger();
        QApplication::processEvents();
        if (std::abs(data(QStringLiteral("startFrame"))) > 1e-6)
            fail("start frame undo failed");

        // The "take" button trims at the playhead and moves the clip start; the
        // two data fields must undo together.
        window.goToFrame(60);
        QApplication::processEvents();
        auto* capture =
            window.findChild<QPushButton*>(QStringLiteral("audioTrimStartFromPlayhead"));
        if (!capture)
        {
            fail("trim capture button missing");
        }
        else
        {
            capture->click();
            QApplication::processEvents();
            if (std::abs(data(QStringLiteral("startFrame")) - 60.0) > 1e-6
                || std::abs(data(QStringLiteral("trimStart")) - 2.0) > 1e-6)
            {
                fail("trim capture did not move start and trim together");
            }
            undo->trigger();
            QApplication::processEvents();
            if (std::abs(data(QStringLiteral("startFrame"))) > 1e-6
                || std::abs(data(QStringLiteral("trimStart"))) > 1e-6)
            {
                fail("trim capture undo did not restore both fields");
            }
        }

        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("timelineTabs"));
        auto* wave = window.findChild<pvf::WaveformView*>();
        if (!tabs || !wave)
        {
            fail("waveform page missing");
        }
        else
        {
            tabs->setCurrentWidget(wave);
            QApplication::processEvents();
            if (wave->laneCountForTest() != 3)
                fail("waveform page did not create one lane per audio object");

            const double zoomBefore = wave->pixelsPerFrameForTest();
            const QPointF pos(500, 80);
            QWheelEvent zoomEvent(pos, wave->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                                  Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase,
                                  false);
            QApplication::sendEvent(wave, &zoomEvent);
            if (!(wave->pixelsPerFrameForTest() > zoomBefore))
                fail("ctrl+wheel did not zoom the waveform");

            wave->setPixelsPerFrameForTest(6.0);
            window.goToFrame(30);
            QApplication::processEvents();
            const QPoint seekPos(wave->frameXForTest(45.0),
                                 wave->laneRectForTest(0).center().y());
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(seekPos),
                              QPointF(wave->mapToGlobal(seekPos)), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(wave, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(seekPos),
                                QPointF(wave->mapToGlobal(seekPos)), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(wave, &release);
            QApplication::processEvents();
            if (window.currentFrameForTest() != 45)
                fail("clicking the waveform did not seek the playhead");

            window.goToFrame(0);
            QApplication::processEvents();
            const int scrollBefore = wave->horizontalScrollForTest();
            QWheelEvent scrollEvent(pos, wave->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                                    Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase,
                                    false);
            QApplication::sendEvent(wave, &scrollEvent);
            if (wave->horizontalScrollForTest() == scrollBefore)
                fail("shift+wheel did not scroll the waveform page");

            const int verticalBefore = wave->verticalScrollForTest();
            QWheelEvent verticalEvent(pos, wave->mapToGlobal(pos), QPoint(),
                                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                                      Qt::NoScrollPhase, false);
            QApplication::sendEvent(wave, &verticalEvent);
            if (wave->verticalScrollForTest() == verticalBefore)
                fail("wheel did not scroll waveform lanes");

            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_waveform_page.png"));
        }

        qInfo() << "audio-undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int clipboardTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* folder = project->addFolder(QStringLiteral("Group"));
    const pvf::EntityId folderId = folder->id;
    pvf::Entity* child = project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    const pvf::EntityId childId = child->id;
    project->setEntityParent(childId, folderId);
    project->setValue(childId, QStringLiteral("x"), 0, 50.0, true);
    project->setValue(childId, QStringLiteral("x"), 50, 150.0, true);

    pvf::MainWindow window(project);
    window.resize(1000, 700);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, project, folderId, childId] {
        int failures = 0;
        const int before = project->entities().size();
        window.copyEntityForTest(folderId);
        window.pasteEntityForTest();
        QApplication::processEvents();

        const pvf::EntityId newRoot = window.selectedEntityForTest();
        const pvf::Entity* root = project->entity(newRoot);
        if (project->entities().size() != before + 2 || !root || !root->isFolder())
        {
            qCritical() << "clipboard-test: paste did not copy the subtree"
                        << project->entities().size() << before
                        << (root ? root->name : QString());
            ++failures;
        }
        else
        {
            const pvf::Entity* copiedChild = nullptr;
            for (const pvf::Entity& entity : project->entities())
                if (entity.parent == newRoot
                    && entity.type == pvf::EntityType::Vector)
                    copiedChild = &entity;
            if (!copiedChild || copiedChild->id == childId
                || std::abs(project->value(copiedChild->id, QStringLiteral("x"), 25)
                            - 100.0) > 1e-6)
            {
                qCritical() << "clipboard-test: child or keyframes lost";
                ++failures;
            }
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            if (project->entities().size() != before)
            {
                qCritical() << "clipboard-test: undo did not remove the paste";
                ++failures;
            }
        }

        window.duplicateEntityForTest(folderId);
        QApplication::processEvents();
        if (project->entities().size() != before + 2)
        {
            qCritical() << "clipboard-test: duplicate failed";
            ++failures;
        }

        qInfo() << "clipboard-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int libraryPickerTest()
{
    const QString firstPath =
        QDir::tempPath() + QStringLiteral("/pvf_pick_a.png");
    const QString secondPath =
        QDir::tempPath() + QStringLiteral("/pvf_pick_b.png");
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(firstPath))
        return 1;
    image.fill(Qt::blue);
    if (!image.save(secondPath))
        return 1;

    auto* project = new pvf::Project;
    project->addImage(firstPath);
    project->addImage(secondPath);
    project->addVector(0, 80, 80, Qt::red, Qt::white, 0);

    int failures = 0;
    const QVector<int> images =
        pvf::AssetLibraryPanel::mediaTemplateIndices(*project,
                                                     pvf::EntityType::Image);
    if (images.size() != 2)
    {
        qCritical() << "library-picker-test: image entries" << images.size();
        ++failures;
    }
    for (int index : images)
        if (index < 0 || index >= project->templates().size()
            || project->templates()[index].type != pvf::EntityType::Image
            || project->templates()[index].path.isEmpty())
        {
            qCritical() << "library-picker-test: wrong entry" << index;
            ++failures;
            break;
        }

    const QVector<int> audios =
        pvf::AssetLibraryPanel::mediaTemplateIndices(*project,
                                                     pvf::EntityType::Audio);
    if (!audios.isEmpty())
    {
        qCritical() << "library-picker-test: audio should be empty"
                    << audios.size();
        ++failures;
    }

    // Open the real picker, check that it lists the two library images and
    // grab it for visual inspection, then cancel.
    QTimer::singleShot(100, [&failures] {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || !dialog->isVisible()
                || dialog->windowTitle() != QStringLiteral("更换素材"))
                continue;
            if (auto* list = dialog->findChild<QListWidget*>())
                if (list->count() != 2)
                {
                    qCritical() << "library-picker-test: dialog entries"
                                << list->count();
                    ++failures;
                }
            dialog->grab().save(QStringLiteral("/tmp/pvf_media_picker.png"));
            dialog->reject();
            return;
        }
    });
    const QString chosen = pvf::AssetLibraryPanel::pickMedia(
        nullptr, project, pvf::EntityType::Image,
        QStringLiteral("图片 (*.png *.jpg)"));
    if (!chosen.isEmpty())
    {
        qCritical() << "library-picker-test: cancel returned a path" << chosen;
        ++failures;
    }

    QFile::remove(firstPath);
    QFile::remove(secondPath);
    qInfo() << "library-picker-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int replaceSourceTest()
{
    const QString firstPath =
        QDir::tempPath() + QStringLiteral("/pvf_replace_a.png");
    const QString secondPath =
        QDir::tempPath() + QStringLiteral("/pvf_replace_b.png");
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(firstPath))
        return 1;
    image.fill(Qt::blue);
    if (!image.save(secondPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::Entity* entity = project->addImage(firstPath);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 0, 123.0, true);
    const pvf::Entity before = *project->entity(id);

    const pvf::MediaImportResult result = pvf::processMediaFile(secondPath, QString());
    QString error;
    const QVector<pvf::EntityId> changed =
        project->replaceEntitySource(id, result, &error);
    const pvf::Entity* replaced = project->entity(id);
    int failures = 0;
    if (!error.isEmpty() || changed.size() != 1 || !replaced
        || replaced->sourcePath != secondPath
        || replaced->image.pixelColor(32, 32) != QColor(Qt::blue)
        || std::abs(project->value(id, QStringLiteral("x"), 0) - 123.0) > 1e-6)
    {
        qCritical() << "replace-test: swap failed" << error
                    << (replaced ? replaced->sourcePath : QString());
        ++failures;
    }
    else
    {
        const pvf::Entity after = *replaced;
        pvf::ReplaceEntitySourcesCommand command({before}, {after});
        command.undo(*project);
        const pvf::Entity* undone = project->entity(id);
        if (!undone || undone->sourcePath != firstPath
            || undone->image.pixelColor(32, 32) != QColor(Qt::red))
        {
            qCritical() << "replace-test: undo failed";
            ++failures;
        }
        command.redo(*project);
        const pvf::Entity* redone = project->entity(id);
        if (!redone || redone->sourcePath != secondPath)
        {
            qCritical() << "replace-test: redo failed";
            ++failures;
        }
    }

    QFile::remove(firstPath);
    QFile::remove(secondPath);
    qInfo() << "replace-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int layerTest()
{
    auto* project = new pvf::Project;
    project->width = 200;
    project->height = 200;
    // The first row of the layer list is the top layer.
    project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    project->addVector(0, 100, 100, Qt::blue, Qt::white, 0);

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        QTransform worldToScreen;
        worldToScreen.translate(100, 100);
        pvf::SceneRenderer::drawEntities(painter, *project, 0, worldToScreen, false);
    }
    const QColor center = image.pixelColor(100, 100);
    qInfo() << "layer-test: overlap colour" << center;
    if (center.red() < 200 || center.blue() > 60)
    {
        qCritical() << "layer-test: bottom row is drawn on top" << center;
        return 1;
    }
    image.save(QStringLiteral("/tmp/pvforge_layers.png"));
    return 0;
}

int frameRestoreTest(QApplication& app)
{
    const QString path =
        QDir::tempPath() + QStringLiteral("/pvforge_frame_test.pvproj");
    auto* seed = new pvf::Project;
    pvf::Entity* entity = seed->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    seed->setValue(entity->id, QStringLiteral("x"), 120, 50.0, true);
    QString error;
    if (!seed->save(path, &error))
    {
        qCritical() << "frame-restore-test: save failed" << error;
        return 1;
    }

    auto* first = new pvf::Project;
    if (!first->load(path, &error))
    {
        qCritical() << "frame-restore-test: first load failed" << error;
        return 1;
    }
    auto* firstWindow = new pvf::MainWindow(first);
    firstWindow->resize(1000, 700);
    firstWindow->show();

    QTimer::singleShot(400, firstWindow, [&app, firstWindow, path] {
        firstWindow->goToFrame(42);
        firstWindow->close();

        auto* second = new pvf::Project;
        QString err;
        if (!second->load(path, &err))
        {
            qCritical() << "frame-restore-test: second load failed" << err;
            app.exit(1);
            return;
        }
        auto* secondWindow = new pvf::MainWindow(second);
        secondWindow->resize(1000, 700);
        secondWindow->show();
        QTimer::singleShot(300, secondWindow,
                           [&app, secondWindow, firstWindow, path] {
                               const int frame = secondWindow->currentFrame();
                               qInfo() << "frame-restore-test: restored frame" << frame;
                               QFile::remove(path);
                               app.exit(frame == 42 ? 0 : 1);
                           });
    });
    return app.exec();
}

int keyframeEditTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        pvf::Entity* entity = project->addVector(0, 200, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);
        QApplication::processEvents();

        auto* opacity =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("row_opacity"));
        auto* keyButton =
            window.findChild<QPushButton*>(QStringLiteral("keyframeButton"));
        if (!opacity || !keyButton)
        {
            qCritical() << "keyframe-edit-test: widgets missing";
            app.exit(1);
            return;
        }

        // Key the element at frame 10, then at frame 5, using the button
        // exactly like a user would.
        window.goToFrame(10);
        keyButton->click();
        QApplication::processEvents();
        window.goToFrame(5);
        keyButton->click();
        QApplication::processEvents();
        // Now make the element appear at frame 5: keyframe 0 at frame 4.
        window.goToFrame(4);
        opacity->setValue(0);
        QApplication::processEvents();

        const auto at = [&](int frame) {
            return project->value(id, QStringLiteral("opacity"), frame);
        };
        qInfo() << "keyframe-edit-test: opacity at 4/5/6/10" << at(4) << at(5) << at(6)
                << at(10);
        if (std::abs(at(4)) > 1e-6)
        {
            qCritical() << "keyframe-edit-test: frame 4 is not transparent";
            ++failures;
        }
        if (at(5) < 0.99 || at(6) < 0.99 || at(10) < 0.99)
        {
            qCritical() << "keyframe-edit-test: later frames became transparent";
            ++failures;
        }
        window.goToFrame(5);
        QApplication::processEvents();
        const double spinAt5 = opacity->value();
        window.goToFrame(10);
        QApplication::processEvents();
        const double spinAt10 = opacity->value();
        qInfo() << "keyframe-edit-test: spin at 5/10" << spinAt5 << spinAt10;
        if (spinAt5 < 99.0 || spinAt10 < 99.0)
        {
            qCritical() << "keyframe-edit-test: panel shows transparency later";
            ++failures;
        }

        // The diamond next to a property adds and removes a single key.
        if (auto* keyOpacity =
                window.findChild<QToolButton*>(QStringLiteral("key_opacity")))
        {
            const auto keyCountAt = [&](double frame) {
                const pvf::Entity* entity = project->entity(id);
                const auto track = entity->tracks.constFind(QStringLiteral("opacity"));
                if (track == entity->tracks.constEnd())
                    return 0;
                int count = 0;
                for (const pvf::Keyframe& key : track->keys)
                    if (std::abs(key.frame - frame) < 1e-6)
                        ++count;
                return count;
            };
            window.goToFrame(7);
            QApplication::processEvents();
            keyOpacity->click();
            QApplication::processEvents();
            if (keyCountAt(7) != 1)
            {
                qCritical() << "keyframe-edit-test: diamond did not add a key";
                ++failures;
            }
            if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            {
                undo->trigger();
                QApplication::processEvents();
                if (keyCountAt(7) != 0)
                {
                    qCritical() << "keyframe-edit-test: undo did not remove the key";
                    ++failures;
                }
            }
            window.goToFrame(5);
            QApplication::processEvents();
            keyOpacity->click();
            QApplication::processEvents();
            if (keyCountAt(5) != 0)
            {
                qCritical() << "keyframe-edit-test: diamond did not remove the key";
                ++failures;
            }
            if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            {
                undo->trigger();
                QApplication::processEvents();
                if (keyCountAt(5) != 1)
                {
                    qCritical() << "keyframe-edit-test: undo did not restore the key";
                    ++failures;
                }
            }
        }
        else
        {
            qCritical() << "keyframe-edit-test: key diamond missing";
            ++failures;
        }

        const pvf::Entity* check = project->entity(id);
        const auto it = check->tracks.constFind(QStringLiteral("opacity"));
        if (it == check->tracks.constEnd())
        {
            qCritical() << "keyframe-edit-test: opacity track missing";
            ++failures;
        }
        else
        {
            qInfo() << "keyframe-edit-test: keys" << it->keys.size();
            for (const pvf::Keyframe& key : it->keys)
                qInfo() << "   key frame" << key.frame << "value" << key.value;
        }
        window.goToFrame(5);
        QApplication::processEvents();
        window.grab().toImage().save(
            QStringLiteral("/tmp/pvforge_keyframes.png"));
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int specialTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->width = 800;
    project->height = 600;
    project->fps = 30;

    int failures = 0;

    const auto render = [&](int frame) {
        QImage image(400, 300, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        QTransform worldToScreen;
        worldToScreen.scale(0.5, 0.5);
        worldToScreen.translate(200, 150);
        pvf::SceneRenderer::drawEntities(painter, *project, frame, worldToScreen, false);
        return image;
    };
    const auto ink = [](const QImage& image) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qGray(image.pixel(x, y)) > 40)
                    ++count;
        return count;
    };

    // Comic balloons in all four shapes, each rendered on its own.
    for (int shape = 0; shape < 4; ++shape)
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("台词\n第二行");
        style.pixelSize = 48;
        style.padding = 14;
        style.color = QColor(30, 30, 36);
        QVariantMap data;
        style.writeTo(data);
        pvf::BubbleStyle bubble;
        bubble.shape = shape;
        bubble.tail = shape == 0 || shape == 2;
        bubble.writeTo(data);

        pvf::Entity* comic = project->addEntity(pvf::EntityType::ComicText,
                                                QStringLiteral("漫画文本框"), data);
        if (!comic)
        {
            qCritical() << "special-test: comic creation failed";
            ++failures;
            continue;
        }
        comic->values[QStringLiteral("x")] = -200.0 + shape * 200.0;
        // Render this balloon alone.
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = other.id != comic->id;
        const QRectF bounds = pvf::SceneRenderer::entityLocalBounds(*comic);
        if (bounds.width() < 10 || bounds.height() < 10)
        {
            qCritical() << "special-test: comic bounds too small" << bounds;
            ++failures;
        }
        QImage shot(400, 300, QImage::Format_ARGB32);
        shot.fill(Qt::black);
        {
            QPainter painter(&shot);
            QTransform worldToScreen;
            worldToScreen.scale(0.5, 0.5);
            worldToScreen.translate(400, 300);
            pvf::SceneRenderer::drawEntities(painter, *project, 0, worldToScreen, false);
        }
        shot.save(QStringLiteral("/tmp/pvforge_comic_%1.png").arg(shape));
        if (ink(shot) <= 0)
        {
            qCritical() << "special-test: comic shape" << shape << " drew nothing";
            ++failures;
        }
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = false;
    }

    // Vertical tategaki inside an ellipse: the whole text block has to fit,
    // not just its centre line.
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("我看旁边的小日子\n过得不错的\n霓虹酱");
        style.pixelSize = 48;
        style.color = QColor(30, 30, 36);
        style.padding = 18;
        style.vertical = true;
        pvf::BubbleStyle bubble;
        bubble.shape = 0;
        bubble.tail = true;

        const pvf::ComicBox box = pvf::ComicBox::build(style, bubble);
        const pvf::TextBlock block = pvf::TextBlock::measure(style);
        if (box.bodySize().width() < block.contentSize().width() * 1.35
            || box.bodySize().height() < block.contentSize().height() * 1.35)
        {
            qCritical() << "special-test: ellipse too tight for vertical text"
                        << box.bodySize() << block.contentSize();
            ++failures;
        }

        pvf::TextStyle spacedStyle = style;
        spacedStyle.letterSpacing = 12;
        if (pvf::TextBlock::measure(spacedStyle).contentSize().height()
            <= block.contentSize().height() + 1.0)
        {
            qCritical() << "special-test: vertical letter spacing did not expand";
            ++failures;
        }

        QImage image(420, 800, QImage::Format_ARGB32);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.translate(image.width() / 2.0, image.height() / 2.0);
            box.paint(painter);
        }
        image.save(QStringLiteral("/tmp/pvf_comic_vertical.png"));
        if (ink(image) <= 0)
        {
            qCritical() << "special-test: vertical balloon drew nothing";
            ++failures;
        }
    }

    // Balloon bounds must include half the stroke, or the rasterised image
    // clips the outline at its top/bottom extremes.
    {
        QVariantMap data;
        data.insert(QStringLiteral("text"), QStringLiteral("真是个变态啊"));
        data.insert(QStringLiteral("size"), 96);
        const pvf::ComicBox box = pvf::ComicBox::build(
            pvf::TextStyle::fromData(data), pvf::BubbleStyle::fromData(data));

        QImage canvas(1600, 1600, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        {
            QPainter painter(&canvas);
            painter.translate(800, 800);
            box.paint(painter);
        }
        const QRectF bounds = box.bounds();
        const QRect area(int(std::floor(bounds.left())) + 800,
                         int(std::floor(bounds.top())) + 800,
                         int(std::ceil(bounds.width())),
                         int(std::ceil(bounds.height())));
        int edge = 0;
        const QRect edgeRect = area.adjusted(0, 0, -1, -1);
        for (int x = edgeRect.left(); x <= edgeRect.right(); ++x)
        {
            if (qAlpha(canvas.pixel(x, edgeRect.top())) > 0)
                ++edge;
            if (qAlpha(canvas.pixel(x, edgeRect.bottom())) > 0)
                ++edge;
        }
        for (int y = edgeRect.top(); y <= edgeRect.bottom(); ++y)
        {
            if (qAlpha(canvas.pixel(edgeRect.left(), y)) > 0)
                ++edge;
            if (qAlpha(canvas.pixel(edgeRect.right(), y)) > 0)
                ++edge;
        }
        canvas.save(QStringLiteral("/tmp/pvf_bubble_bounds.png"));
        if (edge >= 50)
        {
            qCritical() << "special-test: balloon stroke clipped at bounds"
                        << edge << bounds;
            ++failures;
        }
    }

    // Glyph outline: stroke colour appears around the fill.
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("描边");
        style.pixelSize = 64;
        style.color = Qt::white;
        style.strokeColor = QColor(220, 30, 60);
        style.strokeWidth = 6;
        QImage image(320, 200, QImage::Format_ARGB32);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            pvf::TextBlock::measure(style).paint(painter, QPointF(160, 100));
        }
        image.save(QStringLiteral("/tmp/pvf_text_stroke.png"));
        int fill = 0;
        int outline = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor color = image.pixelColor(x, y);
                if (color.red() > 200 && color.green() > 200 && color.blue() > 200)
                    ++fill;
                else if (color.red() > 150 && color.green() < 100)
                    ++outline;
            }
        if (fill == 0 || outline == 0)
        {
            qCritical() << "special-test: text outline missing" << fill << outline;
            ++failures;
        }
    }

    // Radial rays.
    {
        pvf::RadialParams params;
        params.count = 32;
        params.radius = 200;
        params.width = 12;
        QVariantMap data;
        params.writeTo(data);
        pvf::Entity* radial = project->addEntity(pvf::EntityType::RadialLines,
                                                 QStringLiteral("放射状线条"), data);
        if (!radial)
        {
            qCritical() << "special-test: radial creation failed";
            ++failures;
        }
        else
        {
            radial->values[QStringLiteral("x")] = 260.0;
            const QImage first = render(0);
            if (ink(first) <= 0)
            {
                qCritical() << "special-test: radial lines drew nothing";
                ++failures;
            }
            if (first != render(0))
            {
                qCritical() << "special-test: radial lines are not deterministic";
                ++failures;
            }
            if (first == render(8))
            {
                qCritical() << "special-test: radial flicker does not animate";
                ++failures;
            }
        }
    }

    // Flow streaks move with the frame.
    {
        pvf::FlowParams params;
        params.count = 12;
        params.length = 300;
        params.spacing = 60;
        params.area = 900;
        params.speed = 900;
        QVariantMap data;
        params.writeTo(data);
        pvf::Entity* flow = project->addEntity(pvf::EntityType::FlowLines,
                                               QStringLiteral("快速流过"), data);
        if (!flow)
        {
            qCritical() << "special-test: flow creation failed";
            ++failures;
        }
        else
        {
            flow->values[QStringLiteral("x")] = 0.0;
            flow->values[QStringLiteral("y")] = 200.0;
            const QImage first = render(0);
            if (ink(first) <= 0)
            {
                qCritical() << "special-test: flow lines drew nothing";
                ++failures;
            }
            if (first == render(30))
            {
                qCritical() << "special-test: flow lines do not animate";
                ++failures;
            }
        }
    }

    // Halftone screentone: a dot grid that fades along the gradient angle.
    {
        pvf::HalftoneParams params;
        params.area = 600;
        params.spacing = 16;
        params.dotSize = 8;
        QVariantMap data;
        params.writeTo(data);
        pvf::Entity* halftone = project->addEntity(pvf::EntityType::HalftoneDots,
                                                   QStringLiteral("漫画点阵"), data);
        if (!halftone)
        {
            qCritical() << "special-test: halftone creation failed";
            ++failures;
        }
        else
        {
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = other.id != halftone->id;
            const QImage shot = render(0);
            const QImage again = render(0);
            if (halftone->halftonePixmap.isNull()
                || halftone->halftoneData.value(QStringLiteral("spacing")).toDouble()
                       != 16.0)
            {
                qCritical() << "special-test: halftone pattern was not cached";
                ++failures;
            }
            else
            {
                // A second frame reuses the raster; a parameter edit rebuilds it.
                const qint64 cached = halftone->halftonePixmap.cacheKey();
                render(0);
                if (halftone->halftonePixmap.cacheKey() != cached)
                {
                    qCritical() << "special-test: halftone cache rebuilt every frame";
                    ++failures;
                }
                halftone->data[QStringLiteral("spacing")] = 24.0;
                render(0);
                if (halftone->halftonePixmap.cacheKey() == cached
                    || halftone->halftoneData.value(QStringLiteral("spacing"))
                               .toDouble()
                           != 24.0)
                {
                    qCritical() << "special-test: halftone cache not invalidated";
                    ++failures;
                }
            }
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = false;
            shot.save(QStringLiteral("/tmp/pvf_halftone.png"));

            const auto density = [&](int top, int bottom) {
                int count = 0;
                for (int y = top; y < bottom; ++y)
                    for (int x = 0; x < shot.width(); ++x)
                        if (qGray(shot.pixel(x, y)) > 40)
                            ++count;
                return count;
            };
            if (ink(shot) <= 0)
            {
                qCritical() << "special-test: halftone drew nothing";
                ++failures;
            }
            else if (density(0, shot.height() / 2)
                     <= density(shot.height() / 2, shot.height()))
            {
                qCritical() << "special-test: halftone gradient does not fade";
                ++failures;
            }
            if (shot != again)
            {
                qCritical() << "special-test: halftone is not deterministic";
                ++failures;
            }
        }
    }

    // Brand tile wall: a checkerboard of two logos.
    {
        const QString logoA =
            QDir::tempPath() + QStringLiteral("/pvf_wall_logo_a.png");
        const QString logoB =
            QDir::tempPath() + QStringLiteral("/pvf_wall_logo_b.svg");
        QImage a(96, 96, QImage::Format_ARGB32);
        a.fill(Qt::transparent);
        {
            QPainter p(&a);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(10, 10, 76, 76);
        }
        if (!a.save(logoA))
        {
            qCritical() << "special-test: wall logo not saved";
            ++failures;
        }
        // The second slot is an SVG at a tiny intrinsic size; it has to be
        // rasterised at a usable resolution.
        QFile svgFile(logoB);
        if (svgFile.open(QIODevice::WriteOnly))
        {
            svgFile.write(
                "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' "
                "viewBox='0 0 24 24'><path d='M4 4 L20 20 M20 4 L4 20' "
                "stroke='white' stroke-width='3' stroke-linecap='round'/></svg>");
            svgFile.close();
        }
        const QImage svgLogo = pvf::BrandWall::loadLogo(logoB);
        if (svgLogo.isNull()
            || std::max(svgLogo.width(), svgLogo.height()) != 1024)
        {
            qCritical() << "special-test: svg logo not rasterised" << svgLogo.size();
            ++failures;
        }
        pvf::BrandWallParams wallParams;
        wallParams.width = 800;
        wallParams.height = 450;
        wallParams.columns = 8;
        wallParams.rows = 5;
        wallParams.gap = 6;
        wallParams.logoOpacity = 0.9;
        wallParams.logos = QStringList{logoA, logoB};
        wallParams.tileColors =
            QStringList{QColor(210, 70, 160).name(QColor::HexArgb),
                        QColor(232, 232, 238).name(QColor::HexArgb)};
        pvf::Entity* wall = project->addBrandWall(wallParams);
        if (!wall)
        {
            qCritical() << "special-test: brand wall creation failed";
            ++failures;
        }
        else if (std::abs(pvf::SceneRenderer::entityLocalBounds(*wall).width() - 800.0)
                     > 0.01
                 || wall->wallLogos.size() != 2 || wall->wallLogos[0].isNull()
                 || wall->wallLogos[1].isNull())
        {
            qCritical() << "special-test: brand wall bounds or logos wrong"
                        << pvf::SceneRenderer::entityLocalBounds(*wall);
            ++failures;
        }
        else
        {
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = other.id != wall->id;
            const QImage shot = render(0);
            shot.save(QStringLiteral("/tmp/pvforge_brandwall.png"));
            for (pvf::Entity& other : project->mutableEntities())
                other.hidden = false;
            if (ink(shot) <= 0)
            {
                qCritical() << "special-test: brand wall drew nothing";
                ++failures;
            }
            // Checkerboard: neighbouring tiles use different slots, also
            // across rows (the old row * columns indexing broke this).
            const QColor tile00 = shot.pixelColor(96, 94);
            const QColor tile01 = shot.pixelColor(146, 94);
            const QColor tile10 = shot.pixelColor(96, 139);
            if (tile00 == tile01 || tile00 == tile10)
            {
                qCritical() << "special-test: brand wall is not a checkerboard"
                            << tile00 << tile01 << tile10;
                ++failures;
            }
        }
    }

    // Japanese vertical text: columns run right to left.
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("日式竖排\n从右到左");
        style.pixelSize = 48;
        style.vertical = true;
        const pvf::TextBlock block = pvf::TextBlock::measure(style);
        if (block.contentSize().width() <= 0
            || block.contentSize().height() <= block.contentSize().width())
        {
            qCritical() << "special-test: vertical text is not column shaped"
                        << block.contentSize();
            ++failures;
        }
    }

    render(0).save(QStringLiteral("/tmp/pvforge_special.png"));

    // The special types survive a save and load round trip.
    {
        const QString path = QDir::tempPath() +
                             QStringLiteral("/pvforge_special_test.pvproj");
        QString error;
        if (!project->save(path, &error))
        {
            qCritical() << "special-test: save failed" << error;
            ++failures;
        }
        else
        {
            pvf::Project loaded;
            if (!loaded.load(path, &error))
            {
                qCritical() << "special-test: load failed" << error;
                ++failures;
            }
            else
            {
                int comic = 0;
                int radial = 0;
                int flow = 0;
                int halftone = 0;
                int wall = 0;
                for (const pvf::Entity& entity : loaded.entities())
                {
                    if (entity.type == pvf::EntityType::ComicText)
                        ++comic;
                    else if (entity.type == pvf::EntityType::RadialLines)
                        ++radial;
                    else if (entity.type == pvf::EntityType::FlowLines)
                        ++flow;
                    else if (entity.type == pvf::EntityType::HalftoneDots)
                        ++halftone;
                    else if (entity.type == pvf::EntityType::BrandWall)
                        ++wall;
                }
                if (comic != 4 || radial != 1 || flow != 1 || halftone != 1
                    || wall != 1)
                {
                    qCritical() << "special-test: types lost on reload" << comic
                                << radial << flow << halftone << wall;
                    ++failures;
                }
            }
            QFile::remove(path);
        }
    }

    // Performance monitor keeps the latest rolling timings.
    {
        auto& perf = pvf::PerfMonitor::instance();
        perf.setEnabled(true);
        perf.recordScene(7.5);
        perf.recordViewport(3.0);
        perf.recordTimeline(2.0);
        perf.recordVideo(1.0);
        perf.setEntityCount(53);
        const pvf::PerfSnapshot snapshot = perf.snapshot();
        if (!snapshot.enabled || std::abs(snapshot.sceneMs - 7.5) > 1e-6
            || std::abs(snapshot.viewportMs - 3.0) > 1e-6
            || std::abs(snapshot.timelineMs - 2.0) > 1e-6
            || std::abs(snapshot.videoMs - 1.0) > 1e-6
            || snapshot.entities != 53)
        {
            qCritical() << "special-test: performance monitor values wrong"
                        << snapshot.sceneMs << snapshot.viewportMs
                        << snapshot.timelineMs << snapshot.videoMs;
            ++failures;
        }
        perf.setEnabled(false);
        if (perf.snapshot().enabled)
        {
            qCritical() << "special-test: performance monitor did not disable";
            ++failures;
        }
    }

    // The workbench offers the special tools and the layout dialog opens.
    auto* uiProject = new pvf::Project;
    uiProject->width = 1280;
    uiProject->height = 720;
    pvf::MainWindow uiWindow(uiProject);
    uiWindow.resize(1400, 900);
    uiWindow.show();
    QTimer::singleShot(500, &uiWindow, [&app, &uiWindow, uiProject, failures] {
        int local = failures;
        auto* workbench = uiWindow.findChild<pvf::WorkbenchPanel*>();
        auto* types = workbench
            ? workbench->findChild<QListWidget*>(QStringLiteral("typeList"))
            : nullptr;
        auto* special = workbench
            ? workbench->findChild<QListWidget*>(QStringLiteral("specialTypes"))
            : nullptr;
        auto* create = workbench
            ? workbench->findChild<QPushButton*>(QStringLiteral("createButton"))
            : nullptr;
        if (!types || !special || !create)
        {
            qCritical() << "special-test: workbench widgets missing";
            ++local;
        }
        else
        {
            auto* textBoxes =
                workbench->findChild<QListWidget*>(QStringLiteral("textBoxTypes"));
            types->setCurrentRow(2);   // the "text box" page
            QApplication::processEvents();
            for (int row = 0; textBoxes && row < textBoxes->count(); ++row)
            {
                textBoxes->setCurrentRow(row);
                QApplication::processEvents();
                const int before = uiProject->entities().size();
                create->click();
                QApplication::processEvents();
                if (uiProject->entities().size() != before + 1)
                {
                    qCritical() << "special-test: text box row did not create" << row;
                    ++local;
                }
            }
            if (textBoxes)
                textBoxes->setCurrentRow(1);
            QApplication::processEvents();
            workbench->grab().toImage().save(
                QStringLiteral("/tmp/pvforge_special_workbench.png"));

            types->setCurrentRow(4);   // the "special" page
            QApplication::processEvents();
            for (int row = 0; row < special->count(); ++row)
            {
                special->setCurrentRow(row);
                QApplication::processEvents();
                const int before = uiProject->entities().size();
                create->click();
                QApplication::processEvents();
                if (uiProject->entities().size() != before + 1)
                {
                    qCritical() << "special-test: workbench did not create row" << row;
                    ++local;
                }
            }
            special->setCurrentRow(2);   // halftone, for the shot
            QApplication::processEvents();
            workbench->grab().toImage().save(
                QStringLiteral("/tmp/pvforge_halftone_workbench.png"));
            special->setCurrentRow(3);   // brand wall, for the shot
            QApplication::processEvents();
            workbench->grab().toImage().save(
                QStringLiteral("/tmp/pvforge_wall_workbench.png"));
        }

        // The comic spike can be dragged to a new tip position.
        auto* viewport = uiWindow.findChild<pvf::Viewport*>();
        pvf::EntityId comicId = pvf::kInvalidEntity;
        for (const pvf::Entity& entity : uiProject->entities())
        {
            if (entity.type == pvf::EntityType::ComicText)
            {
                comicId = entity.id;
                break;
            }
        }
        if (!viewport || comicId == pvf::kInvalidEntity)
        {
            qCritical() << "special-test: comic entity or viewport missing";
            ++local;
        }
        else
        {
            uiWindow.setSelectedEntityForTest(comicId);
            QApplication::processEvents();
            const QPointF tip = viewport->comicTipForTest(comicId);
            const pvf::Entity* comic = uiProject->entity(comicId);
            const double beforeX = comic->data.value(QStringLiteral("bubbleTipX")).toDouble();
            const auto send = [&](QEvent::Type type, const QPointF& pos) {
                QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)),
                                  Qt::LeftButton,
                                  type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                    : Qt::LeftButton,
                                  Qt::NoModifier);
                QApplication::sendEvent(viewport, &event);
            };
            send(QEvent::MouseButtonPress, tip);
            send(QEvent::MouseMove, tip + QPointF(20, 0));
            send(QEvent::MouseMove, tip + QPointF(40, 0));
            send(QEvent::MouseMove, tip + QPointF(60, 0));
            send(QEvent::MouseButtonRelease, tip + QPointF(60, 0));
            QApplication::processEvents();
            const pvf::Entity* moved = uiProject->entity(comicId);
            const double afterX = moved->data.value(QStringLiteral("bubbleTipX")).toDouble();
            if (std::abs(afterX - beforeX) < 0.01)
            {
                qCritical() << "special-test: comic spike did not move" << beforeX
                            << afterX;
                ++local;
            }
            if (auto* undo = uiWindow.findChild<QAction*>(QStringLiteral("undoAction")))
            {
                undo->trigger();
                QApplication::processEvents();
                const pvf::Entity* undone = uiProject->entity(comicId);
                if (std::abs(undone->data.value(QStringLiteral("bubbleTipX")).toDouble()
                             - beforeX) > 0.01)
                {
                    qCritical() << "special-test: comic spike undo failed";
                    ++local;
                }
            }
        }

        // The layout dialog opens with the style it was given.
        pvf::TextStyle style;
        style.text = QStringLiteral("排版预览\n第二行");
        style.pixelSize = 48;
        style.color = QColor(30, 30, 36);
        style.letterSpacing = 8;
        style.boxWidth = 260;
        pvf::TextLayoutDialog dialog(style);
        dialog.show();
        QApplication::processEvents();
        if (!dialog.findChild<pvf::TextPreview*>(QStringLiteral("layoutPreview")))
        {
            qCritical() << "special-test: layout preview missing";
            ++local;
        }
        auto* letterSpacing =
            dialog.findChild<QDoubleSpinBox*>(QStringLiteral("layoutLetterSpacing"));
        if (!letterSpacing || std::abs(letterSpacing->value() - 8.0) > 1e-6)
        {
            qCritical() << "special-test: layout letter spacing missing";
            ++local;
        }
        if (dialog.style().text != style.text || dialog.style().boxWidth != style.boxWidth)
        {
            qCritical() << "special-test: layout dialog changed the style";
            ++local;
        }
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_text_layout.png"));

        // The status bar performance monitor toggles from the view menu.
        if (auto* perfAction =
                uiWindow.findChild<QAction*>(QStringLiteral("perfMonitorAction")))
        {
            auto* perfLabel =
                uiWindow.findChild<QLabel*>(QStringLiteral("perfInfo"));
            perfAction->setChecked(true);
            QApplication::processEvents();
            if (!perfLabel || !perfLabel->isVisible()
                || !perfLabel->text().contains(QStringLiteral("FPS")))
            {
                qCritical() << "special-test: performance monitor label missing";
                ++local;
            }
            perfAction->setChecked(false);
            QApplication::processEvents();
        }
        else
        {
            qCritical() << "special-test: performance monitor action missing";
            ++local;
        }

        // The wall editor opens with its slots and can add more logos.
        pvf::BrandWallParams wallParams;
        wallParams.width = 640;
        wallParams.height = 360;
        wallParams.columns = 8;
        wallParams.rows = 5;
        wallParams.gap = 4;
        wallParams.logos = QStringList{QString(), QString()};
        wallParams.tileColors = QStringList{QColor(210, 70, 160).name(),
                                            QColor(232, 232, 238).name()};
        pvf::BrandWallDialog wallDialog(wallParams,
                                        QVector<QImage>{QImage(), QImage()});
        wallDialog.show();
        QApplication::processEvents();
        if (!wallDialog.findChild<pvf::BrandWallPreview*>(
                QStringLiteral("wallPreview"))
            || wallDialog.params().logos.size() != 2
            || wallDialog.params().columns != 8
            || wallDialog.params().rows != 5)
        {
            qCritical() << "special-test: wall dialog state wrong";
            ++local;
        }
        wallDialog.grab().toImage().save(
            QStringLiteral("/tmp/pvforge_wall_dialog.png"));

        // A clean scene shot: only the selected comic balloon.
        for (pvf::Entity& entity : uiProject->mutableEntities())
            entity.hidden = entity.id != comicId;
        uiWindow.setSelectedEntityForTest(comicId);
        QApplication::processEvents();
        if (auto* props = uiWindow.findChild<pvf::PropertiesPanel*>())
            if (auto* scroll = qobject_cast<QScrollArea*>(
                    props->parentWidget() ? props->parentWidget()->parentWidget()
                                          : nullptr))
                scroll->verticalScrollBar()->setValue(
                    scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        uiWindow.grab().toImage().save(QStringLiteral("/tmp/pvforge_special_scene.png"));
        qInfo() << "special-test failures" << local;
        app.exit(local == 0 ? 0 : 1);
    });
    return app.exec();
}

int welcomeTest(QApplication& app)
{
    const QString projectPath =
        QDir::tempPath() + QStringLiteral("/pvforge_welcome_test.pvproj");

    pvf::Project project;
    project.name = QStringLiteral("欢迎页测试");
    project.width = 1280;
    project.height = 720;
    project.fps = 30;
    QString error;
    if (!project.save(projectPath, &error))
        return 1;

    QSettings settings;
    const QVariant oldRecent = settings.value(QStringLiteral("recentProjects"));
    settings.setValue(QStringLiteral("recentProjects"), QStringList{projectPath});

    pvf::WelcomeWindow welcome;
    welcome.show();

    QTimer::singleShot(400, &welcome, [&app, &welcome, projectPath, oldRecent] {
        welcome.grab().toImage().save(QStringLiteral("/tmp/pvforge_welcome.png"));

        auto* dialog = new pvf::NewProjectDialog(&welcome);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();

        QTimer::singleShot(300, dialog, [&app, dialog, projectPath, oldRecent] {
            dialog->grab().toImage().save(QStringLiteral("/tmp/pvforge_newproject.png"));
            qInfo() << "welcome-test OK";

            QSettings settings;
            if (oldRecent.isValid())
                settings.setValue(QStringLiteral("recentProjects"), oldRecent);
            else
                settings.remove(QStringLiteral("recentProjects"));
            QFile::remove(projectPath);
            app.exit(0);
        });
    });
    return app.exec();
}

int timelineRowButtonsTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::EntityId firstId = pvf::kInvalidEntity;
    {
        pvf::Entity* first = project->addVector(0, 120, 80, Qt::red, Qt::white, 0);
        firstId = first->id;
        project->setValue(firstId, QStringLiteral("x"), 0, 0, true);
        project->setValue(firstId, QStringLiteral("x"), 60, 300, true);
    }
    {
        pvf::Entity* second = project->addVector(1, 120, 80, Qt::green, Qt::white, 0);
        project->setValue(second->id, QStringLiteral("x"), 30, 100, true);
    }
    qInfo() << "entities" << project->entities().size();

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
        return 1;

    QTimer::singleShot(400, &window, [&app, &window, timeline, project, firstId] {
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(timeline->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };
        auto click = [&](const QPoint& pos) {
            send(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
        };

        int failures = 0;

        // Hover the first row so the inline buttons show.
        send(QEvent::MouseMove, QPoint(400, timeline->rowCenterYForTest(0)),
             Qt::NoButton, Qt::NoButton);
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_row_buttons.png"));

        // Rubber-band from the empty row above down across both entity rows,
        // picking up every keyframe in range.
        const QPoint boxStart(timeline->frameXForTest(5),
                              timeline->rowCenterYForTest(3));
        const QPoint boxEnd(timeline->frameXForTest(65),
                            timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, boxStart, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, boxEnd, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, boxEnd, Qt::LeftButton, Qt::NoButton);
        const int boxed = timeline->selectedKeyframeCount();
        qInfo() << "boxed" << boxed << "entities" << project->entities().size();
        if (boxed != 2)
        {
            qCritical() << "timeline-rowbuttons-test: boxed keyframes" << boxed;
            ++failures;
        }

        // Inline buttons: select all keyframes, hide, lock.
        click(timeline->rowButtonRect(0, 2).center());
        const int all = timeline->selectedKeyframeCount();
        if (all != 2)
        {
            qCritical() << "timeline-rowbuttons-test: select all keyframes" << all;
            ++failures;
        }

        click(timeline->rowButtonRect(0, 0).center());
        if (!project->entity(firstId)->hidden)
        {
            qCritical() << "timeline-rowbuttons-test: hide failed";
            ++failures;
        }

        click(timeline->rowButtonRect(0, 1).center());
        if (!project->entity(firstId)->locked)
        {
            qCritical() << "timeline-rowbuttons-test: lock failed";
            ++failures;
        }

        qInfo() << "timeline-rowbuttons-test: boxed" << boxed << "all" << all << "failures"
                << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int timelineInteractionTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::Entity* first = project->addVector(0, 120, 80, Qt::red, Qt::white, 0);
    project->setValue(first->id, QStringLiteral("x"), 0, 0, true);
    project->setValue(first->id, QStringLiteral("x"), 60, 300, true);
    project->addVector(1, 120, 80, Qt::green, Qt::white, 0);
    project->addVector(2, 120, 80, Qt::blue, Qt::white, 0);

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "timeline-interaction-test: timeline not found";
        return 1;
    }

    QTimer::singleShot(400, &window, [&app, &window, timeline] {
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(timeline->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        int failures = 0;

        // Ruler press moves the playhead.
        const QPoint rulerPoint(timeline->frameXForTest(53), 10);
        send(QEvent::MouseButtonPress, rulerPoint, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, rulerPoint, Qt::LeftButton, Qt::NoButton);
        const int rulerFrame = window.currentFrame();
        if (std::abs(rulerFrame - 53) > 2)
        {
            qCritical() << "timeline-interaction-test: ruler frame" << rulerFrame;
            ++failures;
        }

        // Empty row click jumps on release without scrolling the keyframes
        // out of view.  Row 3 is the padded blank row above the clips.
        const QPoint emptyRow(timeline->frameXForTest(20),
                              timeline->rowCenterYForTest(3));
        send(QEvent::MouseButtonPress, emptyRow, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, emptyRow, Qt::LeftButton, Qt::NoButton);
        const int emptyFrame = window.currentFrame();
        if (std::abs(emptyFrame - 20) > 2)
        {
            qCritical() << "timeline-interaction-test: empty frame" << emptyFrame;
            ++failures;
        }

        // Keyframe click selects it and moves the playhead to the keyframe,
        // so the property panel shows the values stored in that keyframe.
        const int beforeKey = window.currentFrame();
        const QPoint keyPoint(timeline->frameXForTest(60),
                              timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, keyPoint, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, keyPoint, Qt::LeftButton, Qt::NoButton);
        const int afterKey = window.currentFrame();
        if (afterKey != 60)
        {
            qCritical() << "timeline-interaction-test: keyframe did not focus"
                        << beforeKey << afterKey;
            ++failures;
        }
        if (timeline->selectedKeyframeCount() != 1)
        {
            qCritical() << "timeline-interaction-test: keyframe not selected";
            ++failures;
        }

        // Clicking empty space clears the keyframe selection.
        send(QEvent::MouseButtonPress, emptyRow, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, emptyRow, Qt::LeftButton, Qt::NoButton);
        if (timeline->selectedKeyframeCount() != 0)
        {
            qCritical() << "timeline-interaction-test: selection not cleared";
            ++failures;
        }

        // Dragging rubber-bands instead of scrubbing.
        const QPoint dragStart(timeline->frameXForTest(300),
                               timeline->rowCenterYForTest(3));
        const QPoint dragEnd(timeline->frameXForTest(200),
                             timeline->rowCenterYForTest(0));
        send(QEvent::MouseButtonPress, dragStart, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, dragEnd, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, dragEnd, Qt::LeftButton, Qt::NoButton);
        if (window.currentFrame() != emptyFrame)
        {
            qCritical() << "timeline-interaction-test: drag moved playhead";
            ++failures;
        }

        qInfo() << "timeline-interaction-test: ruler" << rulerFrame << "empty" << emptyFrame
                << "key" << beforeKey << "->" << afterKey << "failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
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
        auto* mcpPort = dialog->findChild<QSpinBox*>(QStringLiteral("mcpPort"));
        auto* mcpToken = dialog->findChild<QLineEdit*>(QStringLiteral("mcpToken"));
        auto* gitEnabled =
            dialog->findChild<QCheckBox*>(QStringLiteral("gitEnabled"));
        auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("applyButton"));
        expect(autoSave && commitDelay && snap && snapCenter && htmlScale
                   && htmlMemory && watchdog && videoWidth && workbenchScale
                   && workbenchExpand && mcpPort && mcpToken && gitEnabled
                   && apply,
               QStringLiteral("app settings widgets missing"));
        if (failures)
            return 1;

        expect(autoSave->value() == 5 && commitDelay->value() == 1000
                   && snap->value() == 6
                   && htmlScale->currentData().toDouble() == 0.5
                   && videoWidth->currentData().toInt() == 960
                   && workbenchScale->currentData().toInt() == 50
                   && mcpPort->value() == 47821 && !gitEnabled->isChecked(),
               QStringLiteral("app settings defaults mismatch"));

        dialog->grab().toImage().save(
            QStringLiteral("/tmp/opencode/pvforge-p6-settings.png"));

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
                   && saved.workbenchCacheExpandPercent == 75,
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
                   && afterWorkbench->expandPercent() == 75,
               QStringLiteral("workbench service kept the old settings"));
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

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, oldSnap, oldEdgeCentre] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "settings-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const QPoint center = viewport->rect().center();
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        };

        // A larger snap distance grabs a 10px drag.
        viewport->setSnapPixels(20);
        drag(center + QPoint(40, 0), center + QPoint(50, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "settings-test: large snap did not stick";
            ++failures;
        }

        // A small snap distance ignores it.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        viewport->setSnapPixels(2);
        drag(center + QPoint(40, 0), center + QPoint(50, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) < 1.0)
        {
            qCritical() << "settings-test: small snap still stuck";
            ++failures;
        }

        // Edge-to-centre snapping obeys the preference.
        const double scale = viewport->currentScale();
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        viewport->setSnapPixels(20);
        viewport->setEdgeCenterSnap(true);
        const int edgeDx = int(std::lround(145.0 * scale));
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - 150.0) > 1e-6)
        {
            qCritical() << "settings-test: edge-centre snap on failed"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        viewport->setEdgeCenterSnap(false);
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - 145.0) > 1.0)
        {
            qCritical() << "settings-test: edge-centre snap off failed"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }
        viewport->setEdgeCenterSnap(oldEdgeCentre);
        viewport->setSnapPixels(oldSnap);

        // The settings dialog builds and can be shown.
        pvf::SettingsDialog dialog(&window);
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_settings.png"));

        qInfo() << "settings-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int snapTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "snap-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        const double canvasHalfW = project->width / 2.0;
        const double canvasHalfH = project->height / 2.0;

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, mods);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mods) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
        };

        // X axis snaps to the centre line.
        drag(center + QPoint(40, 0), center + QPoint(42, 0), Qt::NoModifier);
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "snap-test: centre snap"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        // X axis snaps the entity edge flush with the canvas edge.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        const double targetX = canvasHalfW - 150.0 - 5.0;   // right edge 5px short
        const int edgeDx = int(std::lround(targetX * s));
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0), Qt::NoModifier);
        const double expectedEdgeX = canvasHalfW - 150.0;   // entity half width
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - expectedEdgeX) > 1e-6)
        {
            qCritical() << "snap-test: edge snap"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        // Ctrl disables snapping.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        drag(center + QPoint(40, 0), center + QPoint(42, 0), Qt::ControlModifier);
        const double unsnapped = project->value(id, QStringLiteral("x"), 0);
        if (std::abs(unsnapped) < 1.0)
        {
            qCritical() << "snap-test: ctrl still snapped";
            ++failures;
        }

        // Free drag snaps to the bottom-right canvas corner.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        const int cornerDx = int(std::lround((canvasHalfW - 150.0 - 5.0) * s));
        const int cornerDy = int(std::lround((canvasHalfH - 100.0 - 5.0) * s));
        drag(center, center + QPoint(cornerDx, cornerDy), Qt::NoModifier);
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - (canvasHalfW - 150.0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("y"), 0) - (canvasHalfH - 100.0))
                   > 1e-6)
        {
            qCritical() << "snap-test: corner snap"
                        << project->value(id, QStringLiteral("x"), 0)
                        << project->value(id, QStringLiteral("y"), 0);
            ++failures;
        }

        // Snapping still works with a custom pivot: the visible body snaps.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        window.applyPivotForTest(id, 100.0, 0.0);   // x becomes 100, body stays
        const QPoint pivotPoint = viewport->entityPivotForTest(id).toPoint();
        const int moveDx = int(std::lround((canvasHalfW - 150.0 - 5.0) * s));
        drag(pivotPoint + QPoint(30, 0), pivotPoint + QPoint(30 + moveDx, 0),
             Qt::NoModifier);
        const double expectedPivotSnap = canvasHalfW - 150.0 + 100.0;
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - expectedPivotSnap)
            > 2.0)
        {
            qCritical() << "snap-test: pivot offset snap"
                        << project->value(id, QStringLiteral("x"), 0) << expectedPivotSnap;
            ++failures;
        }
        window.applyPivotForTest(id, 0.0, 0.0);

        // Rotated entities snap using their rotated bounds (90° swaps w/h).
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("rotation"), 0, 90.0, false);
        const int rotatedDx =
            int(std::lround((canvasHalfW - 100.0 - 5.0) * s));   // half height now
        drag(center + QPoint(40, 0), center + QPoint(40 + rotatedDx, 0), Qt::NoModifier);
        const double rotatedExpected = canvasHalfW - 100.0;
        if (std::abs(project->value(id, QStringLiteral("x"), 0) - rotatedExpected) > 1.5)
        {
            qCritical() << "snap-test: rotated edge snap"
                        << project->value(id, QStringLiteral("x"), 0) << rotatedExpected;
            ++failures;
        }
        project->setValue(id, QStringLiteral("rotation"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);

        // Scaling the bottom-right corner snaps the edge to the canvas edge.
        project->setValue(id, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0.0, false);
        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);
        const QPoint corner(center.x() + int(std::lround(150.0 * s)),
                            center.y() + int(std::lround(100.0 * s)));
        const QPoint edge(center.x() + int(std::lround((canvasHalfW - 5.0) * s)),
                          center.y() + int(std::lround(100.0 * s)));
        drag(corner, edge, Qt::NoModifier);
        const double scaleX = project->value(id, QStringLiteral("scaleX"), 0);
        const double expected = canvasHalfW / 150.0;
        if (std::abs(scaleX - expected) > 0.05)
        {
            qCritical() << "snap-test: scale snap" << scaleX << expected;
            ++failures;
        }

        qInfo() << "snap-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int scaleHandleTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "scale-handle-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        const int halfW = int(std::lround(150.0 * s));
        const int halfH = int(std::lround(100.0 * s));
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_handles.png"));

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, modifiers);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mods) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
        };

        auto cornerNow = [&] {
            return viewport->entityPivotForTest(id).toPoint()
                   + QPoint(int(std::lround(150.0 * s)), int(std::lround(100.0 * s)));
        };

        // Corner handle scales both axes independently.
        drag(cornerNow(), center + QPoint(2 * halfW, 3 * halfH / 2), Qt::NoModifier);
        const double sx1 = project->value(id, QStringLiteral("scaleX"), 0);
        const double sy1 = project->value(id, QStringLiteral("scaleY"), 0);
        if (std::abs(sx1 - 2.0) > 0.1 || std::abs(sy1 - 1.5) > 0.1)
        {
            qCritical() << "scale-handle-test: corner handle" << sx1 << sy1;
            ++failures;
        }

        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);

        // Shift keeps the aspect ratio. Use the current corner as the target
        // base so the drag distance matches the new centre.
        const QPoint shiftCorner = cornerNow();
        drag(shiftCorner, shiftCorner + QPoint(int(std::lround(150.0 * s)),
                                               int(std::lround(50.0 * s))),
             Qt::ShiftModifier);
        const double sx2 = project->value(id, QStringLiteral("scaleX"), 0);
        const double sy2 = project->value(id, QStringLiteral("scaleY"), 0);
        if (std::abs(sx2 - 2.0) > 0.1 || std::abs(sy2 - 2.0) > 0.1)
        {
            qCritical() << "scale-handle-test: shift uniform" << sx2 << sy2;
            ++failures;
        }

        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);

        // Dragging past the centre mirrors the entity.
        drag(cornerNow(), center - QPoint(halfW, halfH), Qt::NoModifier);
        const double sx3 = project->value(id, QStringLiteral("scaleX"), 0);
        if (sx3 >= -0.5)
        {
            qCritical() << "scale-handle-test: mirror" << sx3;
            ++failures;
        }

        // A 16:9 entity in a 16:9 project: Shift + corner must snap the
        // visual bounds to the canvas edges and fill it from the centre.
        pvf::Entity* videoLike =
            project->addVector(0, 320, 180, Qt::blue, Qt::white, 0);
        const pvf::EntityId videoId = videoLike->id;
        window.setSelectedEntityForTest(videoId);
        const QPoint videoCenter = viewport->entityPivotForTest(videoId).toPoint();
        const QPoint videoCorner(
            videoCenter.x() + int(std::lround(160.0 * s)),
            videoCenter.y() + int(std::lround(90.0 * s)));
        const QPoint canvasCorner(
            videoCenter.x() + int(std::lround(960.0 * s)) - 3,
            videoCenter.y() + int(std::lround(540.0 * s)) - 3);
        drag(videoCorner, canvasCorner, Qt::ShiftModifier);
        double fillX = project->value(videoId, QStringLiteral("scaleX"), 0);
        double fillY = project->value(videoId, QStringLiteral("scaleY"), 0);
        if (std::abs(fillX - 6.0) > 0.05 || std::abs(fillY - 6.0) > 0.05)
        {
            qCritical() << "scale-handle-test: fullscreen snap" << fillX << fillY;
            ++failures;
        }

        // The same visual-AABB snap has to work at special rotation angles.
        project->setValue(videoId, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(videoId, QStringLiteral("scaleY"), 0, 1.0, false);
        project->setValue(videoId, QStringLiteral("rotation"), 0, 180.0, false);
        const QPoint rotatedCorner(
            videoCenter.x() - int(std::lround(160.0 * s)),
            videoCenter.y() - int(std::lround(90.0 * s)));
        const QPoint rotatedTarget(
            videoCenter.x() - int(std::lround(960.0 * s)) + 3,
            videoCenter.y() - int(std::lround(540.0 * s)) + 3);
        drag(rotatedCorner, rotatedTarget, Qt::ShiftModifier);
        fillX = project->value(videoId, QStringLiteral("scaleX"), 0);
        fillY = project->value(videoId, QStringLiteral("scaleY"), 0);
        if (std::abs(fillX - 6.0) > 0.05 || std::abs(fillY - 6.0) > 0.05)
        {
            qCritical() << "scale-handle-test: rotated fullscreen snap" << fillX
                        << fillY;
            ++failures;
        }
        project->setValue(videoId, QStringLiteral("rotation"), 0, 0.0, false);

        qInfo() << "scale-handle-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int scaleUndoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "scale-undo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPoint center = viewport->entityPivotForTest(id).toPoint();
        const QPoint corner(center.x() + int(std::lround(150.0 * s)),
                            center.y() + int(std::lround(100.0 * s)));

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };

        send(QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i)
            send(QEvent::MouseMove,
                 corner + QPoint(int(std::lround(15.0 * s * i)),
                                 int(std::lround(10.0 * s * i))),
                 Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease,
             corner + QPoint(int(std::lround(120.0 * s)), int(std::lround(80.0 * s))),
             Qt::LeftButton, Qt::NoButton);

        const double endSx = project->value(id, QStringLiteral("scaleX"), 0);
        const double endSy = project->value(id, QStringLiteral("scaleY"), 0);
        if (endSx <= 1.5 || endSy <= 1.3)
        {
            qCritical() << "scale-undo-test: scaling did nothing" << endSx << endSy;
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(id, QStringLiteral("scaleX"), 0) - 1.0) > 1e-6
            || std::abs(project->value(id, QStringLiteral("scaleY"), 0) - 1.0) > 1e-6)
        {
            qCritical() << "scale-undo-test: one undo did not restore scale"
                        << project->value(id, QStringLiteral("scaleX"), 0)
                        << project->value(id, QStringLiteral("scaleY"), 0);
            ++failures;
        }

        // Pivot drags merge into one undo step too.
        window.applyPivotForTest(id, 50.0, 0.0);
        window.applyPivotForTest(id, 100.0, 0.0);
        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(id, QStringLiteral("rotPointX"), 0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "scale-undo-test: pivot undo failed"
                        << project->value(id, QStringLiteral("rotPointX"), 0)
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        qInfo() << "scale-undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int timelineFrameTest(QApplication& app)
{
    pvf::Settings::setTimelineDisplayFrames(false);

    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    window.activateWindow();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        if (!timeline)
        {
            qCritical() << "timeline-frame-test: timeline missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        project->addKeyframe(entity->id, QStringLiteral("x"), 100.0, 300.0);

        // Mine-imator's frame-tick density table (frame label mode).
        auto checkSteps = [&](double pixels, int tick, int label) {
            timeline->setPixelsPerFrameForTest(pixels);
            if (timeline->rulerTickStepForTest() != tick
                || timeline->rulerLabelStepForTest() != label)
            {
                qCritical() << "timeline-frame-test: wrong frame steps at" << pixels
                            << timeline->rulerTickStepForTest()
                            << timeline->rulerLabelStepForTest();
                ++failures;
            }
        };
        checkSteps(8.0, 1, 5);
        checkSteps(6.0, 1, 5);
        checkSteps(4.0, 5, 10);
        checkSteps(2.0, 10, 50);
        checkSteps(0.8, 20, 100);
        checkSteps(0.4, 50, 200);

        // Seconds mode must subdivide one second evenly for the project fps.
        auto checkSeconds = [&](int fps, double pixels, int cell, int label) {
            project->fps = fps;
            timeline->setPixelsPerFrameForTest(pixels);
            const int actualCell = timeline->secondsCellStepForTest();
            const int actualLabel = timeline->secondsLabelStepForTest();
            if (actualCell != cell || actualLabel != label)
            {
                qCritical() << "timeline-frame-test: wrong second grid at" << fps
                            << "fps" << pixels << "px: cells" << actualCell
                            << "label" << actualLabel;
                ++failures;
            }
            if (actualLabel % actualCell != 0
                || (fps % actualCell != 0 && actualCell % fps != 0))
            {
                qCritical() << "timeline-frame-test: grid does not divide seconds"
                            << fps << actualCell << actualLabel;
                ++failures;
            }
        };
        // 24 fps: 24, 12, 8, 6, 4, 2 cells per second as the zoom shrinks.
        checkSeconds(24, 6.0, 1, 24);
        checkSeconds(24, 4.0, 2, 24);
        checkSeconds(24, 2.0, 3, 24);
        checkSeconds(24, 1.0, 6, 48);
        checkSeconds(24, 0.5, 12, 96);
        checkSeconds(24, 0.25, 24, 192);
        checkSeconds(24, 32.0, 1, 6);   // 4 cells per quarter second
        // Other rates use their own divisors, not a hard-coded 5 frames.
        checkSeconds(30, 6.0, 1, 30);
        checkSeconds(30, 1.0, 5, 60);
        checkSeconds(60, 6.0, 1, 30);
        checkSeconds(25, 6.0, 1, 25);
        project->fps = 24;   // keep the screenshots at the regression case

        // Clicking the ruler focuses the timeline so arrow keys work.
        const QPoint rulerPoint(230, 10);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(rulerPoint),
                          QPointF(timeline->mapToGlobal(rulerPoint)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(rulerPoint),
                            QPointF(timeline->mapToGlobal(rulerPoint)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &press);
        QApplication::sendEvent(timeline, &release);
        if (!timeline->hasFocus())
        {
            qCritical() << "timeline-frame-test: ruler click did not focus the timeline";
            ++failures;
        }

        auto sendKey = [&](QEvent::Type type, int key, bool autoRepeat) {
            QKeyEvent event(type, key, Qt::NoModifier, QString(), autoRepeat);
            QApplication::sendEvent(timeline, &event);
        };
        auto wait = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };

        // A normal tap (held for a moment) moves exactly one frame. The
        // continuous repeat only starts after the hold delay.
        window.goToFrame(10);
        sendKey(QEvent::KeyPress, Qt::Key_Right, false);
        wait(120);
        sendKey(QEvent::KeyRelease, Qt::Key_Right, false);
        wait(100);
        if (window.currentFrame() != 11)
        {
            qCritical() << "timeline-frame-test: tap did not step exactly one frame"
                        << window.currentFrame();
            ++failures;
        }

        // Holding past the delay keeps stepping at the project frame rate.
        const int holdStart = window.currentFrame();
        sendKey(QEvent::KeyPress, Qt::Key_Right, false);
        wait(650);
        const int heldTo = window.currentFrame();
        sendKey(QEvent::KeyRelease, Qt::Key_Right, false);
        wait(120);
        if (heldTo <= holdStart)
        {
            qCritical() << "timeline-frame-test: hold did not advance" << heldTo;
            ++failures;
        }
        if (window.currentFrame() != heldTo)
        {
            qCritical() << "timeline-frame-test: release did not stop"
                        << heldTo << window.currentFrame();
            ++failures;
        }

        // Left arrow: one tap is one frame, holding runs backwards.
        window.goToFrame(40);
        sendKey(QEvent::KeyPress, Qt::Key_Left, false);
        wait(120);
        sendKey(QEvent::KeyRelease, Qt::Key_Left, false);
        wait(80);
        if (window.currentFrame() != 39)
        {
            qCritical() << "timeline-frame-test: left tap did not step one frame"
                        << window.currentFrame();
            ++failures;
        }

        sendKey(QEvent::KeyPress, Qt::Key_Left, false);
        wait(650);
        const int reverseTo = window.currentFrame();
        sendKey(QEvent::KeyRelease, Qt::Key_Left, false);
        if (reverseTo >= 39)
        {
            qCritical() << "timeline-frame-test: reverse hold did not move" << reverseTo;
            ++failures;
        }

        // Reverse stops at the start, forward loops at the end.
        window.goToFrame(0);
        sendKey(QEvent::KeyPress, Qt::Key_Left, false);
        wait(80);
        sendKey(QEvent::KeyRelease, Qt::Key_Left, false);
        if (window.currentFrame() != 0)
        {
            qCritical() << "timeline-frame-test: reverse passed zero"
                        << window.currentFrame();
            ++failures;
        }
        window.goToFrame(project->timelineLength() - 1);
        sendKey(QEvent::KeyPress, Qt::Key_Right, false);
        sendKey(QEvent::KeyRelease, Qt::Key_Right, false);
        if (window.currentFrame() != 0)
        {
            qCritical() << "timeline-frame-test: forward hold did not loop"
                        << window.currentFrame();
            ++failures;
        }

        // Both ruler label modes render, and the status bar follows the unit
        // instead of always mixing frames and seconds.
        auto* status = window.findChild<QLabel*>(QStringLiteral("statusInfo"));
        auto* framesAction = window.findChild<QAction*>(QStringLiteral("timelineFramesAction"));
        auto* secondsAction =
            window.findChild<QAction*>(QStringLiteral("timelineSecondsAction"));
        if (!status || !framesAction || !secondsAction)
        {
            qCritical() << "timeline-frame-test: status label or unit actions missing";
            ++failures;
        }
        else
        {
            // The visible unit button in the timeline header toggles both the
            // ruler and the status bar, and keeps the menu in sync.
            auto* unitButton =
                window.findChild<QToolButton*>(QStringLiteral("timelineUnitButton"));
            if (!unitButton)
            {
                qCritical() << "timeline-frame-test: unit button missing";
                ++failures;
            }
            else
            {
                const bool before = timeline->displayFrames();
                unitButton->click();
                if (timeline->displayFrames() == before
                    || framesAction->isChecked() != timeline->displayFrames())
                {
                    qCritical() << "timeline-frame-test: unit button did not sync"
                                << timeline->displayFrames()
                                << framesAction->isChecked();
                    ++failures;
                }
                unitButton->click();   // back to seconds
            }

            window.goToFrame(20);
            if (!status->text().contains(QStringLiteral("s /"))
                || status->text().contains(QStringLiteral("帧")))
            {
                qCritical() << "timeline-frame-test: seconds status is wrong"
                            << status->text();
                ++failures;
            }

            framesAction->trigger();
            if (!timeline->displayFrames()
                || !status->text().contains(QStringLiteral("帧"))
                || status->text().contains(QStringLiteral("s /")))
            {
                qCritical() << "timeline-frame-test: frame mode status is wrong"
                            << status->text();
                ++failures;
            }
            timeline->setPixelsPerFrameForTest(6.0);
            QApplication::processEvents();
            timeline->grab().save(QStringLiteral("/tmp/pvforge_timeline_frames.png"));

            secondsAction->trigger();
            if (timeline->displayFrames()
                || !status->text().contains(QStringLiteral("s /")))
            {
                qCritical() << "timeline-frame-test: seconds mode not restored"
                            << status->text();
                ++failures;
            }
            QApplication::processEvents();
            timeline->grab().save(QStringLiteral("/tmp/pvforge_timeline_seconds.png"));
        }

        pvf::Settings::setTimelineDisplayFrames(false);
        qInfo() << "timeline-frame-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int easingTest(QApplication& app)
{
    int failures = 0;
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };

    pvf::EasingSpec linear;
    if (!near(pvf::applyEasing(linear, 0.25), 0.25))
    {
        qCritical() << "easing-test: linear";
        ++failures;
    }

    pvf::EasingSpec instant;
    instant.easing = pvf::Easing::Instant;
    if (!near(pvf::applyEasing(instant, 0.5), 0.0)
        || !near(pvf::applyEasing(instant, 0.999), 0.0)
        || !near(pvf::applyEasing(instant, 1.0), 1.0))
    {
        qCritical() << "easing-test: instant";
        ++failures;
    }

    pvf::EasingSpec inQuad;
    inQuad.easing = pvf::Easing::EaseInQuad;
    pvf::EasingSpec outQuad;
    outQuad.easing = pvf::Easing::EaseOutQuad;
    if (!near(pvf::applyEasing(inQuad, 0.5), 0.25)
        || !near(pvf::applyEasing(outQuad, 0.5), 0.75))
    {
        qCritical() << "easing-test: quad";
        ++failures;
    }

    pvf::EasingSpec bezier;
    bezier.easing = pvf::Easing::Bezier;
    bezier.inX = 0.0;
    bezier.inY = 0.0;
    bezier.outX = 1.0;
    bezier.outY = 1.0;
    if (!near(pvf::applyEasing(bezier, 0.5), 0.5))
    {
        qCritical() << "easing-test: bezier linear handles";
        ++failures;
    }

    pvf::EasingSpec back;
    back.easing = pvf::Easing::EaseOutBack;
    pvf::EasingSpec backIn;
    backIn.easing = pvf::Easing::EaseInBack;
    if (pvf::applyEasing(back, 0.8) <= 1.0 || pvf::applyEasing(backIn, 0.2) >= 0.0)
    {
        qCritical() << "easing-test: overshoot functions are clamped";
        ++failures;
    }

    auto* project = new pvf::Project;
    pvf::Entity* entity = project->addVector(0, 200, 200, Qt::red, Qt::white, 0);
    const pvf::EntityId id = entity->id;
    project->setValue(id, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(id, QStringLiteral("x"), 10, 100.0, true);
    if (!near(project->value(id, QStringLiteral("x"), 5), 50.0))
    {
        qCritical() << "easing-test: linear track";
        ++failures;
    }
    if (!near(project->previousKeyframeFrame(id, 10), 0.0)
        || project->previousKeyframeFrame(id, 0) >= 0.0)
    {
        qCritical() << "easing-test: previous keyframe lookup";
        ++failures;
    }

    // The first keyframe has no incoming segment, so its ease is ignored.
    project->setKeyframeEasing(id, QStringLiteral("x"), 0, back);
    if (!near(project->value(id, QStringLiteral("x"), 5), 50.0))
    {
        qCritical() << "easing-test: first key easing leaked into the segment";
        ++failures;
    }

    project->setKeyframeEasing(id, QStringLiteral("x"), 10, inQuad);
    if (!near(project->value(id, QStringLiteral("x"), 5), 25.0))
    {
        qCritical() << "easing-test: eased track"
                    << project->value(id, QStringLiteral("x"), 5);
        ++failures;
    }

    project->setKeyframeEasing(id, QStringLiteral("x"), 10, instant);
    if (!near(project->value(id, QStringLiteral("x"), 5), 0.0)
        || !near(project->value(id, QStringLiteral("x"), 10), 100.0))
    {
        qCritical() << "easing-test: instant track";
        ++failures;
    }

    project->setKeyframeEasing(id, QStringLiteral("x"), 10, back);
    if (project->value(id, QStringLiteral("x"), 8) <= 100.0)
    {
        qCritical() << "easing-test: track clamped overshoot"
                    << project->value(id, QStringLiteral("x"), 8);
        ++failures;
    }

    pvf::EasingSpec custom;
    custom.easing = pvf::Easing::Bezier;
    custom.inX = 0.25;
    custom.inY = 0.0;
    custom.outX = 0.75;
    custom.outY = 1.0;
    project->setKeyframeEasing(id, QStringLiteral("x"), 10, custom);

    // setKey updates the value without dropping the easing.
    project->setValue(id, QStringLiteral("x"), 10, 200.0, true);
    const pvf::Keyframe* key = project->keyframe(id, QStringLiteral("x"), 10);
    if (!key || key->ease != custom)
    {
        qCritical() << "easing-test: setKey dropped easing";
        ++failures;
    }

    const QString path = QStringLiteral("/tmp/pvforge_easing.pvproj");
    QString error;
    if (!project->save(path, &error))
    {
        qCritical() << "easing-test: save failed" << error;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(path, &error))
        {
            qCritical() << "easing-test: load failed" << error;
            ++failures;
        }
        else
        {
            const pvf::Keyframe* loadedKey = loaded.keyframe(id, QStringLiteral("x"), 10);
            if (!loadedKey || loadedKey->ease != custom)
            {
                qCritical() << "easing-test: easing not serialized";
                ++failures;
            }
        }
    }

    // The custom bezier dialog must build and paint without a project.
    {
        pvf::BezierEditorDialog dialog(custom);
        dialog.show();
        QApplication::processEvents();
        const QImage dialogShot = dialog.grab().toImage();
        dialogShot.save(QStringLiteral("/tmp/pvforge_bezier.png"));
        if (dialogShot.isNull() || dialogShot.width() < 400)
        {
            qCritical() << "easing-test: bezier dialog failed";
            ++failures;
        }
        dialog.close();
    }

    // The visual template picker must build and paint.
    {
        pvf::EasingPicker picker(custom);
        picker.show();
        QApplication::processEvents();
        const QImage pickerShot = picker.grab().toImage();
        pickerShot.save(QStringLiteral("/tmp/pvforge_easing_picker.png"));
        if (pickerShot.isNull() || pickerShot.width() < 200)
        {
            qCritical() << "easing-test: easing picker failed";
            ++failures;
        }
        picker.close();
    }

    // Embedding the picker in a menu (the timeline popup) must keep clicks.
    {
        QMenu menu;
        auto* picker = new pvf::EasingPicker(custom);
        auto* action = new QWidgetAction(&menu);
        action->setDefaultWidget(picker);
        menu.addAction(action);
        menu.popup(QPoint(100, 100));
        QApplication::processEvents();
        menu.grab().toImage().save(QStringLiteral("/tmp/pvforge_easing_menu.png"));

        bool picked = false;
        pvf::EasingSpec chosen;
        QObject::connect(picker, &pvf::EasingPicker::picked, &menu,
                         [&picked, &chosen](const pvf::EasingSpec& spec) {
                             picked = true;
                             chosen = spec;
                         });
        const QPoint tile(28, 46);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(tile),
                          QPointF(picker->mapToGlobal(tile)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(tile),
                            QPointF(picker->mapToGlobal(tile)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(picker, &press);
        QApplication::sendEvent(picker, &release);
        if (!picked || chosen.easing != pvf::Easing::Linear)
        {
            qCritical() << "easing-test: menu picker click failed";
            ++failures;
        }
        menu.close();
    }

    // Check the palette-driven drawing in a dark theme as well.
    {
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(35, 38, 41));
        dark.setColor(QPalette::WindowText, QColor(239, 240, 241));
        dark.setColor(QPalette::Base, QColor(27, 30, 32));
        dark.setColor(QPalette::AlternateBase, QColor(42, 46, 50));
        dark.setColor(QPalette::Text, QColor(239, 240, 241));
        dark.setColor(QPalette::PlaceholderText, QColor(160, 165, 170));
        dark.setColor(QPalette::Mid, QColor(90, 95, 100));
        dark.setColor(QPalette::Highlight, QColor(61, 174, 233));
        dark.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        dark.setColor(QPalette::Button, QColor(42, 46, 50));
        dark.setColor(QPalette::ButtonText, QColor(239, 240, 241));
        QApplication::setPalette(dark);

        pvf::EasingPicker picker(custom);
        picker.show();
        QApplication::processEvents();
        picker.grab().toImage().save(
            QStringLiteral("/tmp/pvforge_easing_picker_dark.png"));
        picker.close();

        pvf::BezierEditorDialog dialog(custom);
        dialog.show();
        QApplication::processEvents();
        dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_bezier_dark.png"));
        dialog.close();
    }

    // Easing changes go through the command stack.
    pvf::EasingSpec oldEase = custom;
    pvf::EasingSpec newEase;
    newEase.easing = pvf::Easing::EaseOutBack;
    pvf::KeyframeEasingRecord record;
    record.entity = id;
    record.property = QStringLiteral("x");
    record.frame = 10.0;
    record.oldEase = oldEase;
    record.newEase = newEase;
    QVector<pvf::KeyframeEasingRecord> records;
    records.append(record);
    project->setKeyframeEasing(id, QStringLiteral("x"), 10, newEase);
    pvf::History history;
    history.push(std::make_unique<pvf::SetKeyframeEasingCommand>(records), *project);
    history.undo(*project);
    const pvf::Keyframe* undone = project->keyframe(id, QStringLiteral("x"), 10);
    if (!undone || undone->ease != oldEase)
    {
        qCritical() << "easing-test: undo";
        ++failures;
    }
    history.redo(*project);
    const pvf::Keyframe* redone = project->keyframe(id, QStringLiteral("x"), 10);
    if (!redone || redone->ease != newEase)
    {
        qCritical() << "easing-test: redo";
        ++failures;
    }

    // The entity properties panel exposes the curve for the current frame.
    {
        auto* windowProject = new pvf::Project;
        pvf::MainWindow window(windowProject);
        pvf::Entity* entity = windowProject->addVector(0, 200, 200, Qt::red, Qt::white, 0);
        windowProject->setValue(entity->id, QStringLiteral("x"), 0, 0.0, true);
        window.goToFrame(0);
        window.setSelectedEntityForTest(entity->id);
        auto* button =
            window.findChild<QPushButton*>(QStringLiteral("keyframeEasingButton"));
        if (!button || button->isEnabled()
            || !button->text().contains(QStringLiteral("首帧")))
        {
            qCritical() << "easing-test: first keyframe must not own a curve"
                        << (button ? button->text() : QStringLiteral("missing"));
            ++failures;
        }
        windowProject->setValue(entity->id, QStringLiteral("x"), 10, 100.0, true);
        window.goToFrame(10);
        if (!button || !button->isEnabled()
            || !button->text().contains(QStringLiteral("→")))
        {
            qCritical() << "easing-test: properties easing button"
                        << (button ? button->text() : QStringLiteral("missing"));
            ++failures;
        }
    }

    qInfo() << "easing-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int videoTest(QApplication& app)
{
    int failures = 0;
    const QString path = QStringLiteral("/tmp/pvf_video.mp4");

    QFile::remove(path);
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=red:s=320x240:d=1:r=30"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=blue:s=320x240:d=1:r=30"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                  QStringLiteral("-filter_complex"),
                  QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0[v]"),
                  QStringLiteral("-map"), QStringLiteral("[v]"),
                  QStringLiteral("-map"), QStringLiteral("2:a"),
                  QStringLiteral("-c:v"), QStringLiteral("libx264"),
                  QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                  QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                  QStringLiteral("-c:a"), QStringLiteral("aac"),
                  QStringLiteral("-shortest"), path});
    if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0)
    {
        qCritical() << "video-test: cannot generate test video"
                    << ffmpeg.readAllStandardError();
        return 1;
    }

    QString error;
    double duration = 0.0;
    const QImage first = pvf::decodeVideoFirstFrame(path, &error, &duration);
    if (first.isNull() || duration < 1.8 || duration > 2.2)
    {
        qCritical() << "video-test: first frame" << error << duration;
        return 1;
    }

    auto centerColor = [](const QImage& image) {
        if (image.isNull())
            return QColor();
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };
    auto isRed = [](const QColor& color) {
        return color.red() > 180 && color.green() < 90 && color.blue() < 90;
    };
    auto isBlue = [](const QColor& color) {
        return color.blue() > 180 && color.red() < 90 && color.green() < 90;
    };

    pvf::Project project;
    project.fps = 10;
    pvf::Entity* video = project.addVideo(path, first, duration, QStringLiteral("Video"));
    pvf::VideoPlayer player;

    player.update(project, 2);
    QColor color = centerColor(video->image);
    if (!isRed(color))
    {
        qCritical() << "video-test: frame 2 is not red" << color;
        ++failures;
    }
    player.update(project, 15);
    color = centerColor(video->image);
    if (!isBlue(color))
    {
        qCritical() << "video-test: frame 15 is not blue" << color;
        ++failures;
    }
    player.update(project, 3);
    color = centerColor(video->image);
    if (!isRed(color))
    {
        qCritical() << "video-test: backward seek is not red" << color;
        ++failures;
    }
    player.update(project, 10);
    color = centerColor(video->image);
    if (!isBlue(color))
    {
        qCritical() << "video-test: frame 10 is not blue" << color;
        ++failures;
    }

    // Background proxy: a short-GOP low-resolution copy makes timeline seeks
    // cheap. The source here is 320x240, so the 960-wide proxy is upscaled.
    const QString proxyPath = pvf::videoProxyCachePath(path);
    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
    {
        QFile::remove(proxyPath);
        pvf::VideoProxyGenerator generator;
        bool proxyDone = false;
        bool proxyOk = false;
        QEventLoop proxyLoop;
        QObject::connect(&generator, &pvf::VideoProxyGenerator::finished, &proxyLoop,
                         [&](const QString& source, bool success) {
                             if (source != path)
                                 return;
                             proxyDone = true;
                             proxyOk = success;
                             proxyLoop.quit();
                         });
        generator.ensure(path);
        if (!proxyDone)
        {
            QTimer::singleShot(30000, &proxyLoop, &QEventLoop::quit);
            proxyLoop.exec();
        }
        if (!proxyOk || !pvf::videoProxyReady(path)
            || pvf::videoDecodePath(path) == path)
        {
            qCritical() << "video-test: proxy generation failed" << proxyOk
                        << pvf::videoProxyCachePath(path);
            ++failures;
        }
        else
        {
            double proxyDuration = 0.0;
            const QImage proxyFirst =
                pvf::decodeVideoFirstFrame(pvf::videoDecodePath(path), nullptr,
                                           &proxyDuration);
            if (proxyFirst.isNull() || proxyFirst.width() != 960)
            {
                qCritical() << "video-test: proxy frame" << proxyFirst.size();
                ++failures;
            }
            else
            {
                // Force the player to reopen on the proxy path.
                player.clear();
                player.update(project, 2);
                color = centerColor(video->image);
                const QRectF bounds = pvf::SceneRenderer::entityLocalBounds(*video);
                if (video->image.width() != 960 || !isRed(color)
                    || std::abs(bounds.width() - 320.0) > 1e-6
                    || std::abs(bounds.height() - 240.0) > 1e-6)
                {
                    qCritical() << "video-test: player did not use the proxy"
                                << video->image.size() << color << bounds;
                    ++failures;
                }
            }
        }
    }
    else
    {
        qInfo() << "video-test: ffmpeg CLI missing, proxy test skipped";
    }

    const pvf::MediaImportResult imported =
        pvf::processMediaFile(path, QString());
    if (imported.type != pvf::EntityType::Video || !imported.audio
        || imported.audio->duration < 1.8 || imported.audio->peaks.isEmpty())
    {
        qCritical() << "video-test: audio track was not extracted"
                    << imported.error
                    << (imported.audio ? imported.audio->duration : -1.0);
        ++failures;
    }
    else
    {
        float peak = 0.0f;
        for (float sample : imported.audio->peaks)
            peak = std::max(peak, sample);
        if (peak < 0.05f)
        {
            qCritical() << "video-test: extracted audio is silent" << peak;
            ++failures;
        }
    }

    pvf::Project importProject;
    importProject.fps = 10;
    if (!importProject.addImportedMedia(imported))
    {
        qCritical() << "video-test: addImportedMedia failed";
        ++failures;
    }
    else
    {
        const auto& templates = importProject.templates();
        if (templates.size() != 2)
        {
            qCritical() << "video-test: expected two templates" << templates.size();
            ++failures;
        }
        const pvf::Template* videoTemplate = nullptr;
        const pvf::Template* audioTemplate = nullptr;
        for (const pvf::Template& tmpl : templates)
        {
            if (tmpl.type == pvf::EntityType::Video)
                videoTemplate = &tmpl;
            else if (tmpl.type == pvf::EntityType::Audio)
                audioTemplate = &tmpl;
        }
        if (!videoTemplate || !audioTemplate || !audioTemplate->audio
            || !audioTemplate->name.endsWith(QStringLiteral(" · 音频")))
        {
            qCritical() << "video-test: audio template missing";
            ++failures;
        }
        else
        {
            const pvf::Template videoCopy = *videoTemplate;
            pvf::Entity* createdVideo = importProject.createFromTemplate(videoCopy);
            const auto& entities = importProject.entities();
            const pvf::Entity* createdAudio = nullptr;
            for (const pvf::Entity& e : entities)
                if (e.type == pvf::EntityType::Audio)
                    createdAudio = &e;
            if (!createdVideo || entities.size() != 2 || !createdAudio
                || !createdAudio->audio
                || createdAudio->data.value(QStringLiteral("pairedVideo")).toInt()
                       != createdVideo->id
                || createdAudio->sourcePath != path)
            {
                qCritical() << "video-test: paired audio entity missing"
                            << entities.size();
                ++failures;
            }
        }
    }

    const QString projectPath = QStringLiteral("/tmp/pvf_video_audio.pvproj");
    QString saveError;
    if (!importProject.save(projectPath, &saveError))
    {
        qCritical() << "video-test: save project" << saveError;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(projectPath, &saveError))
        {
            qCritical() << "video-test: load project" << saveError;
            ++failures;
        }
        else
        {
            bool hasVideoTemplate = false;
            bool hasAudioTemplate = false;
            bool hasAudioEntity = false;
            for (const pvf::Template& tmpl : loaded.templates())
            {
                if (tmpl.type == pvf::EntityType::Video)
                    hasVideoTemplate = true;
                if (tmpl.type == pvf::EntityType::Audio && tmpl.audio)
                    hasAudioTemplate = true;
            }
            for (const pvf::Entity& e : loaded.entities())
                if (e.type == pvf::EntityType::Audio && e.audio)
                    hasAudioEntity = true;
            if (!hasVideoTemplate || !hasAudioTemplate || !hasAudioEntity)
            {
                qCritical() << "video-test: audio pairing not restored"
                            << hasVideoTemplate << hasAudioTemplate
                            << hasAudioEntity;
                ++failures;
            }
        }
    }

    auto* windowProject = new pvf::Project;
    windowProject->fps = 10;
    pvf::Entity* uiVideo =
        windowProject->addVideo(path, first, duration, QStringLiteral("Video"));
    pvf::MainWindow window(windowProject);
    window.resize(900, 700);
    window.show();
    QApplication::processEvents();

    window.goToFrame(15);
    QApplication::processEvents();
    color = centerColor(uiVideo->image);
    if (!isBlue(color))
    {
        qCritical() << "video-test: main window frame 15 is not blue" << color;
        ++failures;
    }
    window.grab().save(QStringLiteral("/tmp/pvf_video_frame.png"));

    window.goToFrame(2);
    QApplication::processEvents();
    color = centerColor(uiVideo->image);
    if (!isRed(color))
    {
        qCritical() << "video-test: main window frame 2 is not red" << color;
        ++failures;
    }

    // The play timer must advance both the playhead and the video frame.
    window.goToFrame(0);
    window.togglePlayback();
    {
        QEventLoop loop;
        QTimer::singleShot(1300, &loop, &QEventLoop::quit);
        loop.exec();
    }
    window.togglePlayback();
    color = centerColor(uiVideo->image);
    if (window.currentFrame() < 10 || !isBlue(color))
    {
        qCritical() << "video-test: playback did not reach the blue frame"
                    << window.currentFrame() << color;
        ++failures;
    }

    // Past the clip duration the video must be gone, not hold its last frame.
    window.goToFrame(21);
    QApplication::processEvents();
    if (!uiVideo->image.isNull())
    {
        qCritical() << "video-test: last frame held past the clip";
        ++failures;
    }
    if (std::abs(pvf::SceneRenderer::entityLocalBounds(*uiVideo).width() - 320.0)
        > 1e-6)
    {
        qCritical() << "video-test: bounds lost after the clip";
        ++failures;
    }
    window.goToFrame(19);
    QApplication::processEvents();
    if (uiVideo->image.isNull())
    {
        qCritical() << "video-test: clip did not return in range";
        ++failures;
    }

    // Library flow: creating the video asset must also create the extracted
    // audio entity, and undo/redo must treat both as one step.
    auto* pairProject = new pvf::Project;
    pairProject->fps = 10;
    pairProject->addImportedMedia(imported);
    pvf::MainWindow pairWindow(pairProject);
    pairWindow.resize(900, 700);
    pairWindow.show();
    QApplication::processEvents();

    auto* assetList =
        pairWindow.findChild<QListWidget*>(QStringLiteral("assetList"));
    auto* assetCreate =
        pairWindow.findChild<QPushButton*>(QStringLiteral("assetCreate"));
    if (!assetList || !assetCreate || assetList->count() < 1)
    {
        qCritical() << "video-test: asset library controls missing";
        ++failures;
    }
    else
    {
        assetList->setCurrentRow(0);
        assetCreate->click();
        QApplication::processEvents();

        auto countAudio = [pairProject]() {
            int count = 0;
            for (const pvf::Entity& e : pairProject->entities())
                if (e.type == pvf::EntityType::Audio)
                    ++count;
            return count;
        };
        if (pairProject->entities().size() != 2 || countAudio() != 1)
        {
            qCritical() << "video-test: video asset did not create audio"
                        << pairProject->entities().size() << countAudio();
            ++failures;
        }

        auto* undoAction =
            pairWindow.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redoAction =
            pairWindow.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!undoAction || !redoAction)
        {
            qCritical() << "video-test: undo actions missing";
            ++failures;
        }
        else
        {
            undoAction->trigger();
            QApplication::processEvents();
            if (!pairProject->entities().isEmpty())
            {
                qCritical() << "video-test: paired undo left entities"
                            << pairProject->entities().size();
                ++failures;
            }
            redoAction->trigger();
            QApplication::processEvents();
            if (pairProject->entities().size() != 2 || countAudio() != 1)
            {
                qCritical() << "video-test: paired redo failed"
                            << pairProject->entities().size() << countAudio();
                ++failures;
            }
            pairWindow.grab().save(QStringLiteral("/tmp/pvf_video_pair.png"));
        }
    }

    if (!proxyPath.isEmpty())
        QFile::remove(proxyPath);

    Q_UNUSED(app);
    qInfo() << "video-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int folderTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* first =
        project->addVector(0, 120, 90, QColor(230, 90, 90), Qt::white, 0);
    pvf::Entity* folder = project->addFolder(QStringLiteral("Group"));
    pvf::Entity* second =
        project->addVector(0, 120, 90, QColor(90, 140, 230), Qt::white, 0);
    const pvf::EntityId firstId = first->id;
    const pvf::EntityId folderId = folder->id;
    const pvf::EntityId secondId = second->id;

    int failures = 0;
    if (!project->setEntityParent(secondId, folderId)
        || second->parent != folderId)
    {
        qCritical() << "folder-test: set parent";
        ++failures;
    }
    if (project->isDescendantOf(secondId, folderId) != true
        || project->isDescendantOf(folderId, secondId))
    {
        qCritical() << "folder-test: descendant check";
        ++failures;
    }
    if (project->setEntityParent(folderId, secondId))
    {
        qCritical() << "folder-test: cycle was allowed";
        ++failures;
    }

    // Layer order is pre-order: first, folder, then its child.
    QVector<int> order = project->entityOrder();
    if (order.size() != 3
        || project->entities()[order[0]].id != firstId
        || project->entities()[order[1]].id != folderId
        || project->entities()[order[2]].id != secondId)
    {
        qCritical() << "folder-test: entity order";
        ++failures;
    }

    // Children follow the folder's coordinate system.
    project->setValue(folderId, QStringLiteral("x"), 0, 100.0, false);
    project->setValue(folderId, QStringLiteral("y"), 0, 50.0, false);
    project->setValue(secondId, QStringLiteral("x"), 0, 10.0, false);
    QPointF childWorld = pvf::SceneRenderer::worldTransform(
                             *project, *project->entity(secondId), 0)
                             .map(QPointF(0, 0));
    if (std::abs(childWorld.x() - 110.0) > 1e-6
        || std::abs(childWorld.y() - 50.0) > 1e-6)
    {
        qCritical() << "folder-test: parent translation" << childWorld;
        ++failures;
    }
    project->setValue(folderId, QStringLiteral("scaleX"), 0, 2.0, false);
    project->setValue(folderId, QStringLiteral("scaleY"), 0, 2.0, false);
    childWorld = pvf::SceneRenderer::worldTransform(
                     *project, *project->entity(secondId), 0)
                     .map(QPointF(0, 0));
    if (std::abs(childWorld.x() - 120.0) > 1e-6
        || std::abs(childWorld.y() - 50.0) > 1e-6)
    {
        qCritical() << "folder-test: parent scale" << childWorld;
        ++failures;
    }
    project->setValue(folderId, QStringLiteral("x"), 0, 0.0, false);
    project->setValue(folderId, QStringLiteral("y"), 0, 0.0, false);
    project->setValue(folderId, QStringLiteral("scaleX"), 0, 1.0, false);
    project->setValue(folderId, QStringLiteral("scaleY"), 0, 1.0, false);
    project->setValue(secondId, QStringLiteral("x"), 0, 0.0, false);

    project->setEntityFlag(folderId, pvf::EntityFlag::Locked, true);
    if (!project->isEffectivelyLocked(secondId)
        || !project->isEffectivelyLocked(folderId))
    {
        qCritical() << "folder-test: effective lock";
        ++failures;
    }
    project->setEntityFlag(folderId, pvf::EntityFlag::Locked, false);
    project->setEntityFlag(folderId, pvf::EntityFlag::Hidden, true);
    if (!project->isEffectivelyHidden(secondId))
    {
        qCritical() << "folder-test: effective hide";
        ++failures;
    }
    project->setEntityFlag(folderId, pvf::EntityFlag::Hidden, false);

    // Parent references survive save/load.
    const QString projectPath = QStringLiteral("/tmp/pvf_folder.pvproj");
    QString error;
    if (!project->save(projectPath, &error))
    {
        qCritical() << "folder-test: save" << error;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(projectPath, &error))
        {
            qCritical() << "folder-test: load" << error;
            ++failures;
        }
        else
        {
            const pvf::Entity* loadedFolder = loaded.entity(folderId);
            const pvf::Entity* loadedSecond = loaded.entity(secondId);
            if (!loadedFolder || !loadedFolder->isFolder() || !loadedSecond
                || loadedSecond->parent != folderId)
            {
                qCritical() << "folder-test: hierarchy not restored";
                ++failures;
            }
        }
    }

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window,
                       [&app, &window, project, firstId, folderId, secondId, failures]() mutable {
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!timeline || !viewport)
        {
            qCritical() << "folder-test: widgets missing";
            app.exit(1);
            return;
        }

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };

        if (timeline->rowCountForTest() != 3
            || timeline->rowEntityForTest(0) != firstId
            || timeline->rowEntityForTest(1) != folderId
            || timeline->rowEntityForTest(2) != secondId
            || timeline->rowDepthForTest(2) != 1)
        {
            qCritical() << "folder-test: tree rows" << timeline->rowCountForTest();
            ++failures;
        }

        // Folder arrow collapses and expands its children.
        const QPoint arrow = timeline->folderArrowRectForTest(1).center();
        sendMouse(QEvent::MouseButtonPress, arrow, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, arrow, Qt::NoButton);
        if (timeline->rowCountForTest() != 2
            || !timeline->rowCollapsedForTest(folderId))
        {
            qCritical() << "folder-test: collapse" << timeline->rowCountForTest();
            ++failures;
        }
        sendMouse(QEvent::MouseButtonPress, arrow, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, arrow, Qt::NoButton);
        if (timeline->rowCountForTest() != 3
            || timeline->rowCollapsedForTest(folderId))
        {
            qCritical() << "folder-test: expand" << timeline->rowCountForTest();
            ++failures;
        }

        // Drag the first row into the folder row.
        const QPoint from(10, timeline->rowButtonRect(0, 0).center().y());
        const QPoint to(10, timeline->rowButtonRect(1, 0).center().y());
        sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, to, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, to, Qt::NoButton);
        QApplication::processEvents();

        const pvf::Entity* first = project->entity(firstId);
        if (!first || first->parent != folderId
            || timeline->rowEntityForTest(0) != folderId
            || timeline->rowEntityForTest(1) != firstId
            || timeline->rowDepthForTest(1) != 1)
        {
            qCritical() << "folder-test: drag into folder"
                        << (first ? first->parent : -999)
                        << timeline->rowEntityForTest(0)
                        << timeline->rowEntityForTest(1);
            ++failures;
        }

        // Layer moves are undoable.
        auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redoAction = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!undoAction || !redoAction)
        {
            qCritical() << "folder-test: undo actions missing";
            ++failures;
        }
        else
        {
            undoAction->trigger();
            QApplication::processEvents();
            first = project->entity(firstId);
            if (!first || first->parent != pvf::kInvalidEntity
                || timeline->rowEntityForTest(0) != firstId
                || timeline->rowEntityForTest(1) != folderId)
            {
                qCritical() << "folder-test: move undo"
                            << (first ? first->parent : -999);
                ++failures;
            }
            redoAction->trigger();
            QApplication::processEvents();
            first = project->entity(firstId);
            if (!first || first->parent != folderId
                || timeline->rowEntityForTest(0) != folderId
                || timeline->rowEntityForTest(1) != firstId)
            {
                qCritical() << "folder-test: move redo"
                            << (first ? first->parent : -999);
                ++failures;
            }
        }

        // The parent combo switches between world and a specific entity.
        window.setSelectedEntityForTest(firstId);
        QApplication::processEvents();
        auto* parents = window.findChild<QComboBox*>(QStringLiteral("parentCombo"));
        if (!parents)
        {
            qCritical() << "folder-test: parent combo missing";
            ++failures;
        }
        else
        {
            const int worldIndex = parents->findData(int(pvf::kInvalidEntity));
            const int folderIndex = parents->findData(int(folderId));
            if (worldIndex < 0 || folderIndex < 0)
            {
                qCritical() << "folder-test: parent choices missing"
                            << parents->count();
                ++failures;
            }
            else
            {
                parents->setCurrentIndex(worldIndex);
                QApplication::processEvents();
                if (first->parent != pvf::kInvalidEntity)
                {
                    qCritical() << "folder-test: parent to world" << first->parent;
                    ++failures;
                }
                parents->setCurrentIndex(folderIndex);
                QApplication::processEvents();
                if (first->parent != folderId)
                {
                    qCritical() << "folder-test: parent to folder" << first->parent;
                    ++failures;
                }
            }
        }

        // Moving a child inside a translated folder keeps the movement in
        // parent space.
        project->setValue(folderId, QStringLiteral("x"), 0, 100.0, false);
        project->setValue(folderId, QStringLiteral("y"), 0, 50.0, false);
        project->setValue(secondId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(secondId, QStringLiteral("y"), 0, 0.0, false);
        // The top layer wins the hit test; park the red rectangle away so
        // the click reaches the child that is being dragged.
        project->setValue(firstId, QStringLiteral("x"), 0, -500.0, false);
        window.setSelectedEntityForTest(secondId);
        QApplication::processEvents();
        const double viewScale = viewport->currentScale();
        const QPoint bodyPoint = viewport->entityPivotForTest(secondId).toPoint();
        auto sendViewport = [viewport](QEvent::Type type, const QPoint& pos,
                                       Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(viewport->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        sendViewport(QEvent::MouseButtonPress, bodyPoint, Qt::LeftButton);
        sendViewport(QEvent::MouseMove, bodyPoint + QPoint(50, 0), Qt::LeftButton);
        sendViewport(QEvent::MouseButtonRelease, bodyPoint + QPoint(50, 0),
                     Qt::NoButton);
        const double childX = project->value(secondId, QStringLiteral("x"), 0);
        if (std::abs(childX - 50.0 / viewScale) > 0.5)
        {
            qCritical() << "folder-test: parent-space move" << childX
                        << 50.0 / viewScale;
            ++failures;
        }
        project->setValue(folderId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(folderId, QStringLiteral("y"), 0, 0.0, false);
        project->setValue(secondId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(secondId, QStringLiteral("y"), 0, 0.0, false);

        // Folders get a gizmo too: dragging the X axis moves the group.
        window.setSelectedEntityForTest(folderId);
        QApplication::processEvents();
        const QPointF folderPivot = viewport->entityPivotForTest(folderId);
        const double folderXBefore =
            project->value(folderId, QStringLiteral("x"), 0);
        sendViewport(QEvent::MouseButtonPress,
                     folderPivot.toPoint() + QPoint(40, 0), Qt::LeftButton);
        sendViewport(QEvent::MouseMove, folderPivot.toPoint() + QPoint(70, 0),
                     Qt::LeftButton);
        sendViewport(QEvent::MouseButtonRelease,
                     folderPivot.toPoint() + QPoint(70, 0), Qt::NoButton);
        const double folderXAfter =
            project->value(folderId, QStringLiteral("x"), 0);
        if (std::abs(folderXAfter - folderXBefore) < 1.0)
        {
            qCritical() << "folder-test: folder gizmo did not move it"
                        << folderXBefore << folderXAfter;
            ++failures;
        }
        project->setValue(folderId, QStringLiteral("x"), 0, 0.0, false);
        project->setValue(folderId, QStringLiteral("y"), 0, 0.0, false);

        // Locking the folder locks and deselects its children.
        window.setSelectedEntityForTest(secondId);
        project->setEntityFlag(folderId, pvf::EntityFlag::Locked, true);
        QApplication::processEvents();
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "folder-test: locked folder child stayed selected";
            ++failures;
        }
        window.setSelectedEntityForTest(secondId);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "folder-test: locked folder child selectable";
            ++failures;
        }
        project->setEntityFlag(folderId, pvf::EntityFlag::Locked, false);
        window.setSelectedEntityForTest(secondId);
        if (window.selectedEntityForTest() != secondId)
        {
            qCritical() << "folder-test: unlocked folder child not selectable";
            ++failures;
        }

        window.setSelectedEntityForTest(folderId);
        QApplication::processEvents();
        window.grab().toImage().save(QStringLiteral("/tmp/pvf_folder_tree.png"));
        qInfo() << "folder-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int audioTest(QApplication& app)
{
    int failures = 0;
    const QString wavPath = QStringLiteral("/tmp/pvf_audio.wav");
    {
        QFile file(wavPath);
        if (!file.open(QIODevice::WriteOnly))
        {
            qCritical() << "audio-test: cannot write wav";
            return 1;
        }
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        QVector<qint16> samples(48000, 0);
        for (int i = 0; i < 100; ++i)
        {
            const qint16 click = qint16(30000 * (1.0 - i / 100.0));
            samples[12000 + i] = click;   // 250 ms
            samples[36000 + i] = click;   // 750 ms
        }
        const quint32 dataSize = quint32(samples.size() * 2);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + dataSize);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(48000)
            << quint32(96000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << dataSize;
        for (qint16 sample : samples)
            out << sample;
    }

    const pvf::AudioDecodeResult decoded = pvf::decodeAudioFile(wavPath);
    if (!decoded.clip)
    {
        qCritical() << "audio-test: decode failed" << decoded.error;
        return 1;
    }
    const auto& clip = decoded.clip;
    if (std::abs(clip->duration - 1.0) > 0.02 || clip->peaks.size() < 900)
    {
        qCritical() << "audio-test: wrong duration/peaks" << clip->duration
                    << clip->peaks.size();
        ++failures;
    }
    const auto peakAt = [&clip](int ms) {
        return ms >= 0 && ms < clip->peaks.size() ? clip->peaks[ms] : 0.0f;
    };
    if (peakAt(250) < 0.5f || peakAt(750) < 0.5f || peakAt(500) > 0.05f)
    {
        qCritical() << "audio-test: peak positions" << peakAt(250) << peakAt(750)
                    << peakAt(500);
        ++failures;
    }

    const pvf::MediaImportResult imported = pvf::processMediaFile(wavPath, QString());
    if (!imported.error.isEmpty() || imported.type != pvf::EntityType::Audio
        || !imported.audio || imported.thumbnail.isNull())
    {
        qCritical() << "audio-test: media import" << imported.error
                    << int(imported.type);
        ++failures;
    }

    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* audio = project->addAudio(wavPath, clip, QStringLiteral("Audio"));
    const pvf::EntityId audioId = audio->id;
    audio->data[QStringLiteral("startFrame")] = 10.0;
    audio->data[QStringLiteral("trimStart")] = 0.25;
    audio->data[QStringLiteral("trimEnd")] = 0.25;
    project->notifyChanged();
    if (project->timelineLength() != 25)
    {
        qCritical() << "audio-test: timeline length" << project->timelineLength();
        ++failures;
    }

    {
        pvf::Project mixProject;
        mixProject.fps = 30;
        mixProject.addAudio(wavPath, clip, QStringLiteral("Mix"));
        const QByteArray pcm = pvf::AudioEngine::mixRange(mixProject, 0.0, 1.0);
        if (pcm.size() != 48000 * 2 * 2)
        {
            qCritical() << "audio-test: mix size" << pcm.size();
            ++failures;
        }
        else
        {
            const auto* samples = reinterpret_cast<const qint16*>(pcm.constData());
            const int click = std::abs(int(samples[12000 * 2]));
            const int silence = std::abs(int(samples[24000 * 2]));
            if (click < 20000 || silence > 50)
            {
                qCritical() << "audio-test: mix timeline" << click << silence;
                ++failures;
            }
        }
    }

    const QString projectPath = QStringLiteral("/tmp/pvf_audio.pvproj");
    QString error;
    if (!project->save(projectPath, &error))
    {
        qCritical() << "audio-test: save" << error;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(projectPath, &error))
        {
            qCritical() << "audio-test: load" << error;
            ++failures;
        }
        else
        {
            const pvf::Entity* loadedAudio = loaded.entity(audioId);
            if (!loadedAudio || loadedAudio->type != pvf::EntityType::Audio
                || !loadedAudio->audio || loadedAudio->audio->peaks.isEmpty())
            {
                qCritical() << "audio-test: audio not restored";
                ++failures;
            }
        }
    }

    {
        pvf::Project libraryProject;
        pvf::Template* tmpl = libraryProject.addMediaTemplate(
            wavPath, pvf::EntityType::Audio, imported.thumbnail, clip->duration,
            QStringLiteral("Library Audio"), clip);
        const pvf::Entity* created =
            tmpl ? libraryProject.createFromTemplate(*tmpl) : nullptr;
        if (!created || created->type != pvf::EntityType::Audio || !created->audio)
        {
            qCritical() << "audio-test: library audio creation failed";
            ++failures;
        }
    }

    // Timeline waveform smoke test.
    auto* uiProject = new pvf::Project;
    pvf::MainWindow window(uiProject);
    window.resize(1200, 800);
    auto* uiAudio = uiProject->addAudio(wavPath, clip, QStringLiteral("Audio"));
    window.setSelectedEntityForTest(uiAudio->id);
    window.show();
    QApplication::processEvents();
    if (auto* timeline = window.findChild<pvf::TimelinePanel*>())
    {
        timeline->grab().toImage().save(QStringLiteral("/tmp/pvf_audio_timeline.png"));
    }
    else
    {
        qCritical() << "audio-test: timeline missing";
        ++failures;
    }

    // Narrow properties panel with a long unbroken asset path.
    uiAudio->name = QStringLiteral("永遠にゲームで対戦したいキリタン");
    uiAudio->sourcePath = QStringLiteral(
        "/home/xmb505/视频/PVForge/assets/"
        "71a0fbde5227bfd7658893e46cabc6ee0660945f71a0fbde5227bfd7658893e46cabc6ee.wav");
    uiProject->notifyChanged();
    window.setSelectedEntityForTest(uiAudio->id);
    window.resize(1100, 800);
    QApplication::processEvents();
    window.grab().save(QStringLiteral("/tmp/pvf_audio_properties.png"));

    auto* startButton = window.findChild<QPushButton*>(
        QStringLiteral("audioStartFromPlayhead"));
    auto* trimStartButton = window.findChild<QPushButton*>(
        QStringLiteral("audioTrimStartFromPlayhead"));
    auto* trimEndButton = window.findChild<QPushButton*>(
        QStringLiteral("audioTrimEndFromPlayhead"));
    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!startButton || !trimStartButton || !trimEndButton || !timeline)
    {
        qCritical() << "audio-test: playhead capture controls missing";
        ++failures;
    }
    else
    {
        // Drag the audio clip on the timeline: 60 px at 6 px/frame = 10 frames.
        uiAudio->data[QStringLiteral("startFrame")] = 10.0;
        uiAudio->data[QStringLiteral("trimStart")] = 0.0;
        uiAudio->data[QStringLiteral("trimEnd")] = 0.0;
        uiProject->notifyChanged();
        window.goToFrame(0);
        timeline->setPixelsPerFrameForTest(6.0);
        QApplication::processEvents();

        auto sendMouse = [timeline](QEvent::Type type, const QPoint& pos,
                                    Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos),
                              QPointF(timeline->mapToGlobal(pos)), Qt::LeftButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(timeline, &event);
        };
        const int clipY = timeline->rowCenterYForTest(0);
        const QPoint dragFrom(300, clipY);   // middle of the 10..40 frame clip
        const QPoint dragTo(360, clipY);     // +10 frames

        sendMouse(QEvent::MouseMove, dragFrom, Qt::NoButton);
        if (timeline->cursor().shape() != Qt::OpenHandCursor)
        {
            qCritical() << "audio-test: no open-hand cursor over the clip";
            ++failures;
        }
        sendMouse(QEvent::MouseButtonPress, dragFrom, Qt::LeftButton);
        if (timeline->cursor().shape() != Qt::ClosedHandCursor)
        {
            qCritical() << "audio-test: no closed-hand cursor while dragging";
            ++failures;
        }
        sendMouse(QEvent::MouseMove, dragTo, Qt::LeftButton);
        timeline->grab().toImage().save(QStringLiteral("/tmp/pvf_audio_drag.png"));
        sendMouse(QEvent::MouseButtonRelease, dragTo, Qt::NoButton);
        if (timeline->cursor().shape() != Qt::OpenHandCursor)
        {
            qCritical() << "audio-test: cursor not restored after drag";
            ++failures;
        }

        double startFrame =
            uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        if (std::abs(startFrame - 20.0) > 1e-6 || window.currentFrame() != 0)
        {
            qCritical() << "audio-test: timeline drag" << startFrame
                        << window.currentFrame();
            ++failures;
        }
        auto* undoAction =
            window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!undoAction || !undoAction->isEnabled())
        {
            qCritical() << "audio-test: audio drag is not undoable";
            ++failures;
        }
        else
        {
            undoAction->trigger();
            startFrame =
                uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
            if (std::abs(startFrame - 10.0) > 1e-6)
            {
                qCritical() << "audio-test: audio drag undo" << startFrame;
                ++failures;
            }
        }

        // A click without dragging still positions the playhead.
        window.goToFrame(0);
        sendMouse(QEvent::MouseButtonPress, dragFrom, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, dragFrom, Qt::NoButton);
        if (window.currentFrame() != 20)
        {
            qCritical() << "audio-test: audio click did not move the playhead"
                        << window.currentFrame();
            ++failures;
        }

        // Capturing the playhead inside the clip moves the start there; it
        // must not fall back to frame 0.
        window.goToFrame(30);
        startButton->click();
        if (window.currentFrame() != 30)
        {
            qCritical() << "audio-test: capture moved the playhead"
                        << window.currentFrame();
            ++failures;
        }
        window.goToFrame(40);
        trimStartButton->click();
        startFrame =
            uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        double trimStart =
            uiAudio->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        if (std::abs(startFrame - 40.0) > 1e-6
            || std::abs(trimStart - 1.0 / 3.0) > 1e-3
            || std::abs(startFrame + (1.0 - trimStart) * 30.0 - 60.0) > 1e-3)
        {
            qCritical() << "audio-test: trim start moved the remaining audio"
                        << startFrame << trimStart;
            ++failures;
        }
        timeline->grab().toImage().save(QStringLiteral("/tmp/pvf_audio_trim.png"));
        window.goToFrame(50);
        trimEndButton->click();
        startFrame =
            uiAudio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        trimStart =
            uiAudio->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double trimEnd =
            uiAudio->data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
        if (std::abs(startFrame - 40.0) > 1e-6
            || std::abs(trimStart - 1.0 / 3.0) > 1e-3
            || std::abs(trimEnd - 1.0 / 3.0) > 1e-3
            || std::abs(startFrame
                        + (1.0 - trimStart - trimEnd) * 30.0 - 50.0)
                   > 1e-3)
        {
            qCritical() << "audio-test: trim end" << startFrame << trimStart
                        << trimEnd;
            ++failures;
        }
        auto* startSpin =
            window.findChild<QSpinBox*>(QStringLiteral("audioStartFrame"));
        if (!startSpin || startSpin->value() != 40)
        {
            qCritical() << "audio-test: start frame field"
                        << (startSpin ? startSpin->value() : -1);
            ++failures;
        }
    }

    qInfo() << "audio-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int cameraTest(QApplication& app)
{
    pvf::Settings::setMonitorVisible(true);
    pvf::Settings::setShowCameraFrame(true);
    auto* project = new pvf::Project;
    project->width = 800;
    project->height = 600;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(500, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        auto* monitor = window.findChild<pvf::CameraMonitor*>();
        if (!viewport || !monitor)
        {
            qCritical() << "camera-test: viewport or monitor missing";
            app.exit(1);
            return;
        }

        auto* red = project->addVector(0, 200, 200, Qt::red, Qt::white, 0);
        auto* green = project->addVector(1, 100, 100, Qt::green, Qt::white, 0);
        const pvf::EntityId redId = red->id;
        const pvf::EntityId greenId = green->id;
        project->setValue(greenId, QStringLiteral("x"), 0, 300.0, false);
        pvf::Entity* camera = project->addCamera(QStringLiteral("Camera 1"));
        const pvf::EntityId cameraId = camera->id;
        window.setSelectedEntityForTest(cameraId);

        if (project->activeCamera != cameraId || project->monitorView != cameraId)
        {
            qCritical() << "camera-test: camera not activated on selection";
            ++failures;
        }
        if (!monitor->isVisible())
        {
            qCritical() << "camera-test: monitor not visible";
            ++failures;
        }
        auto* fpsLabel = monitor->findChild<QLabel*>(QStringLiteral("monitorFpsLabel"));
        if (!fpsLabel || !fpsLabel->text().contains(QStringLiteral("FPS")))
        {
            qCritical() << "camera-test: monitor fps label missing"
                        << (fpsLabel ? fpsLabel->text() : QStringLiteral("null"));
            ++failures;
        }
        if (viewport->cameraFrameForTest().isEmpty())
        {
            qCritical() << "camera-test: viewfinder frame not drawn";
            ++failures;
        }

        // The View menu can hide the viewfinder without changing the camera.
        if (auto* frameAction =
                window.findChild<QAction*>(QStringLiteral("cameraFrameAction")))
        {
            frameAction->trigger();
            if (!viewport->cameraFrameForTest().isEmpty())
            {
                qCritical() << "camera-test: viewfinder did not hide";
                ++failures;
            }
            frameAction->trigger();
            if (viewport->cameraFrameForTest().isEmpty())
            {
                qCritical() << "camera-test: viewfinder did not return";
                ++failures;
            }
        }

        // The camera at 100% shows the whole canvas; the monitor centre must
        // show the red rectangle, not a guide or a dimmed pixel.
        QApplication::processEvents();
        QImage shot = monitor->grab().toImage();
        const int title = 28;
        const QColor center = shot.pixelColor(shot.width() / 2, title + 90);
        if (center.red() < 200)
        {
            qCritical() << "camera-test: monitor centre is not the scene" << center;
            ++failures;
        }
        window.grab().save(QStringLiteral("/tmp/pvforge_camera.png"));

        // Outside the canvas is black: rotate the full-canvas camera 45°.
        window.applyCameraForTest(cameraId, 0, 0, 1.0, 45.0);
        QApplication::processEvents();
        window.grab().save(QStringLiteral("/tmp/pvforge_camera_rotated.png"));
        shot = monitor->grab().toImage();
        const QColor corner = shot.pixelColor(44, title + 4);
        if (corner.red() > 24 || corner.green() > 24 || corner.blue() > 24)
        {
            qCritical() << "camera-test: outside canvas is not black" << corner;
            ++failures;
        }

        // Back to a 50% frame: the green entity outside is dimmed, the red
        // inside stays bright.
        window.applyCameraForTest(cameraId, 0, 0, 0.5, 0.0);
        window.setSelectedEntityForTest(cameraId);
        QApplication::processEvents();
        // The floating monitor can cover the entity being sampled; hide it
        // while grabbing the viewport so the dim check only sees the scene.
        const bool monitorShown = monitor->isVisible();
        monitor->setVisible(false);
        QApplication::processEvents();
        const QImage viewportShot = viewport->grab().toImage();
        monitor->setVisible(monitorShown);
        QApplication::processEvents();
        viewportShot.save(QStringLiteral("/tmp/pvforge_camera_dim.png"));
        const QPoint greenPoint = viewport->entityPivotForTest(greenId).toPoint();
        const QColor dimmed = viewportShot.pixelColor(greenPoint);
        if (dimmed.green() < 40 || dimmed.green() >= 200)
        {
            qCritical() << "camera-test: outside camera not dimmed" << dimmed;
            ++failures;
        }
        const QPoint redPoint = viewport->entityPivotForTest(redId).toPoint();
        const QColor clear = viewportShot.pixelColor(redPoint);
        if (clear.red() < 200)
        {
            qCritical() << "camera-test: inside camera dimmed" << clear;
            ++failures;
        }

        // Letterbox bars outside the project aspect stay pure black.
        shot = monitor->grab().toImage();
        const QColor bar = shot.pixelColor(10, title + 90);
        if (bar.red() > 24 || bar.green() > 24 || bar.blue() > 24)
        {
            qCritical() << "camera-test: letterbox not black" << bar;
            ++failures;
        }

        auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                             Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)),
                              button, buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };

        // Clicking inside the frame passes through to the entity.
        window.setSelectedEntityForTest(cameraId);
        sendMouse(QEvent::MouseButtonPress, redPoint, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, redPoint, Qt::LeftButton, Qt::NoButton);
        if (window.selectedEntityForTest() != redId)
        {
            qCritical() << "camera-test: inside click did not pass through"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Drag the top border to move the camera, then undo once.
        QPolygonF polygon = viewport->cameraFrameForTest();
        QPoint topMiddle = ((polygon[0] + polygon[1]) / 2.0).toPoint();
        window.setSelectedEntityForTest(cameraId);
        sendMouse(QEvent::MouseButtonPress, topMiddle, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, topMiddle + QPoint(40, 30), Qt::NoButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, topMiddle + QPoint(40, 30), Qt::LeftButton,
                  Qt::NoButton);
        QApplication::processEvents();
        const double movedX = project->value(cameraId, QStringLiteral("x"), 0);
        const double movedY = project->value(cameraId, QStringLiteral("y"), 0);
        if (std::abs(movedX) < 1.0 && std::abs(movedY) < 1.0)
        {
            qCritical() << "camera-test: border drag did not move camera" << movedX
                        << movedY;
            ++failures;
        }
        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(cameraId, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "camera-test: camera move was not one undo step"
                        << project->value(cameraId, QStringLiteral("x"), 0);
            ++failures;
        }

        // Corner drag zooms the camera, keeping the centre fixed.
        window.setSelectedEntityForTest(cameraId);
        polygon = viewport->cameraFrameForTest();
        const QPoint cornerPoint = polygon[1].toPoint();
        sendMouse(QEvent::MouseButtonPress, cornerPoint, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, cornerPoint + QPoint(30, 20), Qt::NoButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, cornerPoint + QPoint(30, 20), Qt::LeftButton,
                  Qt::NoButton);
        const double zoomed = project->value(cameraId, QStringLiteral("scaleX"), 0);
        if (zoomed <= 0.5 || zoomed > pvf::Camera::kMaxScale)
        {
            qCritical() << "camera-test: corner drag did not zoom" << zoomed;
            ++failures;
        }

        // Rotation handle turns the camera; the angle snaps to 90°.
        window.applyCameraForTest(cameraId, 0, 0, 0.5, 0.0);
        window.setSelectedEntityForTest(cameraId);
        polygon = viewport->cameraFrameForTest();
        const QPointF topCenter = (polygon[0] + polygon[1]) / 2.0;
        const QPointF rotateHandle = topCenter + QPointF(0, -28);
        const QPointF centerScreen = viewport->entityPivotForTest(cameraId);
        sendMouse(QEvent::MouseButtonPress, rotateHandle.toPoint(), Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseMove, (centerScreen + QPointF(100, 0)).toPoint(),
                  Qt::NoButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, (centerScreen + QPointF(100, 0)).toPoint(),
                  Qt::LeftButton, Qt::NoButton);
        const double rotation = project->value(cameraId, QStringLiteral("rotation"), 0);
        if (std::abs(std::abs(rotation) - 90.0) > 1.0)
        {
            qCritical() << "camera-test: rotation handle failed" << rotation;
            ++failures;
        }
        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();

        // A 100% frame fills the canvas, so it cannot pan.
        window.applyCameraForTest(cameraId, 0, 0, 1.0, 0.0);
        window.setSelectedEntityForTest(cameraId);
        polygon = viewport->cameraFrameForTest();
        topMiddle = ((polygon[0] + polygon[1]) / 2.0).toPoint();
        sendMouse(QEvent::MouseButtonPress, topMiddle, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, topMiddle + QPoint(40, 30), Qt::NoButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, topMiddle + QPoint(40, 30), Qt::LeftButton,
                  Qt::NoButton);
        if (std::abs(project->value(cameraId, QStringLiteral("x"), 0)) > 1e-6
            || std::abs(project->value(cameraId, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "camera-test: 100% camera panned"
                        << project->value(cameraId, QStringLiteral("x"), 0)
                        << project->value(cameraId, QStringLiteral("y"), 0);
            ++failures;
        }
        // Detach and re-dock through the monitor buttons.
        auto* detachButton =
            monitor->findChild<QToolButton*>(QStringLiteral("monitorDetachButton"));
        auto* qualityButton =
            monitor->findChild<QToolButton*>(QStringLiteral("monitorQualityButton"));
        if (!detachButton || !qualityButton)
        {
            qCritical() << "camera-test: monitor buttons missing";
            ++failures;
        }
        else
        {
            // Dragging the title out of the main window pops the monitor out.
            const QPoint grab(10, 10);
            const QPoint grabGlobal = monitor->mapToGlobal(grab);
            const QPoint outside = window.frameGeometry().topLeft() - QPoint(180, 180);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(grab),
                              QPointF(grabGlobal), Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, QPointF(grab + QPoint(30, 30)),
                             QPointF(outside), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,
                                QPointF(grab + QPoint(30, 30)), QPointF(outside),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(monitor, &press);
            QApplication::sendEvent(monitor, &move);
            QApplication::sendEvent(monitor, &release);
            if (!monitor->isDetached())
            {
                qCritical() << "camera-test: drag out did not detach";
                ++failures;
            }

            detachButton->click();
            if (monitor->isDetached())
            {
                qCritical() << "camera-test: redock failed";
                ++failures;
            }
            detachButton->click();
            if (!monitor->isDetached())
            {
                qCritical() << "camera-test: detach failed";
                ++failures;
            }
            detachButton->click();
            if (monitor->isDetached())
            {
                qCritical() << "camera-test: redock failed";
                ++failures;
            }
            const bool wasHigh = monitor->isHighQuality();
            qualityButton->click();
            if (monitor->isHighQuality() == wasHigh)
            {
                qCritical() << "camera-test: quality toggle failed";
                ++failures;
            }
            qualityButton->click();
        }

        // Save and load keeps the active camera and monitor view.
        const QString path = QStringLiteral("/tmp/pvforge_camera.pvproj");
        QString error;
        if (!project->save(path, &error))
        {
            qCritical() << "camera-test: save failed" << error;
            ++failures;
        }
        else
        {
            pvf::Project loaded;
            if (!loaded.load(path, &error))
            {
                qCritical() << "camera-test: load failed" << error;
                ++failures;
            }
            else if (loaded.activeCamera != cameraId || loaded.monitorView != cameraId)
            {
                qCritical() << "camera-test: camera state not saved"
                            << loaded.activeCamera << loaded.monitorView;
                ++failures;
            }
        }

        // Quality toggle keeps rendering the scene.
        if (qualityButton)
        {
            const bool wasHigh = monitor->isHighQuality();
            qualityButton->click();
            QApplication::processEvents();
            QImage highShot = monitor->grab().toImage();
            const QColor highCenter =
                highShot.pixelColor(highShot.width() / 2, title + 90);
            if (monitor->isHighQuality() == wasHigh || highCenter.red() < 150)
            {
                qCritical() << "camera-test: high quality render failed" << highCenter;
                ++failures;
            }
            qualityButton->click();
        }

        // Close and reopen the monitor from the View menu.
        if (auto* monitorAction = window.findChild<QAction*>(QStringLiteral("monitorAction")))
        {
            monitorAction->trigger();
            if (monitor->isVisible())
            {
                qCritical() << "camera-test: monitor close failed";
                ++failures;
            }
            monitorAction->trigger();
            if (!monitor->isVisible())
            {
                qCritical() << "camera-test: monitor reopen failed";
                ++failures;
            }
        }

        // Workbench camera page creates and activates a camera.
        auto* workbenchButton =
            window.findChild<QToolButton*>(QStringLiteral("workbenchButton"));
        auto* workbench = window.findChild<pvf::WorkbenchPanel*>();
        if (workbenchButton && workbench)
        {
            workbenchButton->click();
            auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
            auto* nameEdit =
                workbench->findChild<QLineEdit*>(QStringLiteral("cameraName"));
            auto* create =
                workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
            if (!types || !nameEdit || !create)
            {
                qCritical() << "camera-test: workbench camera widgets missing";
                ++failures;
            }
            else
            {
                types->setCurrentRow(5);   // the camera page
                nameEdit->setText(QStringLiteral("UI Camera"));
                create->click();
                QApplication::processEvents();
                pvf::EntityId uiCamera = pvf::kInvalidEntity;
                for (const pvf::Entity& entity : project->entities())
                    if (entity.isCamera() && entity.name == QStringLiteral("UI Camera"))
                        uiCamera = entity.id;
                if (uiCamera == pvf::kInvalidEntity || project->activeCamera != uiCamera
                    || project->monitorView != uiCamera)
                {
                    qCritical() << "camera-test: workbench camera not active" << uiCamera
                                << project->activeCamera << project->monitorView;
                    ++failures;
                }

                // The monitor dropdown switches the active camera and the
                // work/whole-canvas views.
                auto* combo =
                    monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"));
                if (!combo)
                {
                    qCritical() << "camera-test: camera combo missing";
                    ++failures;
                }
                else
                {
                    combo->setCurrentIndex(combo->findData(cameraId));
                    if (project->activeCamera != cameraId
                        || project->monitorView != cameraId)
                    {
                        qCritical() << "camera-test: combo did not activate camera";
                        ++failures;
                    }
                    combo->setCurrentIndex(combo->findData(pvf::kWorkCameraView));
                    if (project->monitorView != pvf::kWorkCameraView)
                    {
                        qCritical() << "camera-test: combo work view failed";
                        ++failures;
                    }
                    combo->setCurrentIndex(combo->findData(pvf::kWholeCanvasView));
                    if (project->monitorView != pvf::kWholeCanvasView)
                    {
                        qCritical() << "camera-test: combo whole view failed";
                        ++failures;
                    }
                    combo->setCurrentIndex(combo->findData(uiCamera));
                }
            }
        }

        // With no cameras left the monitor falls back to the work camera and
        // the viewfinder disappears.
        QVector<pvf::EntityId> cameras;
        for (const pvf::Entity& entity : project->entities())
            if (entity.isCamera())
                cameras.append(entity.id);
        for (pvf::EntityId id : cameras)
            project->removeEntity(id);
        QApplication::processEvents();
        auto* fallbackCombo =
            monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"));
        if (!fallbackCombo
            || fallbackCombo->currentData().toInt() != pvf::kWorkCameraView
            || !viewport->cameraFrameForTest().isEmpty())
        {
            qCritical() << "camera-test: work camera fallback failed";
            ++failures;
        }
        window.grab().save(QStringLiteral("/tmp/pvforge_camera_work.png"));

        qInfo() << "camera-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int monitorFeatureTest(QApplication& app)
{
    const QString tracePath = QStringLiteral("/tmp/pvf_trace.mp4");
    QFile::remove(tracePath);
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=red:s=320x240:d=1:r=10"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=blue:s=320x240:d=1:r=10"),
                  QStringLiteral("-filter_complex"),
                  QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0"),
                  QStringLiteral("-c:v"), QStringLiteral("libx264"),
                  QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                  QStringLiteral("-preset"), QStringLiteral("ultrafast"), tracePath});
    if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0)
    {
        qCritical() << "monitor-feature-test: cannot generate trace video";
        return 1;
    }

    pvf::Settings::setMonitorVisible(true);
    pvf::Settings::setShowCameraFrame(true);
    auto* project = new pvf::Project;
    project->width = 1920;
    project->height = 1080;
    project->fps = 10;
    pvf::Entity* camera = project->addCamera(QStringLiteral("Monitor Camera"));
    const pvf::EntityId cameraId = camera->id;
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 1.0, false);

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();

    auto* monitor = window.findChild<pvf::CameraMonitor*>();
    if (!monitor)
    {
        qCritical() << "monitor-feature-test: monitor missing";
        return 1;
    }
    auto* traceButton =
        monitor->findChild<QToolButton*>(QStringLiteral("monitorTraceButton"));
    auto* streamButton =
        monitor->findChild<QToolButton*>(QStringLiteral("monitorStreamButton"));
    int failures = 0;
    if (!traceButton || !streamButton)
    {
        qCritical() << "monitor-feature-test: monitor buttons missing";
        ++failures;
    }

    // Work camera mirrors the viewport but must clip outside-canvas objects.
    {
        pvf::Project clipProject;
        clipProject.width = 800;
        clipProject.height = 600;
        clipProject.background = QColor(20, 40, 60);
        pvf::Entity* outside =
            clipProject.addVector(0, 200, 200, Qt::red, Qt::red, 0);
        clipProject.setValue(outside->id, QStringLiteral("x"), 0, 500.0, false);

        QImage preview(400, 300, QImage::Format_ARGB32_Premultiplied);
        preview.fill(Qt::black);
        {
            QPainter painter(&preview);
            QTransform view;
            view.translate(200, 150);
            view.scale(0.25, 0.25);
            pvf::CameraRenderer::renderWorkCamera(
                painter, clipProject, 0, QRectF(0, 0, 400, 300), view,
                QSize(400, 300), QColor(90, 90, 90), false);
        }
        if (preview.pixelColor(325, 150) != QColor(90, 90, 90)
            || preview.pixelColor(200, 150) != QColor(20, 40, 60))
        {
            qCritical() << "monitor-feature-test: work camera leaked canvas"
                        << preview.pixelColor(325, 150)
                        << preview.pixelColor(200, 150);
            ++failures;
        }

        // A brand wall used to replace the clip with its own rect, so it
        // painted over the monitor title bar and the letterbox area.
        pvf::Project wallProject;
        wallProject.width = 400;
        wallProject.height = 300;
        wallProject.background = QColor(20, 20, 20);
        pvf::BrandWallParams wallParams;
        wallParams.width = 900;
        wallParams.height = 600;
        wallParams.columns = 6;
        wallParams.rows = 4;
        QVariantMap wallData;
        wallParams.writeTo(wallData);
        pvf::Entity* wall = wallProject.addEntity(
            pvf::EntityType::BrandWall, QStringLiteral("wall"), wallData);
        wall->values.insert(QStringLiteral("x"), 0.0);
        wall->values.insert(QStringLiteral("y"), -120.0);

        QImage wallPreview(400, 300, QImage::Format_ARGB32_Premultiplied);
        wallPreview.fill(Qt::black);
        const QRect target(0, 40, 400, 260);
        {
            QPainter painter(&wallPreview);
            QTransform view;
            view.translate(200, 150);
            pvf::CameraRenderer::renderWorkCamera(
                painter, wallProject, 0, QRectF(target), view, QSize(400, 300),
                QColor(0, 0, 0), false);
        }
        int leaked = 0;
        int visible = 0;
        for (int y = 0; y < wallPreview.height(); ++y)
            for (int x = 0; x < wallPreview.width(); ++x)
            {
                const QRgb pixel = wallPreview.pixel(x, y);
                if (!target.contains(x, y))
                {
                    if (pixel != qRgb(0, 0, 0))
                        ++leaked;
                }
                else if (pixel != qRgb(0, 0, 0))
                {
                    ++visible;
                }
            }
        if (leaked != 0 || visible == 0)
        {
            qCritical() << "monitor-feature-test: brand wall clip"
                        << "leaked" << leaked << "visible" << visible;
            ++failures;
        }
    }

    if (!monitor->loadTraceVideo(tracePath))
    {
        qCritical() << "monitor-feature-test: trace import failed";
        ++failures;
    }
    else
    {
        monitor->setTraceOpacity(1.0);
        window.goToFrame(15);
        QApplication::processEvents();
        QImage shot = monitor->grab().toImage();
        QColor color = shot.pixelColor(shot.width() / 2, 28 + (shot.height() - 28) / 2);
        if (color.blue() < 180 || color.red() > 90)
        {
            qCritical() << "monitor-feature-test: trace blue frame" << color;
            ++failures;
        }
        window.goToFrame(2);
        QApplication::processEvents();
        shot = monitor->grab().toImage();
        color = shot.pixelColor(shot.width() / 2, 28 + (shot.height() - 28) / 2);
        if (color.red() < 180 || color.blue() > 90)
        {
            qCritical() << "monitor-feature-test: trace red frame" << color;
            ++failures;
        }
        window.goToFrame(15);
        QApplication::processEvents();
        window.grab().toImage().save(QStringLiteral("/tmp/pvf_monitor_features.png"));

        monitor->clearTraceForTest();
        window.goToFrame(15);
        QApplication::processEvents();
        const QImage cleared = monitor->grab().toImage();
        const QColor clearedColor =
            cleared.pixelColor(cleared.width() / 2,
                               28 + (cleared.height() - 28) / 2);
        if (clearedColor.blue() > 90 || clearedColor.red() > 90)
        {
            qCritical() << "monitor-feature-test: trace clear failed"
                        << clearedColor;
            ++failures;
        }
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
    {
        const bool started = monitor->startStreamingForTest();
        if (!started || !monitor->isStreaming())
        {
            qCritical() << "monitor-feature-test: RTP stream did not start";
            ++failures;
        }
        else if (monitor->height() > 60)
        {
            qCritical() << "monitor-feature-test: monitor not collapsed"
                        << monitor->height();
            ++failures;
        }
        else
        {
            // Verify the default HTTP mode actually serves MPEG-TS.
            const QUrl url(monitor->streamUrlForTest());
            QTcpSocket socket;
            socket.connectToHost(QStringLiteral("127.0.0.1"),
                                 quint16(url.port(8080)));
            if (!socket.waitForConnected(2000))
            {
                qCritical() << "monitor-feature-test: HTTP stream connect failed";
                ++failures;
            }
            else
            {
                socket.write("GET /stream.ts HTTP/1.0\r\nHost: localhost\r\n\r\n");
                QByteArray response;
                QEventLoop loop;
                QObject::connect(&socket, &QTcpSocket::readyRead, &loop,
                                 [&response, &socket] { response += socket.readAll(); });
                QTimer::singleShot(2000, &loop, &QEventLoop::quit);
                loop.exec();
                const int headerEnd = response.indexOf("\r\n\r\n");
                if (!response.contains("200 OK") || headerEnd < 0
                    || response.indexOf('\x47', headerEnd) < 0)
                {
                    qCritical() << "monitor-feature-test: HTTP stream payload"
                                << response.size();
                    ++failures;
                }
            }
        }
        monitor->stopStreamingForTest();
        QApplication::processEvents();
        if (monitor->isStreaming() || monitor->height() < 100)
        {
            qCritical() << "monitor-feature-test: RTP stream did not stop"
                        << monitor->height();
            ++failures;
        }
    }
    else
    {
        qInfo() << "monitor-feature-test: ffmpeg missing, streaming skipped";
    }

    qInfo() << "monitor-feature-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int cameraParentTest(QApplication& app)
{
    pvf::Settings::setMonitorVisible(true);
    pvf::Settings::setShowCameraFrame(true);
    auto* project = new pvf::Project;
    project->width = 1920;
    project->height = 1080;
    project->background = QColor(18, 22, 30);

    pvf::Entity* rig = project->addFolder(QStringLiteral("Camera Rig"));
    const pvf::EntityId rigId = rig->id;
    project->setValue(rigId, QStringLiteral("x"), 0, 200.0, false);
    project->setValue(rigId, QStringLiteral("y"), 0, -100.0, false);
    project->setValue(rigId, QStringLiteral("rotation"), 0, 20.0, false);

    pvf::Entity* camera = project->addCamera(QStringLiteral("News Camera"));
    const pvf::EntityId cameraId = camera->id;
    project->setValue(cameraId, QStringLiteral("x"), 0, 120.0, false);
    project->setValue(cameraId, QStringLiteral("y"), 0, 80.0, false);
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 0.6, false);
    project->setValue(cameraId, QStringLiteral("rotation"), 0, 15.0, false);

    pvf::Entity* bar = project->addVector(0, 1800, 100, QColor(200, 30, 40),
                                          QColor(255, 70, 70), 0);
    pvf::Entity* label = project->addText(QStringLiteral("BREAKING NEWS"),
                                          QFont().family(), 56, Qt::white,
                                          int(Qt::AlignCenter));
    const pvf::EntityId barId = bar->id;
    const pvf::EntityId labelId = label->id;
    // Appending to the project vector can reallocate; re-fetch after adding.
    camera = project->entity(cameraId);
    bar = project->entity(barId);
    int failures = 0;
    if (!project->setEntityParent(cameraId, rigId)
        || !project->setEntityParent(barId, cameraId)
        || !project->setEntityParent(labelId, cameraId))
    {
        qCritical() << "camera-parent-test: set parent";
        ++failures;
    }

    // The camera follows the rig: its local centre/rotation/scale are mapped
    // through the rig transform.
    QTransform rigTransform;
    rigTransform.translate(200.0, -100.0);
    rigTransform.rotate(20.0);
    const QPointF expectedCenter = rigTransform.map(QPointF(120.0, 80.0));
    const pvf::Camera::State cameraState =
        pvf::Camera::state(*project, cameraId, 0);
    if ((cameraState.center - expectedCenter).manhattanLength() > 1e-6
        || std::abs(cameraState.rotation - 35.0) > 1e-6
        || std::abs(cameraState.scale - 0.6) > 1e-6)
    {
        qCritical() << "camera-parent-test: camera did not follow rig"
                    << cameraState.center << cameraState.rotation
                    << cameraState.scale;
        ++failures;
    }
    project->setValue(barId, QStringLiteral("x"), 0, 0.0, false);
    project->setValue(barId, QStringLiteral("y"), 0, 220.0, false);
    project->setValue(labelId, QStringLiteral("x"), 0, 0.0, false);
    project->setValue(labelId, QStringLiteral("y"), 0, 220.0, false);

    // The camera view cancels the camera parent transform, so an overlay keeps
    // the same screen position while the camera pans, zooms and rotates.
    const QRectF target(0, 0, 960, 540);
    auto screenPoint = [project, target, cameraId](const pvf::Entity& entity) {
        const pvf::Camera::State state = pvf::Camera::state(*project, cameraId, 0);
        const QPointF world = pvf::SceneRenderer::worldTransform(*project, entity, 0)
                                  .map(QPointF(0, 0));
        QTransform view;
        view.translate(target.center().x(), target.center().y());
        view.scale(target.width() / state.size.width(),
                   target.height() / state.size.height());
        view.rotate(-state.rotation);
        view.translate(-state.center.x(), -state.center.y());
        return view.map(world);
    };
    const QPointF firstScreen = screenPoint(*bar);
    project->setValue(cameraId, QStringLiteral("x"), 0, -200.0, false);
    project->setValue(cameraId, QStringLiteral("y"), 0, 150.0, false);
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 0.4, false);
    project->setValue(cameraId, QStringLiteral("rotation"), 0, -35.0, false);
    const QPointF movedScreen = screenPoint(*bar);
    if ((firstScreen - movedScreen).manhattanLength() > 1e-6)
    {
        qCritical() << "camera-parent-test: overlay moved on screen"
                    << firstScreen << movedScreen;
        ++failures;
    }
    project->setValue(cameraId, QStringLiteral("x"), 0, 120.0, false);
    project->setValue(cameraId, QStringLiteral("y"), 0, 80.0, false);
    project->setValue(cameraId, QStringLiteral("scaleX"), 0, 0.6, false);
    project->setValue(cameraId, QStringLiteral("rotation"), 0, 15.0, false);

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    auto* monitor = window.findChild<pvf::CameraMonitor*>();
    if (!timeline || !monitor)
    {
        qCritical() << "camera-parent-test: widgets missing";
        app.exit(1);
        return 1;
    }
    if (timeline->rowCountForTest() != 4
        || timeline->rowEntityForTest(0) != rigId
        || timeline->rowEntityForTest(1) != cameraId
        || timeline->rowEntityForTest(2) != barId
        || timeline->rowEntityForTest(3) != labelId
        || timeline->rowDepthForTest(1) != 1
        || timeline->rowDepthForTest(2) != 2)
    {
        qCritical() << "camera-parent-test: tree rows" << timeline->rowCountForTest();
        ++failures;
    }

    window.setSelectedEntityForTest(barId);
    QApplication::processEvents();
    auto* parents = window.findChild<QComboBox*>(QStringLiteral("parentCombo"));
    if (!parents || parents->findData(int(cameraId)) < 0)
    {
        qCritical() << "camera-parent-test: camera missing from parent combo";
        ++failures;
    }
    else
    {
        const int worldIndex = parents->findData(int(pvf::kInvalidEntity));
        const int cameraIndex = parents->findData(int(cameraId));
        parents->setCurrentIndex(worldIndex);
        QApplication::processEvents();
        if (bar->parent != pvf::kInvalidEntity)
        {
            qCritical() << "camera-parent-test: parent to world";
            ++failures;
        }
        parents->setCurrentIndex(cameraIndex);
        QApplication::processEvents();
        if (bar->parent != cameraId)
        {
            qCritical() << "camera-parent-test: parent to camera";
            ++failures;
        }
    }

    // The camera itself can use a parent: switch between world and the rig.
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();
    auto* cameraParents =
        window.findChild<QComboBox*>(QStringLiteral("parentCombo"));
    if (!cameraParents || cameraParents->findData(int(rigId)) < 0)
    {
        qCritical() << "camera-parent-test: rig missing from camera parent combo"
                    << "selected" << window.selectedEntityForTest()
                    << "count" << (cameraParents ? cameraParents->count() : -1);
        if (cameraParents)
            for (int i = 0; i < cameraParents->count(); ++i)
                qCritical() << "  combo" << i << cameraParents->itemText(i)
                            << cameraParents->itemData(i);
        ++failures;
    }
    else
    {
        const int worldIndex = cameraParents->findData(int(pvf::kInvalidEntity));
        const int rigIndex = cameraParents->findData(int(rigId));
        cameraParents->setCurrentIndex(worldIndex);
        QApplication::processEvents();
        if (camera->parent != pvf::kInvalidEntity)
        {
            qCritical() << "camera-parent-test: camera parent to world";
            ++failures;
        }
        cameraParents->setCurrentIndex(rigIndex);
        QApplication::processEvents();
        if (camera->parent != rigId)
        {
            qCritical() << "camera-parent-test: camera parent to rig"
                        << camera->parent;
            ++failures;
        }
    }

    // Locking the rig locks the camera and its overlays.
    project->setEntityFlag(rigId, pvf::EntityFlag::Locked, true);
    window.setSelectedEntityForTest(barId);
    if (window.selectedEntityForTest() != pvf::kInvalidEntity)
    {
        qCritical() << "camera-parent-test: rig lock did not lock overlay";
        ++failures;
    }
    window.setSelectedEntityForTest(cameraId);
    if (window.selectedEntityForTest() != pvf::kInvalidEntity)
    {
        qCritical() << "camera-parent-test: rig lock did not lock camera";
        ++failures;
    }
    project->setEntityFlag(rigId, pvf::EntityFlag::Locked, false);
    window.setSelectedEntityForTest(cameraId);
    QApplication::processEvents();

    // The monitor should show the red lower-third inside the camera view.
    const QImage shot = monitor->grab().toImage();
    bool redFound = false;
    for (int y = shot.height() / 2; y < shot.height() - 8 && !redFound; ++y)
    {
        for (int x = shot.width() / 4; x < shot.width() * 3 / 4; ++x)
        {
            const QColor pixel = shot.pixelColor(x, y);
            if (pixel.red() > 150 && pixel.green() < 90 && pixel.blue() < 90)
            {
                redFound = true;
                break;
            }
        }
    }
    if (!redFound)
    {
        qCritical() << "camera-parent-test: monitor missing overlay";
        ++failures;
    }

    window.grab().toImage().save(QStringLiteral("/tmp/pvf_camera_parent.png"));
    qInfo() << "camera-parent-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int guideTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "guide-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* first = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId idA = first->id;
        pvf::Entity* second = project->addVector(0, 200, 120, Qt::green, Qt::white, 0);
        project->setValue(second->id, QStringLiteral("x"), 0, 500.0, false);
        window.setSelectedEntityForTest(idA);

        const double s = viewport->currentScale();
        const QPoint center = viewport->entityPivotForTest(idA).toPoint();
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto hasGuide = [viewport](const QImage& image) {
            QRect skip;
            if (auto* monitor = viewport->findChild<pvf::CameraMonitor*>())
                skip = monitor->geometry();
            for (int y = 0; y < image.height(); y += 3)
            {
                const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                {
                    if (skip.contains(x, y))
                        continue;
                    if (qBlue(line[x]) > 200 && qGreen(line[x]) > 195
                        && qRed(line[x]) < 140)
                        return true;
                }
            }
            return false;
        };

        send(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, center + QPoint(int(std::lround(245.0 * s)), 0),
             Qt::NoButton, Qt::LeftButton);

        viewport->grab().toImage().save(QStringLiteral("/tmp/pvforge_guide.png"));
        if (!hasGuide(viewport->grab().toImage()))
        {
            qCritical() << "guide-test: no guide drawn while dragging";
            ++failures;
        }
        if (std::abs(project->value(idA, QStringLiteral("x"), 0) - 250.0) > 1.5)
        {
            qCritical() << "guide-test: not snapped"
                        << project->value(idA, QStringLiteral("x"), 0);
            ++failures;
        }

        send(QEvent::MouseButtonRelease, center + QPoint(int(std::lround(245.0 * s)), 0),
             Qt::LeftButton, Qt::NoButton);
        viewport->grab().toImage().save(QStringLiteral("/tmp/pvforge_guide_after.png"));
        if (hasGuide(viewport->grab().toImage()))
        {
            qCritical() << "guide-test: guide still visible after release";
            ++failures;
        }

        qInfo() << "guide-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int entitySnapTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "entity-snap-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* first = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId idA = first->id;
        pvf::Entity* second = project->addVector(0, 200, 120, Qt::green, Qt::white, 0);
        const pvf::EntityId idB = second->id;
        project->setValue(idB, QStringLiteral("x"), 0, 500.0, false);
        window.setSelectedEntityForTest(idA);

        const double s = viewport->currentScale();
        const QPoint center = viewport->entityPivotForTest(idA).toPoint();
        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        };
        auto reset = [&] {
            project->setValue(idA, QStringLiteral("x"), 0, 0.0, false);
            project->setValue(idA, QStringLiteral("y"), 0, 0.0, false);
        };

        // Edge alignment: A's right edge snaps to B's left edge (x = 250).
        drag(center, center + QPoint(int(std::lround(245.0 * s)), 0));
        if (std::abs(project->value(idA, QStringLiteral("x"), 0) - 250.0) > 1.5)
        {
            qCritical() << "entity-snap-test: edge alignment"
                        << project->value(idA, QStringLiteral("x"), 0);
            ++failures;
        }

        // Horizontal drag locks the vertical axis.
        reset();
        drag(center, center + QPoint(int(std::lround(100.0 * s)),
                                     int(std::lround(20.0 * s))));
        if (std::abs(project->value(idA, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "entity-snap-test: horizontal lock failed"
                        << project->value(idA, QStringLiteral("y"), 0);
            ++failures;
        }

        // Vertical drag locks the horizontal axis.
        reset();
        drag(center, center + QPoint(int(std::lround(10.0 * s)),
                                     int(std::lround(100.0 * s))));
        if (std::abs(project->value(idA, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "entity-snap-test: vertical lock failed"
                        << project->value(idA, QStringLiteral("x"), 0);
            ++failures;
        }

        qInfo() << "entity-snap-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int rotationSnapTest(QApplication& app)
{
    const QList<int> oldAngles = pvf::Settings::snapAngles();

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, oldAngles] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "rotation-snap-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto angleDistance = [](double a, double b) {
            return std::abs(std::remainder(a - b, 360.0));
        };
        auto rotateTo = [&](double degrees) {
            project->setValue(id, QStringLiteral("rotation"), 0, 0.0, false);
            const QPoint center = viewport->entityPivotForTest(id).toPoint();
            const double radians = degrees * M_PI / 180.0;
            const QPoint end(center.x() + int(std::lround(48.0 * std::cos(radians))),
                             center.y() + int(std::lround(48.0 * std::sin(radians))));
            send(QEvent::MouseButtonPress, center + QPoint(48, 0), Qt::LeftButton,
                 Qt::LeftButton);
            send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
            return project->value(id, QStringLiteral("rotation"), 0);
        };

        pvf::Settings::setSnapAngles({0, 90});

        double rotation = rotateTo(2.0);
        if (std::abs(rotation) > 1e-6)
        {
            qCritical() << "rotation-snap-test: 0 snap" << rotation;
            ++failures;
        }

        rotation = rotateTo(88.0);
        if (std::abs(rotation - 90.0) > 1e-6)
        {
            qCritical() << "rotation-snap-test: 90 snap" << rotation;
            ++failures;
        }

        rotation = rotateTo(45.0);
        if (std::abs(rotation - 45.0) > 1.5)
        {
            qCritical() << "rotation-snap-test: 45 should not snap" << rotation;
            ++failures;
        }

        rotation = rotateTo(268.0);
        if (angleDistance(rotation, 268.0) > 1.5)
        {
            qCritical() << "rotation-snap-test: 270 disabled" << rotation;
            ++failures;
        }

        pvf::Settings::setSnapAngles({0, 90, 270});
        rotation = rotateTo(268.0);
        if (angleDistance(rotation, 270.0) > 1e-6)
        {
            qCritical() << "rotation-snap-test: 270 snap" << rotation;
            ++failures;
        }

        pvf::Settings::setSnapAngles(oldAngles);
        qInfo() << "rotation-snap-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int dragUndoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "drag-undo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        const QPoint center = viewport->entityPivotForTest(id).toPoint();

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto hasRedAxis = [&](const QImage& image) {
            for (int y = center.y() - 2; y <= center.y() + 2; ++y)
            {
                for (int x = center.x() + 20; x <= center.x() + 60; ++x)
                {
                    const QRgb pixel = image.pixel(x, y);
                    // The gizmo axis is (226,86,86); the entity body is pure red.
                    if (qRed(pixel) > 200 && qGreen(pixel) > 60 && qGreen(pixel) < 120
                        && qBlue(pixel) < 120)
                        return true;
                }
            }
            return false;
        };

        // Clicking the entity shows the gizmo immediately.
        if (hasRedAxis(viewport->grab().toImage()))
        {
            qCritical() << "drag-undo-test: gizmo visible before selection";
            ++failures;
        }
        send(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
        if (!hasRedAxis(viewport->grab().toImage()))
        {
            qCritical() << "drag-undo-test: gizmo not drawn after click";
            ++failures;
        }

        // Dragging with many moves is a single undo step.
        send(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 10; ++i)
            send(QEvent::MouseMove, center + QPoint(i * 15, i * 8), Qt::NoButton,
                 Qt::LeftButton);
        send(QEvent::MouseButtonRelease, center + QPoint(150, 80), Qt::LeftButton,
             Qt::NoButton);

        const double endX = project->value(id, QStringLiteral("x"), 0);
        const double endY = project->value(id, QStringLiteral("y"), 0);
        if (std::abs(endX) < 1.0 || std::abs(endY) < 1.0)
        {
            qCritical() << "drag-undo-test: drag did nothing" << endX << endY;
            ++failures;
        }

        if (auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")))
            undo->trigger();
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "drag-undo-test: one undo did not restore"
                        << project->value(id, QStringLiteral("x"), 0)
                        << project->value(id, QStringLiteral("y"), 0);
            ++failures;
        }

        qInfo() << "drag-undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int importAsyncTest(QApplication& app)
{
    const QString dir = QDir::tempPath() + QStringLiteral("/pvforge_async");
    QDir().mkpath(dir);
    const QString imageA = QDir(dir).filePath(QStringLiteral("alpha.png"));
    const QString imageB = QDir(dir).filePath(QStringLiteral("beta.png"));
    QImage bitmapA(64, 64, QImage::Format_ARGB32);
    bitmapA.fill(Qt::red);
    QImage bitmapB(64, 64, QImage::Format_ARGB32);
    bitmapB.fill(Qt::green);
    if (!bitmapA.save(imageA) || !bitmapB.save(imageB))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();
    if (!project->save(QDir(dir).filePath(QStringLiteral("test.pvproj"))))
        return 1;

    QTimer::singleShot(400, &window, [&app, &window, project, imageA, imageB, dir] {
        int failures = 0;
        bool finished = false;
        QEventLoop loop;

        window.importMediaFiles(
            {imageA, imageB}, [&](const QVector<pvf::MediaImportResult>& results) {
                if (results.size() != 2)
                {
                    qCritical() << "import-async-test: result count" << results.size();
                    ++failures;
                }
                for (const pvf::MediaImportResult& result : results)
                {
                    if (!result.error.isEmpty() || result.importedPath.isEmpty())
                    {
                        qCritical() << "import-async-test: import error" << result.error;
                        ++failures;
                        continue;
                    }
                    project->addMediaTemplate(result.importedPath, result.type,
                                              result.thumbnail, result.duration,
                                              result.displayName, result.audio);
                }
                finished = true;
                loop.quit();
            });

        QTimer::singleShot(8000, &loop, &QEventLoop::quit);
        loop.exec();

        if (!finished)
        {
            qCritical() << "import-async-test: timeout";
            ++failures;
        }
        if (project->templateCount() != 2)
        {
            qCritical() << "import-async-test: templates" << project->templateCount();
            ++failures;
        }
        if (auto* bar = window.findChild<QProgressBar*>(QStringLiteral("importProgress")))
        {
            if (bar->isVisible())
            {
                qCritical() << "import-async-test: progress bar still visible";
                ++failures;
            }
        }
        bool namesOk = false;
        for (int i = 0; i < project->templateCount(); ++i)
        {
            const pvf::Template* tmpl = project->templateAt(i);
            if (tmpl && (tmpl->name == QStringLiteral("alpha")
                         || tmpl->name == QStringLiteral("beta")))
                namesOk = true;
        }
        if (!namesOk)
        {
            qCritical() << "import-async-test: display names lost";
            ++failures;
        }

        QDir(dir).removeRecursively();
        qInfo() << "import-async-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int shapeTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        const int shapes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        for (int i = 0; i < 9; ++i)
        {
            pvf::Entity* e =
                project->addVector(shapes[i], 120, 90, Qt::red, Qt::white, 3);
            if (!e)
            {
                qCritical() << "shape-test: create failed" << shapes[i];
                ++failures;
                continue;
            }
            project->setValue(e->id, QStringLiteral("x"), 0, -520.0 + (i % 3) * 520.0,
                              false);
            project->setValue(e->id, QStringLiteral("y"), 0, -260.0 + (i / 3) * 260.0,
                              false);
        }
        if (project->entities().size() != 9)
        {
            qCritical() << "shape-test: entity count" << project->entities().size();
            ++failures;
        }

        // Custom parameters survive on the entity.
        const pvf::Entity& starEntity = project->entities()[6];
        if (starEntity.vectorSides() != 5 || starEntity.vectorInnerRatio() < 0.44)
        {
            qCritical() << "shape-test: star defaults" << starEntity.vectorSides()
                        << starEntity.vectorInnerRatio();
            ++failures;
        }
        const pvf::Entity& roundedEntity = project->entities()[3];
        if (roundedEntity.cornerRadius() <= 0.0)
        {
            qCritical() << "shape-test: corner radius default";
            ++failures;
        }

        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_shapes.png"));
        qInfo() << "shape-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int pivotGizmoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "pivot-gizmo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);
        entity->data[QStringLiteral("rotPointCustom")] = true;

        const double s = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        auto visualCentre = [&] {
            return viewport->entityTransformForTest(id).map(QPointF(0, 0));
        };
        auto gizmoPos = [&] { return viewport->entityPivotForTest(id).toPoint(); };

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, mods);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        };

        // Dragging the X axis moves the pivot; the position compensates so the
        // entity does not move visually.
        const int edgeDx = int(std::lround(145.0 * s));
        drag(center + QPoint(40, 0), center + QPoint(40 + edgeDx, 0));
        const double pivotX = project->value(id, QStringLiteral("rotPointX"), 0);
        const double posX = project->value(id, QStringLiteral("x"), 0);
        QPointF visual = visualCentre();
        if (std::abs(pivotX - 150.0) > 1.0 || std::abs(posX - 150.0) > 1.0)
        {
            qCritical() << "pivot-gizmo-test: axis pivot drag" << pivotX << posX;
            ++failures;
        }
        if (std::abs(visual.x() - center.x()) > 2.0
            || std::abs(visual.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-gizmo-test: entity moved visually" << visual;
            ++failures;
        }
        if (std::abs(gizmoPos().x() - (center.x() + int(std::lround(150.0 * s)))) > 3)
        {
            qCritical() << "pivot-gizmo-test: gizmo not on pivot" << gizmoPos();
            ++failures;
        }

        // Dragging the pivot dot moves both axes freely.
        const QPoint dot = gizmoPos();
        drag(dot, dot + QPoint(int(std::lround(60.0 * s)), int(std::lround(30.0 * s))));
        if (project->value(id, QStringLiteral("rotPointX"), 0) < 190.0
            || project->value(id, QStringLiteral("rotPointY"), 0) < 15.0)
        {
            qCritical() << "pivot-gizmo-test: free pivot drag"
                        << project->value(id, QStringLiteral("rotPointX"), 0)
                        << project->value(id, QStringLiteral("rotPointY"), 0);
            ++failures;
        }

        // Pivot snapping follows the scaled visual edge.
        window.applyPivotForTest(id, 0.0, 0.0);
        project->setValue(id, QStringLiteral("scaleX"), 0, 0.5, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 0.5, false);
        const int visualEdgeDx = int(std::lround(70.0 * s));
        drag(center + QPoint(40, 0), center + QPoint(40 + visualEdgeDx, 0));
        const double snappedPivot = project->value(id, QStringLiteral("rotPointX"), 0);
        if (std::abs(snappedPivot - 75.0) > 1.5)
        {
            qCritical() << "pivot-gizmo-test: visual edge snap" << snappedPivot;
            ++failures;
        }

        // Disabling the edit mode keeps the gizmo on the pivot.
        entity->data[QStringLiteral("rotPointCustom")] = false;
        const QPoint gizmoAfter = gizmoPos();
        const QPoint expectedGizmo(center.x() + int(std::lround(snappedPivot * s)),
                                   center.y());
        if (std::abs(gizmoAfter.x() - expectedGizmo.x()) > 3
            || std::abs(gizmoAfter.y() - expectedGizmo.y()) > 3)
        {
            qCritical() << "pivot-gizmo-test: gizmo left the pivot" << gizmoAfter;
            ++failures;
        }

        // Axis dragging now moves the entity again.
        project->setValue(id, QStringLiteral("scaleX"), 0, 1.0, false);
        project->setValue(id, QStringLiteral("scaleY"), 0, 1.0, false);
        const QPoint gizmoNow = gizmoPos();
        drag(gizmoNow + QPoint(40, 0), gizmoNow + QPoint(40 + int(std::lround(50.0 * s)), 0));
        if (std::abs(project->value(id, QStringLiteral("x"), 0)
                     - (snappedPivot + 50.0))
            > 2.0)
        {
            qCritical() << "pivot-gizmo-test: axis did not move entity"
                        << project->value(id, QStringLiteral("x"), 0);
            ++failures;
        }

        qInfo() << "pivot-gizmo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}


int pivotTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "pivot-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double s = viewport->currentScale();
        const QPointF center(viewport->rect().center());
        auto visualCentre = [&] {
            return viewport->entityTransformForTest(id).map(QPointF(0, 0));
        };

        // Moving the pivot compensates the position: visuals stay put.
        window.applyPivotForTest(id, 100.0, 0.0);
        const double x = project->value(id, QStringLiteral("x"), 0);
        if (std::abs(x - 100.0) > 0.5)
        {
            qCritical() << "pivot-test: position not compensated" << x;
            ++failures;
        }
        QPointF visual = visualCentre();
        if (std::abs(visual.x() - center.x()) > 2.0
            || std::abs(visual.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-test: entity moved visually" << visual;
            ++failures;
        }
        QPointF gizmo = viewport->entityPivotForTest(id);
        if (std::abs(gizmo.x() - (center.x() + 100.0 * s)) > 2.0
            || std::abs(gizmo.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-test: gizmo not on pivot" << gizmo;
            ++failures;
        }

        // Rotation orbits the pivot.
        project->setValue(id, QStringLiteral("rotation"), 0, 90.0, false);
        visual = visualCentre();
        const QPointF expected(center.x() + 100.0 * s, center.y() - 100.0 * s);
        if (std::abs(visual.x() - expected.x()) > 2.0
            || std::abs(visual.y() - expected.y()) > 2.0)
        {
            qCritical() << "pivot-test: rotation orbit" << visual << expected;
            ++failures;
        }

        // Turning off the edit mode keeps the pivot and the gizmo there.
        entity->data[QStringLiteral("rotPointCustom")] = false;
        gizmo = viewport->entityPivotForTest(id);
        if (std::abs(gizmo.x() - (center.x() + 100.0 * s)) > 2.0
            || std::abs(gizmo.y() - center.y()) > 2.0)
        {
            qCritical() << "pivot-test: gizmo left the pivot" << gizmo;
            ++failures;
        }
        visual = visualCentre();
        if (std::abs(visual.x() - expected.x()) > 2.0
            || std::abs(visual.y() - expected.y()) > 2.0)
        {
            qCritical() << "pivot-test: disabling changed rotation centre" << visual;
            ++failures;
        }

        qInfo() << "pivot-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}


int gizmoTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!viewport)
        {
            qCritical() << "gizmo-test: viewport missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 300, 200, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        window.setSelectedEntityForTest(id);

        const double scale = viewport->currentScale();
        const QPoint center = viewport->rect().center();
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_gizmo.png"));

        auto send = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        };

        // Drag the X axis: horizontal only.
        drag(center + QPoint(40, 0), center + QPoint(140, 0));
        const double xAfterAxis = project->value(id, QStringLiteral("x"), 0);
        if (xAfterAxis <= 0.0
            || std::abs(project->value(id, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "gizmo-test: x axis drag" << xAfterAxis;
            ++failures;
        }
        const double expectedX = 100.0 / scale;
        if (std::abs(xAfterAxis - expectedX) > 1.0)
        {
            qCritical() << "gizmo-test: x axis delta" << xAfterAxis << expectedX;
            ++failures;
        }

        project->setValue(id, QStringLiteral("x"), 0, 0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0, false);

        // Drag the Y axis (points up): vertical only. Stay clear of the ring.
        drag(center + QPoint(0, -30), center + QPoint(0, -140));
        const double yAfterAxis = project->value(id, QStringLiteral("y"), 0);
        if (yAfterAxis >= 0.0
            || std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "gizmo-test: y axis drag" << yAfterAxis;
            ++failures;
        }

        project->setValue(id, QStringLiteral("x"), 0, 0, false);
        project->setValue(id, QStringLiteral("y"), 0, 0, false);
        project->setValue(id, QStringLiteral("rotation"), 0, 0, false);

        // Drag the rotation ring: changes the angle, keeps the position.
        drag(center + QPoint(48, 0), center + QPoint(0, 48));
        const double rotation = project->value(id, QStringLiteral("rotation"), 0);
        // Integer mouse coordinates against a fractional pivot give a small
        // angular quantisation error; allow a few degrees.
        if (std::abs(rotation - 90.0) > 4.0)
        {
            qCritical() << "gizmo-test: rotation" << rotation;
            ++failures;
        }
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6
            || std::abs(project->value(id, QStringLiteral("y"), 0)) > 1e-6)
        {
            qCritical() << "gizmo-test: rotation moved the entity";
            ++failures;
        }

        project->setValue(id, QStringLiteral("rotation"), 0, 0, false);

        // Dragging the body moves freely.
        drag(center, center + QPoint(60, 30));
        if (project->value(id, QStringLiteral("x"), 0) <= 0.0
            || project->value(id, QStringLiteral("y"), 0) <= 0.0)
        {
            qCritical() << "gizmo-test: body drag";
            ++failures;
        }

        qInfo() << "gizmo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int lockTest(QApplication& app)
{
    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* viewport = window.findChild<pvf::Viewport*>();
        if (!timeline || !viewport)
        {
            qCritical() << "lock-test: widgets missing";
            app.exit(1);
            return;
        }

        pvf::Entity* entity = project->addVector(0, 200, 120, Qt::red, Qt::white, 0);
        const pvf::EntityId id = entity->id;
        project->setValue(id, QStringLiteral("x"), 0, 0, true);
        project->setValue(id, QStringLiteral("x"), 30, 400, true);

        // A selected entity is deselected when it becomes locked.
        window.setSelectedEntityForTest(id);
        if (window.selectedEntityForTest() != id)
        {
            qCritical() << "lock-test: entity not selected before locking";
            ++failures;
        }
        project->setEntityFlag(id, pvf::EntityFlag::Locked, true);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked entity stayed selected"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Values are frozen.
        project->setValue(id, QStringLiteral("x"), 15, 999, true);
        if (std::abs(project->value(id, QStringLiteral("x"), 15) - 200.0) > 1e-6)
        {
            qCritical() << "lock-test: value changed while locked";
            ++failures;
        }

        // Keyframes cannot be deleted.
        if (!project->removeKeyframesAt(id, 30).isEmpty())
        {
            qCritical() << "lock-test: keyframe removed while locked";
            ++failures;
        }

        // Clicking a locked entity in the viewport must not select it.
        const QPoint center = viewport->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(center),
                          QPointF(viewport->mapToGlobal(center)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(center + QPoint(120, 0)),
                         QPointF(viewport->mapToGlobal(center + QPoint(120, 0))), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(center + QPoint(120, 0)),
                            QPointF(viewport->mapToGlobal(center + QPoint(120, 0))),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &release);
        if (std::abs(project->value(id, QStringLiteral("x"), 0)) > 1e-6)
        {
            qCritical() << "lock-test: locked entity moved";
            ++failures;
        }
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked entity selected in viewport"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Selecting it programmatically is rejected too.
        window.setSelectedEntityForTest(id);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked entity selectable programmatically";
            ++failures;
        }

        // Clicking the locked row name must not select it either.
        const QPoint rowName(10, timeline->rowButtonRect(0, 0).center().y());
        QMouseEvent rowPress(QEvent::MouseButtonPress, QPointF(rowName),
                             QPointF(timeline->mapToGlobal(rowName)), Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &rowPress);
        QMouseEvent rowRelease(QEvent::MouseButtonRelease, QPointF(rowName),
                               QPointF(timeline->mapToGlobal(rowName)), Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &rowRelease);
        if (window.selectedEntityForTest() != pvf::kInvalidEntity)
        {
            qCritical() << "lock-test: locked row selected in timeline"
                        << window.selectedEntityForTest();
            ++failures;
        }

        // Row buttons stay inside the name column.
        if (timeline->rowButtonRect(0, 0).right() > 180
            || timeline->rowButtonRect(0, 2).left() < 0)
        {
            qCritical() << "lock-test: row buttons outside the name column";
            ++failures;
        }

        // Unlocking restores selection and editing.
        project->setEntityFlag(id, pvf::EntityFlag::Locked, false);
        window.setSelectedEntityForTest(id);
        if (window.selectedEntityForTest() != id)
        {
            qCritical() << "lock-test: unlocked entity not selectable";
            ++failures;
        }
        project->setValue(id, QStringLiteral("x"), 15, 999, true);
        if (std::abs(project->value(id, QStringLiteral("x"), 15) - 999.0) > 1e-6)
        {
            qCritical() << "lock-test: unlock did not restore editing";
            ++failures;
        }

        qInfo() << "lock-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int packTest(QApplication& app)
{
    Q_UNUSED(app);
    const QString external = QDir::tempPath() + QStringLiteral("/pvforge_external.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::green);
    if (!img.save(external))
        return 1;

    const QString projectDir = QDir::tempPath() + QStringLiteral("/pvforge_pack_project");
    QDir().mkpath(projectDir);
    const QString projectPath = QDir(projectDir).filePath(QStringLiteral("test.pvproj"));

    pvf::Project project;
    QString error;
    if (!project.save(projectPath, &error))
        return 1;

    int failures = 0;

    // Packing is off by default.
    if (project.packAssets)
    {
        qCritical() << "pack-test: packing should be off by default";
        ++failures;
    }

    // With packing off the original path is kept.
    if (project.importAsset(external) != external)
    {
        qCritical() << "pack-test: import copied while packing is off";
        ++failures;
    }

    // With packing on the file is copied into the project folder.
    project.packAssets = true;
    const QString packed = project.importAsset(external);
    if (packed == external || !QFile::exists(packed)
        || !packed.startsWith(project.assetDirectory()))
    {
        qCritical() << "pack-test: import not packed" << packed;
        ++failures;
    }

    // Saving stores the path relative to the project file.
    project.addImage(packed);
    if (!project.save(projectPath, &error))
        return 1;

    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QString stored = document.object()
                               .value(QStringLiteral("entities"))
                               .toArray()
                               .first()
                               .toObject()
                               .value(QStringLiteral("sourcePath"))
                               .toString();
    if (QFileInfo(stored).isAbsolute() || !stored.startsWith(QStringLiteral("assets")))
    {
        qCritical() << "pack-test: path not stored relative" << stored;
        ++failures;
    }

    // Loading resolves it back to an absolute path.
    pvf::Project loaded;
    if (!loaded.load(projectPath, &error))
    {
        qCritical() << "pack-test: load failed" << error;
        ++failures;
    }
    else if (loaded.entities().isEmpty()
             || loaded.entities().first().sourcePath != packed)
    {
        qCritical() << "pack-test: path not resolved";
        ++failures;
    }
    else if (loaded.entities().first().image.isNull())
    {
        qCritical() << "pack-test: packed image not reloaded";
        ++failures;
    }

    // Asset import recognises images and rejects unsupported files.
    pvf::Project importProject;
    const QString importImage = QDir::tempPath() + QStringLiteral("/pvforge_import.png");
    QImage importBitmap(320, 200, QImage::Format_ARGB32);
    importBitmap.fill(Qt::magenta);
    if (!importBitmap.save(importImage))
        return 1;
    const QString importText = QDir::tempPath() + QStringLiteral("/pvforge_import.txt");
    {
        QFile file(importText);
        file.open(QIODevice::WriteOnly);
        file.write("not a media file");
    }

    if (importProject.importMediaFile(importImage).isEmpty()
        || importProject.templateCount() != 1
        || importProject.templates().first().type != pvf::EntityType::Image)
    {
        qCritical() << "pack-test: image import failed";
        ++failures;
    }
    if (importProject.importMediaFile(importImage).isEmpty()
        || importProject.templateCount() != 1)
    {
        qCritical() << "pack-test: duplicate import not deduplicated";
        ++failures;
    }
    QString importError;
    if (!importProject.importMediaFile(importText, &importError).isEmpty()
        || importProject.templateCount() != 1)
    {
        qCritical() << "pack-test: unsupported file accepted";
        ++failures;
    }
    QFile::remove(importImage);
    QFile::remove(importText);

    // Packing deduplicates identical files and numbers same-name clashes.
    const QString sourceDirA = QDir::tempPath() + QStringLiteral("/pvforge_pack_a");
    const QString sourceDirB = QDir::tempPath() + QStringLiteral("/pvforge_pack_b");
    QDir().mkpath(sourceDirA);
    QDir().mkpath(sourceDirB);
    const QString sameNameA = QDir(sourceDirA).filePath(QStringLiteral("same.png"));
    const QString sameNameB = QDir(sourceDirB).filePath(QStringLiteral("same.png"));
    QImage redImage(64, 64, QImage::Format_ARGB32);
    redImage.fill(Qt::red);
    QImage greenImage(64, 64, QImage::Format_ARGB32);
    greenImage.fill(Qt::green);
    if (!redImage.save(sameNameA) || !greenImage.save(sameNameB))
        return 1;

    const QString dedupeDir = QDir::tempPath() + QStringLiteral("/pvforge_pack_dedupe");
    QDir().mkpath(dedupeDir);
    pvf::Project dedupeProject;
    if (!dedupeProject.save(QDir(dedupeDir).filePath(QStringLiteral("test.pvproj"))))
        return 1;
    dedupeProject.packAssets = true;

    const QString packedFirst = dedupeProject.importAsset(sameNameA);
    const QString packedAgain = dedupeProject.importAsset(sameNameA);
    const QString packedSecond = dedupeProject.importAsset(sameNameB);
    if (packedFirst != packedAgain)
    {
        qCritical() << "pack-test: repeat import created another copy";
        ++failures;
    }
    if (packedFirst == packedSecond)
    {
        qCritical() << "pack-test: same-name content collision";
        ++failures;
    }
    if (!QFile::exists(packedFirst) || !QFile::exists(packedSecond))
    {
        qCritical() << "pack-test: packed files missing";
        ++failures;
    }
    const int packedFiles =
        QDir(dedupeProject.assetDirectory()).entryList(QDir::Files).size();
    if (packedFiles != 2)
    {
        qCritical() << "pack-test: asset folder exploded" << packedFiles;
        ++failures;
    }
    if (dedupeProject.copyIntoProject(packedFirst) != packedFirst)
    {
        qCritical() << "pack-test: already packed file copied again";
        ++failures;
    }

    // Stored names are content hashes.
    if (QFileInfo(packedFirst).completeBaseName().size() != 64)
    {
        qCritical() << "pack-test: asset is not content addressed" << packedFirst;
        ++failures;
    }

    // Identical content with a different file name maps to the same asset.
    const QString contentCopy = QDir(sourceDirB).filePath(QStringLiteral("copy.png"));
    if (!QFile::copy(sameNameA, contentCopy))
        return 1;
    if (dedupeProject.importAsset(contentCopy) != packedFirst)
    {
        qCritical() << "pack-test: identical content not deduplicated";
        ++failures;
    }
    QFile::remove(contentCopy);

    // The library keeps the original display name despite the hash path.
    QString mediaError;
    const QString mediaPath = dedupeProject.importMediaFile(sameNameA, &mediaError);
    bool displayNameOk = false;
    for (int i = 0; i < dedupeProject.templateCount(); ++i)
    {
        const pvf::Template* tmpl = dedupeProject.templateAt(i);
        if (tmpl && tmpl->path == mediaPath && tmpl->name == QStringLiteral("same"))
            displayNameOk = true;
    }
    if (!displayNameOk)
    {
        qCritical() << "pack-test: display name lost";
        ++failures;
    }
    QDir(sourceDirA).removeRecursively();
    QDir(sourceDirB).removeRecursively();
    QDir(dedupeDir).removeRecursively();

    // Library templates keep a small thumbnail, never the full-size bitmap.
    const QString bigPath = QDir::tempPath() + QStringLiteral("/pvforge_big.png");
    QImage big(1600, 1200, QImage::Format_ARGB32);
    big.fill(Qt::blue);
    if (!big.save(bigPath))
        return 1;
    pvf::Project bigProject;
    bigProject.addImage(bigPath);
    const QImage thumb = bigProject.templates().first().thumbnail;
    if (thumb.width() > 320 || thumb.height() > 240)
    {
        qCritical() << "pack-test: thumbnail not downscaled" << thumb.size();
        ++failures;
    }
    QFile::remove(bigPath);

    QDir(projectDir).removeRecursively();
    QFile::remove(external);
    qInfo() << "pack-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int projectLimitTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;

    QTemporaryDir tempDir;
    QTemporaryDir outsideDir;
    if (!tempDir.isValid() || !outsideDir.isValid())
    {
        qCritical() << "project-limit-test: cannot create temp directories";
        return 1;
    }

    const QString projectRoot = tempDir.path();
    const QString projectPath = QDir(projectRoot).filePath(QStringLiteral("limit.pvproj"));

    auto makeProject = [](int version) {
        QJsonObject root;
        root[QStringLiteral("format")] = QStringLiteral("pvforge");
        root[QStringLiteral("version")] = version;
        root[QStringLiteral("name")] = QStringLiteral("limit test");
        root[QStringLiteral("entities")] = QJsonArray();
        root[QStringLiteral("templates")] = QJsonArray();
        root[QStringLiteral("scenes")] = QJsonArray();
        root[QStringLiteral("annotations")] = QJsonArray();
        return root;
    };
    auto makeEntity = [](int id, const QString& type) {
        QJsonObject entity;
        entity[QStringLiteral("id")] = id;
        entity[QStringLiteral("type")] = type;
        entity[QStringLiteral("parent")] = -1;
        entity[QStringLiteral("name")] = QStringLiteral("entity %1").arg(id);
        entity[QStringLiteral("sourcePath")] = QString();
        entity[QStringLiteral("data")] = QJsonObject();
        entity[QStringLiteral("values")] = QJsonObject();
        entity[QStringLiteral("tracks")] = QJsonObject();
        return entity;
    };
    auto writeProject = [](const QString& path, const QJsonObject& root) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
        return file.write(data) == data.size();
    };
    auto writeRaw = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(data) == data.size();
    };
    auto rejected = [&](const QString& label, const QJsonObject& root,
                        const QString& expectedFragment) {
        if (!writeProject(projectPath, root))
        {
            qCritical() << "project-limit-test:" << label << "write failed";
            ++failures;
            return;
        }
        pvf::Project project;
        QString error;
        if (project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test:" << label << "was accepted";
            ++failures;
        }
        else if (!expectedFragment.isEmpty() && !error.contains(expectedFragment))
        {
            qCritical() << "project-limit-test:" << label << "unexpected error" << error;
            ++failures;
        }
    };

    // A safe relative path inside the project directory still loads.
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const QString assetsDir = QDir(projectRoot).filePath(QStringLiteral("assets"));
    QDir().mkpath(assetsDir);
    const QString packedImage = QDir(assetsDir).filePath(QStringLiteral("pic.png"));
    if (!image.save(packedImage))
        return 1;
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("image"));
        entity[QStringLiteral("sourcePath")] = QStringLiteral("assets/pic.png");
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;

        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test: safe relative path rejected" << error;
            ++failures;
        }
        else if (project.entities().size() != 1
                 || project.entities().first().sourcePath != packedImage
                 || project.entities().first().image.isNull())
        {
            qCritical() << "project-limit-test: safe relative path not resolved";
            ++failures;
        }
    }

    // Empty, malformed and non-object files are reported clearly.
    {
        const QString emptyPath = QDir(projectRoot).filePath(QStringLiteral("empty.pvproj"));
        if (!writeRaw(emptyPath, QByteArray()))
            return 1;
        pvf::Project project;
        QString error;
        if (project.load(emptyPath, &error) || !error.contains(QStringLiteral("为空")))
        {
            qCritical() << "project-limit-test: empty file accepted" << error;
            ++failures;
        }
    }
    {
        const QString badPath = QDir(projectRoot).filePath(QStringLiteral("bad.pvproj"));
        if (!writeRaw(badPath, QByteArrayLiteral("{not json")))
            return 1;
        pvf::Project project;
        QString error;
        if (project.load(badPath, &error) || error.isEmpty())
        {
            qCritical() << "project-limit-test: invalid JSON accepted";
            ++failures;
        }
    }
    {
        const QString arrayPath = QDir(projectRoot).filePath(QStringLiteral("array.pvproj"));
        if (!writeRaw(arrayPath, QByteArrayLiteral("[]")))
            return 1;
        pvf::Project project;
        QString error;
        if (project.load(arrayPath, &error) || !error.contains(QStringLiteral("根节点")))
        {
            qCritical() << "project-limit-test: non-object root accepted" << error;
            ++failures;
        }
    }

    // Files larger than 64 MiB are rejected before parsing.
    {
        const QString bigPath = QDir(projectRoot).filePath(QStringLiteral("big.pvproj"));
        QFile bigFile(bigPath);
        if (!bigFile.open(QIODevice::WriteOnly)
            || !bigFile.resize(64LL * 1024 * 1024 + 1))
        {
            qCritical() << "project-limit-test: cannot create oversized file";
            return 1;
        }
        bigFile.close();
        pvf::Project project;
        QString error;
        if (project.load(bigPath, &error)
            || !error.contains(QStringLiteral("过大")))
        {
            qCritical() << "project-limit-test: oversized file accepted" << error;
            ++failures;
        }
        QFile::remove(bigPath);
    }

    // Entity count limit.
    {
        QJsonObject root = makeProject(4);
        QJsonArray entities;
        for (int i = 1; i <= 5001; ++i)
            entities.append(makeEntity(i, QStringLiteral("folder")));
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("5001 entities"), root, QStringLiteral("实体数量"));
    }

    // Total keyframe limit (20 full tracks plus one extra key).
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("folder"));
        QJsonObject tracks;
        for (int track = 0; track < 20; ++track)
        {
            QJsonArray keys;
            for (int key = 0; key < 10000; ++key)
            {
                QJsonObject keyframe;
                keyframe[QStringLiteral("f")] = key;
                keyframe[QStringLiteral("v")] = 0;
                keys.append(keyframe);
            }
            tracks[QStringLiteral("p%1").arg(track)] = keys;
        }
        QJsonArray extra;
        QJsonObject extraKey;
        extraKey[QStringLiteral("f")] = 0;
        extraKey[QStringLiteral("v")] = 0;
        extra.append(extraKey);
        tracks[QStringLiteral("extra")] = extra;
        entity[QStringLiteral("tracks")] = tracks;
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("200001 keyframes"), root, QStringLiteral("关键帧总数"));
    }

    // Per-track keyframe and template count limits.
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("folder"));
        QJsonArray keys;
        for (int key = 0; key < 10001; ++key)
        {
            QJsonObject keyframe;
            keyframe[QStringLiteral("f")] = key;
            keyframe[QStringLiteral("v")] = 0;
            keys.append(keyframe);
        }
        QJsonObject tracks;
        tracks[QStringLiteral("x")] = keys;
        entity[QStringLiteral("tracks")] = tracks;
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("10001 keys per track"), root,
                 QStringLiteral("关键帧数量"));
    }
    {
        QJsonObject root = makeProject(4);
        QJsonArray templates;
        for (int i = 0; i < 1001; ++i)
        {
            QJsonObject tmpl;
            tmpl[QStringLiteral("id")] = QStringLiteral("t%1").arg(i);
            tmpl[QStringLiteral("type")] = QStringLiteral("folder");
            templates.append(tmpl);
        }
        root[QStringLiteral("templates")] = templates;
        rejected(QStringLiteral("1001 templates"), root,
                 QStringLiteral("模板数量"));
    }

    // Single string limit.
    {
        QJsonObject root = makeProject(4);
        root[QStringLiteral("description")] =
            QString(1024 * 1024 + 1, QLatin1Char('x'));
        rejected(QStringLiteral("oversized string"), root, QStringLiteral("字符串长度"));
    }

    // Version 5 and unknown entity types are rejected, not coerced.
    {
        QJsonObject root = makeProject(5);
        rejected(QStringLiteral("version 5"), root, QStringLiteral("过新"));
    }
    {
        QJsonObject root = makeProject(4);
        QJsonArray entities;
        entities.append(makeEntity(1, QStringLiteral("hologram")));
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("unknown entity type"), root,
                 QStringLiteral("未知类型"));
    }
    {
        QJsonObject root = makeProject(4);
        QJsonObject tmpl;
        tmpl[QStringLiteral("id")] = QStringLiteral("t1");
        tmpl[QStringLiteral("type")] = QStringLiteral("hologram");
        QJsonArray templates;
        templates.append(tmpl);
        root[QStringLiteral("templates")] = templates;
        rejected(QStringLiteral("unknown template type"), root,
                 QStringLiteral("未知类型"));
    }

    // Relative paths may not escape the project directory.
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("image"));
        entity[QStringLiteral("sourcePath")] = QStringLiteral("../outside.png");
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        rejected(QStringLiteral("../ escape"), root, QStringLiteral("超出工程目录"));
    }

    // A symlink inside the project pointing outside is rejected.
    // Absolute paths stay supported for the user's own external media.
    const QString outsideImage =
        QDir(outsideDir.path()).filePath(QStringLiteral("outside.png"));
    if (!image.save(outsideImage))
        return 1;
    {
        const QString linkPath = QDir(assetsDir).filePath(QStringLiteral("escape.png"));
        QFile::remove(linkPath);
        if (QFile::link(outsideImage, linkPath)
            && QFileInfo(linkPath).isSymLink())
        {
            QJsonObject root = makeProject(4);
            QJsonObject entity = makeEntity(1, QStringLiteral("image"));
            entity[QStringLiteral("sourcePath")] = QStringLiteral("assets/escape.png");
            QJsonArray entities;
            entities.append(entity);
            root[QStringLiteral("entities")] = entities;
            rejected(QStringLiteral("symlink escape"), root,
                     QStringLiteral("符号链接"));
        }
        QFile::remove(linkPath);
    }
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("image"));
        entity[QStringLiteral("sourcePath")] = outsideImage;
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;
        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test: absolute path rejected" << error;
            ++failures;
        }
        else if (project.entities().size() != 1
                 || project.entities().first().sourcePath != outsideImage
                 || project.entities().first().image.isNull())
        {
            qCritical() << "project-limit-test: absolute path not kept";
            ++failures;
        }
    }

    // A rejected load must not partially replace the current project.
    {
        QJsonObject root = makeProject(4);
        QJsonObject entity = makeEntity(1, QStringLiteral("folder"));
        entity[QStringLiteral("name")] = QStringLiteral("original");
        QJsonArray entities;
        entities.append(entity);
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;

        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error))
        {
            qCritical() << "project-limit-test: baseline load failed" << error;
            ++failures;
        }
        else
        {
            QJsonObject bad = makeProject(4);
            QJsonArray badEntities;
            badEntities.append(makeEntity(1, QStringLiteral("hologram")));
            bad[QStringLiteral("entities")] = badEntities;
            if (!writeProject(projectPath, bad))
                return 1;
            if (project.load(projectPath, &error))
            {
                qCritical() << "project-limit-test: invalid load accepted";
                ++failures;
            }
            else if (project.entities().size() != 1
                     || project.entities().first().name != QStringLiteral("original"))
            {
                qCritical() << "project-limit-test: failed load partially applied";
                ++failures;
            }
        }
    }

    // v1 projects still migrate (default scene is created).
    {
        QJsonObject root = makeProject(1);
        QJsonArray entities;
        entities.append(makeEntity(1, QStringLiteral("folder")));
        root[QStringLiteral("entities")] = entities;
        if (!writeProject(projectPath, root))
            return 1;
        pvf::Project project;
        QString error;
        if (!project.load(projectPath, &error)
            || project.scenes().size() != 1)
        {
            qCritical() << "project-limit-test: v1 migration failed" << error;
            ++failures;
        }
    }

    // Content-addressed copy: the file name must match the copied bytes, and
    // a tampered target is repaired instead of being trusted.
    {
        const QString hashDir = QDir(projectRoot).filePath(QStringLiteral("hash-assets"));
        const QString sourcePath = QDir(projectRoot).filePath(QStringLiteral("payload.bin"));
        const QByteArray payload = QByteArrayLiteral("pvforge hash integrity payload");
        if (!writeRaw(sourcePath, payload))
            return 1;

        QCryptographicHash hasher(QCryptographicHash::Sha256);
        hasher.addData(payload);
        const QString expected = QString::fromLatin1(hasher.result().toHex());
        auto fileHash = [](const QString& path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return QString();
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&file);
            return QString::fromLatin1(hash.result().toHex());
        };

        const QString copied = pvf::copyIntoAssetDirectory(sourcePath, hashDir);
        if (QFileInfo(copied).completeBaseName() != expected
            || fileHash(copied) != expected)
        {
            qCritical() << "project-limit-test: copied asset hash mismatch" << copied;
            ++failures;
        }
        if (pvf::copyIntoAssetDirectory(sourcePath, hashDir) != copied)
        {
            qCritical() << "project-limit-test: identical asset not deduplicated";
            ++failures;
        }

        if (!writeRaw(copied, QByteArrayLiteral("tampered")))
            return 1;
        if (pvf::copyIntoAssetDirectory(sourcePath, hashDir) != copied
            || fileHash(copied) != expected)
        {
            qCritical() << "project-limit-test: tampered asset not repaired";
            ++failures;
        }
    }

    qInfo() << "project-limit-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int renameTest(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_rename.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, assetPath] {
        int failures = 0;
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (!timeline || !undo)
        {
            qCritical() << "rename-test: widgets missing";
            app.exit(1);
            return;
        }
        auto* renameEdit = timeline->findChild<QLineEdit*>(QStringLiteral("renameEdit"));
        if (!renameEdit)
        {
            qCritical() << "rename-test: rename field missing";
            app.exit(1);
            return;
        }

        if (window.importImageFile(assetPath) == pvf::kInvalidEntity)
        {
            qCritical() << "rename-test: import failed";
            app.exit(1);
            return;
        }
        const pvf::EntityId id = project->entities().first().id;
        const QString oldName = project->entities().first().name;

        // Double click the name to start renaming.
        const QPoint namePoint(20, timeline->rowCenterYForTest(0));
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(namePoint),
                                QPointF(timeline->mapToGlobal(namePoint)), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &doubleClick);
        if (!renameEdit->isVisible())
        {
            qCritical() << "rename-test: rename field not shown";
            ++failures;
        }

        renameEdit->setText(QStringLiteral("新名字"));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(renameEdit, &enter);
        if (project->entity(id)->name != QStringLiteral("新名字"))
        {
            qCritical() << "rename-test: name not applied";
            ++failures;
        }

        // Renaming is undoable.
        undo->trigger();
        if (project->entity(id)->name != oldName)
        {
            qCritical() << "rename-test: undo rename failed";
            ++failures;
        }

        // The title tracks the saved/unsaved state.
        if (!window.windowTitle().contains(QStringLiteral("未保存")))
        {
            qCritical() << "rename-test: title not marked unsaved" << window.windowTitle();
            ++failures;
        }

        // Saving clears the modified flag and updates the title.
        const QString projectPath = QDir::tempPath() + QStringLiteral("/pvforge_rename.pvproj");
        QString error;
        if (!project->save(projectPath, &error) || project->modified)
        {
            qCritical() << "rename-test: modified flag not cleared";
            ++failures;
        }
        if (auto* saveAction = window.findChild<QAction*>(QStringLiteral("saveAction")))
            saveAction->trigger();
        if (!window.windowTitle().contains(QStringLiteral("已保存")))
        {
            qCritical() << "rename-test: title not marked saved" << window.windowTitle();
            ++failures;
        }

        // Closing a modified project asks before discarding.
        project->setValue(id, QStringLiteral("x"), 0, 120, true);
        if (!project->modified)
        {
            qCritical() << "rename-test: modified flag not set";
            ++failures;
        }
        QTimer::singleShot(400, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->accept();
        });
        window.close();
        if (window.isVisible())
        {
            qCritical() << "rename-test: close prompt did not appear";
            ++failures;
        }

        QFile::remove(assetPath);
        QFile::remove(projectPath);
        qInfo() << "rename-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int undoTest(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_undo.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTimer::singleShot(400, &window, [&app, &window, project, assetPath] {
        int failures = 0;
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        if (!undo || !redo)
        {
            qCritical() << "undo-test: actions missing";
            app.exit(1);
            return;
        }

        // Creating an entity is undoable.
        if (window.importImageFile(assetPath) == pvf::kInvalidEntity)
        {
            qCritical() << "undo-test: import failed";
            app.exit(1);
            return;
        }
        if (project->entities().size() != 1)
        {
            qCritical() << "undo-test: entity not created";
            ++failures;
        }

        // Deleting the selected entity is undoable.
        QKeyEvent deleteEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(&window, &deleteEvent);
        if (!project->entities().isEmpty())
        {
            qCritical() << "undo-test: delete failed";
            ++failures;
        }
        undo->trigger();
        if (project->entities().size() != 1)
        {
            qCritical() << "undo-test: undo delete failed";
            ++failures;
        }

        // Undoing again removes the creation.
        undo->trigger();
        if (!project->entities().isEmpty())
        {
            qCritical() << "undo-test: undo create failed";
            ++failures;
        }

        // Redo brings it back.
        redo->trigger();
        if (project->entities().size() != 1)
        {
            qCritical() << "undo-test: redo create failed";
            ++failures;
        }

        // Keyframe removal is undoable on the history level.
        pvf::History history;
        if (project->entities().isEmpty())
        {
            qCritical() << "undo-test: no entity for keyframe test";
            app.exit(1);
            return;
        }
        const pvf::EntityId id = project->entities().first().id;
        project->setValue(id, QStringLiteral("x"), 0, 0, true);
        project->setValue(id, QStringLiteral("x"), 60, 400, true);
        const auto records = project->removeKeyframesAt(id, 60);
        history.push(std::make_unique<pvf::RemoveKeyframesCommand>(records), *project);
        if (!records.isEmpty() && project->value(id, QStringLiteral("x"), 60) == 400.0)
        {
            qCritical() << "undo-test: keyframe not removed";
            ++failures;
        }
        history.undo(*project);
        if (std::abs(project->value(id, QStringLiteral("x"), 60) - 400.0) > 1e-6)
        {
            qCritical() << "undo-test: keyframe not restored";
            ++failures;
        }

        QFile::remove(assetPath);
        qInfo() << "undo-test failures" << failures;
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int deleteTest(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_delete.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    pvf::MainWindow window(project);
    window.show();

    if (window.importImageFile(assetPath) == pvf::kInvalidEntity)
        return 1;
    if (project->entities().size() != 1)
        return 1;

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(&window, &event);

    const bool removed = project->entities().isEmpty();
    int tabCount = 0;
    if (auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs")))
        tabCount = tabs->count();

    QFile::remove(assetPath);
    qInfo() << "delete-test: entities after delete" << project->entities().size()
            << "tabs" << tabCount;
    app.exit(removed && tabCount == 1 ? 0 : 1);
    return 0;
}

int timelineZoomTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 24;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "timeline-zoom-test: timeline not found";
        return 1;
    }

    for (int i = 0; i < 30; ++i)
        project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    // Give the time axis some content so it can actually scroll horizontally.
    if (const pvf::Entity* first = project->entities().isEmpty()
                                        ? nullptr
                                        : &project->entities().first())
        project->setValue(first->id, QStringLiteral("x"), 400, 100.0, true);

    QTimer::singleShot(400, &window, [&app, &window, timeline, project] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_before.png"));
        const double before = timeline->pixelsPerFrame();

        const QPointF pos(500, 120);
        for (int i = 0; i < 3; ++i)
        {
            QWheelEvent event(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(timeline, &event);
        }

        int failures = 0;
        const double after = timeline->pixelsPerFrame();
        if (!(after > before * 4.0))
        {
            qCritical() << "timeline-zoom-test: ctrl wheel did not zoom" << before
                        << after;
            ++failures;
        }
        if (!timeline->verticalScrollVisibleForTest())
        {
            qCritical() << "timeline-zoom-test: vertical scrollbar not visible";
            ++failures;
        }
        const int verticalBefore = timeline->verticalScrollForTest();
        // Bottom-up rows: wheel up grows the upward (bottom-anchored) offset.
        QWheelEvent plain(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(timeline, &plain);
        if (timeline->verticalScrollForTest() <= verticalBefore)
        {
            qCritical() << "timeline-zoom-test: wheel did not scroll rows"
                        << verticalBefore << timeline->verticalScrollForTest();
            ++failures;
        }
        const int horizontalBefore = timeline->horizontalScrollForTest();
        QWheelEvent shifted(pos, timeline->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                            Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(timeline, &shifted);
        if (timeline->horizontalScrollForTest() == horizontalBefore)
        {
            qCritical() << "timeline-zoom-test: shift wheel did not scroll time"
                        << horizontalBefore << timeline->horizontalScrollForTest();
            ++failures;
        }

        QTimer::singleShot(200, &window, [&app, &window, before, after, failures] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_zoom_after.png"));
            qInfo() << "timeline-zoom-test:" << before << "->" << after
                    << "failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    });
    return app.exec();
}

int clipTimelineTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::Entity* group = project->addFolder(QStringLiteral("场景组"));
    const pvf::EntityId groupId = group->id;
    pvf::Entity* rect =
        project->addVector(0, 120, 80, QColor(235, 87, 87), Qt::white, 0);
    rect->name = QStringLiteral("红色矩形");
    const pvf::EntityId rectId = rect->id;
    pvf::Entity* text = project->addText(QStringLiteral("标题文字"),
                                         QStringLiteral("Sans"), 64, Qt::white,
                                         Qt::AlignCenter);
    text->name = QStringLiteral("标题文字");
    const pvf::EntityId textId = text->id;
    project->setEntityParent(rectId, groupId);
    project->setEntityParent(textId, groupId);
    project->entity(groupId)->data[QStringLiteral("clipStart")] = 0;
    project->entity(groupId)->data[QStringLiteral("clipEnd")] = 150;
    project->entity(rectId)->data[QStringLiteral("clipStart")] = 0;
    project->entity(rectId)->data[QStringLiteral("clipEnd")] = 120;
    project->entity(textId)->data[QStringLiteral("clipStart")] = 30;
    project->entity(textId)->data[QStringLiteral("clipEnd")] = 150;

    auto audio = std::make_shared<pvf::AudioClip>();
    audio->duration = 4.0;
    audio->peaks.resize(4000);
    for (int i = 0; i < audio->peaks.size(); ++i)
        audio->peaks[i] =
            0.12f + 0.88f * float(std::abs(std::sin(i * 0.006)));
    pvf::Entity* bgm =
        project->addAudio(QStringLiteral("bgm.wav"), audio, QStringLiteral("BGM"));
    const pvf::EntityId bgmId = bgm->id;
    project->setEntityParent(bgmId, groupId);

    auto* timeline = window.findChild<pvf::TimelinePanel*>();
    if (!timeline)
    {
        qCritical() << "clip-timeline-test: no timeline";
        return 1;
    }
    QTimer::singleShot(300, &window,
                       [&app, &window, timeline, groupId, rectId, textId, bgmId] {
        timeline->setPixelsPerFrameForTest(8.0);
        timeline->setCurrentFrame(45);
        timeline->openPopup(groupId);
        QTimer::singleShot(400, &window,
                           [&app, &window, timeline, groupId, rectId, textId,
                            bgmId] {
            int failures = 0;
            pvf::TimelinePanel* popup = nullptr;
            for (pvf::TimelinePanel* panel :
                 window.findChildren<pvf::TimelinePanel*>())
            {
                if (panel->owner() == groupId)
                {
                    popup = panel;
                    break;
                }
            }
            if (!popup)
            {
                qCritical() << "clip-timeline-test: popup missing";
                app.exit(1);
                return;
            }
            QToolButton* timeMode = nullptr;
            for (QToolButton* button : window.findChildren<QToolButton*>())
            {
                if (button->text() == QStringLiteral("相对"))
                {
                    timeMode = button;
                    break;
                }
            }
            bool relativeSwitched = false;
            if (timeMode)
            {
                timeMode->click();
                relativeSwitched = timeMode->text() == QStringLiteral("绝对");
                timeMode->click();
            }
            if (!relativeSwitched)
                ++failures;
            pvf::Project* project = window.project();
            if (!project)
            {
                qCritical() << "clip-timeline-test: project missing";
                app.exit(1);
                return;
            }
            project->addKeyframe(rectId, QStringLiteral("x"), 110, 0.0);
            const QRect before = popup->clipRectFor(rectId);
            const int initialEnd = project->clipEndFrame(rectId);
            const int row = 0;
            auto send = [popup](QEvent::Type type, const QPoint& pos,
                                Qt::MouseButtons buttons) {
                QMouseEvent event(type, QPointF(pos),
                                  QPointF(popup->mapToGlobal(pos)),
                                  Qt::LeftButton, buttons, Qt::NoModifier);
                QApplication::sendEvent(popup, &event);
            };
            const QPoint from(before.right() - 3,
                              popup->rowCenterYForTest(row));
            const QPoint to(from.x() - 30 * 8, from.y());
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
            QApplication::processEvents();
            const int clippedEnd = project->clipEndFrame(rectId);
            const bool keyGone = project->keyframe(rectId, QStringLiteral("x"),
                                                   110.0)
                                 == nullptr;
            const QRect clipped = popup->clipRectFor(rectId);
            const QPoint expandFrom(clipped.right() - 3,
                                    popup->rowCenterYForTest(row));
            const QPoint expandTo(expandFrom.x() + 30 * 8, expandFrom.y());
            send(QEvent::MouseButtonPress, expandFrom, Qt::LeftButton);
            send(QEvent::MouseMove, expandTo, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, expandTo, Qt::NoButton);
            QApplication::processEvents();
            const int expandedEnd = project->clipEndFrame(rectId);
            const bool keyStayedGone =
                project->keyframe(rectId, QStringLiteral("x"), 110.0) == nullptr;
            int undoEnd1 = -1;
            int undoEnd2 = -1;
            bool keyRestored = false;
            for (QAction* action : window.findChildren<QAction*>())
            {
                if (!action->text().contains(QStringLiteral("撤销")))
                    continue;
                action->trigger();
                undoEnd1 = project->clipEndFrame(rectId);
                action->trigger();
                undoEnd2 = project->clipEndFrame(rectId);
                keyRestored =
                    project->keyframe(rectId, QStringLiteral("x"), 110.0) != nullptr;
                break;
            }
            if (clippedEnd >= 110 || !keyGone || expandedEnd <= clippedEnd
                || !keyStayedGone || undoEnd1 != clippedEnd
                || undoEnd2 != initialEnd || !keyRestored)
            {
                qCritical() << "clip-timeline-test: trim" << before << clippedEnd
                            << expandedEnd << keyGone << keyStayedGone << "undo"
                            << undoEnd1 << undoEnd2 << keyRestored;
                ++failures;
            }

            // Clicking a keyframe must feed the property panel's existing
            // easing button (the previous milestone's keyframe page).
            auto* modeButton =
                timeline->findChild<QToolButton*>(QStringLiteral("timelineModeButton"));
            bool modeSwitched = false;
            if (modeButton)
            {
                modeButton->click();
                modeSwitched =
                    modeButton->text() == QStringLiteral("关键帧")
                    && popup->editMode()
                           == pvf::TimelinePanel::EditMode::Keyframes;
            }
            if (!modeSwitched)
            {
                qCritical() << "clip-timeline-test: mode switch failed";
                ++failures;
            }
            project->addKeyframe(rectId, QStringLiteral("x"), 0, 0.0);
            project->addKeyframe(rectId, QStringLiteral("x"), 60, 0.0);
            QApplication::processEvents();
            const QPoint keyPoint(popup->frameXForTest(60),
                                  popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonPress, keyPoint, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, keyPoint, Qt::NoButton);
            QApplication::processEvents();
            auto* easeButton =
                window.findChild<QPushButton*>(QStringLiteral("keyframeEasingButton"));
            const bool easingReady =
                easeButton && easeButton->isEnabled()
                && easeButton->text().contains(QStringLiteral("缓动"));
            if (window.currentFrame() != 60 || !easingReady)
            {
                qCritical() << "clip-timeline-test: easing" << window.currentFrame()
                            << (easeButton ? easeButton->text() : QStringLiteral("none"))
                            << (easeButton && easeButton->isEnabled());
                ++failures;
            }

            // Drag a selected keyframe to a new frame and verify the track
            // data, not just the painted diamond.
            const QPoint keyFrom(popup->frameXForTest(60),
                                 popup->rowCenterYForTest(0));
            const QPoint keyTo(popup->frameXForTest(75),
                               popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonPress, keyFrom, Qt::LeftButton);
            send(QEvent::MouseMove, keyTo, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, keyTo, Qt::NoButton);
            QApplication::processEvents();
            const bool keyMoved = project->keyframe(rectId, QStringLiteral("x"),
                                                    75.0)
                                  != nullptr
                              && project->keyframe(rectId, QStringLiteral("x"),
                                                   60.0)
                                     == nullptr;
            if (!keyMoved)
            {
                qCritical() << "clip-timeline-test: keyframe drag failed";
                ++failures;
            }

            // Double-click inside a clip must create keys at that frame.
            const QPoint dblPoint(popup->frameXForTest(90),
                                  popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonDblClick, dblPoint, Qt::LeftButton);
            QApplication::processEvents();
            const bool keyAdded =
                project->keyframe(rectId, QStringLiteral("x"), 90.0) != nullptr;
            if (!keyAdded)
            {
                qCritical() << "clip-timeline-test: double-click key add failed";
                ++failures;
            }

            // Rubber-band select across rows and frames.
            const QPoint boxStart(popup->frameXForTest(130),
                                  popup->rowCenterYForTest(0));
            const QPoint boxEnd(popup->frameXForTest(0),
                                popup->rowCenterYForTest(2));
            send(QEvent::MouseButtonPress, boxStart, Qt::LeftButton);
            send(QEvent::MouseMove, boxEnd, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, boxEnd, Qt::NoButton);
            QApplication::processEvents();
            const int boxed = popup->selectedKeyframeCount();
            if (boxed < 3)
            {
                qCritical() << "clip-timeline-test: box select" << boxed;
                ++failures;
            }

            // Delete row 0 and verify rows 1/2 keep their positions.
            const QPoint redBody(popup->frameXForTest(30),
                                 popup->rowCenterYForTest(0));
            send(QEvent::MouseButtonPress, redBody, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, redBody, Qt::NoButton);
            QKeyEvent rowDelete(QEvent::KeyPress, Qt::Key_Delete,
                                Qt::NoModifier);
            QApplication::sendEvent(popup, &rowDelete);
            QApplication::processEvents();
            if (project->entity(rectId)
                || popup->rowEntityForTest(0) != pvf::kInvalidEntity
                || popup->rowEntityForTest(1) != textId
                || popup->rowEntityForTest(2) != bgmId)
            {
                qCritical() << "clip-timeline-test: stable rows"
                            << popup->rowEntityForTest(0)
                            << popup->rowEntityForTest(1)
                            << popup->rowEntityForTest(2) << textId << bgmId;
                ++failures;
            }

            // Dragging the playhead past the right edge must keep scrolling
            // and keep the line inside the visible timeline.
            auto sendMain = [timeline](QEvent::Type type, const QPoint& pos,
                                       Qt::MouseButtons buttons) {
                QMouseEvent event(type, QPointF(pos),
                                  QPointF(timeline->mapToGlobal(pos)),
                                  Qt::LeftButton, buttons, Qt::NoModifier);
                QApplication::sendEvent(timeline, &event);
            };
            const int beforeScroll = timeline->horizontalScrollForTest();
            const int beforeFrame = window.currentFrame();
            const QPoint scrubStart(20, 10);
            const QPoint scrubEnd(timeline->width() + 80, 10);
            sendMain(QEvent::MouseButtonPress, scrubStart, Qt::LeftButton);
            sendMain(QEvent::MouseMove, scrubEnd, Qt::LeftButton);
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 400)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            sendMain(QEvent::MouseButtonRelease, scrubEnd, Qt::NoButton);
            QApplication::processEvents();
            const int playheadX = timeline->frameXForTest(window.currentFrame());
            if (window.currentFrame() <= beforeFrame || playheadX < 0
                || playheadX >= timeline->width())
            {
                qCritical() << "clip-timeline-test: scrub" << beforeScroll
                            << timeline->horizontalScrollForTest() << beforeFrame
                            << window.currentFrame() << playheadX
                            << timeline->width();
                ++failures;
            }

            // The private timeline must hide the playhead when the global
            // frame is outside the parent clip range.
            timeline->setCurrentFrame(200);
            QApplication::processEvents();
            const bool popupHidden = !popup->playheadVisibleForTest();
            timeline->setCurrentFrame(60);
            QApplication::processEvents();
            const bool popupShown = popup->playheadVisibleForTest();
            if (!popupHidden || !popupShown)
            {
                qCritical() << "clip-timeline-test: popup playhead"
                            << popupHidden << popupShown;
                ++failures;
            }

            // With the parent clip start scrolled off the left edge, the
            // popup must scroll to the same global frame as the main axis.
            timeline->setHorizontalScrollForTest(320);
            QApplication::processEvents();
            const int popupAxisX =
                popup->mapToGlobal(QPoint(popup->frameXForTest(75), 0)).x();
            const int mainAxisX =
                timeline->mapToGlobal(QPoint(timeline->frameXForTest(75), 0)).x();
            if (std::abs(popupAxisX - mainAxisX) > 2)
            {
                qCritical() << "clip-timeline-test: popup axis"
                            << popupAxisX << mainAxisX;
                ++failures;
            }
            auto* assetList =
                window.findChild<QListWidget*>(QStringLiteral("assetList"));
            auto* viewToggle =
                window.findChild<QToolButton*>(QStringLiteral("assetViewToggle"));
            if (!assetList || !viewToggle)
            {
                qCritical() << "clip-timeline-test: asset widgets missing";
                ++failures;
            }
            else
            {
                viewToggle->click();
                const bool iconMode =
                    assetList->viewMode() == QListView::IconMode;
                viewToggle->click();
                const bool listMode =
                    assetList->viewMode() == QListView::ListMode;
                QApplication::processEvents();
                assetList->setCurrentRow(0);
                const QRect itemRect = assetList->visualItemRect(assetList->item(0));
                const QPoint hoverPos = itemRect.center();
                QMouseEvent hover(
                    QEvent::MouseMove, QPointF(hoverPos),
                    QPointF(assetList->viewport()->mapToGlobal(hoverPos)),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(assetList->viewport(), &hover);
                QApplication::processEvents();
                auto* hoverPreview = window.findChild<QWidget*>(
                    QStringLiteral("assetHoverPreview"));
                const bool hoverVisible = hoverPreview && hoverPreview->isVisible();
                if (!iconMode || !listMode || !hoverVisible)
                {
                    qCritical() << "clip-timeline-test: asset view"
                                << iconMode << listMode << hoverVisible;
                    ++failures;
                }
            }

            auto* unitButton =
                timeline->findChild<QToolButton*>(QStringLiteral("timelineUnitButton"));
            if (unitButton)
            {
                unitButton->click();
                const bool framesOn = timeline->displayFrames();
                unitButton->click();
                const bool framesOff = !timeline->displayFrames();
                if (!framesOn || !framesOff)
                {
                    qCritical() << "clip-timeline-test: unit toggle" << framesOn
                                << framesOff;
                    ++failures;
                }
            }

            // Drag a library template onto the timeline and delete the new
            // clip with Del (right-click uses the same delete signal).
            const int beforeCount = int(project->entities().size());
            QMimeData dropMime;
            dropMime.setData(QStringLiteral("application/x-pvforge-asset-template"),
                             QByteArray::number(0));
            const pvf::EntityId droppedId =
                timeline->dropAssetForTest(dropMime, 30, 3);
            QApplication::processEvents();
            const bool dropped =
                int(project->entities().size()) == beforeCount + 1
                && droppedId != pvf::kInvalidEntity
                && project->clipStartFrame(droppedId) == 30
                && timeline->rowForEntityForTest(droppedId) == 3
                && timeline->rowEntityForTest(0) == groupId;
            if (!dropped)
            {
                qCritical() << "clip-timeline-test: asset drop"
                            << beforeCount << project->entities().size()
                            << timeline->rowForEntityForTest(droppedId)
                            << timeline->rowEntityForTest(0) << groupId;
                ++failures;
            }
            else
            {
                const QPoint clipPoint(
                    timeline->frameXForTest(45),
                    timeline->rowCenterYForTest(3));
                sendMain(QEvent::MouseButtonPress, clipPoint, Qt::LeftButton);
                sendMain(QEvent::MouseButtonRelease, clipPoint, Qt::NoButton);
                QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete,
                                    Qt::NoModifier);
                QApplication::sendEvent(timeline, &deleteKey);
                QApplication::processEvents();
                if (project->entity(droppedId)
                    || timeline->rowEntityForTest(0) != groupId)
                {
                    qCritical() << "clip-timeline-test: delete clip"
                                << timeline->rowEntityForTest(0) << groupId;
                    ++failures;
                }
            }

            window.grab().toImage().save(
                QStringLiteral("/tmp/pvforge_clip_timeline.png"));
            qInfo() << "clip-timeline-test: popup panels"
                    << window.findChildren<pvf::TimelinePanel*>().size()
                    << "trim" << clippedEnd << "expand" << expandedEnd
                    << "key gone" << keyGone << "failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    });
    return app.exec();
}

int openTimelineTest(QApplication& app){
    auto* project = new pvf::Project;
    project->fps = 60;
    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    window.goToFrame(0);
    window.togglePlayback();

    QTimer::singleShot(2500, &window, [&app, &window] {
        const int frame = window.currentFrame();
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_open_timeline.png"));
        qInfo() << "open-timeline-test: frame after 2.5s =" << frame;
        app.exit(frame > 130 ? 0 : 1);
    });
    return app.exec();
}

int libraryTest(QApplication& app)
{
    const QString assetPath =
        QDir::tempPath() + QStringLiteral("/pvforge_library_asset_")
        + QString(80, QLatin1Char('x')) + QStringLiteral(".png");
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(QColor(70, 130, 200));
    {
        QPainter p(&img);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPointSize(28);
        f.setBold(true);
        p.setFont(f);
        p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("ASSET"));
    }
    if (!img.save(assetPath))
        return 1;

    auto* project = new pvf::Project;
    project->name = QStringLiteral("素材库测试");
    pvf::MainWindow window(project);
    window.resize(1440, 900);
    window.show();

    project->addImage(assetPath);
    project->addText(QStringLiteral("素材库文字"), QFont().family(), 96, Qt::white,
                     int(Qt::AlignCenter));
    project->addVector(1, 220, 220, QColor(255, 120, 160), QColor(255, 255, 255), 6);

    for (auto* section : window.findChildren<pvf::CollapsibleSection*>())
        if (section->title() == QStringLiteral("素材库"))
            section->setExpanded(true);
    qInfo() << "library-test: templates" << project->templates().size()
            << "entities" << project->entities().size();

    QTimer::singleShot(400, &window, [&app, &window, project, assetPath] {
        bool layoutOk = true;
        if (auto* list = window.findChild<QListWidget*>(QStringLiteral("assetList")))
            qInfo() << "library-test: list items" << list->count();
        if (auto* scroll = window.findChild<QScrollArea*>(QStringLiteral("projectScroll")))
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs"));
        auto* panel = window.findChild<QWidget*>(QStringLiteral("timelineAssetPanel"));
        auto* list = window.findChild<QListWidget*>(QStringLiteral("assetList"));
        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* moved = window.findChild<QLabel*>(QStringLiteral("assetMovedLabel"));
        if (!tabs || !panel || !list || !timeline || !moved)
        {
            qCritical() << "library-test: asset widgets missing";
            layoutOk = false;
        }
        else
        {
            const QPoint panelPos = panel->mapTo(&window, QPoint(0, 0));
            const QPoint timelinePos = timeline->mapTo(&window, QPoint(0, 0));
            qInfo() << "library-test: widths tabs" << tabs->width()
                    << "panel" << panel->width() << "panel min"
                    << panel->minimumSizeHint().width();
            layoutOk = !tabs->isAncestorOf(panel)
                       && panel->width() >= 140 && panel->width() <= 260
                       && panelPos.x() < timelinePos.x()
                       && list->count() > 0
                       && !list->item(0)->icon().isNull();
            if (!layoutOk)
                qCritical() << "library-test: asset bin is not left of timeline"
                            << panel->width() << panelPos << timelinePos;
            if (!moved->isVisible())
                qCritical() << "library-test: project tab signpost missing";
        }
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_library.png"));

        auto* create = window.findChild<QPushButton*>(QStringLiteral("assetCreate"));
        auto* remove = window.findChild<QPushButton*>(QStringLiteral("assetRemove"));
        if (!create || !remove || !create->isVisible() || !remove->isVisible())
        {
            qCritical() << "library-test: create/remove button missing or hidden"
                        << (create ? create->isVisible() : false)
                        << (remove ? remove->isVisible() : false);
            app.exit(1);
            return;
        }

        const int before = project->entities().size();
        create->click();

        QTimer::singleShot(300, &window, [&app, &window, project, before, assetPath, layoutOk] {
            const int after = project->entities().size();
            qInfo() << "library-test: entities" << before << "->" << after;
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_entity_tab.png"));

            if (auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs")))
                tabs->setCurrentIndex(0);
            QTimer::singleShot(250, &window, [&app, &window, after, before, assetPath, layoutOk] {
                window.grab().toImage().save(
                    QStringLiteral("/tmp/pvforge_projectprops.png"));
                QFile::remove(assetPath);
                app.exit(after == before + 1 && layoutOk ? 0 : 1);
            });
        });
    });
    return app.exec();
}

} // namespace

namespace {

int specialDemo(QApplication& app)
{
    auto* project = new pvf::Project;
    project->name = QStringLiteral("特殊工具演示");
    project->width = 1920;
    project->height = 1080;
    project->background = QColor(16, 16, 22);

    const auto place = [](pvf::Entity* entity, double x, double y) {
        if (!entity)
            return;
        entity->values[QStringLiteral("x")] = x;
        entity->values[QStringLiteral("y")] = y;
    };

    // Press conference brand wall backdrop.
    const QString wallLogoA =
        QDir::tempPath() + QStringLiteral("/pvf_demo_wall_a.png");
    const QString wallLogoB =
        QDir::tempPath() + QStringLiteral("/pvf_demo_wall_b.png");
    {
        QImage a(128, 128, QImage::Format_ARGB32);
        a.fill(Qt::transparent);
        {
            QPainter p(&a);
            p.setRenderHint(QPainter::Antialiasing, true);
            QFont font = p.font();
            font.setPixelSize(66);
            font.setBold(true);
            p.setFont(font);
            p.setPen(QColor(96, 96, 108));
            p.drawText(a.rect(), Qt::AlignCenter, QStringLiteral("PV"));
        }
        a.save(wallLogoA);

        QImage b(128, 128, QImage::Format_ARGB32);
        b.fill(Qt::transparent);
        {
            QPainter p(&b);
            p.setRenderHint(QPainter::Antialiasing, true);
            QPolygonF star;
            for (int i = 0; i < 10; ++i)
            {
                const double angle = -M_PI / 2 + i * M_PI / 5;
                const double radius = (i % 2 == 0) ? 52.0 : 22.0;
                star << QPointF(64 + std::cos(angle) * radius,
                                64 + std::sin(angle) * radius);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(96, 96, 108));
            p.drawPolygon(star);
        }
        b.save(wallLogoB);
    }
    pvf::BrandWallParams wallParams;
    wallParams.width = project->width;
    wallParams.height = project->height;
    wallParams.columns = 6;
    wallParams.rows = 3;
    wallParams.logoOpacity = 0.55;
    wallParams.logos = QStringList{wallLogoA, wallLogoB};
    wallParams.tileColors =
        QStringList{QColor(248, 248, 250).name(QColor::HexArgb),
                    QColor(232, 232, 238).name(QColor::HexArgb)};
    // Background motion streaks.
    pvf::FlowParams flow;
    flow.count = 34;
    flow.length = 620;
    flow.spacing = 72;
    flow.thickness = 9;
    flow.area = 2400;
    flow.speed = 620;
    flow.color = QColor(120, 140, 180, 190);
    QVariantMap flowData;
    flow.writeTo(flowData);
    place(project->addEntity(pvf::EntityType::FlowLines,
                             QStringLiteral("快速流过"), flowData),
          0, 0);

    // Manga screentone shading over the wall.
    pvf::HalftoneParams halftone;
    halftone.area = 760;
    halftone.spacing = 18;
    halftone.dotSize = 8;
    halftone.angle = 45;
    halftone.gradientAngle = 135;
    halftone.minRatio = 0.0;
    halftone.jitter = 0.12;
    halftone.color = QColor(24, 24, 32, 170);
    QVariantMap halftoneData;
    halftone.writeTo(halftoneData);
    place(project->addEntity(pvf::EntityType::HalftoneDots,
                             QStringLiteral("漫画点阵"), halftoneData),
          600, 330);

    // Focus rays behind the balloons.
    pvf::RadialParams radial;
    radial.count = 56;
    radial.radius = 560;
    radial.width = 22;
    radial.color = QColor(255, 208, 96);
    radial.spin = 4.0;
    QVariantMap radialData;
    radial.writeTo(radialData);
    place(project->addEntity(pvf::EntityType::RadialLines,
                             QStringLiteral("放射状线条"), radialData),
          0, 0);

    // Comic balloons.
    const auto balloon = [&](double x, double y, const QString& text, int size,
                             int shape, double tipX, double tipY,
                             const QColor& fill) {
        pvf::TextStyle style;
        style.text = text;
        style.pixelSize = size;
        style.vertical = shape == 1;
        style.color = fill.lightness() > 140 ? QColor(28, 28, 34) : QColor(255, 255, 250);
        style.padding = 24.0;
        style.align = int(Qt::AlignCenter);
        style.lineHeight = 1.1;
        pvf::BubbleStyle bubble;
        bubble.shape = shape;
        bubble.tail = shape == 0 || shape == 2;
        bubble.fill = fill;
        bubble.stroke = QColor(24, 22, 28);
        bubble.strokeWidth = 6;
        bubble.tipX = tipX;
        bubble.tipY = tipY;
        QVariantMap data;
        style.writeTo(data);
        bubble.writeTo(data);
        place(project->addEntity(pvf::EntityType::ComicText,
                                 QStringLiteral("漫画文本框"), data),
              x, y);
    };

    balloon(-560, -190, QStringLiteral("欢迎来到\nPVForge"), 66, 0, 0.25, 1.5,
            QColor(255, 255, 255));
    balloon(-560, 290, QStringLiteral("云朵气泡"), 58, 2, -0.2, 1.4,
            QColor(255, 255, 255));
    balloon(620, -200, QStringLiteral("日式竖排\n从右到左"), 58, 1, 0, 1.3,
            QColor(255, 255, 255));
    balloon(0, 300, QStringLiteral("爆炸式！"), 66, 3, 0, 1.3,
            QColor(255, 246, 210));

    pvf::TextStyle title;
    title.text = QStringLiteral("特殊工具");
    title.pixelSize = 104;
    title.color = QColor(255, 255, 255);
    title.strokeColor = QColor(24, 22, 28);
    title.strokeWidth = 5;
    title.padding = 8;
    QVariantMap titleData;
    title.writeTo(titleData);
    place(project->addEntity(pvf::EntityType::Text,
                             QStringLiteral("文字"), titleData),
          -650, -450);

    // Created last so it sits on the bottom row of the layer list and stays
    // behind the other tools.
    project->addBrandWall(wallParams);

    auto* window = new pvf::MainWindow(project);
    window->resize(1440, 900);
    window->show();
    if (auto* viewport = window->findChild<pvf::Viewport*>())
    {
        viewport->resetView();
        qInfo() << "special-demo: viewport" << viewport << "size" << viewport->size();
    }
    QTimer::singleShot(900, window, [window] {
        window->grab().toImage().save(
            QStringLiteral("/tmp/pvforge_special_demo.png"));
    });
    return app.exec();
}

int renderDemo(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_demo_asset.png");

    QImage asset(400, 400, QImage::Format_ARGB32);
    asset.fill(Qt::transparent);
    {
        QPainter p(&asset);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 170, 255));
        p.drawRoundedRect(20, 20, 360, 360, 48, 48);
        p.setBrush(QColor(255, 255, 255));
        p.drawEllipse(120, 90, 160, 160);
        p.setBrush(QColor(20, 30, 50));
        p.drawEllipse(160, 130, 80, 80);
        p.setPen(QPen(QColor(255, 255, 255), 6));
        QFont f = p.font();
        f.setPointSize(36);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 280, 400, 80), Qt::AlignCenter, QStringLiteral("PV"));
    }
    if (!asset.save(assetPath))
    {
        qCritical() << "demo: failed to save asset";
        return 1;
    }

    auto* testProject = new pvf::Project;
    pvf::MainWindow window(testProject);
    window.resize(1440, 900);
    window.show();

    const pvf::EntityId id = window.importImageFile(assetPath);
    if (id == pvf::kInvalidEntity)
    {
        qCritical() << "demo: import failed";
        return 1;
    }

    auto* project = window.project();
    project->setValue(id, QStringLiteral("x"), 0, -500, true);
    project->setValue(id, QStringLiteral("x"), 30, 500, true);
    project->setValue(id, QStringLiteral("rotation"), 0, -20, true);
    project->setValue(id, QStringLiteral("rotation"), 30, 20, true);
    project->setValue(id, QStringLiteral("opacity"), 0, 0.25, true);
    project->setValue(id, QStringLiteral("opacity"), 30, 1.0, true);

    QTimer::singleShot(400, &window, [&app, &window] {
        window.goToFrame(0);
        const QImage f0 = window.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        window.goToFrame(15);
        const QImage f15 = window.grab().toImage().convertToFormat(QImage::Format_ARGB32);

        f0.save(QStringLiteral("/tmp/pvforge_demo_f0.png"));
        f15.save(QStringLiteral("/tmp/pvforge_demo_f15.png"));

        int diff = 0;
        if (f0.size() == f15.size())
        {
            for (int y = 0; y < f0.height(); ++y)
            {
                const auto* a = reinterpret_cast<const QRgb*>(f0.constScanLine(y));
                const auto* b = reinterpret_cast<const QRgb*>(f15.constScanLine(y));
                for (int x = 0; x < f0.width(); ++x)
                {
                    if (std::abs(qRed(a[x]) - qRed(b[x])) + std::abs(qGreen(a[x]) - qGreen(b[x]))
                            + std::abs(qBlue(a[x]) - qBlue(b[x]))
                        > 40)
                        ++diff;
                }
            }
        }

        qInfo() << "demo: differing pixels between frame 0 and 15:" << diff;
        app.exit(diff > 1000 ? 0 : 1);
    });

    return app.exec();
}

} // namespace

namespace {

int playTest(QApplication& app)
{
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString path = QDir::tempPath() + QStringLiteral("/pvforge_playtest.png");
    if (!img.save(path))
        return 1;

    auto* testProject = new pvf::Project;
    pvf::MainWindow window(testProject);
    window.resize(1200, 800);
    window.show();

    const pvf::EntityId id = window.importImageFile(path);
    if (id == pvf::kInvalidEntity)
        return 1;
    window.project()->setValue(id, QStringLiteral("x"), 0, 0, true);
    window.project()->setValue(id, QStringLiteral("x"), 60, 600, true);

    window.goToFrame(0);
    window.togglePlayback();

    QTimer::singleShot(600, &window, [&app, &window] {
        const int frame = window.currentFrame();
        qInfo() << "play-test: frame after 600ms =" << frame;
        app.exit(frame >= 10 ? 0 : 1);
    });
    return app.exec();
}

int layoutTest(QApplication& app)
{
    auto* testProject = new pvf::Project;
    testProject->fps = 24;
    pvf::Entity* entity =
        testProject->addVector(0, 120, 120, Qt::red, Qt::white, 0);
    const pvf::EntityId entityId = entity->id;
    pvf::MainWindow window(testProject);
    window.resize(480, 1060);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window, testProject, entityId] {
        int failures = 0;
        auto* objectTab =
            window.findChild<QScrollArea*>(QStringLiteral("objectTab"));
        if (!objectTab || objectTab->isVisible())
        {
            qCritical() << "layout-test: object tab visible at startup"
                        << (objectTab ? objectTab->isVisible() : false);
            ++failures;
        }
        window.setSelectedEntityForTest(entityId);
        QApplication::processEvents();
        if (!objectTab || !objectTab->isVisible())
        {
            qCritical() << "layout-test: object tab did not appear";
            ++failures;
        }
        window.setSelectedEntityForTest(pvf::kInvalidEntity);
        QApplication::processEvents();
        if (objectTab && objectTab->isVisible())
        {
            qCritical() << "layout-test: object tab did not hide";
            ++failures;
        }

        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_layout_narrow.png"));
        qInfo() << "layout-test: size" << window.size() << "minimum" << window.minimumSize();

        // Restore the v0.2.0 vertical ratio and check the library/buttons.
        window.resize(1440, 900);
        QApplication::processEvents();

        auto* timeline = window.findChild<pvf::TimelinePanel*>();
        auto* viewport = window.findChild<pvf::Viewport*>();
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("rightTabs"));
        auto* panel =
            window.findChild<QWidget*>(QStringLiteral("timelineAssetPanel"));
        auto* split =
            window.findChild<QSplitter*>(QStringLiteral("timelineAssetSplit"));
        auto* create = window.findChild<QPushButton*>(QStringLiteral("assetCreate"));
        auto* remove = window.findChild<QPushButton*>(QStringLiteral("assetRemove"));
        if (!timeline || !viewport || !tabs || !panel || !split)
        {
            qCritical() << "layout-test: layout widgets missing";
            ++failures;
        }
        else
        {
            qInfo() << "layout-test: 1440x900 timeline" << timeline->geometry()
                    << "asset" << panel->geometry()
                    << "viewport" << viewport->geometry()
                    << "rightTabs" << tabs->width();
            if (timeline->height() < 190 || timeline->height() > 260)
            {
                qCritical() << "layout-test: timeline height is not about 220"
                            << timeline->height();
                ++failures;
            }
            const QPoint panelPos = panel->mapTo(&window, QPoint(0, 0));
            const QPoint timelinePos = timeline->mapTo(&window, QPoint(0, 0));
            if (panelPos.x() >= timelinePos.x()
                || panel->width() < 140 || panel->width() > 260)
            {
                qCritical() << "layout-test: asset bin is not left of timeline"
                            << panel->width() << panelPos << timelinePos;
                ++failures;
            }
            if (timeline->width() < window.centralWidget()->width() * 0.7)
            {
                qCritical() << "layout-test: timeline lost too much width"
                            << timeline->width() << window.centralWidget()->width();
                ++failures;
            }
            if (tabs->isAncestorOf(panel))
            {
                qCritical() << "layout-test: asset panel is still in the right dock";
                ++failures;
            }
            if (!create || !remove || !create->isVisible() || !remove->isVisible())
            {
                qCritical() << "layout-test: asset create/remove buttons hidden";
                ++failures;
            }
        }

        // The 640x560 workbench must stay above the timeline and keep its
        // create button visible, even when the viewport shrinks.
        auto* wbButton =
            window.findChild<QToolButton*>(QStringLiteral("workbenchButton"));
        auto* workbench = window.findChild<pvf::WorkbenchPanel*>();
        const auto settle = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };
        if (wbButton && workbench)
        {
            wbButton->click();
            settle(300);
            auto* wbCreate =
                workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
            if (!workbench->isVisible() || !wbCreate || !wbCreate->isVisible())
            {
                qCritical() << "layout-test: workbench create button not visible";
                ++failures;
            }
            else
            {
                QApplication::processEvents();
                if (auto* scroll = window.findChild<QScrollArea*>(
                        QStringLiteral("projectScroll")))
                    scroll->verticalScrollBar()->setValue(
                        scroll->verticalScrollBar()->maximum());
                QApplication::processEvents();
                window.grab().toImage().save(
                    QStringLiteral("/tmp/opencode/pvforge-layout-assets.png"));
                const int createBottom =
                    wbCreate->mapTo(&window, QPoint(0, wbCreate->height())).y();
                const int timelineTop =
                    timeline->mapTo(&window, QPoint(0, 0)).y();
                if (createBottom >= timelineTop)
                {
                    qCritical() << "layout-test: workbench create button is clipped"
                                << createBottom << timelineTop;
                    ++failures;
                }
            }

            window.resize(1000, 620);
            QApplication::processEvents();
            workbench->showAt(QPoint(8, 8));
            settle(250);
            window.grab().toImage().save(
                QStringLiteral("/tmp/opencode/pvforge-layout-small.png"));
            auto* smallCreate =
                workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
            if (!workbench->isVisible() || !smallCreate
                || !smallCreate->isVisible())
            {
                qCritical() << "layout-test: small viewport hides the workbench";
                ++failures;
            }
            else if (smallCreate->mapTo(&window, QPoint(0, smallCreate->height())).y()
                     >= timeline->mapTo(&window, QPoint(0, 0)).y())
            {
                qCritical() << "layout-test: small viewport clips the create button";
                ++failures;
            }
            workbench->hidePanel();
        }
        app.exit(failures == 0 ? 0 : 1);
    });
    return app.exec();
}

int workbenchTest(QApplication& app)
{
    QString error;
    const QImage videoFrame =
        pvf::decodeVideoFirstFrame(QStringLiteral("/tmp/pvf_test.mp4"), &error);
    if (videoFrame.isNull())
    {
        qCritical() << "workbench-test: video decode failed:" << error;
        return 1;
    }
    qInfo() << "workbench-test: video first frame" << videoFrame.size();

    auto* testProject = new pvf::Project;
    pvf::MainWindow window(testProject);
    window.resize(1440, 900);
    window.show();

    auto* project = window.project();

    pvf::Entity* circle = project->addVector(
        1, 220, 220, QColor(255, 120, 160, 200), QColor(255, 255, 255), 8);
    project->setValue(circle->id, QStringLiteral("x"), 0, 300, false);
    project->setValue(circle->id, QStringLiteral("y"), 0, -200, false);

    pvf::Entity* video = project->addVideo(QStringLiteral("/tmp/pvf_test.mp4"), videoFrame);
    project->setValue(video->id, QStringLiteral("x"), 0, -350, false);
    project->setValue(video->id, QStringLiteral("y"), 0, -200, false);
    project->setValue(video->id, QStringLiteral("scaleX"), 0, 0.6, false);
    project->setValue(video->id, QStringLiteral("scaleY"), 0, 0.6, false);

    auto* button = window.findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    auto* workbench = window.findChild<pvf::WorkbenchPanel*>();
    if (!button || !workbench)
    {
        qCritical() << "workbench-test: UI objects not found";
        return 1;
    }
    button->click();

    QTimer::singleShot(450, &window, [&app, &window, workbench, button] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_workbench.png"));

        auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
        auto* content = workbench->findChild<QLineEdit*>(QStringLiteral("textContent"));
        auto* create = workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
        if (!types || !content || !create)
        {
            qCritical() << "workbench-test: form widgets not found";
            app.exit(1);
            return;
        }

        types->setCurrentRow(1);
        content->setText(QStringLiteral("工作台创建的文字"));
        create->click();

        QTimer::singleShot(350, &window, [&app, &window, button, workbench] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_types.png"));
            const int count = window.project()->entities().size();
            qInfo() << "workbench-test: entities after text create =" << count;

            button->click();
            auto* typeList =
                workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
            typeList->setCurrentRow(3);   // vector page

            QTimer::singleShot(300, &window, [&app, &window, workbench, count,
                                              typeList] {
                window.grab().toImage().save(
                    QStringLiteral("/tmp/pvforge_workbench_vector.png"));
                auto* createButton =
                    workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
                createButton->click();

                QTimer::singleShot(250, &window, [&app, &window, count] {
                    window.grab().toImage().save(QStringLiteral("/tmp/pvforge_vector.png"));
                    const int after = window.project()->entities().size();
                    qInfo() << "workbench-test: entities after vector create =" << after;
                    app.exit(after == count + 1 ? 0 : 1);
                });
            });
        });
    });

    return app.exec();
}

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

struct McpHttpResponse
{
    int status = -1;
    int expectedBody = 0;
    QByteArray headers;
    QByteArray body;
    bool timedOut = false;
};

McpHttpResponse mcpExchange(quint16 port, const QByteArray& request,
                            int timeoutMs = 5000)
{
    McpHttpResponse response;
    QTcpSocket socket;
    QByteArray received;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&timeout, &QTimer::timeout, &loop, [&response, &loop] {
        response.timedOut = true;
        loop.quit();
    });
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] {
        socket.write(request);
        socket.flush();
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] {
        received += socket.readAll();
        const int headerEnd = received.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        if (response.status < 0)
        {
            const QByteArray head = received.left(headerEnd);
            response.headers = head;
            const QList<QByteArray> statusLine =
                head.split('\n').first().trimmed().split(' ');
            if (statusLine.size() >= 2)
                response.status = statusLine.at(1).toInt();
            for (const QByteArray& rawLine : head.split('\n'))
            {
                QByteArray line = rawLine;
                if (line.endsWith('\r'))
                    line.chop(1);
                const int colon = line.indexOf(':');
                if (colon > 0
                    && line.left(colon).trimmed().toLower()
                           == QByteArrayLiteral("content-length"))
                    response.expectedBody =
                        line.mid(colon + 1).trimmed().toInt();
            }
        }
        if (received.size() >= headerEnd + 4 + response.expectedBody)
            loop.quit();
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop,
                     [&loop](QAbstractSocket::SocketError) { loop.quit(); });

    timeout.start(timeoutMs);
    socket.connectToHost(QHostAddress::LocalHost, port);
    loop.exec();

    const int headerEnd = received.indexOf("\r\n\r\n");
    if (headerEnd >= 0)
        response.body = received.mid(headerEnd + 4);
    socket.abort();
    return response;
}

QByteArray mcpPostRequest(quint16 port, const QByteArray& payload,
                          const QByteArray& token, const QByteArray& contentType,
                          qint64 declaredLength, const QByteArray& extraHeaders,
                          const QByteArray& hostHeader)
{
    QByteArray request;
    request += "POST /mcp HTTP/1.1\r\n";
    request += "Host: "
               + (hostHeader.isEmpty()
                      ? QByteArrayLiteral("127.0.0.1:")
                            + QByteArray::number(port)
                      : hostHeader)
               + "\r\n";
    request += "Authorization: Bearer " + token + "\r\n";
    request += "Content-Type: " + contentType + "\r\n";
    request += "Accept: application/json\r\n";
    request += "Content-Length: "
               + QByteArray::number(declaredLength >= 0 ? declaredLength
                                                        : qint64(payload.size()))
               + "\r\n";
    request += extraHeaders;
    request += "Connection: close\r\n\r\n";
    request += payload;
    return request;
}

McpHttpResponse mcpHttpPost(quint16 port, const QByteArray& payload,
                            const QByteArray& token,
                            const QByteArray& contentType =
                                QByteArrayLiteral("application/json"),
                            qint64 declaredLength = -1,
                            const QByteArray& extraHeaders = QByteArray(),
                            const QByteArray& hostHeader = QByteArray())
{
    return mcpExchange(port,
                       mcpPostRequest(port, payload, token, contentType,
                                      declaredLength, extraHeaders, hostHeader));
}

bool mcpWaitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs)
    {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

int mcpStatusFromRaw(const QByteArray& received)
{
    const int headerEnd = received.indexOf("\r\n\r\n");
    const QByteArray head =
        headerEnd >= 0 ? received.left(headerEnd) : received;
    const QList<QByteArray> statusLine =
        head.split('\n').first().trimmed().split(' ');
    return statusLine.size() >= 2 ? statusLine.at(1).toInt() : -1;
}

QJsonObject mcpRpc(quint16 port, const QByteArray& token, const QJsonObject& request,
                   int* statusOut = nullptr)
{
    const McpHttpResponse response = mcpHttpPost(
        port, QJsonDocument(request).toJson(QJsonDocument::Compact), token);
    if (statusOut)
        *statusOut = response.status;
    return QJsonDocument::fromJson(response.body).object();
}

int mcpReadonlyTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-readonly-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP 测试工程");
    project->width = 1280;
    project->height = 720;
    project->fps = 24;
    pvf::Entity* folder = project->addFolder(QStringLiteral("组"));
    pvf::Entity* child = project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    child->name = QStringLiteral("移动矩形");
    const pvf::EntityId childId = child->id;
    project->setEntityParent(childId, folder->id);
    project->setValue(childId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(childId, QStringLiteral("x"), 48, 480.0, true);
    pvf::Entity* camera = project->addCamera(QStringLiteral("主摄像机"));
    project->activeCamera = camera->id;
    const int entityCount = project->entities().size();

    pvf::McpServer server(project);
    server.setToken(QByteArrayLiteral("test-token"));
    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-endpoint-XXXXXX"));
    server.setEndpointFilePath(endpointTemp.path()
                               + QStringLiteral("/mcp-endpoint.json"));

    int signalled = 0;
    int signalledFailures = 0;
    QString lastSignalName;
    QString lastArgsJson;
    QString lastResultJson;
    qint64 lastDuration = -1;
    bool lastSignalOk = false;
    QObject::connect(
        &server, &pvf::McpServer::toolInvoked,
        [&](const QString& name, const QString& argsJson, const QString& resultJson,
            qint64 durationMs, bool ok) {
            ++signalled;
            if (!ok)
                ++signalledFailures;
            lastSignalName = name;
            lastArgsJson = argsJson;
            lastResultJson = resultJson;
            lastDuration = durationMs;
            lastSignalOk = ok;
        });

    QString error;
    const quint16 startedPort = server.start(0, &error);
    if (startedPort == 0)
    {
        qCritical() << "mcp-readonly-test: start failed" << error;
        delete project;
        return 1;
    }
    failIf(server.port() != startedPort,
           QStringLiteral("start() port and port() disagree"));
    failIf(server.port() == 0, QStringLiteral("start(0) did not report a port"));
    failIf(!server.isRunning(), QStringLiteral("server is not running after start"));
    failIf(server.endpointUrl().isEmpty(), QStringLiteral("endpoint URL is empty"));

    const quint16 port = startedPort;
    const QByteArray token = server.token();
    failIf(token.isEmpty(), QStringLiteral("token is empty"));

    // initialize
    QJsonObject initParams;
    initParams[QStringLiteral("protocolVersion")] = QStringLiteral("2025-06-18");
    initParams[QStringLiteral("capabilities")] = QJsonObject();
    initParams[QStringLiteral("clientInfo")] =
        QJsonObject{{QStringLiteral("name"), QStringLiteral("test-client")},
                    {QStringLiteral("version"), QStringLiteral("0.0")}};
    QJsonObject initRequest;
    initRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    initRequest[QStringLiteral("id")] = 1;
    initRequest[QStringLiteral("method")] = QStringLiteral("initialize");
    initRequest[QStringLiteral("params")] = initParams;

    const McpHttpResponse initHttp = mcpHttpPost(
        port, QJsonDocument(initRequest).toJson(QJsonDocument::Compact), token);
    failIf(initHttp.status != 200,
           QStringLiteral("initialize HTTP status %1").arg(initHttp.status));
    failIf(!initHttp.headers.toLower().contains(
               QByteArrayLiteral("content-type: application/json")),
           QStringLiteral("initialize response is not application/json"));
    const QJsonObject initResult =
        QJsonDocument::fromJson(initHttp.body).object().value(QStringLiteral("result"))
            .toObject();
    failIf(initResult.value(QStringLiteral("protocolVersion")).toString().isEmpty(),
           QStringLiteral("initialize did not return protocolVersion"));
    const QJsonObject capabilities =
        initResult.value(QStringLiteral("capabilities")).toObject();
    failIf(!capabilities.contains(QStringLiteral("tools")),
           QStringLiteral("initialize missing tools capability"));
    failIf(!capabilities.contains(QStringLiteral("resources")),
           QStringLiteral("initialize missing resources capability"));
    failIf(initResult.value(QStringLiteral("serverInfo"))
                   .toObject()
                   .value(QStringLiteral("name"))
                   .toString()
               != QStringLiteral("PVForge"),
           QStringLiteral("initialize serverInfo.name mismatch"));

    // tools/list
    QJsonObject listRequest;
    listRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    listRequest[QStringLiteral("id")] = 2;
    listRequest[QStringLiteral("method")] = QStringLiteral("tools/list");
    const QJsonObject listResponse = mcpRpc(port, token, listRequest);
    const QJsonArray tools =
        listResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("tools"))
            .toArray();
    QStringList toolNames;
    for (const QJsonValue& value : tools)
        toolNames.append(value.toObject().value(QStringLiteral("name")).toString());
    for (const QString& required :
         {QStringLiteral("project.summary"), QStringLiteral("timeline.get"),
          QStringLiteral("entity.get"), QStringLiteral("assets.list"),
          QStringLiteral("guide.get")})
        failIf(!toolNames.contains(required),
               QStringLiteral("tools/list missing %1").arg(required));
    failIf(toolNames.contains(QStringLiteral("entity.create")),
           QStringLiteral("tools/list advertises a write tool"));

    const auto callTool = [&](int id, const QString& name, const QJsonObject& args,
                              int* statusOut = nullptr) {
        QJsonObject params;
        params[QStringLiteral("name")] = name;
        params[QStringLiteral("arguments")] = args;
        QJsonObject request;
        request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        request[QStringLiteral("id")] = id;
        request[QStringLiteral("method")] = QStringLiteral("tools/call");
        request[QStringLiteral("params")] = params;
        return mcpRpc(port, token, request, statusOut);
    };

    // project.summary
    int status = 0;
    const QJsonObject summaryResponse =
        callTool(3, QStringLiteral("project.summary"), QJsonObject(), &status);
    failIf(status != 200, QStringLiteral("project.summary HTTP %1").arg(status));
    const QJsonObject summaryResult =
        summaryResponse.value(QStringLiteral("result")).toObject();
    failIf(summaryResult.value(QStringLiteral("isError")).toBool(),
           QStringLiteral("project.summary returned isError"));
    const QJsonObject summaryStructured =
        summaryResult.value(QStringLiteral("structuredContent")).toObject();
    failIf(!summaryStructured.value(QStringLiteral("ok")).toBool(),
           QStringLiteral("project.summary structured ok is false"));
    const QJsonObject summaryData =
        summaryStructured.value(QStringLiteral("data")).toObject();
    failIf(summaryData.value(QStringLiteral("width")).toInt() != 1280,
           QStringLiteral("project.summary width mismatch"));
    failIf(summaryData.value(QStringLiteral("height")).toInt() != 720,
           QStringLiteral("project.summary height mismatch"));
    failIf(summaryData.value(QStringLiteral("fps")).toInt() != 24,
           QStringLiteral("project.summary fps mismatch"));
    failIf(summaryData.value(QStringLiteral("durationFrames")).toInt()
               != project->timelineLength(),
           QStringLiteral("project.summary duration mismatch"));
    failIf(summaryData.value(QStringLiteral("activeCamera"))
                   .toObject()
                   .value(QStringLiteral("id"))
                   .toInt()
               != camera->id,
           QStringLiteral("project.summary active camera mismatch"));
    failIf(summaryData.value(QStringLiteral("scenesAvailable")).toBool(),
           QStringLiteral("project.summary pretends scenes exist"));
    failIf(summaryResult.value(QStringLiteral("content")).toArray().isEmpty(),
           QStringLiteral("project.summary content is empty"));

    // timeline.get
    QJsonObject timelineArgs;
    timelineArgs[QStringLiteral("limit")] = 10;
    const QJsonObject timelineResponse =
        callTool(4, QStringLiteral("timeline.get"), timelineArgs);
    const QJsonObject timelineStructured =
        timelineResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject();
    const QJsonObject timelineData =
        timelineStructured.value(QStringLiteral("data")).toObject();
    const QJsonArray timelineEntities =
        timelineData.value(QStringLiteral("entities")).toArray();
    failIf(timelineEntities.size() != entityCount,
           QStringLiteral("timeline.get entity count %1 != %2")
               .arg(timelineEntities.size())
               .arg(entityCount));
    failIf(timelineStructured.value(QStringLiteral("page"))
                   .toObject()
                   .value(QStringLiteral("total"))
                   .toInt()
               != entityCount,
           QStringLiteral("timeline.get page total mismatch"));
    if (!timelineEntities.isEmpty())
    {
        const QJsonObject first = timelineEntities.first().toObject();
        failIf(!first.contains(QStringLiteral("tracks")),
               QStringLiteral("timeline.get omitted tracks"));
        failIf(!first.contains(QStringLiteral("clipStart"))
                   || !first.contains(QStringLiteral("clipEnd"))
                   || !first.contains(QStringLiteral("row")),
               QStringLiteral("timeline.get omitted clip/row fields"));
    }

    bool foundChild = false;
    for (const QJsonValue& value : timelineEntities)
    {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("id")).toInt() != childId)
            continue;
        foundChild = true;
        const QJsonObject tracks = object.value(QStringLiteral("tracks")).toObject();
        failIf(tracks.value(QStringLiteral("x")).toArray().size() != 2,
               QStringLiteral("timeline.get x track key count mismatch"));
    }
    failIf(!foundChild, QStringLiteral("timeline.get did not include the child"));

    // entity.get
    QJsonObject entityArgs;
    entityArgs[QStringLiteral("id")] = childId;
    entityArgs[QStringLiteral("frame")] = 24;
    const QJsonObject entityResponse =
        callTool(5, QStringLiteral("entity.get"), entityArgs);
    const QJsonObject entityStructured =
        entityResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject();
    const QJsonObject entityData =
        entityStructured.value(QStringLiteral("data")).toObject();
    failIf(entityData.value(QStringLiteral("name")).toString()
               != QStringLiteral("移动矩形"),
           QStringLiteral("entity.get name mismatch"));
    failIf(entityData.value(QStringLiteral("tracks"))
                   .toObject()
                   .value(QStringLiteral("x"))
                   .toArray()
                   .size()
               != 2,
           QStringLiteral("entity.get track mismatch"));
    failIf(std::abs(entityData.value(QStringLiteral("evaluated"))
                            .toObject()
                            .value(QStringLiteral("x"))
                            .toDouble()
                        - 240.0)
               > 1e-6,
           QStringLiteral("entity.get x at frame 24 is not 240"));

    // Write tools are rejected, and nothing changes.
    const QJsonObject writeResponse = callTool(
        6, QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}});
    const QJsonObject writeResult =
        writeResponse.value(QStringLiteral("result")).toObject();
    failIf(!writeResult.value(QStringLiteral("isError")).toBool(),
           QStringLiteral("entity.create was not rejected"));
    failIf(writeResult.value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("permission_denied"),
           QStringLiteral("entity.create did not return permission_denied"));
    failIf(project->entities().size() != entityCount,
           QStringLiteral("rejected write tool changed the project"));

    // Missing required id -> invalid_argument.
    const QJsonObject invalidResponse =
        callTool(7, QStringLiteral("entity.get"), QJsonObject());
    const QJsonObject invalidResult =
        invalidResponse.value(QStringLiteral("result")).toObject();
    failIf(invalidResult.value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("invalid_argument"),
           QStringLiteral("entity.get without id did not return invalid_argument"));

    // Deferred scene tools -> not_available.
    const QJsonObject sceneResponse =
        callTool(8, QStringLiteral("scene.list"), QJsonObject());
    failIf(sceneResponse.value(QStringLiteral("result"))
                   .toObject()
                   .value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("not_available"),
           QStringLiteral("scene.list did not return not_available"));

    // guide.get
    const QJsonObject guideResponse =
        callTool(9, QStringLiteral("guide.get"), QJsonObject());
    const QString guideText =
        guideResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject()
            .value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("markdown"))
            .toString();
    failIf(guideText.isEmpty(), QStringLiteral("guide.get returned empty text"));

    // resources/list + resources/read
    QJsonObject resourcesRequest;
    resourcesRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    resourcesRequest[QStringLiteral("id")] = 10;
    resourcesRequest[QStringLiteral("method")] = QStringLiteral("resources/list");
    const QJsonObject resourcesResponse = mcpRpc(port, token, resourcesRequest);
    const QJsonArray resources =
        resourcesResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("resources"))
            .toArray();
    QStringList resourceUris;
    for (const QJsonValue& value : resources)
        resourceUris.append(value.toObject().value(QStringLiteral("uri")).toString());
    failIf(!resourceUris.contains(QStringLiteral("pvforge://guide/authoring")),
           QStringLiteral("resources/list missing authoring guide"));
    failIf(!resourceUris.contains(QStringLiteral("pvforge://project/schema")),
           QStringLiteral("resources/list missing project schema"));

    QJsonObject readParams;
    readParams[QStringLiteral("uri")] = QStringLiteral("pvforge://guide/authoring");
    QJsonObject readRequest;
    readRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    readRequest[QStringLiteral("id")] = 11;
    readRequest[QStringLiteral("method")] = QStringLiteral("resources/read");
    readRequest[QStringLiteral("params")] = readParams;
    const QJsonObject readResponse = mcpRpc(port, token, readRequest);
    const QJsonArray contents =
        readResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("contents"))
            .toArray();
    failIf(contents.isEmpty()
               || contents.first()
                      .toObject()
                      .value(QStringLiteral("text"))
                      .toString()
                      .isEmpty(),
           QStringLiteral("resources/read returned no text"));

    // ping
    QJsonObject pingRequest;
    pingRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    pingRequest[QStringLiteral("id")] = 12;
    pingRequest[QStringLiteral("method")] = QStringLiteral("ping");
    const QJsonObject pingResponse = mcpRpc(port, token, pingRequest);
    failIf(!pingResponse.value(QStringLiteral("result")).isObject(),
           QStringLiteral("ping did not return an object result"));
    failIf(!pingResponse.value(QStringLiteral("result")).toObject().isEmpty(),
           QStringLiteral("ping result is not empty"));

    // Notifications are accepted with 202 and no body.
    QJsonObject noteRequest;
    noteRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    noteRequest[QStringLiteral("method")] = QStringLiteral("notifications/cancelled");
    noteRequest[QStringLiteral("params")] =
        QJsonObject{{QStringLiteral("requestId"), 99},
                    {QStringLiteral("reason"), QStringLiteral("test")}};
    const McpHttpResponse noteResponse = mcpHttpPost(
        port, QJsonDocument(noteRequest).toJson(QJsonDocument::Compact), token);
    failIf(noteResponse.status != 202,
           QStringLiteral("notification HTTP %1 != 202").arg(noteResponse.status));
    failIf(!noteResponse.body.isEmpty(),
           QStringLiteral("notification returned a body"));

    // Authentication and transport rejection.
    const McpHttpResponse badToken = mcpHttpPost(
        port, QJsonDocument(initRequest).toJson(QJsonDocument::Compact),
        QByteArrayLiteral("wrong-token"));
    failIf(badToken.status != 401,
           QStringLiteral("bad token HTTP %1 != 401").arg(badToken.status));
    failIf(!badToken.headers.toLower().contains(QByteArrayLiteral("www-authenticate")),
           QStringLiteral("401 response lacks WWW-Authenticate"));

    QJsonObject simpleRequest;
    simpleRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    simpleRequest[QStringLiteral("id")] = 13;
    simpleRequest[QStringLiteral("method")] = QStringLiteral("ping");
    const McpHttpResponse wrongType = mcpHttpPost(
        port, QJsonDocument(simpleRequest).toJson(QJsonDocument::Compact), token,
        QByteArrayLiteral("text/plain"));
    failIf(wrongType.status != 415,
           QStringLiteral("wrong content type HTTP %1 != 415").arg(wrongType.status));

    const McpHttpResponse tooLarge = mcpHttpPost(
        port, QByteArrayLiteral("{}"), token, QByteArrayLiteral("application/json"),
        server.maxBodyBytes() + 1);
    failIf(tooLarge.status != 413,
           QStringLiteral("oversized body HTTP %1 != 413").arg(tooLarge.status));

    const McpHttpResponse malformed =
        mcpHttpPost(port, QByteArrayLiteral("{"), token);
    failIf(malformed.status != 200,
           QStringLiteral("malformed JSON HTTP %1 != 200").arg(malformed.status));
    const QJsonObject malformedResponse =
        QJsonDocument::fromJson(malformed.body).object();
    failIf(malformedResponse.value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toInt()
               != -32700,
           QStringLiteral("malformed JSON did not return -32700"));

    // Every tools/call emitted one log signal with the required fields.
    failIf(signalled < 7,
           QStringLiteral("toolInvoked emitted %1 times, expected at least 7")
               .arg(signalled));
    failIf(signalledFailures < 3,
           QStringLiteral("toolInvoked did not report the rejected calls"));
    failIf(lastSignalName != QStringLiteral("guide.get"),
           QStringLiteral("toolInvoked last name is %1").arg(lastSignalName));
    failIf(lastArgsJson.isEmpty() || lastResultJson.isEmpty(),
           QStringLiteral("toolInvoked JSON payloads are empty"));
    failIf(lastDuration < 0, QStringLiteral("toolInvoked duration is negative"));
    failIf(!lastSignalOk, QStringLiteral("toolInvoked last call is not ok"));

    server.stop();
    failIf(server.isRunning(), QStringLiteral("server still running after stop"));
    failIf(server.port() != 0, QStringLiteral("port not cleared after stop"));

    delete project;
    qInfo() << "mcp-readonly-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpHttpTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-http-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP HTTP 测试工程");
    project->width = 640;
    project->height = 360;
    project->fps = 24;

    pvf::McpServer server(project);
    server.setToken(QByteArrayLiteral("http-test-token"));
    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-http-endpoint-XXXXXX"));
    server.setEndpointFilePath(endpointTemp.path()
                               + QStringLiteral("/server.json"));

    const QByteArray pingBody =
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");

    QString error;
    const quint16 port = server.start(0, &error);
    if (port == 0)
    {
        qCritical() << "mcp-http-test: start failed" << error;
        delete project;
        return 1;
    }

    const QByteArray token = server.token();
    const QByteArray host =
        QByteArrayLiteral("127.0.0.1:") + QByteArray::number(port);
    const QByteArray localhostHost =
        QByteArrayLiteral("localhost:") + QByteArray::number(port);
    const QByteArray allowedOrigin = QByteArrayLiteral("http://") + host;

    failIf(server.endpointUrl().contains(QString::fromLatin1(token)),
           QStringLiteral("endpointUrl leaks the token"));

    // Correct Host + Bearer token works and never grants CORS.
    const McpHttpResponse normal = mcpHttpPost(port, pingBody, token);
    failIf(normal.status != 200,
           QStringLiteral("normal request HTTP %1 != 200").arg(normal.status));
    failIf(normal.headers.toLower().contains(
               QByteArrayLiteral("access-control-allow-origin")),
           QStringLiteral("normal response contains Access-Control-Allow-Origin"));

    const McpHttpResponse localhost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), localhostHost);
    failIf(localhost.status != 200,
           QStringLiteral("localhost Host HTTP %1 != 200").arg(localhost.status));

    const McpHttpResponse upperLocalhost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), localhostHost.toUpper());
    failIf(upperLocalhost.status != 200,
           QStringLiteral("uppercase localhost Host HTTP %1 != 200")
               .arg(upperLocalhost.status));

    // DNS rebinding: foreign or mismatched Host is rejected.
    const McpHttpResponse evilHost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), QByteArrayLiteral("evil.com"));
    failIf(evilHost.status != 403,
           QStringLiteral("evil Host HTTP %1 != 403").arg(evilHost.status));
    failIf(evilHost.body.contains(token),
           QStringLiteral("rejected Host response leaks the token"));
    failIf(evilHost.body.contains(QByteArrayLiteral("evil.com")),
           QStringLiteral("rejected Host response echoes the Host header"));

    const quint16 otherPort = port == 65535 ? quint16(port - 1)
                                            : quint16(port + 1);
    const McpHttpResponse wrongPortHost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(),
        QByteArrayLiteral("127.0.0.1:") + QByteArray::number(otherPort));
    failIf(wrongPortHost.status != 403,
           QStringLiteral("mismatched Host port HTTP %1 != 403")
               .arg(wrongPortHost.status));

    const QByteArray noHost =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nAuthorization: Bearer ")
        + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: ")
        + QByteArray::number(pingBody.size()) + QByteArrayLiteral("\r\n\r\n")
        + pingBody;
    failIf(mcpExchange(port, noHost).status != 403,
           QStringLiteral("missing Host was not rejected with 403"));

    // Origin rules: foreign and opaque origins are rejected, loopback is
    // accepted, and clients without Origin (non-browser) keep working.
    const McpHttpResponse evilOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: http://evil.com\r\n"));
    failIf(evilOrigin.status != 403,
           QStringLiteral("evil Origin HTTP %1 != 403").arg(evilOrigin.status));
    failIf(evilOrigin.body.contains(QByteArrayLiteral("evil.com")),
           QStringLiteral("rejected Origin response echoes the Origin header"));
    failIf(evilOrigin.body.contains(token),
           QStringLiteral("rejected Origin response leaks the token"));

    const McpHttpResponse nullOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: null\r\n"));
    failIf(nullOrigin.status != 403,
           QStringLiteral("Origin: null HTTP %1 != 403").arg(nullOrigin.status));

    const McpHttpResponse httpsOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: https://127.0.0.1:") + QByteArray::number(port)
            + QByteArrayLiteral("\r\n"));
    failIf(httpsOrigin.status != 403,
           QStringLiteral("https Origin HTTP %1 != 403").arg(httpsOrigin.status));

    const McpHttpResponse loopbackOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: ") + allowedOrigin + QByteArrayLiteral("\r\n"));
    failIf(loopbackOrigin.status != 200,
           QStringLiteral("loopback Origin HTTP %1 != 200")
               .arg(loopbackOrigin.status));

    const McpHttpResponse localhostOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: http://") + localhostHost
            + QByteArrayLiteral("\r\n"));
    failIf(localhostOrigin.status != 200,
           QStringLiteral("localhost Origin HTTP %1 != 200")
               .arg(localhostOrigin.status));

    const McpHttpResponse crossSite = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Sec-Fetch-Site: cross-site\r\n"));
    failIf(crossSite.status != 403,
           QStringLiteral("cross-site Sec-Fetch-Site HTTP %1 != 403")
               .arg(crossSite.status));

    // Preflight is refused and never answered with CORS headers.
    const QByteArray optionsRequest =
        QByteArrayLiteral("OPTIONS /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAccess-Control-Request-Method: POST\r\n"
                            "Content-Length: 0\r\n\r\n");
    const McpHttpResponse options = mcpExchange(port, optionsRequest);
    failIf(options.status != 405,
           QStringLiteral("OPTIONS HTTP %1 != 405").arg(options.status));
    failIf(options.headers.toLower().contains(
               QByteArrayLiteral("access-control-allow-origin")),
           QStringLiteral("OPTIONS response contains Access-Control-Allow-Origin"));

    // Request framing and size limits.
    const QByteArray oversizedHeader =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nX-Filler: ") + QByteArray(17000, 'a')
        + QByteArrayLiteral("\r\n\r\n");
    const McpHttpResponse bigHeader = mcpExchange(port, oversizedHeader);
    failIf(bigHeader.status != 431,
           QStringLiteral("oversized header HTTP %1 != 431")
               .arg(bigHeader.status));

    const QByteArray duplicateLength =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: 2\r\nContent-Length: 3\r\n\r\n{}");
    const McpHttpResponse doubleLength = mcpExchange(port, duplicateLength);
    failIf(doubleLength.status != 400,
           QStringLiteral("duplicate Content-Length HTTP %1 != 400")
               .arg(doubleLength.status));

    const QByteArray bothFramings =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "Content-Length: 2\r\n\r\n{}");
    const McpHttpResponse smuggling = mcpExchange(port, bothFramings);
    failIf(smuggling.status != 400,
           QStringLiteral("Content-Length + Transfer-Encoding HTTP %1 != 400")
               .arg(smuggling.status));

    const QByteArray chunkedOnly =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    failIf(mcpExchange(port, chunkedOnly).status != 400,
           QStringLiteral("chunked body was not rejected with 400"));

    const QByteArray negativeLength =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: -1\r\n\r\n");
    failIf(mcpExchange(port, negativeLength).status != 400,
           QStringLiteral("negative Content-Length was not rejected with 400"));

    const McpHttpResponse tooLarge = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"),
        server.maxBodyBytes() + 1);
    failIf(tooLarge.status != 413,
           QStringLiteral("oversized body HTTP %1 != 413").arg(tooLarge.status));

    // Authentication failures never echo either token.
    const McpHttpResponse badToken =
        mcpHttpPost(port, pingBody, QByteArrayLiteral("wrong-secret-token"));
    failIf(badToken.status != 401,
           QStringLiteral("bad token HTTP %1 != 401").arg(badToken.status));
    failIf(badToken.body.contains(QByteArrayLiteral("wrong-secret-token")),
           QStringLiteral("401 response echoes the submitted token"));
    failIf(badToken.body.contains(token),
           QStringLiteral("401 response leaks the configured token"));

    // An empty token must never leave the server unauthenticated.
    pvf::McpServer generated(project);
    generated.setToken(QByteArray());
    failIf(generated.token().isEmpty(),
           QStringLiteral("empty token was not replaced with a random token"));
    failIf(generated.token().size() < 32,
           QStringLiteral("generated token is too short"));

    // Connection cap: the third concurrent connection is refused.
    pvf::McpServer limited(project);
    limited.setToken(QByteArrayLiteral("limit-token"));
    limited.setEndpointFilePath(endpointTemp.path()
                                + QStringLiteral("/limited.json"));
    pvf::McpServer::HttpLimits limits = limited.httpLimits();
    limits.maxConnections = 2;
    limits.headerTimeoutMs = 5000;
    limits.requestTimeoutMs = 5000;
    limited.setHttpLimits(limits);
    const quint16 limitedPort = limited.start(0, &error);
    failIf(limitedPort == 0,
           QStringLiteral("limited server failed to start: %1").arg(error));
    if (limitedPort != 0)
    {
        QTcpSocket first;
        QTcpSocket second;
        first.connectToHost(QHostAddress::LocalHost, limitedPort);
        second.connectToHost(QHostAddress::LocalHost, limitedPort);
        failIf(!mcpWaitUntil(
                   [&limited] { return limited.activeConnectionCount() == 2; },
                   3000),
               QStringLiteral("connection limit server did not accept two peers"));

        QTcpSocket third;
        third.connectToHost(QHostAddress::LocalHost, limitedPort);
        failIf(!mcpWaitUntil(
                   [&third] {
                       return third.bytesAvailable() > 0
                              || third.state() == QAbstractSocket::UnconnectedState;
                   },
                   3000),
               QStringLiteral("third connection was neither refused nor answered"));
        const int thirdStatus = mcpStatusFromRaw(third.readAll());
        failIf(thirdStatus != 503,
               QStringLiteral("third connection HTTP %1 != 503")
                   .arg(thirdStatus));

        first.abort();
        second.abort();
        third.abort();
        limited.stop();
        failIf(limited.activeConnectionCount() != 0,
               QStringLiteral("connection counter is not empty after stop"));
    }

    // Header and body deadlines are configurable so the test stays fast.
    pvf::McpServer slow(project);
    slow.setToken(QByteArrayLiteral("slow-token"));
    slow.setEndpointFilePath(endpointTemp.path() + QStringLiteral("/slow.json"));
    pvf::McpServer::HttpLimits slowLimits = slow.httpLimits();
    slowLimits.headerTimeoutMs = 150;
    slowLimits.requestTimeoutMs = 150;
    slow.setHttpLimits(slowLimits);
    const quint16 slowPort = slow.start(0, &error);
    failIf(slowPort == 0,
           QStringLiteral("timeout server failed to start: %1").arg(error));
    if (slowPort != 0)
    {
        const QByteArray slowHost =
            QByteArrayLiteral("127.0.0.1:") + QByteArray::number(slowPort);
        const QByteArray partialHeader =
            QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + slowHost
            + QByteArrayLiteral("\r\nAuthorization: Bearer slow-token\r\n"
                                "Content-Type: application/json\r\n");
        const McpHttpResponse headerTimeout =
            mcpExchange(slowPort, partialHeader, 3000);
        failIf(headerTimeout.timedOut,
               QStringLiteral("client timed out before the header deadline"));
        failIf(headerTimeout.status != 408,
               QStringLiteral("header timeout HTTP %1 != 408")
                   .arg(headerTimeout.status));

        const QByteArray partialBody =
            QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + slowHost
            + QByteArrayLiteral("\r\nAuthorization: Bearer slow-token\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: 100\r\n\r\n");
        const McpHttpResponse bodyTimeout =
            mcpExchange(slowPort, partialBody, 3000);
        failIf(bodyTimeout.timedOut,
               QStringLiteral("client timed out before the body deadline"));
        failIf(bodyTimeout.status != 408,
               QStringLiteral("body timeout HTTP %1 != 408")
                   .arg(bodyTimeout.status));

        slow.stop();
    }

    server.stop();
    delete project;
    qInfo() << "mcp-http-test failures" << failures;
    return failures == 0 ? 0 : 1;
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

    pvf::Project project;
    project.name = QStringLiteral("检查点测试");
    project.width = 640;
    project.height = 360;
    project.fps = 30;
    pvf::Entity* entity = project.addImage(imagePath, QStringLiteral("still"));
    if (!entity)
    {
        fail(QStringLiteral("could not add test entity"));
        return 1;
    }
    const pvf::EntityId entityId = entity->id;
    project.setValue(entityId, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(entityId, QStringLiteral("x"), 60, 100.0, true);
    QString error;
    if (!project.addScene(QStringLiteral("Intro"), 0, 60, QColor(90, 170, 255),
                          QString(), &error))
    {
        fail(QStringLiteral("scene setup failed: %1").arg(error));
        return 1;
    }
    if (!project.save(projectPath, &error))
    {
        fail(QStringLiteral("project save failed: %1").arg(error));
        return 1;
    }

    const QByteArray json1 = readAll(projectPath);
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

int mcpUiTest(QApplication& app)
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-ui-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity =
        project->addVector(0, 100, 80, Qt::red, Qt::white, 0);
    entity->name = QStringLiteral("MCP 测试矩形");
    const pvf::EntityId entityId = entity->id;
    project->setValue(entityId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(entityId, QStringLiteral("x"), 30, 300.0, true);

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-ui-endpoint-XXXXXX"));
    window.mcpServerForTest()->setEndpointFilePath(
        endpointTemp.path() + QStringLiteral("/mcp-endpoint.json"));

    QTimer::singleShot(
        300, &window, [&app, &window, &failures, entityId, fail, failIf] {
            auto* server = window.mcpServerForTest();
            auto* panel = window.mcpLogPanelForTest();
            auto* statusButton =
                window.findChild<QToolButton*>(QStringLiteral("mcpStatusButton"));
            if (!server || !panel || !statusButton)
            {
                qCritical() << "mcp-ui-test: server/log/status missing" << server
                            << panel << statusButton;
                app.exit(1);
                return;
            }

            auto treeText = [panel] {
                QString text;
                auto* tree =
                    panel->findChild<QTreeWidget*>(QStringLiteral("mcpLogTree"));
                if (!tree)
                    return text;
                for (int i = 0; i < tree->topLevelItemCount(); ++i)
                {
                    QTreeWidgetItem* item = tree->topLevelItem(i);
                    for (int column = 0; column < item->columnCount(); ++column)
                        text += item->text(column) + QLatin1Char('\n');
                    for (int childIndex = 0; childIndex < item->childCount();
                         ++childIndex)
                        for (int column = 0;
                             column < item->child(childIndex)->columnCount();
                             ++column)
                            text += item->child(childIndex)->text(column)
                                    + QLatin1Char('\n');
                }
                return text;
            };

            failIf(server->isRunning(), QStringLiteral("server auto-started"));
            failIf(panel->isVisible(),
                   QStringLiteral("log visible before MCP was enabled"));
            failIf(!statusButton->isVisible(),
                   QStringLiteral("status entry is hidden at startup"));
            failIf(!statusButton->text().contains(QStringLiteral("MCP")),
                   QStringLiteral("status entry has no MCP text"));

            auto* dialog = window.openSettingsDialogForTest();
            if (!dialog)
            {
                fail(QStringLiteral("settings dialog was not created"));
                app.exit(1);
                return;
            }
            QApplication::processEvents();

            auto* categories =
                dialog->findChild<QListWidget*>(QStringLiteral("settingsCategories"));
            bool hasMcpCategory = false;
            if (categories)
            {
                for (int i = 0; i < categories->count(); ++i)
                    if (categories->item(i)->text() == QStringLiteral("MCP"))
                    {
                        hasMcpCategory = true;
                        categories->setCurrentRow(i);
                    }
            }
            failIf(!hasMcpCategory, QStringLiteral("settings has no MCP page"));

            auto* enabledCheck =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpEnableCheck"));
            auto* autoPort =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpAutoPort"));
            auto* portSpin =
                dialog->findChild<QSpinBox*>(QStringLiteral("mcpPortSpin"));
            auto* tokenEdit =
                dialog->findChild<QLineEdit*>(QStringLiteral("mcpTokenEdit"));
            auto* statusLabel =
                dialog->findChild<QLabel*>(QStringLiteral("mcpStatusLabel"));
            auto* errorLabel =
                dialog->findChild<QLabel*>(QStringLiteral("mcpErrorLabel"));
            auto* startButton =
                dialog->findChild<QPushButton*>(QStringLiteral("mcpStartButton"));
            auto* stopButton =
                dialog->findChild<QPushButton*>(QStringLiteral("mcpStopButton"));
            auto* logToggle =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpLogEnabled"));
            auto* permissionCombo =
                dialog->findChild<QComboBox*>(QStringLiteral("mcpPermissionCombo"));
            auto* copyConfig = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpCopyConfigButton"));
            auto* resetToken = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpResetTokenButton"));
            if (!enabledCheck || !autoPort || !portSpin || !tokenEdit
                || !statusLabel || !errorLabel || !startButton || !stopButton
                || !logToggle || !permissionCombo || !copyConfig || !resetToken)
            {
                qCritical() << "mcp-ui-test: MCP controls missing";
                app.exit(1);
                return;
            }

            failIf(enabledCheck->isChecked(),
                   QStringLiteral("MCP enable switch defaults to on"));
            failIf(!autoPort->isChecked(),
                   QStringLiteral("port should default to automatic"));
            failIf(!(portSpin->minimum() <= 1024 && portSpin->maximum() >= 65535),
                   QStringLiteral("manual port range is too narrow"));
            failIf(permissionCombo->currentIndex() != 0,
                   QStringLiteral("permission mode is not read-only"));
            failIf(!logToggle->isChecked(),
                   QStringLiteral("log window should be enabled by default"));
            failIf(tokenEdit->text()
                       != QString::fromLatin1(server->token()),
                   QStringLiteral("settings token does not match the server"));
            failIf(statusLabel->text().contains(
                       QString::fromLatin1(server->token())),
                   QStringLiteral("token is echoed in the settings status"));
            failIf(!errorLabel->isHidden(),
                   QStringLiteral("error label is visible before an error"));

            // Only a human action starts the server.
            enabledCheck->setChecked(true);
            QApplication::processEvents();
            failIf(!server->isRunning(),
                   QStringLiteral("enable switch did not start the server"));
            failIf(server->port() == 0,
                   QStringLiteral("enabled server has no listening port"));
            const quint16 port = server->port();
            const QByteArray token = server->token();
            failIf(!statusLabel->text().contains(QStringLiteral("监听中")),
                   QStringLiteral("settings status does not show listening"));
            failIf(!statusLabel->text().contains(QString::number(port)),
                   QStringLiteral("settings status does not show the actual port"));
            failIf(statusButton->text().contains(QStringLiteral("关闭")),
                   QStringLiteral("status bar was not updated after start"));
            failIf(!tokenEdit->text().contains(QString::fromLatin1(token)),
                   QStringLiteral("token field did not refresh after start"));

            stopButton->click();
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("dialog stop button did not stop the server"));
            failIf(server->port() != 0,
                   QStringLiteral("port not cleared after dialog stop"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status not reset after stop"));
            startButton->click();
            QApplication::processEvents();
            failIf(!server->isRunning(),
                   QStringLiteral("dialog start button did not start the server"));

            copyConfig->click();
            QApplication::processEvents();
            const QJsonObject config =
                QJsonDocument::fromJson(
                    QApplication::clipboard()->text().toUtf8())
                    .object();
            const QJsonObject configuredServer =
                config.value(QStringLiteral("mcpServers"))
                    .toObject()
                    .value(QStringLiteral("pvforge"))
                    .toObject();
            failIf(!configuredServer.value(QStringLiteral("url"))
                        .toString()
                        .contains(QString::number(server->port())),
                   QStringLiteral("copied config has no live endpoint"));
            failIf(!configuredServer.value(QStringLiteral("headers"))
                        .toObject()
                        .value(QStringLiteral("Authorization"))
                        .toString()
                        .contains(QString::fromLatin1(token)),
                   QStringLiteral("copied config has no bearer token"));

            const quint16 livePort = server->port();
            QJsonObject summaryParams;
            summaryParams[QStringLiteral("name")] =
                QStringLiteral("project.summary");
            summaryParams[QStringLiteral("arguments")] = QJsonObject();
            QJsonObject summaryRequest;
            summaryRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            summaryRequest[QStringLiteral("id")] = 100;
            summaryRequest[QStringLiteral("method")] = QStringLiteral("tools/call");
            summaryRequest[QStringLiteral("params")] = summaryParams;
            const QJsonObject summaryResponse =
                mcpRpc(livePort, token, summaryRequest);
            failIf(summaryResponse.value(QStringLiteral("result")).isUndefined(),
                   QStringLiteral("project.summary returned no result"));
            QApplication::processEvents();

            failIf(panel->entryCountForTest() < 1,
                   QStringLiteral("toolInvoked did not add a log row"));
            failIf(!panel->isVisible(),
                   QStringLiteral("first tool call did not unfold the log"));
            failIf(!statusButton->text().contains(QStringLiteral("已启用")),
                   QStringLiteral("status bar entry does not show MCP enabled"));
            dialog->grab().toImage().save(
                QStringLiteral("/tmp/pvf_mcp_settings.png"));
            panel->grab().toImage().save(
                QStringLiteral("/tmp/pvf_mcp_log.png"));

            QJsonObject entityArgs;
            entityArgs[QStringLiteral("id")] = entityId;
            entityArgs[QStringLiteral("frame")] = 12;
            QJsonObject entityParams;
            entityParams[QStringLiteral("name")] = QStringLiteral("entity.get");
            entityParams[QStringLiteral("arguments")] = entityArgs;
            QJsonObject entityRequest;
            entityRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            entityRequest[QStringLiteral("id")] = 101;
            entityRequest[QStringLiteral("method")] = QStringLiteral("tools/call");
            entityRequest[QStringLiteral("params")] = entityParams;
            const QJsonObject entityResponse =
                mcpRpc(livePort, token, entityRequest);
            failIf(entityResponse.value(QStringLiteral("result")).isUndefined(),
                   QStringLiteral("entity.get returned no result"));
            QApplication::processEvents();

            failIf(panel->entryCountForTest() < 2,
                   QStringLiteral("second tool call did not add a log row"));
            const QString target = panel->entryTargetForTest(1);
            failIf(!target.contains(QStringLiteral("实体 #")),
                   QStringLiteral("log target has no entity: %1").arg(target));
            failIf(!target.contains(QStringLiteral("帧 12")),
                   QStringLiteral("log target has no frame: %1").arg(target));
            const QString detail = panel->entryDetailForTest(1);
            failIf(!detail.contains(QStringLiteral("参数")),
                   QStringLiteral("expanded row has no args"));
            failIf(!detail.contains(QStringLiteral("结果")),
                   QStringLiteral("expanded row has no result"));

            auto* tree =
                panel->findChild<QTreeWidget*>(QStringLiteral("mcpLogTree"));
            if (tree && tree->topLevelItem(1))
            {
                tree->topLevelItem(1)->setExpanded(true);
                failIf(!tree->topLevelItem(1)->isExpanded(),
                       QStringLiteral("log row cannot expand"));
            }
            else
            {
                fail(QStringLiteral("log tree row missing"));
            }

            auto* detachButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogDetachButton"));
            if (detachButton)
            {
                detachButton->click();
                QApplication::processEvents();
                failIf(!panel->isDetached(),
                       QStringLiteral("detach button did not pop the log out"));
                detachButton->click();
                QApplication::processEvents();
                failIf(panel->isDetached(),
                       QStringLiteral("detach button did not dock the log"));
            }
            else
            {
                fail(QStringLiteral("detach button missing"));
            }

            const QString logText = treeText();
            failIf(logText.contains(QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the log"));
            failIf(statusButton->text().contains(QString::fromLatin1(token))
                       || statusButton->toolTip().contains(
                           QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the status entry"));
            failIf(statusLabel->text().contains(QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the settings status"));

            panel->activateEntryForTest(1);
            QApplication::processEvents();
            failIf(window.selectedEntityForTest() != entityId,
                   QStringLiteral("clicking the log row did not select the entity"));
            failIf(window.currentFrameForTest() != 12,
                   QStringLiteral("clicking the log row did not jump to the frame"));

            bool stopSignalled = false;
            QObject::connect(panel, &pvf::McpLogPanel::stopAiRequested,
                             [&stopSignalled] { stopSignalled = true; });
            auto* stopAi = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogStopButton"));
            failIf(!stopAi || !stopAi->isEnabled(),
                   QStringLiteral("stop AI button is missing or disabled"));
            if (stopAi)
                stopAi->click();
            QApplication::processEvents();
            failIf(!stopSignalled,
                   QStringLiteral("stop AI button did not emit the signal"));

            const QByteArray oldToken = server->token();
            resetToken->click();
            QApplication::processEvents();
            failIf(server->token().isEmpty() || server->token() == oldToken,
                   QStringLiteral("token reset did not generate a new token"));
            failIf(tokenEdit->text() != QString::fromLatin1(server->token()),
                   QStringLiteral("token field did not update after reset"));
            failIf(treeText().contains(QString::fromLatin1(server->token())),
                   QStringLiteral("new token is echoed in the log"));

            auto* clearButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogClearButton"));
            if (clearButton)
                clearButton->click();
            QApplication::processEvents();
            failIf(panel->entryCountForTest() != 0,
                   QStringLiteral("clear did not empty the log"));

            enabledCheck->setChecked(false);
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("disabling MCP did not stop the server"));
            failIf(server->port() != 0,
                   QStringLiteral("port not cleared after disabling MCP"));
            failIf(statusButton->text().contains(QStringLiteral("已启用")),
                   QStringLiteral("status bar still says MCP is enabled"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status is not stopped"));

            // A busy manual port shows an error without leaking the token,
            // and retrying on the same port works once it is free.
            QTcpServer busyPortServer;
            if (!busyPortServer.listen(QHostAddress::LocalHost, 0))
            {
                fail(QStringLiteral("could not reserve a busy port"));
            }
            else
            {
                const quint16 busyPort = busyPortServer.serverPort();
                autoPort->setChecked(false);
                portSpin->setValue(busyPort);
                enabledCheck->setChecked(true);
                QApplication::processEvents();
                failIf(server->isRunning(),
                       QStringLiteral("server started on an occupied port"));
                failIf(errorLabel->isHidden() || errorLabel->text().isEmpty(),
                       QStringLiteral("start failure did not show an error"));
                failIf(errorLabel->text().contains(
                           QString::fromLatin1(server->token())),
                       QStringLiteral("token is echoed in the error text"));
                failIf(!statusButton->text().contains(QStringLiteral("错误")),
                       QStringLiteral("status bar does not show the MCP error"));
                busyPortServer.close();
                QApplication::processEvents();
                startButton->click();
                QApplication::processEvents();
                failIf(!server->isRunning(),
                       QStringLiteral("retry after freeing the port failed"));
                failIf(server->port() != busyPort,
                       QStringLiteral("manual port was not honoured: %1 != %2")
                           .arg(server->port())
                           .arg(busyPort));
                failIf(!errorLabel->isHidden(),
                       QStringLiteral("error text did not clear on success"));
                enabledCheck->setChecked(false);
                QApplication::processEvents();
                failIf(server->isRunning(),
                       QStringLiteral("manual-port server did not stop"));
            }

            panel->showFromMenu();
            QApplication::processEvents();
            panel->close();
            QApplication::processEvents();
            failIf(panel->isVisible(),
                   QStringLiteral("closing the log window did not hide it"));
            failIf(!statusButton->isVisible(),
                   QStringLiteral("status entry disappeared after closing the log"));
            failIf(!statusButton->text().contains(QStringLiteral("MCP")),
                   QStringLiteral("status entry lost its MCP label"));
            statusButton->click();
            QApplication::processEvents();
            failIf(!panel->isVisible(),
                   QStringLiteral("status entry did not reopen the log"));
            panel->close();
            QApplication::processEvents();

            dialog->close();
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("closing settings started the server"));

            qInfo() << "mcp-ui-test failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    return app.exec();
}


int mcpWriteTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-write-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };
    const auto expectOk = [&](const pvf::McpWriteResult& result, const QString& label) {
        if (result.ok)
            return true;
        fail(QStringLiteral("%1: %2 (%3)")
                 .arg(label, result.message,
                      pvf::McpWriteResult::codeName(result.error)));
        return false;
    };
    const auto expectCode = [&](const pvf::McpWriteResult& result,
                                pvf::McpErrorCode code, const QString& label) {
        if (result.ok || result.error != code)
        {
            fail(QStringLiteral("%1: expected %2, got %3 (%4)")
                     .arg(label, pvf::McpWriteResult::codeName(code),
                          pvf::McpWriteResult::codeName(result.error),
                          result.message));
            return false;
        }
        return true;
    };

    pvf::Project project;
    project.fps = 30;
    project.width = 1280;
    project.height = 720;

    pvf::Entity* holder =
        project.addVector(0, 100, 100, QColor(80, 80, 80), Qt::white, 0);
    if (!holder)
        return 1;
    project.setValue(holder->id, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(holder->id, QStringLiteral("x"), 240, 240.0, true);

    QString error;
    pvf::Scene* sceneA = project.addScene(QStringLiteral("A"), 0, 120,
                                          QColor(255, 80, 80),
                                          QStringLiteral("场景 A"), &error);
    if (!sceneA)
    {
        qCritical() << "mcp-write-test: add scene A" << error;
        return 1;
    }
    const QString sceneAId = sceneA->id;
    const pvf::EntityId sceneAFolder = sceneA->folderId;

    pvf::Scene* sceneB = project.addScene(QStringLiteral("B"), 150, 240,
                                          QColor(80, 200, 120),
                                          QStringLiteral("场景 B"), &error);
    if (!sceneB)
    {
        qCritical() << "mcp-write-test: add scene B" << error;
        return 1;
    }
    const pvf::EntityId sceneBFolder = sceneB->folderId;

    pvf::Entity* aEntity =
        project.addVector(0, 120, 90, QColor(230, 90, 90), Qt::white, 0);
    aEntity->name = QStringLiteral("场景 A 矩形");
    const pvf::EntityId aId = aEntity->id;
    project.setEntityParent(aId, sceneAFolder);
    pvf::Entity* bEntity =
        project.addVector(0, 120, 90, QColor(90, 140, 230), Qt::white, 0);
    bEntity->name = QStringLiteral("场景 B 矩形");
    const pvf::EntityId bId = bEntity->id;
    project.setEntityParent(bId, sceneBFolder);
    pvf::Entity* camera = project.addCamera(QStringLiteral("主摄像机"));
    const pvf::EntityId cameraId = camera->id;
    project.activeCamera = cameraId;

    pvf::History history;
    pvf::McpCommandService service(&project, &history);
    failIf(service.isArmed(), QStringLiteral("service starts armed"));
    failIf(service.isStopped(), QStringLiteral("service starts stopped"));

    const auto entityName = [&project](pvf::EntityId id) {
        const pvf::Entity* entity = project.entity(id);
        return entity ? entity->name : QString();
    };

    // ---- Unarmed writes are refused -------------------------------------
    const int countBeforeUnarmed = int(project.entities().size());
    const pvf::McpWriteResult unarmedResult = service.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("未解锁")}});
    expectCode(unarmedResult, pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("unarmed create"));
    failIf(project.entities().size() != countBeforeUnarmed,
           QStringLiteral("unarmed create changed the project"));
    const QJsonObject unarmedJson = unarmedResult.toJson();
    failIf(unarmedJson.value(QStringLiteral("ok")).toBool(),
           QStringLiteral("error result claims ok"));
    failIf(unarmedJson.value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("permission_denied"),
           QStringLiteral("error result code mismatch"));

    // ---- Arm the scene and create/update/move/keyframe -------------------
    failIf(!service.arm(sceneAFolder, &error),
           QStringLiteral("arm scene A: %1").arg(error));
    failIf(service.sceneId() != sceneAId,
           QStringLiteral("arm did not snapshot scene A"));
    failIf(service.sceneRootId() != sceneAFolder,
           QStringLiteral("arm did not snapshot the folder"));

    const QJsonObject createArgs{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("name"), QStringLiteral("AI 文本")},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("text"), QStringLiteral("你好")},
                     {QStringLiteral("size"), 48}}},
        {QStringLiteral("transform"),
         QJsonObject{{QStringLiteral("x"), 10.0},
                     {QStringLiteral("y"), 20.0}}},
        {QStringLiteral("frame"), 0.0}};
    const pvf::McpWriteResult createResult =
        service.execute(QStringLiteral("entity.create"), createArgs);
    int textId = pvf::kInvalidEntity;
    if (expectOk(createResult, QStringLiteral("create text")))
    {
        textId = createResult.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* created = project.entity(textId);
        failIf(!created, QStringLiteral("created entity is missing"));
        if (created)
        {
            failIf(created->type != pvf::EntityType::Text,
                   QStringLiteral("created type is not Text"));
            failIf(created->parent != sceneAFolder,
                   QStringLiteral("created entity did not default to scene A"));
            failIf(created->data.value(QStringLiteral("text")).toString()
                       != QStringLiteral("你好"),
                   QStringLiteral("created text data mismatch"));
        }
        failIf(!qFuzzyCompare(project.value(textId, QStringLiteral("x"), 0) + 1.0,
                              11.0),
               QStringLiteral("created transform x mismatch"));
        failIf(!history.canUndo(), QStringLiteral("create did not reach History"));
        failIf(history.undoText().isEmpty(),
               QStringLiteral("create History text is empty"));
        history.undo(project);
        failIf(project.entity(textId) != nullptr,
               QStringLiteral("undo did not remove the created entity"));
        history.redo(project);
        failIf(project.entity(textId) == nullptr,
               QStringLiteral("redo did not restore the created entity"));
    }

    const QJsonObject updateArgs{
        {QStringLiteral("id"), textId},
        {QStringLiteral("name"), QStringLiteral("AI 文本 2")},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("text"), QStringLiteral("更新")}}},
        {QStringLiteral("transform"),
         QJsonObject{{QStringLiteral("x"), 30.0}}},
        {QStringLiteral("frame"), 0.0}};
    const pvf::McpWriteResult updateResult =
        service.execute(QStringLiteral("entity.update"), updateArgs);
    if (expectOk(updateResult, QStringLiteral("update text")))
    {
        const pvf::Entity* updated = project.entity(textId);
        failIf(!updated, QStringLiteral("updated entity is missing"));
        if (updated)
        {
            failIf(updated->name != QStringLiteral("AI 文本 2"),
                   QStringLiteral("update name mismatch"));
            failIf(updated->data.value(QStringLiteral("text")).toString()
                       != QStringLiteral("更新"),
                   QStringLiteral("update data mismatch"));
        }
        failIf(!qFuzzyCompare(project.value(textId, QStringLiteral("x"), 0) + 1.0,
                              31.0),
               QStringLiteral("update transform x mismatch"));
    }

    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("type"),
                                QStringLiteral("vector")}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("entity.update must not change type"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("data"),
                                QJsonObject{{QStringLiteral("sourcePath"),
                                             QStringLiteral("/etc/passwd")}}}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("entity.update path injection"));

    QJsonArray keyframes;
    keyframes.append(QJsonObject{{QStringLiteral("frame"), 0.0},
                                 {QStringLiteral("value"), 0.0}});
    keyframes.append(QJsonObject{{QStringLiteral("frame"), 60.0},
                                 {QStringLiteral("value"), 300.0},
                                 {QStringLiteral("ease"), QStringLiteral("easeinquad")}});
    const pvf::McpWriteResult keyframeResult = service.execute(
        QStringLiteral("keyframes.set"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("property"), QStringLiteral("x")},
                    {QStringLiteral("keys"), keyframes}});
    if (expectOk(keyframeResult, QStringLiteral("set keyframes")))
    {
        const pvf::Keyframe* key = project.keyframe(textId, QStringLiteral("x"), 60);
        failIf(!key, QStringLiteral("keyframe at 60 is missing"));
        if (key)
        {
            failIf(!qFuzzyCompare(key->value + 1.0, 301.0),
                   QStringLiteral("keyframe value mismatch"));
            failIf(key->ease.easing != pvf::Easing::EaseInQuad,
                   QStringLiteral("keyframe ease mismatch"));
        }
    }

    const pvf::McpWriteResult easingResult = service.execute(
        QStringLiteral("easing.set"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("property"), QStringLiteral("x")},
                    {QStringLiteral("frame"), 60.0},
                    {QStringLiteral("ease"), QStringLiteral("easeoutquad")}});
    if (expectOk(easingResult, QStringLiteral("set easing")))
    {
        const pvf::Keyframe* key = project.keyframe(textId, QStringLiteral("x"), 60);
        failIf(!key || key->ease.easing != pvf::Easing::EaseOutQuad,
               QStringLiteral("easing.set did not update the keyframe"));
    }

    const pvf::McpWriteResult folderResult = service.execute(
        QStringLiteral("folder.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("组")}});
    int folderId = pvf::kInvalidEntity;
    if (expectOk(folderResult, QStringLiteral("create folder")))
    {
        folderId = folderResult.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* folder = project.entity(folderId);
        failIf(!folder || !folder->isFolder(),
               QStringLiteral("created folder is missing"));
        failIf(folder && folder->parent != sceneAFolder,
               QStringLiteral("created folder did not default to scene A"));
    }

    const pvf::McpWriteResult moveResult = service.execute(
        QStringLiteral("entity.move"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("parent"), folderId},
                    {QStringLiteral("index"), 0}});
    if (expectOk(moveResult, QStringLiteral("move entity")))
    {
        const pvf::Entity* moved = project.entity(textId);
        failIf(!moved || moved->parent != folderId,
               QStringLiteral("entity.move did not update the parent"));
    }

    // ---- Scene boundary ---------------------------------------------------
    const int countBeforeScope = int(project.entities().size());
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), bId},
                               {QStringLiteral("name"), QStringLiteral("越界")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("update entity in scene B"));
    expectCode(service.execute(
                   QStringLiteral("keyframes.set"),
                   QJsonObject{{QStringLiteral("id"), bId},
                               {QStringLiteral("property"), QStringLiteral("x")},
                               {QStringLiteral("keys"),
                                QJsonArray{QJsonObject{
                                    {QStringLiteral("frame"), 0.0},
                                    {QStringLiteral("value"), 1.0}}}}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("keyframes in scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.move"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("parent"), sceneBFolder}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("move entity into scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("越界")},
                               {QStringLiteral("parent"), sceneBFolder}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create entity in scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("根层")},
                               {QStringLiteral("parent"), -1}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create entity at the root"));
    expectCode(service.execute(
                   QStringLiteral("folder.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("根层组")},
                               {QStringLiteral("parent"), -1}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create root folder"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), cameraId},
                               {QStringLiteral("name"), QStringLiteral("越界摄像机")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("update global camera"));
    expectCode(service.execute(QStringLiteral("entities.retire"),
                               QJsonObject{{QStringLiteral("id"), bId}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("retire entity in scene B"));
    failIf(project.entities().size() != countBeforeScope,
           QStringLiteral("rejected scope writes changed the project"));
    failIf(entityName(aId) != QStringLiteral("场景 A 矩形"),
           QStringLiteral("rejected scope write renamed scene A entity"));

    service.setAllowGlobal(true);
    const pvf::McpWriteResult cameraResult = service.execute(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("id"), cameraId},
                    {QStringLiteral("name"), QStringLiteral("主摄像机 2")}});
    failIf(!expectOk(cameraResult, QStringLiteral("update camera with allowGlobal")),
           QStringLiteral("allowGlobal did not unlock the camera"));
    failIf(entityName(cameraId) != QStringLiteral("主摄像机 2"),
           QStringLiteral("camera rename did not apply"));
    service.setAllowGlobal(false);

    // ---- Retire -----------------------------------------------------------
    const int countBeforeRetire = int(project.entities().size());
    const pvf::McpWriteResult retireResult = service.execute(
        QStringLiteral("entities.retire"),
        QJsonObject{{QStringLiteral("id"), textId}});
    if (expectOk(retireResult, QStringLiteral("retire entity")))
    {
        const pvf::Entity* retired = project.entity(textId);
        failIf(!retired, QStringLiteral("retire deleted the entity"));
        if (retired)
        {
            failIf(!retired->locked, QStringLiteral("retired entity is not locked"));
            const pvf::Entity* sceneFolder = project.entity(retired->parent);
            failIf(!sceneFolder || sceneFolder->name != QStringLiteral("A"),
                   QStringLiteral("retired entity is not in _retired/A"));
            const pvf::Entity* retiredRoot =
                sceneFolder ? project.entity(sceneFolder->parent) : nullptr;
            failIf(!retiredRoot || retiredRoot->name != QStringLiteral("_retired"),
                   QStringLiteral("retired scene folder has no _retired parent"));
        }
        failIf(!qFuzzyIsNull(project.value(textId, QStringLiteral("opacity"), 0)),
               QStringLiteral("retired opacity at frame 0 is not 0"));
        failIf(!qFuzzyIsNull(project.effectiveOpacity(textId, 60)),
               QStringLiteral("retired effective opacity is not 0"));
        failIf(int(project.entities().size()) < countBeforeRetire,
               QStringLiteral("retire shrank the project"));
        history.undo(project);
        const pvf::Entity* restored = project.entity(textId);
        failIf(!restored || restored->locked,
               QStringLiteral("undo did not restore the retired entity"));
        failIf(restored && restored->parent != folderId,
               QStringLiteral("undo did not restore the old parent"));
        history.redo(project);
        const pvf::Entity* retiredAgain = project.entity(textId);
        failIf(!retiredAgain || !retiredAgain->locked,
               QStringLiteral("redo did not retire the entity again"));
    }

    // ---- Batch atomicity --------------------------------------------------
    const QString aOriginalName = entityName(aId);
    const int countBeforeFailedBatch = int(project.entities().size());
    const QString undoBeforeFailedBatch = history.undoText();
    QJsonArray failedOps;
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次矩形")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.update")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("id"), aId},
                     {QStringLiteral("name"), QStringLiteral("批次改名")}}}});
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.update")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("id"), 999999},
                     {QStringLiteral("name"), QStringLiteral("不存在")}}}});
    const pvf::McpWriteResult failedBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), failedOps}});
    expectCode(failedBatch, pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("failed batch"));
    failIf(project.entities().size() != countBeforeFailedBatch,
           QStringLiteral("failed batch did not roll back entity count"));
    failIf(entityName(aId) != aOriginalName,
           QStringLiteral("failed batch kept an entity rename"));
    failIf(history.undoText() != undoBeforeFailedBatch,
           QStringLiteral("failed batch pushed a History step"));
    for (const pvf::Entity& entity : project.entities())
        failIf(entity.name == QStringLiteral("批次矩形"),
               QStringLiteral("failed batch kept a created entity"));

    const int countBeforeGoodBatch = int(project.entities().size());
    QJsonArray goodOps;
    goodOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次 1")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    goodOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次 2")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    const pvf::McpWriteResult goodBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), goodOps}});
    if (expectOk(goodBatch, QStringLiteral("successful batch")))
    {
        failIf(project.entities().size() != countBeforeGoodBatch + 2,
               QStringLiteral("successful batch did not create both entities"));
        failIf(history.undoText() != QStringLiteral("AI 批量操作"),
               QStringLiteral("batch did not push one History step"));
        history.undo(project);
        failIf(project.entities().size() != countBeforeGoodBatch,
               QStringLiteral("batch undo did not remove both entities"));
        history.redo(project);
        failIf(project.entities().size() != countBeforeGoodBatch + 2,
               QStringLiteral("batch redo did not restore both entities"));
    }

    QJsonArray tooManyOps;
    for (int index = 0; index < pvf::McpCommandService::kMaxBatchOperations + 1;
         ++index)
    {
        tooManyOps.append(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("entity.update")},
            {QStringLiteral("args"),
             QJsonObject{{QStringLiteral("id"), aId},
                         {QStringLiteral("name"), QStringLiteral("限额")}}}});
    }
    expectCode(service.execute(QStringLiteral("batch.run"),
                               QJsonObject{{QStringLiteral("operations"), tooManyOps}}),
               pvf::McpErrorCode::LimitExceeded,
               QStringLiteral("batch over 200 operations"));
    failIf(entityName(aId) != aOriginalName,
           QStringLiteral("over-limit batch changed the project"));

    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}}),
               pvf::McpErrorCode::NotAvailable,
               QStringLiteral("create image without a path"));
    expectCode(service.execute(QStringLiteral("html.create_layer"), QJsonObject()),
               pvf::McpErrorCode::NotAvailable,
               QStringLiteral("HTML layer write"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), 999999},
                               {QStringLiteral("name"), QStringLiteral("不存在")}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("update missing entity"));

    // ---- Stop during a single call ---------------------------------------
    const int countBeforeStop = int(project.entities().size());
    const QString undoBeforeStop = history.undoText();
    bool stopFired = false;
    QMetaObject::Connection stopConnection = QObject::connect(
        &project, &pvf::Project::changed, [&]() {
            if (stopFired)
                return;
            stopFired = true;
            service.stop();
        });
    const pvf::McpWriteResult stoppedCreate = service.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("急停文本")}});
    QObject::disconnect(stopConnection);
    expectCode(stoppedCreate, pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("stop during single create"));
    failIf(!stopFired, QStringLiteral("stop signal did not fire"));
    failIf(project.entities().size() != countBeforeStop,
           QStringLiteral("stopped create was not rolled back"));
    failIf(history.undoText() != undoBeforeStop,
           QStringLiteral("stopped create pushed a History step"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("急停后")}}),
               pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("write after stop"));

    // ---- Stop during a batch ---------------------------------------------
    failIf(!service.arm(sceneAFolder, &error),
           QStringLiteral("re-arm after stop: %1").arg(error));
    failIf(service.isStopped(), QStringLiteral("re-arm did not clear stopped"));
    const QString aBeforeStoppedBatch = entityName(aId);
    const QString undoBeforeStoppedBatch = history.undoText();
    stopFired = false;
    int changedCount = 0;
    stopConnection = QObject::connect(
        &project, &pvf::Project::changed, [&]() {
            ++changedCount;
            if (stopFired || changedCount < 3)
                return;
            stopFired = true;
            service.stop();
        });
    QJsonArray stoppedOps;
    for (int index = 0; index < 10; ++index)
    {
        stoppedOps.append(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("entity.update")},
            {QStringLiteral("args"),
             QJsonObject{{QStringLiteral("id"), aId},
                         {QStringLiteral("name"),
                          QStringLiteral("急停批次 %1").arg(index)}}}});
    }
    const pvf::McpWriteResult stoppedBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), stoppedOps}});
    QObject::disconnect(stopConnection);
    expectCode(stoppedBatch, pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("stop during batch"));
    failIf(!stopFired, QStringLiteral("batch stop signal did not fire"));
    failIf(entityName(aId) != aBeforeStoppedBatch,
           QStringLiteral("stopped batch was not rolled back"));
    failIf(history.undoText() != undoBeforeStoppedBatch,
           QStringLiteral("stopped batch pushed a History step"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("急停后 2")}}),
               pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("write after batch stop"));

    const QStringList operations = pvf::McpCommandService::operationNames();
    for (const QString& required :
         {QStringLiteral("entity.create"), QStringLiteral("entity.update"),
          QStringLiteral("keyframes.set"), QStringLiteral("easing.set"),
          QStringLiteral("entity.move"), QStringLiteral("folder.create"),
          QStringLiteral("entities.retire"), QStringLiteral("scene.create"),
          QStringLiteral("batch.run")})
        failIf(!operations.contains(required),
               QStringLiteral("operationNames missing %1").arg(required));

    // ---- scene.create appends and does not widen the scope ---------------
    pvf::Project sceneProject;
    pvf::Entity* sceneHolder =
        sceneProject.addVector(0, 100, 100, Qt::gray, Qt::white, 0);
    sceneProject.setValue(sceneHolder->id, QStringLiteral("x"), 0, 0.0, true);
    sceneProject.setValue(sceneHolder->id, QStringLiteral("x"), 300, 300.0, true);
    pvf::Scene* firstScene = sceneProject.addScene(
        QStringLiteral("S1"), 0, 100, QColor(90, 170, 255), QString(), &error);
    if (!firstScene)
    {
        qCritical() << "mcp-write-test: add scene S1" << error;
        return 1;
    }
    const pvf::EntityId firstSceneFolder = firstScene->folderId;
    pvf::History sceneHistory;
    pvf::McpCommandService sceneService(&sceneProject, &sceneHistory);
    failIf(!sceneService.arm(firstSceneFolder, &error),
           QStringLiteral("arm scene S1: %1").arg(error));

    expectCode(sceneService.execute(
                   QStringLiteral("scene.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("S2")},
                               {QStringLiteral("startFrame"), 50},
                               {QStringLiteral("endFrame"), 300}}),
               pvf::McpErrorCode::Conflict,
               QStringLiteral("scene.create before the last scene"));
    failIf(sceneProject.scenes().size() != 1,
           QStringLiteral("rejected scene.create changed the scene list"));

    const pvf::McpWriteResult sceneResult = sceneService.execute(
        QStringLiteral("scene.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("S2")},
                    {QStringLiteral("startFrame"), 150},
                    {QStringLiteral("endFrame"), 300}});
    QString newSceneId;
    pvf::EntityId newSceneFolder = pvf::kInvalidEntity;
    if (expectOk(sceneResult, QStringLiteral("scene.create append")))
    {
        newSceneId = sceneResult.data.value(QStringLiteral("id")).toString();
        newSceneFolder =
            sceneResult.data.value(QStringLiteral("folderId")).toInt();
        failIf(sceneProject.scenes().size() != 2,
               QStringLiteral("scene.create did not append"));
        failIf(sceneProject.scenes().last().id != newSceneId,
               QStringLiteral("scene.create did not append at the end"));
        sceneHistory.undo(sceneProject);
        failIf(sceneProject.scenes().size() != 1,
               QStringLiteral("scene.create undo did not remove the scene"));
        sceneHistory.redo(sceneProject);
        failIf(sceneProject.scenes().size() != 2,
               QStringLiteral("scene.create redo did not restore the scene"));
        if (sceneProject.scenes().size() == 2)
        {
            const pvf::EntityId redoneFolder =
                sceneProject.scenes().last().folderId;
            const pvf::Entity* redone = sceneProject.entity(redoneFolder);
            failIf(!redone || !redone->isFolder(),
                   QStringLiteral("scene.create redo left a dangling folder"));
            failIf(redoneFolder == newSceneFolder,
                   QStringLiteral("scene.create redo reused the removed folder"));
        }
    }

    const pvf::McpWriteResult scopedCreate = sceneService.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("作用域")}});
    if (expectOk(scopedCreate, QStringLiteral("create after scene.create")))
    {
        const pvf::EntityId createdId =
            scopedCreate.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* created = sceneProject.entity(createdId);
        failIf(!created || created->parent != firstSceneFolder,
               QStringLiteral("scene.create widened the write scope"));
        failIf(created && created->parent == newSceneFolder,
               QStringLiteral("new entity landed in the new scene"));
    }

    // ---- No scene requires an explicit human unlock -----------------------
    pvf::Project noSceneProject;
    pvf::Entity* noSceneEntity =
        noSceneProject.addVector(0, 100, 100, Qt::gray, Qt::white, 0);
    Q_UNUSED(noSceneEntity);
    pvf::History noSceneHistory;
    pvf::McpCommandService noSceneService(&noSceneProject, &noSceneHistory,
                                          pvf::kInvalidEntity, true);
    expectCode(noSceneService.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("无场景")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("armed without a scene"));
    expectCode(noSceneService.execute(
                   QStringLiteral("scene.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("无场景")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("scene.create without a scene"));
    failIf(!noSceneService.armUnscoped(&error),
           QStringLiteral("arm unscoped no-scene project: %1").arg(error));
    expectOk(noSceneService.execute(
                 QStringLiteral("entity.create"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("name"), QStringLiteral("显式解锁")}}),
             QStringLiteral("create after explicit unscoped unlock"));

    // ---- Project entity limit --------------------------------------------
    pvf::Project fullProject;
    QVector<pvf::Entity> manyEntities;
    manyEntities.reserve(pvf::McpCommandService::kMaxProjectEntities);
    for (int index = 1; index <= pvf::McpCommandService::kMaxProjectEntities;
         ++index)
    {
        pvf::Entity entity;
        entity.id = index;
        entity.type = pvf::EntityType::Folder;
        entity.name = QStringLiteral("填充 %1").arg(index);
        manyEntities.append(entity);
    }
    fullProject.setEntities(manyEntities);
    pvf::McpCommandService fullService(&fullProject);
    failIf(!fullService.armUnscoped(&error),
           QStringLiteral("arm unscoped full project: %1").arg(error));
    expectCode(fullService.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("超限")}}),
               pvf::McpErrorCode::LimitExceeded,
               QStringLiteral("create beyond 5000 entities"));
    failIf(fullProject.entities().size()
               != pvf::McpCommandService::kMaxProjectEntities,
           QStringLiteral("limit rejection changed the full project"));

    qInfo() << "mcp-write-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpIntegrationTest(QApplication& app)
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-integration-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
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

    QTemporaryDir projectTemp(QDir::tempPath()
                              + QStringLiteral("/pvforge-mcp-integration-XXXXXX"));
    QTemporaryDir endpointTemp(
        QDir::tempPath()
        + QStringLiteral("/pvforge-mcp-integration-endpoint-XXXXXX"));
    QTemporaryDir checkpointTemp(
        QDir::tempPath()
        + QStringLiteral("/pvforge-mcp-integration-checkpoints-XXXXXX"));
    if (!projectTemp.isValid() || !endpointTemp.isValid()
        || !checkpointTemp.isValid())
    {
        fail(QStringLiteral("could not create temporary directories"));
        return 1;
    }

    qputenv("PVFORGE_CHECKPOINT_DIR", checkpointTemp.path().toUtf8());
    const QString projectPath =
        projectTemp.path() + QStringLiteral("/integration.pvproj");
    const QString endpointPath =
        endpointTemp.path() + QStringLiteral("/mcp-endpoint.json");

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP 集成测试");
    project->width = 640;
    project->height = 360;
    project->fps = 30;

    pvf::Entity* holder =
        project->addVector(0, 100, 80, QColor(120, 120, 120), Qt::white, 0);
    if (!holder)
    {
        fail(QStringLiteral("could not add a scene entity"));
        return 1;
    }
    holder->name = QStringLiteral("场景占位");
    project->setValue(holder->id, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(holder->id, QStringLiteral("x"), 300, 120.0, true);

    QString error;
    pvf::Scene* scene = project->addScene(QStringLiteral("场景 A"), 0, 60,
                                          QColor(90, 170, 255),
                                          QStringLiteral("集成测试场景"), &error);
    if (!scene)
    {
        qCritical() << "mcp-integration-test: add scene failed" << error;
        return 1;
    }
    const pvf::EntityId sceneFolder = scene->folderId;
    project->setEntityParent(holder->id, sceneFolder);
    if (!project->save(projectPath, &error))
    {
        qCritical() << "mcp-integration-test: save failed" << error;
        return 1;
    }

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    pvf::McpServer* server = window.mcpServerForTest();
    if (!server)
    {
        fail(QStringLiteral("main window has no MCP server"));
        return 1;
    }
    server->setEndpointFilePath(endpointPath);

    QTimer::singleShot(
        300, &window,
        [&app, &window, &fail, &failIf, &ownerOnly, &checkpointTemp, &failures,
         projectPath, endpointPath, sceneFolder] {
            auto* dialog = window.openSettingsDialogForTest();
            if (!dialog)
            {
                fail(QStringLiteral("settings dialog was not created"));
                app.exit(1);
                return;
            }
            QApplication::processEvents();

            auto* enable =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpEnableCheck"));
            auto* permission = dialog->findChild<QComboBox*>(
                QStringLiteral("mcpPermissionCombo"));
            auto* errorLabel = dialog->findChild<QLabel*>(
                QStringLiteral("mcpErrorLabel"));
            auto* statusLabel = dialog->findChild<QLabel*>(
                QStringLiteral("mcpStatusLabel"));
            auto* logEnabled = dialog->findChild<QCheckBox*>(
                QStringLiteral("mcpLogEnabled"));
            auto* stdioConfigButton = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpCopyStdioConfigButton"));
            if (!enable || !permission || !errorLabel || !statusLabel
                || !logEnabled || !stdioConfigButton)
            {
                qCritical() << "mcp-integration-test: MCP controls missing";
                app.exit(1);
                return;
            }

            pvf::Project* current = window.project();
            pvf::McpServer* server = window.mcpServerForTest();
            pvf::McpLogPanel* panel = window.mcpLogPanelForTest();
            auto* statusButton = window.findChild<QToolButton*>(
                QStringLiteral("mcpStatusButton"));
            if (!current || !server || !panel || !statusButton)
            {
                fail(QStringLiteral("main window MCP objects missing"));
                app.exit(1);
                return;
            }

            const auto logText = [panel] {
                QString text;
                auto* tree = panel->findChild<QTreeWidget*>(
                    QStringLiteral("mcpLogTree"));
                if (!tree)
                    return text;
                for (int i = 0; i < tree->topLevelItemCount(); ++i)
                {
                    QTreeWidgetItem* item = tree->topLevelItem(i);
                    for (int column = 0; column < item->columnCount(); ++column)
                        text += item->text(column) + QLatin1Char('\n');
                    for (int child = 0; child < item->childCount(); ++child)
                        for (int column = 0;
                             column < item->child(child)->columnCount(); ++column)
                            text += item->child(child)->text(column)
                                    + QLatin1Char('\n');
                }
                return text;
            };

            // Only a human action starts the server.
            enable->setChecked(true);
            QApplication::processEvents();
            if (!server->isRunning())
            {
                fail(QStringLiteral("MCP did not start"));
                app.exit(1);
                return;
            }
            const quint16 port = server->port();
            const QByteArray token = server->token();
            const QByteArray tokenText = token;

            // The endpoint file follows the published contract and is private.
            const QFileInfo endpointInfo(endpointPath);
            failIf(!endpointInfo.exists(),
                   QStringLiteral("endpoint file was not written"));
            failIf(!ownerOnly(endpointPath, false),
                   QStringLiteral("endpoint file is not 0600"));
            failIf(!ownerOnly(endpointInfo.absolutePath(), true),
                   QStringLiteral("endpoint directory is not 0700"));
            QFile endpointFile(endpointPath);
            QJsonObject endpointObject;
            if (endpointFile.open(QIODevice::ReadOnly))
            {
                endpointObject =
                    QJsonDocument::fromJson(endpointFile.readAll()).object();
                endpointFile.close();
            }
            failIf(endpointObject.value(QStringLiteral("format")).toString()
                       != QStringLiteral("pvforge-mcp-endpoint"),
                   QStringLiteral("endpoint format mismatch"));
            failIf(endpointObject.value(QStringLiteral("version")).toInt() != 1,
                   QStringLiteral("endpoint version mismatch"));
            failIf(endpointObject.value(QStringLiteral("url")).toString()
                       != server->endpointUrl(),
                   QStringLiteral("endpoint URL mismatch"));
            failIf(endpointObject.value(QStringLiteral("token")).toString()
                       != QString::fromLatin1(token),
                   QStringLiteral("endpoint token mismatch"));
            failIf(qint64(endpointObject.value(QStringLiteral("pid")).toDouble())
                       != QCoreApplication::applicationPid(),
                   QStringLiteral("endpoint pid mismatch"));
            failIf(server->endpointUrl().contains(QString::fromLatin1(token)),
                   QStringLiteral("endpoint URL leaks the token"));

            // An unwritable endpoint file is only a warning: the authenticated
            // HTTP server must still come up.
            QFile endpointBlocker(projectPath + QStringLiteral(".blocker"));
            if (endpointBlocker.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                pvf::McpServer withoutEndpoint(current);
                withoutEndpoint.setEndpointFilePath(
                    endpointBlocker.fileName()
                    + QStringLiteral("/mcp-endpoint.json"));
                QString withoutEndpointError;
                const quint16 withoutEndpointPort =
                    withoutEndpoint.start(0, &withoutEndpointError);
                failIf(withoutEndpointPort == 0,
                       QStringLiteral("server refused to start when the endpoint "
                                      "file was unwritable: %1")
                           .arg(withoutEndpointError));
                withoutEndpoint.stop();
                endpointBlocker.close();
            }

            const auto toolNames = [&] {
                QStringList names;
                QJsonObject request;
                request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
                request[QStringLiteral("id")] = 1;
                request[QStringLiteral("method")] = QStringLiteral("tools/list");
                const QJsonObject response = mcpRpc(port, token, request);
                const QJsonArray tools =
                    response.value(QStringLiteral("result"))
                        .toObject()
                        .value(QStringLiteral("tools"))
                        .toArray();
                for (const QJsonValue& value : tools)
                    names.append(value.toObject()
                                     .value(QStringLiteral("name"))
                                     .toString());
                return names;
            };
            const auto callTool = [&](int id, const QString& name,
                                      const QJsonObject& args) {
                QJsonObject params;
                params[QStringLiteral("name")] = name;
                params[QStringLiteral("arguments")] = args;
                QJsonObject request;
                request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
                request[QStringLiteral("id")] = id;
                request[QStringLiteral("method")] = QStringLiteral("tools/call");
                request[QStringLiteral("params")] = params;
                const QJsonObject response = mcpRpc(port, token, request);
                const QJsonObject result =
                    response.value(QStringLiteral("result")).toObject();
                return result.value(QStringLiteral("structuredContent"))
                    .toObject();
            };
            const auto errorCode = [](const QJsonObject& structured) {
                return structured.value(QStringLiteral("error"))
                    .toObject()
                    .value(QStringLiteral("code"))
                    .toString();
            };

            // Read-only: no write tool is advertised and a direct call is refused.
            QStringList names = toolNames();
            for (const QString& required :
                 {QStringLiteral("project.summary"), QStringLiteral("timeline.get"),
                  QStringLiteral("entity.get"), QStringLiteral("assets.list"),
                  QStringLiteral("guide.get")})
                failIf(!names.contains(required),
                       QStringLiteral("read-only tools/list missing %1")
                           .arg(required));
            for (const QString& write : pvf::McpServer::writeToolNames())
                failIf(names.contains(write),
                       QStringLiteral("read-only tools/list advertises %1")
                           .arg(write));
            const QJsonObject denied = callTool(
                2, QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("name"), QStringLiteral("只读拒绝")}});
            failIf(errorCode(denied) != QStringLiteral("permission_denied"),
                   QStringLiteral("read-only write call did not return "
                                  "permission_denied"));

            // A project without a saved path must not arm.
            const QString savedPath = current->filePath;
            current->filePath.clear();
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            failIf(window.mcpCommandsForTest() != nullptr,
                   QStringLiteral("unsaved project armed writes"));
            failIf(errorLabel->isHidden()
                       || !errorLabel->text().contains(QStringLiteral("保存")),
                   QStringLiteral("unsaved project did not explain the save "
                                  "requirement"));
            failIf(permission->currentIndex() != 0,
                   QStringLiteral("permission combo did not revert after failure"));
            current->filePath = savedPath;

            // A playhead in a scene gap must not arm.
            window.goToFrame(90);
            QApplication::processEvents();
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            failIf(window.mcpCommandsForTest() != nullptr,
                   QStringLiteral("scene gap armed writes"));
            failIf(!errorLabel->text().contains(QStringLiteral("场景")),
                   QStringLiteral("scene gap did not explain the scene requirement"));
            failIf(permission->currentIndex() != 0,
                   QStringLiteral("permission combo did not revert after gap "
                                  "failure"));
            window.goToFrame(10);
            QApplication::processEvents();

            pvf::CheckpointManager checkpoints(checkpointTemp.path());
            failIf(!checkpoints.listCheckpoints(projectPath).isEmpty(),
                   QStringLiteral("failed arm attempts created checkpoints"));

            // With a saved project and an active scene, arming succeeds only
            // after a durable checkpoint.
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            pvf::McpCommandService* commands = window.mcpCommandsForTest();
            failIf(!commands || !commands->isArmed(),
                   QStringLiteral("saved scene did not arm writes"));
            if (commands)
            {
                failIf(commands->sceneRootId() != sceneFolder,
                       QStringLiteral("armed scope is not the scene folder"));
                failIf(commands->sceneName() != QStringLiteral("场景 A"),
                       QStringLiteral("armed scope has the wrong scene name"));
                failIf(commands->allowGlobal(),
                       QStringLiteral("global objects unlocked without UI"));
            }
            failIf(!errorLabel->isHidden(),
                   QStringLiteral("arm success left an error visible"));
            const QVector<pvf::CheckpointInfo> firstCheckpoints =
                checkpoints.listCheckpoints(projectPath);
            failIf(firstCheckpoints.isEmpty(),
                   QStringLiteral("arm did not create a checkpoint"));
            failIf(!logText().contains(QStringLiteral("已武装")),
                   QStringLiteral("log does not show the armed scene"));
            failIf(!statusButton->text().contains(QStringLiteral("武装")),
                   QStringLiteral("status bar does not show armed state"));

            names = toolNames();
            for (const QString& write : pvf::McpServer::writeToolNames())
                failIf(!names.contains(write),
                       QStringLiteral("armed tools/list missing %1").arg(write));

            // HTTP write tools go straight to McpCommandService.
            const QJsonObject created = callTool(
                10, QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("name"), QStringLiteral("AI 标题")}});
            failIf(!created.value(QStringLiteral("ok")).toBool(),
                   QStringLiteral("entity.create failed: %1")
                       .arg(QString::fromUtf8(
                           QJsonDocument(created).toJson(QJsonDocument::Compact))));
            const pvf::EntityId createdId =
                created.value(QStringLiteral("data"))
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toInt();
            const pvf::Entity* createdEntity = current->entity(createdId);
            failIf(!createdEntity || createdEntity->parent != sceneFolder,
                   QStringLiteral("entity.create did not land in the armed scene"));

            QJsonArray keys;
            keys.append(QJsonObject{{QStringLiteral("frame"), 0},
                                    {QStringLiteral("value"), 0}});
            keys.append(QJsonObject{{QStringLiteral("frame"), 30},
                                    {QStringLiteral("value"), 120}});
            const QJsonObject keyed = callTool(
                11, QStringLiteral("keyframes.set"),
                QJsonObject{{QStringLiteral("id"), createdId},
                            {QStringLiteral("property"), QStringLiteral("x")},
                            {QStringLiteral("keys"), keys}});
            failIf(!keyed.value(QStringLiteral("ok")).toBool(),
                   QStringLiteral("keyframes.set failed"));
            if (const pvf::Entity* entity = current->entity(createdId))
                failIf(entity->tracks.value(QStringLiteral("x")).keys.size() != 2,
                       QStringLiteral("keyframes.set did not write two keys"));

            QJsonArray operations;
            operations.append(QJsonObject{
                {QStringLiteral("op"), QStringLiteral("entity.update")},
                {QStringLiteral("args"),
                 QJsonObject{{QStringLiteral("id"), createdId},
                             {QStringLiteral("name"),
                              QStringLiteral("批量改名")}}}});
            operations.append(QJsonObject{
                {QStringLiteral("op"), QStringLiteral("keyframes.set")},
                {QStringLiteral("args"),
                 QJsonObject{
                     {QStringLiteral("id"), createdId},
                     {QStringLiteral("property"), QStringLiteral("y")},
                     {QStringLiteral("keys"),
                      QJsonArray{
                          QJsonObject{{QStringLiteral("frame"), 0},
                                      {QStringLiteral("value"), 0}},
                          QJsonObject{{QStringLiteral("frame"), 15},
                                      {QStringLiteral("value"), 60}}}}}}});
            const QJsonObject batch = callTool(
                12, QStringLiteral("batch.run"),
                QJsonObject{{QStringLiteral("operations"), operations}});
            failIf(!batch.value(QStringLiteral("ok")).toBool(),
                   QStringLiteral("batch.run failed"));
            failIf(batch.value(QStringLiteral("data"))
                           .toObject()
                           .value(QStringLiteral("count"))
                           .toInt()
                       != 2,
                   QStringLiteral("batch.run count mismatch"));
            if (const pvf::Entity* entity = current->entity(createdId))
            {
                failIf(entity->name != QStringLiteral("批量改名"),
                       QStringLiteral("batch.run did not rename the entity"));
                failIf(entity->tracks.value(QStringLiteral("y")).keys.size() != 2,
                       QStringLiteral("batch.run did not set y keys"));
            }

            // Notifications are recorded without interrupting the call.
            int notifications = 0;
            QString lastNotification;
            QObject::connect(server, &pvf::McpServer::notificationReceived,
                             [&](const QString& method, const QString&) {
                                 ++notifications;
                                 lastNotification = method;
                             });
            QJsonObject notification;
            notification[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            notification[QStringLiteral("method")] =
                QStringLiteral("notifications/cancelled");
            notification[QStringLiteral("params")] =
                QJsonObject{{QStringLiteral("requestId"), 11},
                            {QStringLiteral("reason"), QStringLiteral("test")}};
            const McpHttpResponse notificationResponse = mcpHttpPost(
                port,
                QJsonDocument(notification).toJson(QJsonDocument::Compact),
                token);
            failIf(notificationResponse.status != 202,
                   QStringLiteral("notification HTTP %1 != 202")
                       .arg(notificationResponse.status));
            failIf(notifications != 1
                       || lastNotification
                              != QStringLiteral("notifications/cancelled"),
                   QStringLiteral("cancellation notification was not forwarded"));

            // Token never reaches logs, status text or errors.
            failIf(logText().contains(QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the log"));
            failIf(statusButton->text().contains(QString::fromLatin1(tokenText))
                       || statusButton->toolTip().contains(
                           QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the status entry"));
            failIf(statusLabel->text().contains(QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the settings status"));
            failIf(errorLabel->text().contains(QString::fromLatin1(tokenText)),
                   QStringLiteral("token is echoed in the settings error"));

            // Emergency stop: writes answer stopped_by_human and change nothing.
            const int entityCountBeforeStop = int(current->entities().size());
            auto* stopButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogStopButton"));
            failIf(!stopButton || !stopButton->isEnabled(),
                   QStringLiteral("stop AI button is missing or disabled"));
            if (stopButton)
                stopButton->click();
            QApplication::processEvents();
            failIf(!commands || !commands->isStopped() || commands->isArmed(),
                   QStringLiteral("emergency stop did not mark the service "
                                  "stopped"));
            auto* logStatus = panel->findChild<QLabel*>(
                QStringLiteral("mcpLogStatusLabel"));
            failIf(!logStatus
                       || !logStatus->text().contains(QStringLiteral("急停")),
                   QStringLiteral("log header does not show the emergency stop"));
            failIf(!statusButton->text().contains(QStringLiteral("急停")),
                   QStringLiteral("status bar does not show the emergency stop"));
            const QJsonObject stopped = callTool(
                20, QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("name"), QStringLiteral("急停拒绝")}});
            failIf(errorCode(stopped) != QStringLiteral("stopped_by_human"),
                   QStringLiteral("post-stop write did not return "
                                  "stopped_by_human"));
            failIf(current->entities().size() != entityCountBeforeStop,
                   QStringLiteral("post-stop write changed the project"));
            names = toolNames();
            for (const QString& write : pvf::McpServer::writeToolNames())
                failIf(names.contains(write),
                       QStringLiteral("stopped tools/list advertises %1")
                           .arg(write));

            // Re-arm for the same scene; each arm creates a new checkpoint.
            failIf(permission->currentIndex() != 0,
                   QStringLiteral("emergency stop did not reset the permission "
                                  "combo"));
            permission->setCurrentIndex(1);
            QApplication::processEvents();
            failIf(!window.mcpCommandsForTest()
                       || !window.mcpCommandsForTest()->isArmed(),
                   QStringLiteral("re-arm after stop failed"));
            failIf(window.mcpCommandsForTest() == commands,
                   QStringLiteral("re-arm reused the stopped service"));
            failIf(checkpoints.listCheckpoints(projectPath).size()
                       <= firstCheckpoints.size(),
                   QStringLiteral("re-arm did not create another checkpoint"));

            // Stopping MCP removes the endpoint file and the write service.
            enable->setChecked(false);
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("disabling MCP did not stop the server"));
            failIf(QFileInfo::exists(endpointPath),
                   QStringLiteral("endpoint file survived MCP shutdown"));
            failIf(window.mcpCommandsForTest() != nullptr,
                   QStringLiteral("write service survived MCP shutdown"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status is not stopped"));

            // The stdio configuration references the installed bridge.
            stdioConfigButton->click();
            QApplication::processEvents();
            const QJsonObject stdioConfig =
                QJsonDocument::fromJson(
                    QApplication::clipboard()->text().toUtf8())
                    .object();
            const QJsonObject stdioServer =
                stdioConfig.value(QStringLiteral("mcpServers"))
                    .toObject()
                    .value(QStringLiteral("pvforge"))
                    .toObject();
            failIf(!stdioServer.value(QStringLiteral("command"))
                        .toString()
                        .endsWith(QStringLiteral("pvforge-mcp")),
                   QStringLiteral("stdio config has no pvforge-mcp command"));
            failIf(!stdioServer.value(QStringLiteral("args"))
                        .toArray()
                        .contains(QJsonValue(QStringLiteral("--endpoint-file"))),
                   QStringLiteral("stdio config has no --endpoint-file"));

            dialog->close();
            QApplication::processEvents();

            qInfo() << "mcp-integration-test failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    return app.exec();
}

} // namespace

namespace {

QPointF rotatePointForTest(const QPointF& point, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    return QPointF(point.x() * std::cos(radians) - point.y() * std::sin(radians),
                   point.x() * std::sin(radians) + point.y() * std::cos(radians));
}

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

} // namespace

int sessionGizmoTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    // P3b snap settings: the 0/30/45/90 set and a 6 px threshold.  Use the
    // same explicit organisation/application QSettings as SettingsStore.
    {
        QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
        settings.setValue(QStringLiteral("snap/pixels"), 6);
        settings.setValue(QStringLiteral("snap/canvasCenter"), true);
        settings.setValue(QStringLiteral("snap/entityEdges"), true);
        settings.setValue(QStringLiteral("snap/edgeMidpoints"), true);
        settings.setValue(QStringLiteral("snap/angles"),
                          QStringLiteral("0,30,45,90"));
        settings.sync();
    }

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p3b-gizmo.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P3b Gizmo");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "gizmo-test: create failed" << error;
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
    entity.content.vector.fillColor = QStringLiteral("#ffffff");
    // Model space is centred on the canvas origin with +Y up, so (0,0) is the
    // middle of the 640x360 canvas.
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    const pvf::Uuid uuid = entity.uuid;
    project.entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("pvf: init"),
                                          &error))
    {
        qCritical() << "gizmo-test: init commit failed" << error;
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
        qCritical() << "gizmo-test: no session viewport";
        return 1;
    }
    auto* document = sessionPtr->document();
    const auto flushSession = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        return flushError;
    };
    document->setSelection(uuid);
    sessionPtr->setPlayhead(0);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();

    MouseDriver mouse{viewport};
    const auto undoDepth = [document] { return document->store()->undoDepth(); };

    // 1) The gizmo axes follow the entity: +X right, +Y up.  Model space is
    //    Y-up (the viewport applies the single model -> screen Y flip) and a
    //    positive rotation is counter-clockwise on screen.
    const QPointF pivot = viewport->sessionPivotForTest();
    const QPointF modelOrigin =
        viewport->projectToWidgetForTest(QPointF(0.0, 0.0));
    if (std::hypot(pivot.x() - modelOrigin.x(),
                   pivot.y() - modelOrigin.y()) > 0.5)
    {
        qCritical() << "gizmo-test: model origin is not the canvas centre"
                    << modelOrigin << pivot;
        ++failures;
    }
    const QPointF xHandle =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    const QPointF yHandle =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY);
    if (!(xHandle.x() > pivot.x() + 10.0
          && std::abs(xHandle.y() - pivot.y()) < 1.0))
    {
        qCritical() << "gizmo-test: X axis not to the right" << pivot << xHandle;
        ++failures;
    }
    if (!(yHandle.y() < pivot.y() - 10.0
          && std::abs(yHandle.x() - pivot.x()) < 1.0))
    {
        qCritical() << "gizmo-test: Y axis not up" << pivot << yHandle;
        ++failures;
    }

    // 2) Rotate 90 degrees counter-clockwise on screen with the corner handle.
    //    Widget space is Y-down, so the screen-space target is -90 while the
    //    model rotation must become +90.  One gesture = one commit.
    flushSession();
    const int depthBeforeRotate = undoDepth();
    const QPointF rotateStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF rotateTarget =
        pivot + rotatePointForTest(rotateStart - pivot, -90.0);
    mouse.drag(rotateStart, rotateTarget);
    QApplication::processEvents();
    const QString rotateFlushError = flushSession();
    if (!rotateFlushError.isEmpty())
    {
        qCritical() << "gizmo-test: rotate flush failed" << rotateFlushError;
        ++failures;
    }
    pvf::gizmo::Transform rotated = viewport->gizmoTransformForTest(uuid);
    if (std::abs(std::remainder(rotated.rotation - 90.0, 360.0)) > 0.01)
    {
        qCritical() << "gizmo-test: rotation snap 90 failed" << rotated.rotation;
        ++failures;
    }
    if (undoDepth() != depthBeforeRotate + 1)
    {
        qCritical() << "gizmo-test: rotation was not exactly one commit"
                    << depthBeforeRotate << undoDepth();
        ++failures;
    }

    // Auto-key writes the current frame's key (relative frame 0 here).
    {
        const auto& rotatedEntity = sessionPtr->project().entities[uuid];
        const auto track = rotatedEntity.tracks.constFind(
            QStringLiteral("rotation"));
        bool keyed = false;
        if (track != rotatedEntity.tracks.constEnd())
            for (const pvf::model::Keyframe& key : track.value().keys)
                if (key.frame == 0
                    && std::abs(std::remainder(key.value - 90.0, 360.0)) < 0.01)
                    keyed = true;
        if (!keyed)
        {
            qCritical() << "gizmo-test: auto-key did not write the rotation key";
            ++failures;
        }
    }

    // After rotating +90 (counter-clockwise) the local axes must have turned
    // with the entity: +X points up on screen, +Y points left.
    const QPointF pivot90 = viewport->sessionPivotForTest();
    const QPointF xHandle90 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    const QPointF yHandle90 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY);
    if (!(xHandle90.y() < pivot90.y() - 10.0
          && std::abs(xHandle90.x() - pivot90.x()) < 1.0))
    {
        qCritical() << "gizmo-test: rotated X axis did not follow"
                    << pivot90 << xHandle90;
        ++failures;
    }
    if (!(yHandle90.x() < pivot90.x() - 10.0
          && std::abs(yHandle90.y() - pivot90.y()) < 1.0))
    {
        qCritical() << "gizmo-test: rotated Y axis did not follow"
                    << pivot90 << yHandle90;
        ++failures;
    }
    document->undo();
    QApplication::processEvents();
    if (std::abs(viewport->gizmoTransformForTest(uuid).rotation) > 0.01)
    {
        qCritical() << "gizmo-test: undo did not restore rotation";
        ++failures;
    }

    // 3) Drag projection: with rotation 30 the local X axis is 30 degrees
    // away from screen X, so a horizontal 120 px drag only advances x by the
    // projected amount.  Ctrl disables snapping.
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 30.0;
    document->commitEntity(uuid, QStringLiteral("pvf: set rotation"), &error);
    flushSession();
    QApplication::processEvents();
    // A +30 model rotation is counter-clockwise: the 48 px +X handle moves
    // right and up on screen, and +Y moves left and up.
    const QPointF pivot30 = viewport->sessionPivotForTest();
    const QPointF x30 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX) - pivot30;
    const QPointF y30 =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY) - pivot30;
    const double axisLength = 48.0;
    const double cos30 = std::cos(qDegreesToRadians(30.0));
    const double sin30 = std::sin(qDegreesToRadians(30.0));
    if (std::abs(x30.x() - axisLength * cos30) > 0.5
        || std::abs(x30.y() + axisLength * sin30) > 0.5
        || std::abs(y30.x() + axisLength * sin30) > 0.5
        || std::abs(y30.y() + axisLength * cos30) > 0.5)
    {
        qCritical() << "gizmo-test: rotation 30 axes are not Y-up/CCW" << x30
                    << y30;
        ++failures;
    }
    const pvf::gizmo::Transform beforeProjection =
        viewport->gizmoTransformForTest(uuid);
    const QPointF axisStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    const double scale = viewport->currentScale();
    mouse.drag(axisStart, axisStart + QPointF(120.0, 0.0),
               Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform projected = viewport->gizmoTransformForTest(uuid);
    const double expected = (120.0 / scale) * std::cos(qDegreesToRadians(30.0));
    const double deltaX = projected.x - beforeProjection.x;
    const double deltaY = projected.y - beforeProjection.y;
    if (std::abs(deltaX - expected) > 1.5 || std::abs(deltaY) > 1.5)
    {
        qCritical() << "gizmo-test: X axis projection wrong" << deltaX << deltaY
                    << expected;
        ++failures;
    }
    // The projection gesture wrote auto-key tracks at frame 0; undo it so the
    // next checks start from the plain rotation-30 state.
    document->undo();
    QApplication::processEvents();

    // The Y axis is perpendicular: the same horizontal drag advances y by the
    // projected amount instead.
    const QPointF yAxisStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveY);
    const pvf::gizmo::Transform beforeY =
        viewport->gizmoTransformForTest(uuid);
    mouse.drag(yAxisStart, yAxisStart + QPointF(120.0, 0.0),
               Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform movedY = viewport->gizmoTransformForTest(uuid);
    const double expectedY =
        -(120.0 / scale) * std::sin(qDegreesToRadians(30.0));
    if (std::abs(movedY.y - beforeY.y - expectedY) > 1.5
        || std::abs(movedY.x - beforeY.x) > 1.5)
    {
        qCritical() << "gizmo-test: Y axis projection wrong"
                    << movedY.x - beforeY.x << movedY.y - beforeY.y << expectedY;
        ++failures;
    }
    document->undo();
    QApplication::processEvents();

    // 4) Angle snapping from AppSettings (0/30/45/90) and Ctrl bypass.
    if (std::abs(viewport->snapAngleForTest(28.5, false) - 30.0) > 0.01
        || std::abs(viewport->snapAngleForTest(47.0, true) - 45.0) > 0.01
        || std::abs(viewport->snapAngleForTest(52.0, false) - 52.0) > 0.01)
    {
        qCritical() << "gizmo-test: angle snap settings wrong"
                    << viewport->snapAngleForTest(28.5, false)
                    << viewport->snapAngleForTest(47.0, true)
                    << viewport->snapAngleForTest(52.0, false);
        ++failures;
    }
    const double rotationBeforeCtrl = viewport->gizmoTransformForTest(uuid).rotation;
    const QPointF ctrlRotateStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF ctrlPivot = viewport->sessionPivotForTest();
    // 28.5 degrees counter-clockwise on screen: widget Y is down, so the
    // screen-space rotation is negative while the model delta stays positive.
    const QPointF ctrlRotateTarget =
        ctrlPivot + rotatePointForTest(ctrlRotateStart - ctrlPivot, -28.5);
    mouse.drag(ctrlRotateStart, ctrlRotateTarget, Qt::ControlModifier);
    QApplication::processEvents();
    const double ctrlRotation = viewport->gizmoTransformForTest(uuid).rotation;
    const double ctrlDelta =
        std::remainder(ctrlRotation - rotationBeforeCtrl, 360.0);
    if (std::abs(ctrlDelta - 28.5) > 0.6)
    {
        qCritical() << "gizmo-test: Ctrl did not bypass angle snap" << ctrlDelta;
        ++failures;
    }
    flushSession();
    document->undo();
    QApplication::processEvents();

    // 5) Move snapping: near the canvas centre (the model origin) the drag
    // snaps to it; Ctrl keeps the unsnapped position.
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 0.0;
    sessionPtr->mutableProject().entities[uuid].transform.x = 3.0;
    sessionPtr->mutableProject().entities[uuid].transform.y = 0.0;
    document->commitEntity(uuid, QStringLiteral("pvf: offset"), &error);
    flushSession();
    QApplication::processEvents();
    const QPointF snapStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    mouse.drag(snapStart, snapStart + QPointF(1.0, 0.0));
    QApplication::processEvents();
    flushSession();
    const double snappedX = viewport->gizmoTransformForTest(uuid).x;
    if (std::abs(snappedX - 0.0) > 0.01)
    {
        qCritical() << "gizmo-test: centre-line snap failed" << snappedX;
        ++failures;
    }
    document->undo();
    QApplication::processEvents();
    const QPointF ctrlStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    mouse.drag(ctrlStart, ctrlStart + QPointF(1.0, 0.0), Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const double ctrlX = viewport->gizmoTransformForTest(uuid).x;
    const double expectedCtrlX = 3.0 + 1.0 / scale;
    if (std::abs(ctrlX - expectedCtrlX) > 0.2)
    {
        qCritical() << "gizmo-test: Ctrl did not bypass move snap" << ctrlX
                    << expectedCtrlX;
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    // 6) Esc cancels the live gesture without a commit.
    flushSession();
    const int depthBeforeCancel = undoDepth();
    const QPointF cancelStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
    mouse.send(QEvent::MouseButtonPress, cancelStart, Qt::LeftButton,
               Qt::LeftButton);
    mouse.send(QEvent::MouseMove, cancelStart + QPointF(80.0, 0.0), Qt::NoButton,
               Qt::LeftButton);
    if (!viewport->sessionDraggingForTest())
    {
        qCritical() << "gizmo-test: drag did not start";
        ++failures;
    }
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(viewport, &escape);
    mouse.send(QEvent::MouseButtonRelease, cancelStart + QPointF(80.0, 0.0),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    if (undoDepth() != depthBeforeCancel
        || std::abs(viewport->gizmoTransformForTest(uuid).x - 3.0) > 0.01)
    {
        qCritical() << "gizmo-test: Esc did not cancel" << undoDepth()
                    << viewport->gizmoTransformForTest(uuid).x;
        ++failures;
    }

    // 7) Workbench cache is bypassed while dragging and refreshed on release.
    auto* workbench = sessionPtr->workbench();
    if (!workbench)
    {
        qCritical() << "gizmo-test: workbench cache missing";
        ++failures;
    }
    else
    {
        viewport->repaint();
        QApplication::processEvents();
        bool hit = false;
        const QImage cachedBefore = workbench->lookup(0, pvf::Uuid(), &hit);
        if (!hit || cachedBefore.isNull())
        {
            qCritical() << "gizmo-test: cache was not seeded";
            ++failures;
        }

        const int hitsBefore = workbench->stats().hits;
        const QPointF cacheStart =
            viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::MoveX);
        mouse.send(QEvent::MouseButtonPress, cacheStart, Qt::LeftButton,
                   Qt::LeftButton);
        mouse.send(QEvent::MouseMove, cacheStart + QPointF(60.0, 0.0),
                   Qt::NoButton, Qt::LeftButton);
        viewport->repaint();
        QApplication::processEvents();
        if (workbench->stats().hits != hitsBefore)
        {
            qCritical() << "gizmo-test: cache was used during a drag"
                        << hitsBefore << workbench->stats().hits;
            ++failures;
        }
        mouse.send(QEvent::MouseButtonRelease, cacheStart + QPointF(60.0, 0.0),
                   Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();

        bool updated = false;
        QImage cachedAfter;
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < 3000)
        {
            bool afterHit = false;
            cachedAfter = workbench->lookup(0, pvf::Uuid(), &afterHit);
            if (afterHit && !cachedAfter.isNull()
                && cachedAfter != cachedBefore)
            {
                updated = true;
                break;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
        }
        if (!updated)
        {
            qCritical() << "gizmo-test: cache was not refreshed after release";
            ++failures;
        }
    }

    // Screenshot with a visible gizmo.  The floating monitor would cover the
    // handles, so hide it for this shot only.
    if (auto* monitor = window.findChild<pvf::CameraMonitor*>())
        monitor->hide();
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 30.0;
    document->commitEntity(uuid, QStringLiteral("pvf: shot rotation"), &error);
    flushSession();
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-p3b-gizmo.png");
    if (!window.grab().save(screenshot))
    {
        qCritical() << "gizmo-test: screenshot failed" << screenshot;
        ++failures;
    }
    else
    {
        qInfo() << "gizmo-test: screenshot" << screenshot;
    }

    if (failures == 0)
        qInfo() << "gizmo-test: ok";
    return failures == 0 ? 0 : 1;
}

// Inkscape-style corner rotate handles: drawing geometry, square hit boxes,
// constant screen size, Shift-opposite rotation, Ctrl snap bypass, one commit
// per gesture, Esc cancel and negative-scale corner identity.
int inkscapeHandlesTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    {
        QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
        settings.setValue(QStringLiteral("snap/pixels"), 6);
        settings.setValue(QStringLiteral("snap/angles"), QStringLiteral("0,30,45,90"));
        settings.sync();
    }

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("inkscape-handles.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Inkscape Handles");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "inkscape-handles-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("弯箭头");
    entity.start = 0;
    entity.end = 60;
    entity.row = 0;
    entity.content.kind = pvf::ContentKind::Vector;
    entity.content.vector.width = 200.0;
    entity.content.vector.height = 100.0;
    entity.content.vector.fillColor = QStringLiteral("#ffffff");
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    const pvf::Uuid uuid = entity.uuid;
    project.entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("pvf: init"), &error))
    {
        qCritical() << "inkscape-handles-test: init commit failed" << error;
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
        qCritical() << "inkscape-handles-test: no session viewport";
        return 1;
    }
    auto* document = sessionPtr->document();
    const auto flushSession = [sessionPtr] {
        QString flushError;
        sessionPtr->flush(&flushError);
        return flushError;
    };
    document->setSelection(uuid);
    sessionPtr->setPlayhead(0);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();

    MouseDriver mouse{viewport};
    const auto undoDepth = [document] { return document->store()->undoDepth(); };
    const auto nearAngle = [](double value, double expected) {
        return std::abs(std::remainder(value - expected, 360.0)) < 0.01;
    };
    const auto rotateHandle = [&](int visualCorner) {
        return pvf::gizmo::rotateHandleForVisualCorner(visualCorner);
    };
    const auto expectedAngles = [&](const std::array<double, 4>& angles,
                                    double rotation, const char* label) {
        for (int visual = 0; visual < 4; ++visual)
        {
            const double actual =
                viewport->gizmoRotateGlyphAngleForTest(uuid, visual);
            // A positive entity rotation is counter-clockwise in the Y-up
            // model; QPainter::rotate() is clockwise on screen, so the glyph
            // angle must decrease by the same amount.
            if (!nearAngle(actual, angles[visual] - rotation))
            {
                qCritical() << "inkscape-handles-test:" << label << "glyph"
                            << visual << "angle" << actual << "expected"
                            << angles[visual] - rotation;
                ++failures;
            }
        }
    };

    // 1) Unzoomed geometry: each corner glyph rides its visual corner at a
    //    fixed 3 px inset, and each hit box is an 18 px axis-aligned square.
    const std::array<double, 4> baseAngles{270.0, 0.0, 90.0, 180.0};
    if (std::abs(pvf::gizmo::kRotateHandleSize - 18.0) > 1e-9
        || std::abs(pvf::gizmo::kRotateHitSize - 18.0) > 1e-9)
    {
        qCritical() << "inkscape-handles-test: unexpected handle size";
        ++failures;
    }
    for (int visual = 0; visual < 4; ++visual)
    {
        const QPointF corner = viewport->gizmoCornerForTest(uuid, visual);
        const pvf::gizmo::Handle handle = rotateHandle(visual);
        const QPointF centre = viewport->gizmoHandleForTest(uuid, handle);
        const double inset = std::hypot(centre.x() - corner.x(),
                                        centre.y() - corner.y());
        if (std::abs(inset - pvf::gizmo::kRotateHandleInset) > 0.01)
        {
            qCritical() << "inkscape-handles-test: inset" << visual << inset;
            ++failures;
        }
        const QRectF hit = viewport->gizmoRotateHitRectForTest(uuid, handle);
        if (std::abs(hit.width() - pvf::gizmo::kRotateHitSize) > 0.01
            || std::abs(hit.height() - pvf::gizmo::kRotateHitSize) > 0.01
            || std::abs(hit.center().x() - centre.x()) > 0.01
            || std::abs(hit.center().y() - centre.y()) > 0.01)
        {
            qCritical() << "inkscape-handles-test: hit rect" << visual << hit;
            ++failures;
        }
        if (viewport->gizmoHandleAtForTest(centre) != handle)
        {
            qCritical() << "inkscape-handles-test: centre hit" << visual;
            ++failures;
        }
        // Square, not circle: a diagonal offset closer than the half-size is
        // inside, a point past the side is outside.  Probe the outward
        // diagonal so the nearby scale square cannot win the nearest-centre
        // test.
        const QPointF outward = corner - centre;
        const QPointF inside = centre
                               + QPointF(outward.x() < 0.0 ? -8.9 : 8.9,
                                         outward.y() < 0.0 ? -8.9 : 8.9);
        const pvf::gizmo::Handle hitInside = viewport->gizmoHandleAtForTest(inside)
                                                 .value_or(pvf::gizmo::Handle::MoveX);
        if (!pvf::gizmo::isRotateHandle(hitInside))
        {
            qCritical() << "inkscape-handles-test: square corner miss" << visual;
            ++failures;
        }
        const QPointF outside = centre + QPointF(9.2, 0.0);
        const auto outsideHit = viewport->gizmoHandleAtForTest(outside);
        if (outsideHit && pvf::gizmo::isRotateHandle(*outsideHit))
        {
            qCritical() << "inkscape-handles-test: square side hit" << visual;
            ++failures;
        }
    }
    expectedAngles(baseAngles, 0.0, "rotation 0");

    // 2) Zoom must not change the screen-pixel inset, hit size or icon angle.
    const QPointF zoomCentre = viewport->rect().center();
    for (int step = 0; step < 4; ++step)
    {
        QWheelEvent zoomEvent(zoomCentre,
                              viewport->mapToGlobal(zoomCentre.toPoint()),
                              QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewport, &zoomEvent);
    }
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    for (int visual = 0; visual < 4; ++visual)
    {
        const QPointF corner = viewport->gizmoCornerForTest(uuid, visual);
        const pvf::gizmo::Handle handle = rotateHandle(visual);
        const QPointF centre = viewport->gizmoHandleForTest(uuid, handle);
        const double inset = std::hypot(centre.x() - corner.x(),
                                        centre.y() - corner.y());
        const QRectF hit = viewport->gizmoRotateHitRectForTest(uuid, handle);
        if (std::abs(inset - pvf::gizmo::kRotateHandleInset) > 0.01
            || std::abs(hit.width() - pvf::gizmo::kRotateHitSize) > 0.01
            || std::abs(hit.height() - pvf::gizmo::kRotateHitSize) > 0.01)
        {
            qCritical() << "inkscape-handles-test: zoom changed geometry"
                        << visual << inset << hit.size();
            ++failures;
        }
    }
    expectedAngles(baseAngles, 0.0, "zoomed");
    viewport->resetView();
    QApplication::processEvents();

    // 3) Icon direction turns with the entity in 90 degree steps (the glyph
    //    angle decreases as the Y-up rotation grows counter-clockwise).
    const auto setRotation = [&](double rotation) {
        auto it = sessionPtr->mutableProject().entities.find(uuid);
        if (it == sessionPtr->mutableProject().entities.end())
            return;
        it->transform.rotation = rotation;
        document->commitEntity(uuid, QStringLiteral("pvf: set rotation"), &error);
        flushSession();
        QApplication::processEvents();
        viewport->repaint();
        QApplication::processEvents();
    };
    const auto setScale = [&](double scaleX, double scaleY) {
        auto it = sessionPtr->mutableProject().entities.find(uuid);
        if (it == sessionPtr->mutableProject().entities.end())
            return;
        it->transform.scaleX = scaleX;
        it->transform.scaleY = scaleY;
        document->commitEntity(uuid, QStringLiteral("pvf: set scale"), &error);
        flushSession();
        QApplication::processEvents();
        viewport->repaint();
        QApplication::processEvents();
    };

    setRotation(90.0);
    expectedAngles(baseAngles, 90.0, "rotation 90");
    setRotation(180.0);
    expectedAngles(baseAngles, 180.0, "rotation 180");
    setRotation(0.0);
    document->setSelection(uuid);
    QApplication::processEvents();

    // 4) Shift: rotate around the opposite visual corner.  A screen-space -90
    //    degree drag is +90 in the Y-up model.
    flushSession();
    const int depthBeforeShift = undoDepth();
    const QPointF shiftCorner = viewport->gizmoCornerForTest(uuid, 3);
    const QPointF shiftStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF shiftTarget =
        shiftCorner + rotatePointForTest(shiftStart - shiftCorner, -90.0);
    mouse.send(QEvent::MouseButtonPress, shiftStart, Qt::LeftButton,
               Qt::LeftButton, Qt::ShiftModifier);
    mouse.send(QEvent::MouseMove, shiftTarget, Qt::NoButton, Qt::LeftButton,
               Qt::ShiftModifier);
    if (!viewport->sessionDraggingForTest()
        || !viewport->gizmoDraggingAroundCornerForTest()
        || viewport->gizmoCornerHandlesVisibleForTest())
    {
        qCritical() << "inkscape-handles-test: Shift drag state"
                    << viewport->sessionDraggingForTest()
                    << viewport->gizmoDraggingAroundCornerForTest()
                    << viewport->gizmoCornerHandlesVisibleForTest();
        ++failures;
    }
    if (std::hypot(viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).x()
                       - shiftCorner.x(),
                   viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).y()
                       - shiftCorner.y())
        > 0.6)
    {
        qCritical() << "inkscape-handles-test: Shift origin"
                    << viewport->projectToWidgetForTest(
                           viewport->gizmoDragOriginForTest())
                    << shiftCorner;
        ++failures;
    }
    mouse.send(QEvent::MouseButtonRelease, shiftTarget, Qt::LeftButton,
               Qt::NoButton, Qt::ShiftModifier);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform shiftResult = viewport->gizmoTransformForTest(uuid);
    const QPointF shiftCornerAfter = viewport->gizmoCornerForTest(uuid, 3);
    if (!nearAngle(shiftResult.rotation, 90.0))
    {
        qCritical() << "inkscape-handles-test: Shift rotation"
                    << shiftResult.rotation;
        ++failures;
    }
    if (std::hypot(shiftCornerAfter.x() - shiftCorner.x(),
                   shiftCornerAfter.y() - shiftCorner.y()) > 0.6)
    {
        qCritical() << "inkscape-handles-test: opposite corner moved"
                    << shiftCorner << shiftCornerAfter;
        ++failures;
    }
    if (undoDepth() != depthBeforeShift + 1)
    {
        qCritical() << "inkscape-handles-test: Shift commits"
                    << depthBeforeShift << undoDepth();
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    // 5) Snap settings 0/30/45/90 with a 3 degree threshold; Ctrl bypasses it.
    flushSession();
    const int depthBeforeSnap = undoDepth();
    const QPointF snapPivot = viewport->sessionPivotForTest();
    const QPointF snapStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF snapTarget =
        snapPivot + rotatePointForTest(snapStart - snapPivot, -28.5);
    mouse.drag(snapStart, snapTarget);
    QApplication::processEvents();
    flushSession();
    const pvf::gizmo::Transform snapped = viewport->gizmoTransformForTest(uuid);
    if (!nearAngle(snapped.rotation, 30.0))
    {
        qCritical() << "inkscape-handles-test: snap 30" << snapped.rotation;
        ++failures;
    }
    if (undoDepth() != depthBeforeSnap + 1)
    {
        qCritical() << "inkscape-handles-test: snap commits" << depthBeforeSnap
                    << undoDepth();
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    const pvf::gizmo::Transform beforeCtrl = viewport->gizmoTransformForTest(uuid);
    mouse.drag(snapStart, snapTarget, Qt::ControlModifier);
    QApplication::processEvents();
    flushSession();
    const double ctrlDelta =
        std::remainder(viewport->gizmoTransformForTest(uuid).rotation
                           - beforeCtrl.rotation,
                       360.0);
    if (std::abs(ctrlDelta - 28.5) > 0.6)
    {
        qCritical() << "inkscape-handles-test: Ctrl delta" << ctrlDelta;
        ++failures;
    }
    document->undo();
    document->setSelection(uuid);
    QApplication::processEvents();

    // 6) Esc cancels without a commit, and the drag hides the corner handles.
    flushSession();
    const int depthBeforeCancel = undoDepth();
    const QPointF cancelStart =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    mouse.send(QEvent::MouseButtonPress, cancelStart, Qt::LeftButton,
               Qt::LeftButton);
    mouse.send(QEvent::MouseMove, cancelStart + QPointF(40.0, 10.0),
               Qt::NoButton, Qt::LeftButton);
    if (viewport->gizmoCornerHandlesVisibleForTest())
    {
        qCritical() << "inkscape-handles-test: corners visible while dragging";
        ++failures;
    }
    const QPointF pivotDuringDrag = viewport->sessionPivotForTest();
    if (std::hypot(viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).x()
                       - pivotDuringDrag.x(),
                   viewport->projectToWidgetForTest(
                       viewport->gizmoDragOriginForTest()).y()
                       - pivotDuringDrag.y())
        > 0.6
        || viewport->gizmoDraggingAroundCornerForTest())
    {
        qCritical() << "inkscape-handles-test: default origin is not the pivot";
        ++failures;
    }
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(viewport, &escape);
    mouse.send(QEvent::MouseButtonRelease, cancelStart + QPointF(40.0, 10.0),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    if (undoDepth() != depthBeforeCancel
        || !nearAngle(viewport->gizmoTransformForTest(uuid).rotation, 0.0))
    {
        qCritical() << "inkscape-handles-test: Esc cancel" << undoDepth()
                    << viewport->gizmoTransformForTest(uuid).rotation;
        ++failures;
    }

    // 7) Negative scale: RotateTopLeft stays the visual top-left handle (the
    //    local corner under it swaps) and the icon is not mirrored.
    const QPointF baseTopLeft =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopLeft);
    const QPointF baseTopRight =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const QPointF baseBottomRight =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateBottomRight);
    const double baseAngleTopLeft = viewport->gizmoRotateGlyphAngleForTest(uuid, 0);
    if (std::hypot(viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).x()
                       - viewport->gizmoCornerForTest(uuid, 2).x(),
                   viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).y()
                       - viewport->gizmoCornerForTest(uuid, 2).y())
        > 0.01)
    {
        qCritical() << "inkscape-handles-test: opposite corner (base)";
        ++failures;
    }

    setScale(-1.0, 1.0);
    const QPointF flippedTopLeft =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopLeft);
    const QPointF flippedTopRight =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopRight);
    const double flippedAngleTopLeft =
        viewport->gizmoRotateGlyphAngleForTest(uuid, 0);
    // The mirrored box occupies the same screen rectangle, so the visual
    // top-left handle must not move.  Moving to baseTopRight is exactly the
    // old local-corner bug.
    if (std::hypot(flippedTopLeft.x() - baseTopLeft.x(),
                   flippedTopLeft.y() - baseTopLeft.y()) > 0.01
        || std::hypot(flippedTopRight.x() - baseTopRight.x(),
                      flippedTopRight.y() - baseTopRight.y()) > 0.01
        || std::hypot(flippedTopLeft.x() - baseTopRight.x(),
                      flippedTopLeft.y() - baseTopRight.y()) < 1.0)
    {
        qCritical() << "inkscape-handles-test: flip X corner identity"
                    << flippedTopLeft << baseTopLeft << flippedTopRight
                    << baseTopRight;
        ++failures;
    }
    if (std::hypot(viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).x()
                       - viewport->gizmoCornerForTest(uuid, 2).x(),
                   viewport->gizmoOppositeCornerForTest(
                       uuid, pvf::gizmo::Handle::RotateTopLeft).y()
                       - viewport->gizmoCornerForTest(uuid, 2).y())
        > 0.01)
    {
        qCritical() << "inkscape-handles-test: opposite corner (flip X)";
        ++failures;
    }
    if (!nearAngle(flippedAngleTopLeft, baseAngleTopLeft))
    {
        qCritical() << "inkscape-handles-test: icon mirrored on flip"
                    << flippedAngleTopLeft << baseAngleTopLeft;
        ++failures;
    }
    if (viewport->gizmoHandleAtForTest(flippedTopLeft)
        != pvf::gizmo::Handle::RotateTopLeft)
    {
        qCritical() << "inkscape-handles-test: visual top-left hit";
        ++failures;
    }
    setScale(-1.0, -1.0);
    const QPointF bothTopLeft =
        viewport->gizmoHandleForTest(uuid, pvf::gizmo::Handle::RotateTopLeft);
    if (std::hypot(bothTopLeft.x() - baseTopLeft.x(),
                   bothTopLeft.y() - baseTopLeft.y()) > 0.01
        || std::hypot(bothTopLeft.x() - baseBottomRight.x(),
                      bothTopLeft.y() - baseBottomRight.y()) < 1.0)
    {
        qCritical() << "inkscape-handles-test: flip XY corner identity"
                    << bothTopLeft << baseTopLeft << baseBottomRight;
        ++failures;
    }
    setScale(1.0, 1.0);
    document->setSelection(uuid);
    QApplication::processEvents();

    // 8) Screenshot with a selected entity and all four carrow handles.
    if (auto* monitor = window.findChild<pvf::CameraMonitor*>())
        monitor->hide();
    sessionPtr->mutableProject().entities[uuid].transform.rotation = 30.0;
    document->commitEntity(uuid, QStringLiteral("pvf: shot rotation"), &error);
    flushSession();
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-inkscape-handles.png");
    if (!window.grab().save(screenshot))
    {
        qCritical() << "inkscape-handles-test: screenshot failed" << screenshot;
        ++failures;
    }
    else
    {
        qInfo() << "inkscape-handles-test: screenshot" << screenshot;
    }

    if (failures == 0)
        qInfo() << "inkscape-handles-test: ok";
    return failures == 0 ? 0 : 1;
}

int cameraMonitorTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    // Generate a blue reference video for the trace overlay.
    const QString tracePath = QDir(temp.path()).filePath(QStringLiteral("trace.mp4"));
    bool traceVideoReady = false;
    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
    {
        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"),
                     {QStringLiteral("-y"), QStringLiteral("-v"),
                      QStringLiteral("error"), QStringLiteral("-f"),
                      QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("color=c=blue:s=320x240:d=1:r=30"),
                      QStringLiteral("-c:v"), QStringLiteral("libx264"),
                      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                      QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                      tracePath});
        traceVideoReady = ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0;
        if (!traceVideoReady)
            qInfo() << "camera-monitor-test: ffmpeg trace generation skipped";
    }

    const auto buildProject = [&temp](const QString& name, bool withCamera) {
        const QString dir = QDir(temp.path()).filePath(name);
        pvf::model::ProjectSettings settings;
        settings.name = name;
        settings.width = 640;
        settings.height = 360;
        settings.fps = 30;
        QString error;
        auto session =
            pvf::EditorSession::createProject(dir, settings, &error);
        if (!session)
            return std::unique_ptr<pvf::EditorSession>();
        auto& project = session->mutableProject();

        pvf::model::Entity shape;
        shape.uuid = pvf::model::newUuid();
        shape.name = QStringLiteral("红色方块");
        shape.start = 0;
        shape.end = 60;
        shape.content.kind = pvf::ContentKind::Vector;
        shape.content.vector.width = 260.0;
        shape.content.vector.height = 160.0;
        shape.content.vector.fillColor = QStringLiteral("#ff0000");
        shape.transform.x = 0.0;
        shape.transform.y = 0.0;
        project.entities.insert(shape.uuid, shape);

        pvf::Uuid cameraUuid;
        if (withCamera)
        {
            pvf::model::Entity camera;
            camera.uuid = pvf::model::newUuid();
            camera.name = QStringLiteral("主摄像机");
            camera.start = 0;
            camera.end = 60;
            camera.content.kind = pvf::ContentKind::Camera;
            camera.transform.x = 0.0;
            camera.transform.y = 0.0;
            cameraUuid = camera.uuid;
            project.entities.insert(cameraUuid, camera);
        }
        if (!session->document()->commitEntity(shape.uuid,
                                               QStringLiteral("pvf: init shape"),
                                               &error)
            || (withCamera
                && !session->document()->commitEntity(
                    cameraUuid, QStringLiteral("pvf: init camera"), &error))
            || !session->document()->commitProject(QStringLiteral("pvf: init"),
                                                   &error))
        {
            qCritical() << "camera-monitor-test: commit failed" << error;
            return std::unique_ptr<pvf::EditorSession>();
        }
        session->flush();
        session->document()->setSelection(cameraUuid);
        return session;
    };

    // A camera project: the monitor must show the camera view.
    auto session = buildProject(QStringLiteral("with-camera.pvproj"), true);
    if (!session)
        return 1;
    const pvf::Uuid cameraUuid = session->activeCameraUuid();
    if (cameraUuid.isEmpty())
    {
        qCritical() << "camera-monitor-test: no active camera";
        return 1;
    }

    pvf::MainWindow window(session.release());
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* sessionPtr = window.editorSessionForTest();
    auto* monitor = window.findChild<pvf::CameraMonitor*>();
    if (!sessionPtr || !monitor)
    {
        qCritical() << "camera-monitor-test: monitor missing";
        return 1;
    }
    monitor->setVisibleFromMenu(true);
    monitor->setFrame(0);
    monitor->repaint();
    QApplication::processEvents();

    if (monitor->findChild<QToolButton*>(QStringLiteral("monitorStreamButton")))
    {
        qCritical() << "camera-monitor-test: session monitor has a stream button";
        ++failures;
    }

    const QImage preview = monitor->previewImageForTest();
    if (preview.isNull())
    {
        qCritical() << "camera-monitor-test: camera preview is empty";
        ++failures;
    }
    else
    {
        const QColor center = preview.pixelColor(preview.width() / 2,
                                                 preview.height() / 2);
        if (center.red() < 180 || center.green() > 100 || center.blue() > 100)
        {
            qCritical() << "camera-monitor-test: camera view centre is not red"
                        << center << preview.size();
            ++failures;
        }
    }
    if (monitor->monitorCameraForTest() != cameraUuid)
    {
        qCritical() << "camera-monitor-test: monitor camera not synced";
        ++failures;
    }

    // The viewport draws the selected camera's viewfinder and can drag it.
    auto* viewport = window.viewportForTest();
    if (!viewport || viewport->cameraFrameForTest().isEmpty())
    {
        qCritical() << "camera-monitor-test: viewfinder frame missing";
        ++failures;
    }
    else
    {
        const auto flushSession = [sessionPtr] {
            QString flushError;
            sessionPtr->flush(&flushError);
            return flushError;
        };
        auto* document = sessionPtr->document();
        MouseDriver mouse{viewport};
        const QPolygonF frame = viewport->cameraFrameForTest();
        const QPointF edge = frame[0] * 0.75 + frame[1] * 0.25;
        flushSession();
        const int depthBefore = document->store()->undoDepth();
        const double xBefore =
            viewport->gizmoTransformForTest(cameraUuid).x;
        mouse.drag(edge, edge + QPointF(60.0, 0.0));
        QApplication::processEvents();
        flushSession();
        const double xAfter = viewport->gizmoTransformForTest(cameraUuid).x;
        const double expectedDelta = 60.0 / viewport->currentScale();
        if (std::abs(xAfter - xBefore - expectedDelta) > 2.0
            || document->store()->undoDepth() != depthBefore + 1)
        {
            qCritical() << "camera-monitor-test: camera move drag failed"
                        << xBefore << xAfter << expectedDelta
                        << document->store()->undoDepth();
            ++failures;
        }
        document->undo();
        QApplication::processEvents();
        document->setSelection(cameraUuid);
        QApplication::processEvents();

        // Rotation handle: 90 degrees around the frame centre.
        const QPolygonF rotatedFrame = viewport->cameraFrameForTest();
        if (rotatedFrame.size() < 4)
        {
            qCritical() << "camera-monitor-test: viewfinder missing after undo";
            ++failures;
        }
        else
        {
            const QPointF topCenter =
                (rotatedFrame[0] + rotatedFrame[1]) / 2.0;
            const QPointF frameCenter =
                (rotatedFrame[0] + rotatedFrame[2]) / 2.0;
            QPointF up = topCenter - frameCenter;
            up /= std::max(1e-6, std::hypot(up.x(), up.y()));
            const QPointF handle = topCenter + up * 28.0;
            // 90 degrees counter-clockwise on screen.  Widget space is Y-down,
            // so the screen-space target is -90 and the model rotation +90.
            const QPointF target =
                frameCenter + rotatePointForTest(handle - frameCenter, -90.0);
            mouse.drag(handle, target);
            QApplication::processEvents();
            flushSession();
            const double rotation =
                viewport->gizmoTransformForTest(cameraUuid).rotation;
            if (std::abs(std::remainder(rotation - 90.0, 360.0)) > 0.5)
            {
                qCritical()
                    << "camera-monitor-test: camera rotation drag failed"
                    << rotation;
                ++failures;
            }
            document->undo();
            QApplication::processEvents();
        }
    }

    // Switching the combo to the whole canvas must stick.
    monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"))
        ->setCurrentIndex(0);
    QApplication::processEvents();
    if (!sessionPtr->activeCameraUuid().isEmpty())
    {
        qCritical() << "camera-monitor-test: whole-canvas switch did not stick";
        ++failures;
    }
    monitor->findChild<QComboBox*>(QStringLiteral("monitorCameraCombo"))
        ->setCurrentIndex(1);
    QApplication::processEvents();
    if (sessionPtr->activeCameraUuid() != cameraUuid)
    {
        qCritical() << "camera-monitor-test: camera switch did not restore";
        ++failures;
    }

    // Trace reference video: decoded through video2, alpha blended on top.
    monitor->setTraceOpacity(0.5);
    if (traceVideoReady)
    {
        if (!monitor->loadTraceVideo(tracePath))
        {
            qCritical() << "camera-monitor-test: trace import failed"
                        << monitor->traceErrorForTest();
            ++failures;
        }
        if (monitor->traceImageForTest().isNull())
        {
            qCritical() << "camera-monitor-test: trace image empty";
            ++failures;
        }
        if (std::abs(monitor->traceOpacityForTest() - 0.5) > 0.001)
        {
            qCritical() << "camera-monitor-test: trace opacity wrong";
            ++failures;
        }
        monitor->repaint();
        QApplication::processEvents();
        const QImage shot = monitor->grab().toImage();
        const QColor blended = shot.pixelColor(
            shot.width() / 2, 28 + (shot.height() - 28) / 2);
        if (blended.blue() < 60 || blended.red() < 40)
        {
            qCritical() << "camera-monitor-test: trace overlay not blended"
                        << blended;
            ++failures;
        }
    }
    if (monitor->loadTraceVideo(
            QDir(temp.path()).filePath(QStringLiteral("missing.mp4"))))
    {
        qCritical() << "camera-monitor-test: missing trace did not fail";
        ++failures;
    }
    if (monitor->traceErrorForTest().isEmpty())
    {
        qCritical() << "camera-monitor-test: missing trace gave no hint";
        ++failures;
    }

    // A project without cameras must not crash and must show the canvas.
    auto noCamera = buildProject(QStringLiteral("no-camera.pvproj"), false);
    if (!noCamera)
        return 1;
    pvf::MainWindow noCameraWindow(noCamera.release());
    noCameraWindow.resize(1000, 700);
    noCameraWindow.show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* noCameraMonitor = noCameraWindow.findChild<pvf::CameraMonitor*>();
    if (!noCameraMonitor)
    {
        qCritical() << "camera-monitor-test: no-camera monitor missing";
        ++failures;
    }
    else
    {
        noCameraMonitor->setVisibleFromMenu(true);
        noCameraMonitor->setFrame(0);
        noCameraMonitor->repaint();
        QApplication::processEvents();
        if (noCameraMonitor->previewImageForTest().isNull())
        {
            qCritical() << "camera-monitor-test: no-camera preview empty";
            ++failures;
        }
    }

    if (failures == 0)
        qInfo() << "camera-monitor-test: ok";
    return failures == 0 ? 0 : 1;
}


namespace {

bool writeTestPng(const QString& path, const QColor& color, const QSize& size = QSize(64, 40))
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    return image.save(path);
}

bool runTestFfmpeg(const QStringList& arguments)
{
    QProcess process;
    process.start(QStringLiteral("ffmpeg"), arguments);
    return process.waitForFinished(60000) && process.exitCode() == 0;
}

QString makeTestWav(const QString& directory)
{
    const QString path = QDir(directory).filePath(QStringLiteral("tone.wav"));
    if (!runTestFfmpeg({QStringLiteral("-y"), QStringLiteral("-v"),
                        QStringLiteral("error"), QStringLiteral("-f"),
                        QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                        path}))
        return {};
    return path;
}

QString makeTestVideo(const QString& directory, const QString& name,
                      double seconds, bool withAudio)
{
    const QString path = QDir(directory).filePath(name);
    QStringList args = {
        QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=blue:s=320x180:d=%1:r=30").arg(seconds)};
    if (withAudio)
    {
        args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
             << QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1")
                    .arg(seconds)
             << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-shortest");
    }
    args << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-preset") << QStringLiteral("ultrafast") << path;
    if (!runTestFfmpeg(args))
        return {};
    return path;
}

QString makeTestPvhtml(const QString& directory)
{
    const QString path = QDir(directory).filePath(QStringLiteral("html"));
    if (!QDir().mkpath(path))
        return {};
    QFile html(QDir(path).filePath(QStringLiteral("index.html")));
    if (!html.open(QIODevice::WriteOnly))
        return {};
    html.write("<!doctype html><html><body style='background:#123'>PVForge</body></html>");
    html.close();

    QJsonObject manifest;
    manifest.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
    manifest.insert(QStringLiteral("logicalSize"), QJsonArray{320, 180});
    manifest.insert(QStringLiteral("timeDependent"), false);
    QFile json(QDir(path).filePath(QStringLiteral("pvhtml.json")));
    if (!json.open(QIODevice::WriteOnly))
        return {};
    json.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    json.close();
    return path;
}

int panelRowForHash(const pvf::AssetLibraryPanel& panel, const QString& hash)
{
    for (int row = 0; row < panel.assetCount(); ++row)
        if (panel.assetHashAt(row) == hash)
            return row;
    return -1;
}

} // namespace

int assetsTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-assets.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 素材箱");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "assets-test: create failed" << error;
        return 1;
    }

    const QString alphaPath = QDir(temp.path()).filePath(QStringLiteral("alpha.png"));
    const QString betaPath = QDir(temp.path()).filePath(QStringLiteral("beta.png"));
    const QString videoPath = makeTestVideo(temp.path(), QStringLiteral("clip.mp4"), 2.0,
                                            true);
    const QString wavPath = makeTestWav(temp.path());
    const QString htmlPath = makeTestPvhtml(temp.path());
    if (!writeTestPng(alphaPath, QColor(220, 70, 70))
        || !writeTestPng(betaPath, QColor(70, 130, 220)) || videoPath.isEmpty()
        || wavPath.isEmpty() || htmlPath.isEmpty())
    {
        qCritical() << "assets-test: cannot create test media";
        return 1;
    }

    const auto alpha = session->assets().importFile(alphaPath, &error);
    const auto beta = session->assets().importFile(betaPath, &error);
    const auto video = session->assets().importVideoWithAudio(videoPath, &error);
    const auto audio = session->assets().importFile(wavPath, &error);
    const auto html = session->assets().importPvhtml(htmlPath, &error);
    if (!alpha || !beta || !video || !audio || !html)
    {
        qCritical() << "assets-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    if (!live || !panel)
    {
        qCritical() << "assets-test: panel missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "assets-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto row = [&](const QString& hash) { return panelRowForHash(*panel, hash); };

    // Visible entry points: the asset bin now sits left of the timeline and
    // keeps the create/remove buttons, not hidden test-only objects.
    auto* rightTabs = window->findChild<QTabWidget*>(QStringLiteral("rightTabs"));
    auto* timelineAssets =
        window->findChild<QSplitter*>(QStringLiteral("timelineAssetSplit"));
    auto* createButton =
        window->findChild<QPushButton*>(QStringLiteral("assetCreate"));
    auto* removeButton =
        window->findChild<QPushButton*>(QStringLiteral("assetRemove"));
    check(rightTabs && !rightTabs->isAncestorOf(panel),
          QStringLiteral("asset panel is still in the right dock"));
    check(timelineAssets && timelineAssets->isAncestorOf(panel),
          QStringLiteral("asset panel is not in the timeline asset split"));
    check(createButton && createButton->isVisible(),
          QStringLiteral("create button is not visible"));
    check(removeButton && removeButton->isVisible(),
          QStringLiteral("delete button is not visible"));

    auto* sessionTimeline = window->timelineForTest();
    auto* sessionViewport = window->viewportForTest();
    check(sessionTimeline && sessionTimeline->height() >= 190
              && sessionTimeline->height() <= 260,
          QStringLiteral("timeline height is not about 220"));
    check(sessionTimeline
              && sessionTimeline->width()
                     > window->centralWidget()->width() * 0.7,
          QStringLiteral("timeline lost most of its width"));
    check(sessionViewport && sessionViewport->width() > 1000
              && sessionViewport->height() > 520,
          QStringLiteral("viewport did not regain its v0.2.0 height"));

    if (auto* scroll =
            window->findChild<QScrollArea*>(QStringLiteral("projectScroll")))
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QApplication::processEvents();
    window->grab().save(QStringLiteral("/tmp/opencode/pvforge-session-library.png"));

    check(panel->assetCount() >= 5,
          QStringLiteral("asset list does not contain the imported assets"));
    check(row(alpha->hash) >= 0 && row(video->video.hash) >= 0
              && row(html->hash) >= 0,
          QStringLiteral("imported hashes missing from the list"));

    // Search matches name, hash and tags.
    panel->setSearchText(QStringLiteral("alpha"));
    check(panel->assetCount() == 1 && panel->assetHashAt(0) == alpha->hash,
          QStringLiteral("search did not filter by name"));
    panel->setSearchText(QString());
    check(panel->assetCount() >= 5, QStringLiteral("search clear did not restore"));

    // Tags and the tag filter.
    QString tagsError;
    check(panel->setAssetTags(alpha->hash, {QStringLiteral("intro"),
                                            QStringLiteral("red")},
                              &tagsError),
          QStringLiteral("set tags failed: %1").arg(tagsError));
    check(panel->tagFilters().contains(QStringLiteral("intro")),
          QStringLiteral("tag filter list missing the new tag"));
    panel->setTagFilter(QStringLiteral("intro"));
    check(panel->assetCount() == 1 && panel->assetHashAt(0) == alpha->hash,
          QStringLiteral("tag filter did not filter"));
    check(panel->assetTagsAt(0).contains(QStringLiteral("intro")),
          QStringLiteral("row tags missing"));
    panel->setTagFilter(QString());
    check(panel->assetCount() >= 5, QStringLiteral("tag clear did not restore"));

    // Thumbnails for every asset type.
    const QPixmap alphaThumb = panel->assetThumbnailAt(row(alpha->hash));
    const QPixmap videoThumb = panel->assetThumbnailAt(row(video->video.hash));
    const QPixmap audioThumb = panel->assetThumbnailAt(row(audio->hash));
    const QPixmap htmlThumb = panel->assetThumbnailAt(row(html->hash));
    check(!alphaThumb.isNull() && !videoThumb.isNull() && !audioThumb.isNull()
              && !htmlThumb.isNull(),
          QStringLiteral("one or more thumbnails are null"));

    // Rename writes asset.json and refreshes the list.
    check(panel->renameAsset(alpha->hash, QStringLiteral("Alpha 重命名"), &tagsError),
          QStringLiteral("rename failed: %1").arg(tagsError));
    check(panel->assetNameAt(row(alpha->hash)) == QStringLiteral("Alpha 重命名"),
          QStringLiteral("renamed asset name not shown"));

    // 创建实体 creates at the playhead through EditActions, like the legacy
    // button; dragging to the timeline stays the second entry point.
    if (auto* list = window->findChild<QListWidget*>(QStringLiteral("assetList")))
    {
        list->setCurrentRow(row(alpha->hash));
        QApplication::processEvents();
    }
    const qsizetype beforeCreate = live->project().entities.size();
    if (createButton)
        createButton->click();
    QApplication::processEvents();
    check(live->project().entities.size() == beforeCreate + 1,
          QStringLiteral("create button did not add an entity"));

    // Deleting a store entry keeps the entity and shows the offline state.
    pvf::model::Entity reference;
    reference.uuid = pvf::model::newUuid();
    reference.name = QStringLiteral("引用 beta");
    reference.start = 0;
    reference.end = 30;
    reference.content.kind = pvf::ContentKind::Asset;
    reference.content.asset.hash = beta->hash;
    reference.content.asset.type = pvf::AssetType::Image;
    const pvf::Uuid referenceUuid = reference.uuid;
    live->mutableProject().entities.insert(referenceUuid, reference);
    if (!live->document()->commitEntity(referenceUuid,
                                        QStringLiteral("p5 assets setup"), &error))
    {
        qCritical() << "assets-test: setup commit failed" << error;
        return 1;
    }

    // A missing content file (asset.json still present) is a different
    // offline state; re-importing the same bytes repairs it in place.
    const QStringList betaContent = QDir(beta->directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (!betaContent.isEmpty())
        QFile::remove(beta->directory + QLatin1Char('/') + betaContent.first());
    panel->refresh();
    const int contentMissingRow = row(beta->hash);
    check(contentMissingRow >= 0 && panel->assetMissingAt(contentMissingRow),
          QStringLiteral("content-missing asset is not offline"));
    check(panel->assetMissingReasonAt(contentMissingRow) == QStringLiteral("内容缺失"),
          QStringLiteral("content-missing reason is not 内容缺失"));
    check(panel->reimportMissing(beta->hash, betaPath, &tagsError),
          QStringLiteral("re-import did not repair missing content: %1")
              .arg(tagsError));
    const int repairedRow = row(beta->hash);
    check(repairedRow >= 0 && !panel->assetMissingAt(repairedRow),
          QStringLiteral("repaired content still shows offline"));

    check(panel->deleteAsset(beta->hash, &tagsError),
          QStringLiteral("delete asset failed: %1").arg(tagsError));
    check(live->project().entities.contains(referenceUuid),
          QStringLiteral("deleting an asset removed its entity"));
    const int goneRow = row(beta->hash);
    check(goneRow >= 0 && panel->assetMissingAt(goneRow)
              && panel->assetMissingReasonAt(goneRow) == QStringLiteral("目录缺失"),
          QStringLiteral("deleted asset is not offline"));

    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    if (!window->grab().save(QStringLiteral("/tmp/opencode/pvforge-p5-assets.png")))
    {
        qCritical() << "assets-test: screenshot failed";
        ++failures;
    }

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "assets-test OK: list/search/tags/thumbnails/rename/delete/missing";
    return failures == 0 ? 0 : 1;
}

int assetImportTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-import.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 导入");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "asset-import-test: create failed" << error;
        return 1;
    }

    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("import.png"));
    const QString wavPath = makeTestWav(temp.path());
    const QString videoPath = makeTestVideo(temp.path(), QStringLiteral("import.mp4"),
                                            2.0, true);
    const QString htmlPath = makeTestPvhtml(temp.path());
    if (!writeTestPng(imagePath, QColor(40, 180, 90)) || wavPath.isEmpty()
        || videoPath.isEmpty() || htmlPath.isEmpty())
    {
        qCritical() << "asset-import-test: cannot create test media";
        return 1;
    }

    pvf::AssetLibraryPanel panel(session.get());
    QString lastStatus;
    QObject::connect(&panel, &pvf::AssetLibraryPanel::statusMessage,
                     [&lastStatus](const QString& text) { lastStatus = text; });

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "asset-import-test:" << message;
            ++failures;
        }
        return condition;
    };

    check(panel.importPaths({imagePath, wavPath, videoPath, htmlPath}, &error),
          QStringLiteral("importPaths failed: %1").arg(error));

    int images = 0;
    int videos = 0;
    int audios = 0;
    int htmls = 0;
    QString videoHash;
    for (const pvf::assets::StoredAsset& asset : session->assets().list())
    {
        switch (asset.type)
        {
        case pvf::AssetType::Image:
            ++images;
            break;
        case pvf::AssetType::Video:
            ++videos;
            videoHash = asset.hash;
            break;
        case pvf::AssetType::Audio:
            ++audios;
            break;
        case pvf::AssetType::Html:
            ++htmls;
            break;
        }
    }
    check(images == 1 && videos == 1 && htmls == 1,
          QStringLiteral("store types wrong: image=%1 video=%2 html=%3")
              .arg(images)
              .arg(videos)
              .arg(htmls));
    check(audios == 2, QStringLiteral("video WAV was not split (audios=%1)").arg(audios));
    check(lastStatus.contains(QStringLiteral("WAV")),
          QStringLiteral("import status does not mention the split WAV"));

    const auto videoAsset = session->assets().get(videoHash);
    const QString audioHash = videoAsset
                                  ? videoAsset->metadata.value(QStringLiteral("audioHash"))
                                        .toString()
                                  : QString();
    check(!audioHash.isEmpty() && session->assets().get(audioHash).has_value(),
          QStringLiteral("video metadata audioHash does not resolve"));

    // The video extraction queue shows a bottom progress row; cancel removes
    // it whether the job is still running or already finished.
    for (int i = 0; i < 100 && panel.progressRowCount() == 0; ++i)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    check(panel.progressRowCount() > 0,
          QStringLiteral("video extraction progress row did not appear"));
    if (panel.progressRowCount() > 0)
    {
        check(panel.progressTextAt(0).contains(QStringLiteral("抽帧")),
              QStringLiteral("progress text is not an extraction status"));
        if (auto* cancel = panel.progressCancelButtonAt(0))
        {
            cancel->click();
            for (int i = 0; i < 100 && panel.progressRowCount() > 0; ++i)
            {
                QThread::msleep(20);
                QApplication::processEvents();
            }
            check(panel.progressRowCount() == 0,
                  QStringLiteral("progress cancel did not remove the row"));
        }
    }

    // Failure path keeps the existing assets and reports through the status
    // signal and the returned error.
    lastStatus.clear();
    error.clear();
    const bool failed = panel.importPaths(
        {QDir(temp.path()).filePath(QStringLiteral("missing.png"))}, &error);
    check(!failed && error.contains(QStringLiteral("文件不存在")),
          QStringLiteral("missing file was not reported"));
    check(lastStatus.contains(QStringLiteral("文件不存在")),
          QStringLiteral("failure was not sent to the status bar"));

    if (failures == 0)
        qInfo() << "asset-import-test OK: image/video/audio/pvhtml, WAV split, failure status";
    return failures == 0 ? 0 : 1;
}

int assetDragTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-drag.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 拖拽");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "asset-drag-test: create failed" << error;
        return 1;
    }

    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("drag.png"));
    const QString videoPath = makeTestVideo(temp.path(), QStringLiteral("drag.mp4"), 2.0,
                                            true);
    const QString wavPath = makeTestWav(temp.path());
    const QString htmlPath = makeTestPvhtml(temp.path());
    if (!writeTestPng(imagePath, QColor(240, 160, 40)) || videoPath.isEmpty()
        || wavPath.isEmpty() || htmlPath.isEmpty())
    {
        qCritical() << "asset-drag-test: cannot create test media";
        return 1;
    }

    const auto image = session->assets().importFile(imagePath, &error);
    const auto video = session->assets().importVideoWithAudio(videoPath, &error);
    const auto audio = session->assets().importFile(wavPath, &error);
    const auto html = session->assets().importPvhtml(htmlPath, &error);
    if (!image || !video || !audio || !html)
    {
        qCritical() << "asset-drag-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !panel || !timeline)
    {
        qCritical() << "asset-drag-test: window parts missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "asset-drag-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto depth = [live] { return live->document()->store()->undoDepth(); };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "asset-drag-test: flush failed" << flushError;
    };
    auto mime = [panel](const QString& hash) {
        return std::unique_ptr<QMimeData>(panel->assetMimeData(hash));
    };

    const int imageDepth = depth();
    const pvf::EntityId imageId =
        timeline->dropAssetForTest(*mime(image->hash), 45, -1);
    check(imageId != pvf::kInvalidEntity, QStringLiteral("image drop returned invalid id"));
    const pvf::Uuid imageUuid = timeline->uuidForEntityId(imageId);
    const pvf::model::Entity imageEntity =
        live->project().entities.value(imageUuid);
    check(imageEntity.content.kind == pvf::ContentKind::Asset
              && imageEntity.content.asset.hash == image->hash
              && imageEntity.content.asset.type == pvf::AssetType::Image,
          QStringLiteral("image entity content wrong"));
    check(imageEntity.start == 45 && imageEntity.end == 75,
          QStringLiteral("image drop range wrong: %1..%2")
              .arg(imageEntity.start)
              .arg(imageEntity.end));
    flush();
    check(depth() == imageDepth + 1,
          QStringLiteral("image drop was not one commit"));

    const int videoDepth = depth();
    const int beforeVideo = live->project().entities.size();
    const pvf::EntityId videoId =
        timeline->dropAssetForTest(*mime(video->video.hash), 60, -1);
    check(videoId != pvf::kInvalidEntity, QStringLiteral("video drop failed"));
    check(live->project().entities.size() == beforeVideo + 2,
          QStringLiteral("video drop did not create picture + audio"));
    const pvf::Uuid videoUuid = timeline->uuidForEntityId(videoId);
    const pvf::model::Entity videoEntity =
        live->project().entities.value(videoUuid);
    check(videoEntity.start == 60 && videoEntity.end == 120,
          QStringLiteral("video drop range wrong"));
    check(videoEntity.row == 1, QStringLiteral("video auto row wrong: %1")
                                    .arg(videoEntity.row));
    const QString audioHash =
        video->video.metadata.value(QStringLiteral("audioHash")).toString();
    bool paired = false;
    for (const pvf::model::Entity& entity : live->project().entities)
        if (entity.content.kind == pvf::ContentKind::Asset
            && entity.content.asset.type == pvf::AssetType::Audio
            && entity.content.asset.hash == audioHash
            && entity.start == 60 && entity.end == 120)
            paired = true;
    check(paired, QStringLiteral("paired WAV audio entity missing"));
    flush();
    check(depth() == videoDepth + 1,
          QStringLiteral("video + audio drop was not one commit"));

    const int audioDepth = depth();
    const pvf::EntityId audioId =
        timeline->dropAssetForTest(*mime(audio->hash), 0, -1);
    check(audioId != pvf::kInvalidEntity, QStringLiteral("audio drop failed"));
    const pvf::Uuid audioUuid = timeline->uuidForEntityId(audioId);
    const pvf::model::Entity audioEntity =
        live->project().entities.value(audioUuid);
    check(audioEntity.content.asset.type == pvf::AssetType::Audio
              && audioEntity.start == 0 && audioEntity.end == 60,
          QStringLiteral("audio drop content/range wrong"));
    flush();
    check(depth() == audioDepth + 1, QStringLiteral("audio drop was not one commit"));

    const pvf::EntityId htmlId =
        timeline->dropAssetForTest(*mime(html->hash), 120, -1);
    check(htmlId != pvf::kInvalidEntity, QStringLiteral("html drop failed"));
    const pvf::model::Entity htmlEntity =
        live->project().entities.value(timeline->uuidForEntityId(htmlId));
    check(htmlEntity.content.asset.type == pvf::AssetType::Html,
          QStringLiteral("html entity type wrong"));

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "asset-drag-test OK: image/video+audio/audio/html, one commit each";
    return failures == 0 ? 0 : 1;
}

int assetReplaceTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-replace.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 替换");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "asset-replace-test: create failed" << error;
        return 1;
    }

    const QString longPath = makeTestVideo(temp.path(), QStringLiteral("long.mp4"), 4.0,
                                           false);
    const QString shortPath = makeTestVideo(temp.path(), QStringLiteral("short.mp4"),
                                            1.0, false);
    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("replace.png"));
    const QString wavPath = makeTestWav(temp.path());
    if (longPath.isEmpty() || shortPath.isEmpty() || wavPath.isEmpty()
        || !writeTestPng(imagePath, QColor(80, 200, 220)))
    {
        qCritical() << "asset-replace-test: cannot create test media";
        return 1;
    }

    const auto longVideo = session->assets().importFile(longPath, &error);
    const auto shortVideo = session->assets().importFile(shortPath, &error);
    const auto image = session->assets().importFile(imagePath, &error);
    const auto audio = session->assets().importFile(wavPath, &error);
    if (!longVideo || !shortVideo || !image || !audio)
    {
        qCritical() << "asset-replace-test: import failed" << error;
        return 1;
    }

    auto makeEntity = [&session](const QString& name, int start, int end,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        entity.content.kind = kind;
        session->mutableProject().entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid videoEntity =
        makeEntity(QStringLiteral("长视频"), 10, 100, pvf::ContentKind::Asset);
    session->mutableProject().entities[videoEntity].content.asset.type =
        pvf::AssetType::Video;
    session->mutableProject().entities[videoEntity].content.asset.hash =
        longVideo->hash;
    session->mutableProject().entities[videoEntity].content.asset.sourceIn = 5;
    pvf::model::Track track;
    for (int frame : {0, 20, 29, 60, 89})
        track.keys.append(pvf::model::Keyframe{frame, double(frame), {}, {}});
    session->mutableProject().entities[videoEntity].tracks.insert(
        QStringLiteral("x"), track);

    const pvf::Uuid childEntity =
        makeEntity(QStringLiteral("子实体"), 10, 60, pvf::ContentKind::Empty);
    session->mutableProject().entities[childEntity].parentUuid = videoEntity;

    const pvf::Uuid imageEntity =
        makeEntity(QStringLiteral("图片"), 0, 30, pvf::ContentKind::Asset);
    session->mutableProject().entities[imageEntity].content.asset.type =
        pvf::AssetType::Image;
    session->mutableProject().entities[imageEntity].content.asset.hash =
        image->hash;

    const pvf::Uuid textEntity =
        makeEntity(QStringLiteral("文字"), 0, 30, pvf::ContentKind::Text);
    const pvf::Uuid cameraEntity =
        makeEntity(QStringLiteral("摄像机"), 0, 30, pvf::ContentKind::Camera);
    const pvf::Uuid emptyEntity =
        makeEntity(QStringLiteral("空"), 0, 30, pvf::ContentKind::Empty);
    const pvf::Uuid clampEntity =
        makeEntity(QStringLiteral("源偏移越界"), 0, 100, pvf::ContentKind::Asset);
    session->mutableProject().entities[clampEntity].content.asset.type =
        pvf::AssetType::Video;
    session->mutableProject().entities[clampEntity].content.asset.hash =
        longVideo->hash;
    session->mutableProject().entities[clampEntity].content.asset.sourceIn = 200;

    const QVector<pvf::Uuid> setup = {videoEntity, childEntity, imageEntity,
                                      textEntity, cameraEntity, emptyEntity,
                                      clampEntity};
    if (!session->document()->commitEntities(setup, QStringLiteral("p5 setup"),
                                             &error))
    {
        qCritical() << "asset-replace-test: setup commit failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* panel = window->assetLibraryForTest();
    auto* properties = window->findChild<pvf::PropertiesPanel*>();
    if (!live || !panel || !properties)
    {
        qCritical() << "asset-replace-test: window parts missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "asset-replace-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto depth = [live] { return live->document()->store()->undoDepth(); };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "asset-replace-test: flush failed" << flushError;
    };

    // Incompatible groups are rejected before writing.
    error.clear();
    check(!live->editActions()->replaceAsset(imageEntity, audio->hash, &error)
              && error.contains(QStringLiteral("不兼容")),
          QStringLiteral("audio -> image was not rejected"));
    error.clear();
    check(!live->editActions()->replaceAsset(textEntity, image->hash, &error),
          QStringLiteral("generated content accepted an asset"));
    error.clear();
    check(!live->editActions()->replaceAsset(cameraEntity, image->hash, &error),
          QStringLiteral("camera accepted an asset"));

    // Empty entities accept any group.
    error.clear();
    check(live->editActions()->replaceAsset(emptyEntity, image->hash, &error),
          QStringLiteral("empty entity rejected an asset: %1").arg(error));
    check(live->project().entities.value(emptyEntity).content.kind
              == pvf::ContentKind::Asset,
          QStringLiteral("empty entity did not become an asset"));

    // A short video trims end, drops keys past the new duration and clamps
    // sourceIn. Child entities that would fall outside are rejected first.
    error.clear();
    const bool trimmed = live->editActions()->replaceAsset(videoEntity,
                                                           shortVideo->hash, &error);
    check(!trimmed && error.contains(QStringLiteral("子实体")),
          QStringLiteral("out-of-range child was not rejected"));
    live->mutableProject().entities[childEntity].end = 35;
    if (!live->document()->commitEntity(childEntity,
                                        QStringLiteral("p5 child trim"), &error))
    {
        qCritical() << "asset-replace-test: child commit failed" << error;
        return 1;
    }
    flush();

    const int beforeReplace = depth();
    error.clear();
    check(live->editActions()->replaceAsset(videoEntity, shortVideo->hash, &error),
          QStringLiteral("short video replace failed: %1").arg(error));
    const pvf::model::Entity replaced =
        live->project().entities.value(videoEntity);
    const int shortFrames =
        shortVideo->metadata.value(QStringLiteral("durationFrames")).toInt();
    check(shortFrames > 0, QStringLiteral("short video has no durationFrames"));
    const int expectedDuration = shortFrames - 5;
    check(replaced.end == replaced.start + expectedDuration,
          QStringLiteral("end was not trimmed: %1..%2 expected duration %3")
              .arg(replaced.start)
              .arg(replaced.end)
              .arg(expectedDuration));
    check(replaced.content.asset.hash == shortVideo->hash
              && replaced.content.asset.sourceIn == 5,
          QStringLiteral("asset/sourceIn not updated"));
    bool outOfRangeKey = false;
    if (const auto trackIt = replaced.tracks.constFind(QStringLiteral("x"));
        trackIt != replaced.tracks.constEnd())
        for (const pvf::model::Keyframe& key : trackIt->keys)
            if (key.frame >= expectedDuration)
                outOfRangeKey = true;
    check(!outOfRangeKey, QStringLiteral("out-of-range keys were kept"));
    flush();
    check(depth() == beforeReplace + 1,
          QStringLiteral("replace was not exactly one commit"));

    // sourceIn is clamped into the new material and the entity shrinks to the
    // single remaining frame.
    error.clear();
    check(live->editActions()->replaceAsset(clampEntity, shortVideo->hash, &error),
          QStringLiteral("sourceIn clamp replace failed: %1").arg(error));
    const pvf::model::Entity clamped =
        live->project().entities.value(clampEntity);
    check(clamped.content.asset.sourceIn == shortFrames - 1,
          QStringLiteral("sourceIn was not clamped: %1")
              .arg(clamped.content.asset.sourceIn));
    check(clamped.start == 0 && clamped.end == 1,
          QStringLiteral("clamped entity range wrong: %1..%2")
              .arg(clamped.start)
              .arg(clamped.end));

    // Asset bin "replace selected entity" uses the same path.
    live->document()->setSelection(imageEntity);
    error.clear();
    check(panel->replaceSelectedEntityWith(image->hash, &error) == false,
          QStringLiteral("replacing with the same asset should fail"));
    error.clear();
    check(panel->replaceSelectedEntityWith(longVideo->hash, &error),
          QStringLiteral("asset bin replacement path failed: %1").arg(error));
    check(live->project().entities.value(imageEntity).content.asset.hash
              == longVideo->hash,
          QStringLiteral("asset bin replacement did not write the entity"));

    // The properties panel exposes the replace button for session assets.
    live->document()->setSelection(imageEntity);
    QApplication::processEvents();
    auto* replaceButton =
        properties->findChild<QPushButton*>(QStringLiteral("replaceSourceButton"));
    check(replaceButton && replaceButton->isVisible(),
          QStringLiteral("properties replace button is hidden"));

    // Delete the short asset, keep the entity, then recover by re-importing
    // the same file (same hash).
    error.clear();
    check(panel->deleteAsset(shortVideo->hash, &error),
          QStringLiteral("delete short asset failed: %1").arg(error));
    check(live->project().entities.value(videoEntity).content.asset.hash
              == shortVideo->hash,
          QStringLiteral("deleting an asset changed the entity"));
    const int missingRow = panelRowForHash(*panel, shortVideo->hash);
    check(missingRow >= 0 && panel->assetMissingAt(missingRow),
          QStringLiteral("missing asset not shown after delete"));
    error.clear();
    check(panel->reimportMissing(shortVideo->hash, shortPath, &error),
          QStringLiteral("re-import did not recover the asset: %1").arg(error));
    check(live->assets().get(shortVideo->hash).has_value(),
          QStringLiteral("store still missing after recovery"));
    const int restoredRow = panelRowForHash(*panel, shortVideo->hash);
    check(restoredRow >= 0 && !panel->assetMissingAt(restoredRow),
          QStringLiteral("restored asset still shows offline"));

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "asset-replace-test OK: groups/trim keys/sourceIn/child guard/missing recovery";
    return failures == 0 ? 0 : 1;
}

int workbenchAssetsTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p5-workbench.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P5 工作台");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "workbench-assets-test: create failed" << error;
        return 1;
    }

    const QString imagePath = QDir(temp.path()).filePath(QStringLiteral("workbench.png"));
    if (!writeTestPng(imagePath, QColor(120, 200, 90)))
    {
        qCritical() << "workbench-assets-test: cannot create image";
        return 1;
    }
    const auto image = session->assets().importFile(imagePath, &error);
    if (!image)
    {
        qCritical() << "workbench-assets-test: import failed" << error;
        return 1;
    }

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* workbench = window->findChild<pvf::WorkbenchPanel*>();
    auto* workbenchButton =
        window->findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    if (!live || !workbench || !workbenchButton)
    {
        qCritical() << "workbench-assets-test: window parts missing";
        return 1;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "workbench-assets-test:" << message;
            ++failures;
        }
        return condition;
    };
    auto depth = [live] { return live->document()->store()->undoDepth(); };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "workbench-assets-test: flush failed" << flushError;
    };
    auto showWorkbench = [&] {
        workbenchButton->click();
        QApplication::processEvents();
        return workbench->isVisible();
    };
    auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
    auto* create = workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
    if (!types || !create)
    {
        qCritical() << "workbench-assets-test: controls missing";
        return 1;
    }
    auto selectType = [&](const QString& name) {
        for (int row = 0; row < types->count(); ++row)
            if (types->item(row)->text() == name)
            {
                types->setCurrentRow(row);
                QApplication::processEvents();
                return true;
            }
        return false;
    };
    auto lastEntity = [live] {
        return live->project().entities.value(live->document()->selection());
    };

    live->setPlayhead(90);

    // Media entry uses the store and creates the visual entity.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for media"))
        || !check(selectType(QStringLiteral("媒体")),
                  QStringLiteral("media page missing")))
        return 1;
    workbench->selectSessionMediaForTest(image->hash);
    QApplication::processEvents();
    check(create->isEnabled(), QStringLiteral("media create stayed disabled"));
    const int mediaDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity mediaEntity = lastEntity();
    check(mediaEntity.content.kind == pvf::ContentKind::Asset
              && mediaEntity.content.asset.hash == image->hash,
          QStringLiteral("media creation did not use the asset store"));
    check(mediaEntity.start == 90 && mediaEntity.end == 120,
          QStringLiteral("media start is not the playhead"));
    check(depth() == mediaDepth + 1,
          QStringLiteral("media creation was not one commit"));
    check(!workbench->isVisible(),
          QStringLiteral("workbench stayed open after media create"));

    // Text entry.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for text"))
        || !check(selectType(QStringLiteral("文字")), QStringLiteral("text page missing")))
        return 1;
    if (auto* textContent =
            workbench->findChild<QLineEdit*>(QStringLiteral("textContent")))
        textContent->setText(QStringLiteral("P5 标题"));
    const int textDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity textEntity = lastEntity();
    check(textEntity.content.kind == pvf::ContentKind::Text
              && textEntity.content.text.text == QStringLiteral("P5 标题"),
          QStringLiteral("text content wrong"));
    check(textEntity.start == 90 && textEntity.end == 120,
          QStringLiteral("text start is not the playhead"));
    check(textEntity.row == 1,
          QStringLiteral("text auto row wrong: %1").arg(textEntity.row));
    check(depth() == textDepth + 1,
          QStringLiteral("text creation was not one commit"));

    // Vector entry.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for vector"))
        || !check(selectType(QStringLiteral("矢量")),
                  QStringLiteral("vector page missing")))
        return 1;
    const int vectorDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity vectorEntity = lastEntity();
    check(vectorEntity.content.kind == pvf::ContentKind::Vector,
          QStringLiteral("vector content wrong"));
    check(vectorEntity.start == 90 && vectorEntity.end == 120,
          QStringLiteral("vector start is not the playhead"));
    check(vectorEntity.row == 2,
          QStringLiteral("vector auto row wrong: %1").arg(vectorEntity.row));
    check(depth() == vectorDepth + 1,
          QStringLiteral("vector creation was not one commit"));

    // Camera entry.
    if (!check(showWorkbench(), QStringLiteral("workbench did not open for camera"))
        || !check(selectType(QStringLiteral("摄像机")),
                  QStringLiteral("camera page missing")))
        return 1;
    if (auto* cameraName =
            workbench->findChild<QLineEdit*>(QStringLiteral("cameraName")))
        cameraName->setText(QStringLiteral("测试机位"));
    const int cameraDepth = depth();
    create->click();
    QApplication::processEvents();
    flush();
    pvf::model::Entity cameraEntity = lastEntity();
    check(cameraEntity.content.kind == pvf::ContentKind::Camera
              && cameraEntity.name == QStringLiteral("测试机位"),
          QStringLiteral("camera content/name wrong"));
    check(cameraEntity.start == 90 && cameraEntity.end == 120,
          QStringLiteral("camera start is not the playhead"));
    check(cameraEntity.row == 3,
          QStringLiteral("camera auto row wrong: %1").arg(cameraEntity.row));
    check(depth() == cameraDepth + 1,
          QStringLiteral("camera creation was not one commit"));

    window->close();
    QApplication::processEvents();
    if (failures == 0)
        qInfo() << "workbench-assets-test OK: media/text/vector/camera -> playhead/auto row/one commit";
    return failures == 0 ? 0 : 1;
}

QJsonObject mcpStructuredResult(const McpHttpResponse& response)
{
    const QJsonObject body = QJsonDocument::fromJson(response.body).object();
    return body.value(QStringLiteral("result"))
        .toObject()
        .value(QStringLiteral("structuredContent"))
        .toObject();
}

McpHttpResponse mcpCallTool(quint16 port, const QByteArray& token,
                            const QString& tool, const QJsonObject& args)
{
    QJsonObject params;
    params.insert(QStringLiteral("name"), tool);
    params.insert(QStringLiteral("arguments"), args);
    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), 1);
    request.insert(QStringLiteral("method"), QStringLiteral("tools/call"));
    request.insert(QStringLiteral("params"), params);
    return mcpHttpPost(port,
                       QJsonDocument(request).toJson(QJsonDocument::Compact),
                       token);
}

int mcpSessionTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "mcp-test:" << message;
            ++failures;
        }
        return condition;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString configPath = QDir(temp.path()).filePath(QStringLiteral("mcp.json"));
    {
        QFile config(configPath);
        if (!config.open(QIODevice::WriteOnly))
            return 1;
        config.write("{\"port\":0}");
    }
    pvf::EditorSession::setMcpConfigPathForTest(configPath);
    pvf::EditorSession::setMcpPortOverrideForTest(0);

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p6-mcp.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P6 MCP");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error, false);
    if (!session)
    {
        qCritical() << "mcp-test: create failed" << error;
        return 1;
    }
    auto* window = new pvf::MainWindow(session.release());
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* server = live ? live->mcpServer() : nullptr;
    if (!check(live && server, QStringLiteral("session has no MCP server")))
        return 1;
    check(server->isRunning(), QStringLiteral("MCP server is not running"));
    check(server->port() > 0, QStringLiteral("MCP port is not assigned"));
    check(!server->token().isEmpty(), QStringLiteral("MCP token is empty"));

    // The token and port live in the persisted config file.
    {
        QFile config(configPath);
        if (config.open(QIODevice::ReadOnly))
        {
            const QJsonObject stored =
                QJsonDocument::fromJson(config.readAll()).object();
            check(stored.value(QStringLiteral("token")).toString()
                      == QString::fromLatin1(server->token()),
                  QStringLiteral("token was not persisted"));
        }
        else
        {
            check(false, QStringLiteral("cannot read MCP config"));
        }
    }

    // tools/list over HTTP with the persisted Bearer token.
    QJsonObject listRequest;
    listRequest.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    listRequest.insert(QStringLiteral("id"), 1);
    listRequest.insert(QStringLiteral("method"), QStringLiteral("tools/list"));
    const McpHttpResponse listResponse = mcpHttpPost(
        server->port(),
        QJsonDocument(listRequest).toJson(QJsonDocument::Compact),
        server->token());
    check(listResponse.status == 200,
          QStringLiteral("tools/list HTTP %1").arg(listResponse.status));
    const QJsonArray tools =
        QJsonDocument::fromJson(listResponse.body)
            .object()
            .value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("tools"))
            .toArray();
    check(tools.size() == 46,
          QStringLiteral("tools/list returned %1 tools, expected 46")
              .arg(tools.size()));

    // Read tools work without Git.
    const McpHttpResponse readResponse =
        mcpCallTool(server->port(), server->token(),
                    QStringLiteral("project.get"), {});
    check(mcpStructuredResult(readResponse)
              .value(QStringLiteral("ok"))
              .toBool(),
          QStringLiteral("project.get failed without Git"));

    // Write tools are denied with permission_denied/git_disabled.
    const McpHttpResponse deniedResponse =
        mcpCallTool(server->port(), server->token(),
                    QStringLiteral("entity.create"),
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("denied")},
                                {QStringLiteral("start"), 0},
                                {QStringLiteral("end"), 30}});
    const QJsonObject denied = mcpStructuredResult(deniedResponse);
    check(!denied.value(QStringLiteral("ok")).toBool(true),
          QStringLiteral("write succeeded with Git disabled"));
    check(denied.value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toString()
              == QStringLiteral("permission_denied"),
          QStringLiteral("write error is not permission_denied"));
    check(denied.value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("details"))
                  .toObject()
                  .value(QStringLiteral("reason"))
                  .toString()
              == QStringLiteral("git_disabled"),
          QStringLiteral("write denial reason is not git_disabled"));

    const bool gitAvailable = pvf::store::ProjectStore::gitBinaryAvailable();
    if (gitAvailable)
    {
        pvf::next::AppSettings appSettings = live->appSettings();
        appSettings.gitEnabled = true;
        appSettings.onboardingDone = true;
        QString applyError;
        check(live->applyAppSettings(appSettings, &applyError),
              QStringLiteral("enabling Git failed: %1").arg(applyError));
        check(live->document()->store()->gitEnabled(),
              QStringLiteral("store did not enable Git"));

        const int beforeEntities = live->project().entities.size();
        const McpHttpResponse writeResponse =
            mcpCallTool(server->port(), server->token(),
                        QStringLiteral("entity.create"),
                        QJsonObject{{QStringLiteral("name"),
                                     QStringLiteral("AI 写入")},
                                    {QStringLiteral("start"), 0},
                                    {QStringLiteral("end"), 30}});
        const QJsonObject written = mcpStructuredResult(writeResponse);
        check(written.value(QStringLiteral("ok")).toBool(),
              QStringLiteral("entity.create failed after enabling Git"));
        check(!written.value(QStringLiteral("commit"))
                   .toObject()
                   .value(QStringLiteral("shortHash"))
                   .toString()
                   .isEmpty(),
              QStringLiteral("write result has no commit hash"));
        QApplication::processEvents();
        check(live->project().entities.size() == beforeEntities + 1,
              QStringLiteral("document did not reload the AI-written entity"));
        check(window->timelineForTest()
                  && window->timelineForTest()->entityRowCountForTest()
                         == live->project().entities.size(),
              QStringLiteral("timeline was not refreshed after the AI write"));
        check(window->mcpLogPanelForTest()
                  && window->mcpLogPanelForTest()->entryCountForTest() > 0,
              QStringLiteral("MCP log has no tool row"));
        if (window->mcpLogPanelForTest())
        {
            const int row =
                window->mcpLogPanelForTest()->entryCountForTest() - 1;
            check(window->mcpLogPanelForTest()->entryDetailForTest(row)
                      .contains(QStringLiteral("提交：")),
                  QStringLiteral("MCP log row does not show the commit"));
        }

        window->showMcpLogForTest();
        QApplication::processEvents();
        QDir().mkpath(QStringLiteral("/tmp/opencode"));
        check(window->grab().save(
                  QStringLiteral("/tmp/opencode/pvforge-p6-mcp.png")),
              QStringLiteral("screenshot failed"));

        // Emergency stop: reads keep working, writes return stopped_by_human.
        server->stopAi();
        QApplication::processEvents();
        check(server->aiStopped(), QStringLiteral("stopAi did not latch"));
        check(window->humanEditingEnabledForTest(),
              QStringLiteral("human editing stayed locked after stop"));
        const QJsonObject stoppedWrite =
            mcpStructuredResult(mcpCallTool(
                server->port(), server->token(),
                QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("name"),
                             QStringLiteral("stopped")},
                            {QStringLiteral("start"), 0},
                            {QStringLiteral("end"), 10}}));
        check(stoppedWrite.value(QStringLiteral("error"))
                      .toObject()
                      .value(QStringLiteral("code"))
                      .toString()
                  == QStringLiteral("stopped_by_human"),
              QStringLiteral("write after stop is not stopped_by_human"));
        check(mcpStructuredResult(mcpCallTool(
                  server->port(), server->token(),
                  QStringLiteral("project.get"), {}))
                  .value(QStringLiteral("ok"))
                  .toBool(),
              QStringLiteral("read tool stopped working after stop"));

        server->resumeAi();
        QApplication::processEvents();
        const QJsonObject resumedWrite =
            mcpStructuredResult(mcpCallTool(
                server->port(), server->token(),
                QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("name"),
                             QStringLiteral("resumed")},
                            {QStringLiteral("start"), 0},
                            {QStringLiteral("end"), 10}}));
        check(resumedWrite.value(QStringLiteral("ok")).toBool(),
              QStringLiteral("write failed after resume"));
    }

    // Restarting the session keeps the token from the config file.
    const QByteArray tokenBefore = server->token();
    window->close();
    QApplication::processEvents();
    delete window;
    QApplication::processEvents();

    auto reopened = pvf::EditorSession::openProject(projectDir, &error, false);
    if (check(reopened != nullptr,
              QStringLiteral("reopen failed: %1").arg(error)))
    {
        check(reopened->mcpServer()
                  && reopened->mcpServer()->token() == tokenBefore,
              QStringLiteral("token changed across restart"));
        check(reopened->mcpServer()
                  && reopened->mcpServer()->isRunning(),
              QStringLiteral("MCP server did not restart"));

        // Switching projects must stop the old listener before the next
        // session binds the same configured port.
        const quint16 oldPort = reopened->mcpServer()->port();
        auto* reopenedWindow = new pvf::MainWindow(reopened.release());
        reopenedWindow->resize(1200, 800);
        reopenedWindow->show();
        QApplication::processEvents();

        const QString secondDir =
            QDir(temp.path()).filePath(QStringLiteral("p6-mcp-second.pvproj"));
        pvf::model::ProjectSettings secondSettings;
        secondSettings.name = QStringLiteral("P6 MCP 2");
        secondSettings.width = 640;
        secondSettings.height = 360;
        secondSettings.fps = 30;
        QString secondError;
        auto secondStore = pvf::store::ProjectStore::create(
            secondDir, secondSettings, &secondError, false);
        check(secondStore.has_value(),
              QStringLiteral("second project create failed: %1")
                  .arg(secondError));
        check(reopenedWindow->openSessionProjectForTest(secondDir, &secondError),
              QStringLiteral("project switch failed: %1").arg(secondError));
        QApplication::processEvents();

        pvf::MainWindow* switchedWindow = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* candidate = qobject_cast<pvf::MainWindow*>(widget);
            if (candidate && candidate != reopenedWindow
                && candidate->editorSessionForTest())
            {
                switchedWindow = candidate;
                break;
            }
        }
        check(switchedWindow != nullptr,
              QStringLiteral("switch did not open a new window"));
        check(switchedWindow && switchedWindow->editorSessionForTest()->mcpServer()
                  && switchedWindow->editorSessionForTest()->mcpServer()->isRunning(),
              QStringLiteral("MCP did not restart after the project switch"));
        check(switchedWindow
                  && switchedWindow->editorSessionForTest()->mcpServer()->port()
                         == oldPort,
              QStringLiteral("MCP port changed after the project switch"));
        if (switchedWindow)
            switchedWindow->close();
        delete reopenedWindow;
        QApplication::processEvents();
    }

    qInfo() << "mcp-test failures" << failures;
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

int mcpLockTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "mcp-lock-test:" << message;
            ++failures;
        }
        return condition;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p6-lock.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P6 锁");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    QString error;
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error, false);
    if (!session)
    {
        qCritical() << "mcp-lock-test: create failed" << error;
        return 1;
    }
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* server = live ? live->mcpServer() : nullptr;
    if (!check(live && server, QStringLiteral("no MCP server")))
        return 1;
    auto* timeline = window->timelineForTest();
    auto* props = window->propertiesForTest();
    auto* library = window->assetLibraryForTest();
    auto* workbench = window->workbenchForTest();
    auto* viewport = window->viewportForTest();
    auto* log = window->mcpLogPanelForTest();
    check(timeline && props && library && workbench && viewport && log,
          QStringLiteral("one or more legacy panels are missing"));
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("human editing is locked before any AI write"));

    // A read-only tool never locks the UI.
    server->toolCallStarted(QStringLiteral("project.get"),
                            QStringLiteral("{}"), false);
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("read-only call locked human editing"));
    check(timeline && timeline->isEnabled(),
          QStringLiteral("read-only call disabled the timeline"));
    server->toolCallFinished(QStringLiteral("project.get"),
                             QStringLiteral("{\"ok\":true}"), 1, true);
    QApplication::processEvents();

    // A write locks every old write entry point.
    server->toolCallStarted(QStringLiteral("entity.create"),
                            QStringLiteral("{}"), true);
    QApplication::processEvents();
    check(!window->humanEditingEnabledForTest(),
          QStringLiteral("write did not lock human editing"));
    check(timeline && !timeline->isEnabled(),
          QStringLiteral("timeline is still enabled"));
    check(props && !props->isEnabled(),
          QStringLiteral("properties panel is still enabled"));
    check(library && !library->isEnabled(),
          QStringLiteral("asset bin is still enabled"));
    check(workbench && !workbench->isEnabled(),
          QStringLiteral("workbench is still enabled"));
    check(viewport && !viewport->editingEnabled(),
          QStringLiteral("viewport editing is still enabled"));
    check(log && log->isEnabled(),
          QStringLiteral("MCP log is disabled while AI writes"));
    if (auto* save = window->findChild<QAction*>(QStringLiteral("saveAction")))
        check(!save->isEnabled(), QStringLiteral("save action is enabled"));
    if (auto* settingsAction =
            window->findChild<QAction*>(QStringLiteral("settingsAction")))
        check(!settingsAction->isEnabled(),
              QStringLiteral("settings action is enabled"));

    QString lockError;
    pvf::EditPlan emptyPlan;
    check(!live->editActions()->apply(emptyPlan, &lockError),
          QStringLiteral("EditActions accepted a write while locked"));
    check(lockError.contains(QStringLiteral("AI")),
          QStringLiteral("EditActions lock error is unclear: %1").arg(lockError));
    lockError.clear();
    check(!live->document()->commitProject(QStringLiteral("locked"), &lockError),
          QStringLiteral("Document accepted a write while locked"));
    check(lockError.contains(QStringLiteral("AI")),
          QStringLiteral("Document lock error is unclear: %1").arg(lockError));

    // Emergency stop releases human editing even before the tool unwinds.
    auto* stopButton = log
                           ? log->findChild<QToolButton*>(
                                 QStringLiteral("mcpLogStopButton"))
                           : nullptr;
    auto* resumeButton = log
                             ? log->findChild<QToolButton*>(
                                   QStringLiteral("mcpLogResumeButton"))
                             : nullptr;
    check(stopButton && stopButton->isEnabled(),
          QStringLiteral("MCP log stop button is not enabled"));
    if (stopButton)
        stopButton->click();
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("stop did not release human editing"));
    check(timeline && timeline->isEnabled(),
          QStringLiteral("timeline stayed disabled after stop"));
    check(viewport && viewport->editingEnabled(),
          QStringLiteral("viewport stayed locked after stop"));
    check(resumeButton && resumeButton->isEnabled(),
          QStringLiteral("MCP log resume button is not enabled"));
    server->toolCallFinished(QStringLiteral("entity.create"),
                             QStringLiteral("{\"ok\":false}"), 1, false);
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("tool finish re-locked the stopped editor"));

    if (resumeButton)
        resumeButton->click();
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("resume locked human editing"));
    check(!server->aiStopped(), QStringLiteral("resume did not clear the stop"));

    qInfo() << "mcp-lock-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

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
    bool testMode = false;
    for (const QString& argument : app.arguments())
        if (argument.endsWith(QStringLiteral("-test")))
            testMode = true;
    if (testMode)
    {
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

    pvf::Lang::load(pvf::Settings::language());

    if (app.arguments().contains(QStringLiteral("--mcp-test")))
        return mcpSessionTest(app);
    if (app.arguments().contains(QStringLiteral("--git-test")))
        return gitSessionTest(app);
    if (app.arguments().contains(QStringLiteral("--mcp-lock-test")))
        return mcpLockTest(app);
    if (app.arguments().contains(QStringLiteral("--self-test")))
        return selfTest();
    if (app.arguments().contains(QStringLiteral("--session-test")))
        return sessionTest();
    if (app.arguments().contains(QStringLiteral("--properties-test")))
        return propertiesTest(app);
    if (app.arguments().contains(QStringLiteral("--keyframe-easing-group-test")))
        return keyframeEasingGroupTest(app);
    if (app.arguments().contains(QStringLiteral("--properties-special-test")))
        return propertiesSpecialTest(app);
    if (app.arguments().contains(QStringLiteral("--solver-ui-test")))
        return solverUiTest(app);
    if (app.arguments().contains(QStringLiteral("--solver-mini-preview-test")))
        return solverMiniPreviewTest(app);
    if (app.arguments().contains(QStringLiteral("--solver-convert-test")))
        return solverConvertTest(app);
    if (app.arguments().contains(QStringLiteral("--html-vars-test")))
        return htmlVarsTest(app);
    if (app.arguments().contains(QStringLiteral("--text-layout-test")))
        return textLayoutTest(app);
    if (app.arguments().contains(QStringLiteral("--brandwall-create-test")))
        return brandWallCreateTest(app);
    if (app.arguments().contains(QStringLiteral("--assets-test")))
        return assetsTest(app);
    if (app.arguments().contains(QStringLiteral("--asset-import-test")))
        return assetImportTest(app);
    if (app.arguments().contains(QStringLiteral("--asset-drag-test")))
        return assetDragTest(app);
    if (app.arguments().contains(QStringLiteral("--asset-replace-test")))
        return assetReplaceTest(app);
    if (app.arguments().contains(QStringLiteral("--workbench-assets-test")))
        return workbenchAssetsTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-test")))
        return timelineTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-bottomup-test")))
        return timelineBottomUpTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-ui-test")))
        return timelineUiTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-assets-test")))
        return timelineAssetsTest(app);
    const int viewportIndex = app.arguments().indexOf(
        QStringLiteral("--viewport-test"));
    if (viewportIndex >= 0)
    {
        QString projectDir = QStringLiteral("/tmp/pvfe2e/proj");
        if (viewportIndex + 1 < app.arguments().size()
            && !app.arguments().at(viewportIndex + 1).startsWith(QLatin1Char('-')))
            projectDir = app.arguments().at(viewportIndex + 1);
        return viewportTest(app, projectDir);
    }
    if (app.arguments().contains(QStringLiteral("--timeline-edit-test")))
        return timelineEditTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-keys-test")))
        return timelineKeysTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-split-test")))
        return timelineSplitTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-marker-test")))
        return timelineMarkerTest(app);
    const int e2eIndex = app.arguments().indexOf(
        QStringLiteral("--timeline-edit-e2e"));
    if (e2eIndex >= 0)
    {
        QString projectDir = QStringLiteral("/tmp/pvfe2e/proj");
        if (e2eIndex + 1 < app.arguments().size()
            && !app.arguments().at(e2eIndex + 1).startsWith(QLatin1Char('-')))
            projectDir = app.arguments().at(e2eIndex + 1);
        return timelineEditE2eTest(app, projectDir);
    }
    if (app.arguments().contains(QStringLiteral("--save-test")))
        return saveTest();
    if (app.arguments().contains(QStringLiteral("--scene-test")))
        return sceneTest();
    if (app.arguments().contains(QStringLiteral("--annotation-test")))
        return annotationTest();
    if (app.arguments().contains(QStringLiteral("--checkpoint-test")))
        return checkpointTest();
    if (app.arguments().contains(QStringLiteral("--scene-ui-test")))
        return sceneUiTest(app);
    if (app.arguments().contains(QStringLiteral("--annotation-ui-test")))
        return annotationUiTest(app);
    if (app.arguments().contains(QStringLiteral("--autosave-test")))
        return autoSaveTest();
    if (app.arguments().contains(QStringLiteral("--special-test")))
        return specialTest(app);
    if (app.arguments().contains(QStringLiteral("--export-test")))
        return exportTest(app);
    if (app.arguments().contains(QStringLiteral("--export-dialog-test")))
        return exportDialogTest(app);
    if (app.arguments().contains(QStringLiteral("--mcp-readonly-test")))
        return mcpReadonlyTest();
    if (app.arguments().contains(QStringLiteral("--mcp-http-test")))
        return mcpHttpTest();
    if (app.arguments().contains(QStringLiteral("--mcp-ui-test")))
        return mcpUiTest(app);
    if (app.arguments().contains(QStringLiteral("--mcp-bridge-test")))
        return pvf::mcpBridgeTest();
    if (app.arguments().contains(QStringLiteral("--mcp-write-test")))
        return mcpWriteTest();
    if (app.arguments().contains(QStringLiteral("--mcp-integration-test")))
        return mcpIntegrationTest(app);
    if (app.arguments().contains(QStringLiteral("--keyframe-edit-test")))
        return keyframeEditTest(app);
    if (app.arguments().contains(QStringLiteral("--frame-restore-test")))
        return frameRestoreTest(app);
    if (app.arguments().contains(QStringLiteral("--layer-test")))
        return layerTest();
    if (app.arguments().contains(QStringLiteral("--replace-test")))
        return replaceSourceTest();
    if (app.arguments().contains(QStringLiteral("--library-picker-test")))
        return libraryPickerTest();
    if (app.arguments().contains(QStringLiteral("--clipboard-test")))
        return clipboardTest(app);
    if (app.arguments().contains(QStringLiteral("--group-test")))
        return groupTest(app);
    if (app.arguments().contains(QStringLiteral("--opacity-test")))
        return opacityTest(app);
    if (app.arguments().contains(QStringLiteral("--multidrag-test")))
        return multiDragTest(app);
    if (app.arguments().contains(QStringLiteral("--keyframe-drag-test")))
        return keyframeDragTest(app);
    if (app.arguments().contains(QStringLiteral("--curve-editor-test")))
        return curveEditorTest(app);
    if (app.arguments().contains(QStringLiteral("--audio-undo-test")))
        return audioUndoTest(app);
    if (app.arguments().contains(QStringLiteral("--special-demo")))
        return specialDemo(app);
    if (app.arguments().contains(QStringLiteral("--welcome-test")))
        return welcomeTest(app);
    if (app.arguments().contains(QStringLiteral("--library-test")))
        return libraryTest(app);
    if (app.arguments().contains(QStringLiteral("--open-timeline-test")))
        return openTimelineTest(app);
    if (app.arguments().contains(QStringLiteral("--clip-timeline-test")))
        return clipTimelineTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-zoom-test")))
        return timelineZoomTest(app);
    if (app.arguments().contains(QStringLiteral("--delete-test")))
        return deleteTest(app);
    if (app.arguments().contains(QStringLiteral("--undo-test")))
        return undoTest(app);
    if (app.arguments().contains(QStringLiteral("--rename-test")))
        return renameTest(app);
    if (app.arguments().contains(QStringLiteral("--pack-test")))
        return packTest(app);
    if (app.arguments().contains(QStringLiteral("--project-limit-test")))
        return projectLimitTest(app);
    if (app.arguments().contains(QStringLiteral("--lock-test")))
        return lockTest(app);
    if (app.arguments().contains(QStringLiteral("--gizmo-test")))
    {
        const int sessionResult = sessionGizmoTest(app);
        if (sessionResult != 0)
            return sessionResult;
        return gizmoTest(app);
    }
    if (app.arguments().contains(QStringLiteral("--inkscape-handles-test")))
        return inkscapeHandlesTest(app);
    if (app.arguments().contains(QStringLiteral("--camera-monitor-test")))
        return cameraMonitorTest(app);
    if (app.arguments().contains(QStringLiteral("--pivot-test")))
        return pivotTest(app);
    if (app.arguments().contains(QStringLiteral("--pivot-gizmo-test")))
        return pivotGizmoTest(app);
    if (app.arguments().contains(QStringLiteral("--shape-test")))
        return shapeTest(app);
    if (app.arguments().contains(QStringLiteral("--import-async-test")))
        return importAsyncTest(app);
    if (app.arguments().contains(QStringLiteral("--drag-undo-test")))
        return dragUndoTest(app);
    if (app.arguments().contains(QStringLiteral("--rotation-snap-test")))
        return rotationSnapTest(app);
    if (app.arguments().contains(QStringLiteral("--entity-snap-test")))
        return entitySnapTest(app);
    if (app.arguments().contains(QStringLiteral("--guide-test")))
        return guideTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-frame-test")))
        return timelineFrameTest(app);
    if (app.arguments().contains(QStringLiteral("--camera-test")))
        return cameraTest(app);
    if (app.arguments().contains(QStringLiteral("--camera-parent-test")))
        return cameraParentTest(app);
    if (app.arguments().contains(QStringLiteral("--monitor-feature-test")))
        return monitorFeatureTest(app);
    if (app.arguments().contains(QStringLiteral("--easing-test")))
        return easingTest(app);
    if (app.arguments().contains(QStringLiteral("--folder-test")))
        return folderTest(app);
    if (app.arguments().contains(QStringLiteral("--audio-test")))
        return audioTest(app);
    if (app.arguments().contains(QStringLiteral("--video-test")))
        return videoTest(app);
    if (app.arguments().contains(QStringLiteral("--scale-undo-test")))
        return scaleUndoTest(app);
    if (app.arguments().contains(QStringLiteral("--scale-handle-test")))
        return scaleHandleTest(app);
    if (app.arguments().contains(QStringLiteral("--snap-test")))
        return snapTest(app);
    if (app.arguments().contains(QStringLiteral("--settings-test")))
        return settingsTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-interaction-test")))
        return timelineInteractionTest(app);
    if (app.arguments().contains(QStringLiteral("--timeline-rowbuttons-test")))
        return timelineRowButtonsTest(app);
    if (app.arguments().contains(QStringLiteral("--demo-shot")))
        return renderDemo(app);
    if (app.arguments().contains(QStringLiteral("--play-test")))
        return playTest(app);
    if (app.arguments().contains(QStringLiteral("--workbench-test")))
        return workbenchTest(app);
    if (app.arguments().contains(QStringLiteral("--layout-test")))
        return layoutTest(app);
    if (app.arguments().contains(QStringLiteral("--html-capture-test")))
        return pvf::html::htmlCaptureTest();
    if (app.arguments().contains(QStringLiteral("--html-layer-test")))
        return pvf::html::htmlLayerTest();

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