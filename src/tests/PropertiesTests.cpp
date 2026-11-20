#include "TestApp.h"
#include "TestRegistry.h"

#include <QFormLayout>
#include <QJsonArray>

namespace {

using pvf::model::Keyframe;

const Keyframe* keyAt(const pvf::EditorSession* session, const pvf::Uuid& uuid,
                      int frame)
{
    const auto it = session->project().entities.constFind(uuid);
    if (it == session->project().entities.constEnd())
        return nullptr;
    return pvf::model::keyframeAt(it.value().keyframes, frame);
}

int countKeys(const pvf::EditorSession* session, const pvf::Uuid& uuid)
{
    const auto it = session->project().entities.constFind(uuid);
    return it == session->project().entities.constEnd()
               ? 0
               : int(it.value().keyframes.size());
}

bool noSingleParameterUi(pvf::PropertiesPanel* panel, QString* detail)
{
    for (QToolButton* button : panel->findChildren<QToolButton*>())
    {
        const QString name = button->objectName();
        if (name.startsWith(QStringLiteral("key_"))
            || name.startsWith(QStringLiteral("trackDiamond_"))
            || name.startsWith(QStringLiteral("sessionHtmlVarDiamond_")))
        {
            if (detail)
                *detail = name;
            return false;
        }
    }
    if (panel->findChild<QCheckBox*>(QStringLiteral("autoKeyCheck")))
    {
        if (detail)
            *detail = QStringLiteral("autoKeyCheck");
        return false;
    }
    return true;
}

int propertiesTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("wholeframe-properties.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("整帧属性");
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
    auto makeEntity = [&project](const QString& name, int start, int end,
                                 int row, pvf::ContentKind kind) {
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

    const pvf::Uuid text =
        makeEntity(QStringLiteral("标题"), 0, 120, 0, pvf::ContentKind::Text);
    project.entities[text].transform.x = 10.0;
    project.entities[text].transform.y = 20.0;
    project.entities[text].transform.opacity = 0.8;
    project.entities[text].content.text.text = QStringLiteral("PVForge");
    project.entities[text].content.text.pixelSize = 96;

    const pvf::Uuid vector =
        makeEntity(QStringLiteral("矢量"), 10, 60, 1, pvf::ContentKind::Vector);
    project.entities[vector].content.vector.width = 400.0;
    project.entities[vector].content.vector.height = 300.0;

    const pvf::Uuid camera =
        makeEntity(QStringLiteral("摄像机"), 0, 120, 2, pvf::ContentKind::Camera);
    project.entities[camera].transform.scaleX = 1.0;
    project.entities[camera].transform.scaleY = 1.0;

    const pvf::Uuid image =
        makeEntity(QStringLiteral("图片"), 0, 60, 3, pvf::ContentKind::Asset);
    project.entities[image].content.asset.type = pvf::AssetType::Image;
    project.entities[image].content.asset.hash = QStringLiteral("imagehash");

    const pvf::Uuid audio =
        makeEntity(QStringLiteral("音频"), 0, 60, 4, pvf::ContentKind::Asset);
    project.entities[audio].content.asset.type = pvf::AssetType::Audio;
    project.entities[audio].content.asset.hash = QStringLiteral("audiohash");
    project.entities[audio].content.asset.volume = 0.75;

    const pvf::Uuid solverRoot =
        makeEntity(QStringLiteral("求解物体"), 0, 60, 5,
                   pvf::ContentKind::Empty);
    {
        Keyframe key;
        key.frame = 0;
        project.entities[solverRoot].keyframes.append(key);
    }

    const QVector<pvf::Uuid> all = {text, vector, camera, image, audio,
                                    solverRoot};
    if (!session->document()->commitEntities(all,
                                             QStringLiteral("wholeframe setup"),
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

    QString lastError;
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [&lastError](const QString& message) {
                         lastError = message;
                     });
    QObject::connect(panel, &pvf::PropertiesPanel::sessionStatusMessage,
                     [&lastError](const QString& message) {
                         lastError = message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "properties-test:" << message;
        return condition;
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "properties-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };

    QString detail;
    if (!check(noSingleParameterUi(panel, &detail),
               QStringLiteral("single-parameter key UI still present: %1")
                   .arg(detail)))
        return 1;
    if (!check(!window->findChild<QAction*>(QStringLiteral("autoKeyAction")),
               QStringLiteral("auto-key menu action still present")))
        return 1;

    document->setSelection(text);
    live->setPlayhead(0);
    QApplication::processEvents();
    QApplication::processEvents();

    auto* nameEdit = panel->findChild<QLineEdit*>(QStringLiteral("entityName"));
    auto* xSpin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_x"));
    auto* ySpin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_y"));
    auto* rotationSpin =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_rotation"));
    auto* opacitySpin =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_opacity"));
    auto* status = panel->findChild<QLabel*>(QStringLiteral("keyframeStatus"));
    auto* deleteKey =
        panel->findChild<QPushButton*>(QStringLiteral("keyframeDeleteButton"));
    if (!check(nameEdit && xSpin && ySpin && rotationSpin && opacitySpin
                   && status && deleteKey,
               QStringLiteral("basic/transform/status widgets missing")))
        return 1;

    // Native styling: no hardcoded QSS, left-aligned forms, one field column.
    QString styledWidget;
    for (QWidget* widget : panel->findChildren<QWidget*>())
        if (!widget->styleSheet().isEmpty())
        {
            styledWidget = widget->objectName();
            break;
        }
    if (!check(styledWidget.isEmpty(),
               QStringLiteral("property widget still carries QSS: %1")
                   .arg(styledWidget)))
        return 1;
    for (QFormLayout* form : panel->findChildren<QFormLayout*>())
        if (!check((form->labelAlignment() & Qt::AlignLeft) != 0
                       && form->fieldGrowthPolicy()
                              == QFormLayout::AllNonFixedFieldsGrow,
                   QStringLiteral("property form is not left/native aligned")))
            return 1;
    auto* section = panel->findChild<pvf::CollapsibleSection*>();
    auto* header = section ? section->findChild<QToolButton*>() : nullptr;
    if (!check(section && header && header->font().bold()
                   && header->sizePolicy().horizontalPolicy()
                          != QSizePolicy::Expanding,
               QStringLiteral("collapsible header is not flush-left bold")))
        return 1;
    auto* textAlign = panel->findChild<QComboBox*>(QStringLiteral("textAlign"));
    auto* textColor =
        panel->findChild<QPushButton*>(QStringLiteral("textColor"));
    QApplication::processEvents();
    if (!check(textAlign && textColor
                   && std::abs(xSpin->width() - textAlign->width()) <= 1
                   && std::abs(xSpin->width() - textColor->width()) <= 1,
               QStringLiteral("property field widths differ (%1/%2/%3)")
                   .arg(xSpin->width())
                   .arg(textAlign ? textAlign->width() : -1)
                   .arg(textColor ? textColor->width() : -1)))
        return 1;

    // No key: the panel shows the playhead status and an edit creates one
    // fully populated whole-frame key.
    if (!check(status->text().contains(QStringLiteral("当前无关键帧")),
               QStringLiteral("empty-frame status text wrong: %1")
                   .arg(status->text())))
        return 1;
    int before = depth();
    xSpin->setValue(120.0);
    QApplication::processEvents();
    flush();
    if (!check(depth() == before + 1,
               QStringLiteral("first auto key was not one commit")))
        return 1;
    {
        const Keyframe* key = keyAt(live, text, 0);
        if (!check(key && std::abs(key->x - 120.0) < 1e-6,
                   QStringLiteral("x edit did not create key at frame 0"))
            || !check(key && std::abs(key->scaleX - 1.0) < 1e-6
                           && std::abs(key->scaleY - 1.0) < 1e-6
                           && std::abs(key->opacity - 0.8) < 1e-6,
                       QStringLiteral("auto key did not fill every animation "
                                      "parameter"))
            || !check(key && key->content.contains(QStringLiteral("pixelSize")),
                       QStringLiteral("auto key content parameters incomplete")))
            return 1;
    }
    if (!check(status->text().contains(QStringLiteral("整帧关键帧")),
               QStringLiteral("key status text missing: %1").arg(status->text()))
        || !check(status->palette().color(QPalette::WindowText)
                      == status->palette().color(QPalette::Text),
                  QStringLiteral("key status did not return to the normal colour")))
        return 1;

    // Same frame updates the existing key instead of appending.
    before = depth();
    ySpin->setValue(55.0);
    QApplication::processEvents();
    flush();
    if (!check(countKeys(live, text) == 1,
               QStringLiteral("second edit appended another key"))
        || !check(keyAt(live, text, 0)
                      && std::abs(keyAt(live, text, 0)->y - 55.0) < 1e-6,
                  QStringLiteral("same-frame edit did not update the key"))
        || !check(depth() == before + 1,
                  QStringLiteral("same-frame edit was not one commit")))
        return 1;

    // A new playhead frame creates another whole-frame key.
    live->setPlayhead(10);
    QApplication::processEvents();
    rotationSpin->setValue(30.0);
    QApplication::processEvents();
    flush();
    if (!check(countKeys(live, text) == 2
                   && keyAt(live, text, 10)
                   && std::abs(keyAt(live, text, 10)->rotation - 30.0) < 1e-6,
               QStringLiteral("new frame did not create a second full key")))
        return 1;

    // Single selected key wins over the playhead.
    live->setPlayhead(20);
    QApplication::processEvents();
    panel->setSelectedKeyframes({pvf::KeyRef{text, 0}});
    live->setPlayhead(20);
    QApplication::processEvents();
    ySpin->setValue(77.0);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, text, 0)
                   && std::abs(keyAt(live, text, 0)->y - 77.0) < 1e-6,
               QStringLiteral("single selected key did not receive the edit"))
        || !check(!keyAt(live, text, 20),
                  QStringLiteral("single selected key edit created a playhead key")))
        return 1;

    // Multi-selection edits only the playhead frame; the set itself is not
    // rewritten or collapsed.
    panel->setSelectedKeyframes(
        {pvf::KeyRef{text, 0}, pvf::KeyRef{text, 10}});
    live->setPlayhead(7);
    QApplication::processEvents();
    rotationSpin->setValue(11.0);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, text, 7)
                   && std::abs(keyAt(live, text, 7)->rotation - 11.0) < 1e-6,
               QStringLiteral("multi-selection did not edit the playhead frame"))
        || !check(panel->selectedKeyframeCount() == 2,
                  QStringLiteral("multi-selection was collapsed by an edit")))
        return 1;

    // Out of range: static parameters only, no key, status message.
    panel->setSelectedKeyframes({});
    live->setPlayhead(200);
    QApplication::processEvents();
    lastError.clear();
    const int keysBefore = countKeys(live, text);
    const double staticOpacity = live->project().entities.value(text).transform.opacity;
    opacitySpin->setValue(30.0);
    QApplication::processEvents();
    flush();
    if (!check(countKeys(live, text) == keysBefore,
               QStringLiteral("out-of-range edit created a key"))
        || !check(std::abs(live->project()
                               .entities.value(text)
                               .transform.opacity
                           - 0.3)
                      < 1e-6,
                  QStringLiteral("out-of-range edit did not change static opacity"))
        || !check(std::abs(staticOpacity - 0.8) < 1e-6,
                  QStringLiteral("static opacity setup changed before edit"))
        || !check(lastError.contains(QStringLiteral("静态")),
                  QStringLiteral("out-of-range edit did not report static-only")))
        return 1;

    // Whole-key delete button removes exactly one frame.
    live->setPlayhead(0);
    panel->setSelectedKeyframes({pvf::KeyRef{text, 0}});
    QApplication::processEvents();
    before = depth();
    deleteKey->click();
    QApplication::processEvents();
    flush();
    if (!check(!keyAt(live, text, 0),
               QStringLiteral("delete-key button did not remove the key"))
        || !check(depth() == before + 1,
                  QStringLiteral("delete-key was not one commit")))
        return 1;

    // Easing and note are per whole-frame key.
    live->setPlayhead(10);
    panel->setSelectedKeyframes({pvf::KeyRef{text, 10}});
    QApplication::processEvents();
    auto* easing =
        panel->findChild<QComboBox*>(QStringLiteral("keyframeEasingGroup"));
    auto* note = panel->findChild<QLineEdit*>(QStringLiteral("keyframeNote"));
    if (!check(easing && note, QStringLiteral("easing/note widgets missing")))
        return 1;
    before = depth();
    easing->setCurrentIndex(easing->findData(QStringLiteral("easeinoutcubic")));
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, text, 10)
                   && keyAt(live, text, 10)->easing.type
                          == QStringLiteral("easeinoutcubic"),
               QStringLiteral("whole-frame easing was not written"))
        || !check(depth() == before + 1,
                  QStringLiteral("whole-frame easing was not one commit")))
        return 1;
    note->setText(QStringLiteral("副歌第一拍"));
    QMetaObject::invokeMethod(note, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, text, 10)
                   && keyAt(live, text, 10)->note
                          == QStringLiteral("副歌第一拍"),
               QStringLiteral("keyframe note was not written")))
        return 1;

    // Content-animation parameters use the same whole-frame rule.
    panel->setSelectedKeyframes({});
    document->setSelection(text);
    live->setPlayhead(0);
    QApplication::processEvents();
    auto* textSize = panel->findChild<QSpinBox*>(QStringLiteral("textSize"));
    if (!check(textSize, QStringLiteral("text size widget missing")))
        return 1;
    textSize->setValue(144);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, text, 0)
                   && keyAt(live, text, 0)->content
                              .value(QStringLiteral("pixelSize"))
                              .toDouble()
                          == 144.0,
               QStringLiteral("text size did not enter key.content.pixelSize")))
        return 1;

    document->setSelection(vector);
    live->setPlayhead(10);
    QApplication::processEvents();
    auto* vectorWidth =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("vectorWidth"));
    if (!check(vectorWidth, QStringLiteral("vector width widget missing")))
        return 1;
    vectorWidth->setValue(512.0);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, vector, 0)
                   && std::abs(keyAt(live, vector, 0)
                                   ->content.value(QStringLiteral("width"))
                                   .toDouble()
                               - 512.0)
                          < 1e-6,
               QStringLiteral("vector width did not create a content key")))
        return 1;

    // Camera scale writes one uniform whole-frame key.
    document->setSelection(camera);
    live->setPlayhead(0);
    QApplication::processEvents();
    auto* cameraScale =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_scaleX"));
    if (!check(cameraScale && cameraScale->minimum() == 5.0
                   && cameraScale->maximum() == 100.0,
               QStringLiteral("camera scale range missing")))
        return 1;
    cameraScale->setValue(50.0);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, camera, 0)
                   && std::abs(keyAt(live, camera, 0)->scaleX - 0.5) < 1e-6
                   && std::abs(keyAt(live, camera, 0)->scaleY - 0.5) < 1e-6,
               QStringLiteral("camera scale did not write a uniform key")))
        return 1;

    // Audio volume is an animated content parameter.
    document->setSelection(audio);
    live->setPlayhead(5);
    QApplication::processEvents();
    auto* audioVolume =
        panel->findChild<QSpinBox*>(QStringLiteral("audioVolume"));
    if (!check(audioVolume, QStringLiteral("audio volume widget missing")))
        return 1;
    audioVolume->setValue(50);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, audio, 5)
                   && std::abs(keyAt(live, audio, 5)
                                   ->content.value(QStringLiteral("volume"))
                                   .toDouble()
                               - 0.5)
                          < 1e-6,
               QStringLiteral("audio volume did not create a content key")))
        return 1;

    // Solver conversion from the last key and cancellation.
    document->setSelection(solverRoot);
    live->setPlayhead(0);
    QApplication::processEvents();
    auto* solverEffect =
        panel->findChild<QComboBox*>(QStringLiteral("solverEffectCombo"));
    auto* solverTarget =
        panel->findChild<QComboBox*>(QStringLiteral("solverTargetCombo"));
    auto* solverCreate =
        panel->findChild<QPushButton*>(QStringLiteral("solverCreateButton"));
    auto* solverCancel =
        panel->findChild<QPushButton*>(QStringLiteral("solverCancelButton"));
    if (!check(solverEffect && solverTarget && solverCreate && solverCancel,
               QStringLiteral("solver widgets missing")))
        return 1;
    solverEffect->setCurrentIndex(
        solverEffect->findData(int(pvf::SolverEffect::Gravity)));
    solverTarget->setCurrentIndex(solverTarget->findData(vector));
    before = depth();
    solverCreate->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(solverRoot).solvers.size() == 1,
               QStringLiteral("solver conversion did not create a segment"))
        || !check(depth() == before + 1,
                  QStringLiteral("solver conversion was not one commit")))
        return 1;
    QApplication::processEvents();

    // A playhead inside an explicit closed solver segment is in range, but the
    // frame is solver-driven and must not be written as a normal whole-frame
    // keyframe, nor fall back to changing static transform defaults.
    {
        auto& project = live->mutableProject();
        project.entities[solverRoot].solvers[0].endFrame = 20;
        QString closeError;
        if (!document->commitEntity(solverRoot,
                                    QStringLiteral("pvf: close solver"),
                                    &closeError))
        {
            qCritical() << "properties-test: close solver failed" << closeError;
            return 1;
        }
        flush();
        document->setSelection(solverRoot);
        live->setPlayhead(10);
        QApplication::processEvents();

        const double staticX =
            live->project().entities.value(solverRoot).transform.x;
        auto* xSpin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_x"));
        if (!check(xSpin, QStringLiteral("x spin missing for solver lock test")))
            return 1;
        lastError.clear();
        xSpin->setValue(staticX + 40.0);
        QApplication::processEvents();
        flush();
        if (!check(!keyAt(live, solverRoot, 10),
                   QStringLiteral("closed solver wrote a normal keyframe"))
            || !check(std::abs(live->project()
                                   .entities.value(solverRoot)
                                   .transform.x
                               - staticX)
                          < 1e-9,
                      QStringLiteral("closed solver changed static x"))
            || !check(lastError.contains(QStringLiteral("求解")),
                      QStringLiteral("closed solver did not report the block")))
            return 1;

        lastError.clear();
        if (auto* resetBtn =
                panel->findChild<QPushButton*>(QStringLiteral("transformReset")))
        {
            resetBtn->click();
            QApplication::processEvents();
            if (!check(lastError.contains(QStringLiteral("求解")),
                       QStringLiteral("reset inside solver did not report the block"))
                || !check(std::abs(live->project()
                                       .entities.value(solverRoot)
                                       .transform.x
                                   - staticX)
                              < 1e-9,
                          QStringLiteral("reset inside solver changed static x")))
                return 1;
        }

        // Copy from the solver-start key, then try to paste onto the locked
        // middle frame: paste must report the same solver block and not create
        // a normal keyframe.
        auto* copyBtn =
            panel->findChild<QPushButton*>(QStringLiteral("transformCopy"));
        auto* pasteBtn =
            panel->findChild<QPushButton*>(QStringLiteral("transformPaste"));
        if (copyBtn && pasteBtn)
        {
            live->setPlayhead(0);
            QApplication::processEvents();
            copyBtn->click();
            live->setPlayhead(10);
            QApplication::processEvents();
            lastError.clear();
            pasteBtn->click();
            QApplication::processEvents();
            if (!check(lastError.contains(QStringLiteral("求解")),
                       QStringLiteral("paste inside solver did not report the block"))
                || !check(!keyAt(live, solverRoot, 10),
                          QStringLiteral("paste inside solver created a normal key")))
                return 1;
        }
    }

    live->setPlayhead(0);
    QApplication::processEvents();
    if (!check(solverCancel->isVisible() && solverCancel->isEnabled(),
               QStringLiteral("cancel-solver action is not available")))
        return 1;
    before = depth();
    solverCancel->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(solverRoot).solvers.isEmpty(),
               QStringLiteral("cancel solver did not remove the segment"))
        || !check(depth() == before + 1,
                  QStringLiteral("cancel solver was not one commit")))
        return 1;

    // Easing combo is a single whole-frame control; no per-track combos.
    if (!check(!panel->findChild<QComboBox*>(QStringLiteral("trackEasing_x")),
               QStringLiteral("per-track easing combo still present")))
        return 1;

    // Screenshot with a whole-frame key selected.
    document->setSelection(text);
    live->setPlayhead(10);
    panel->setSelectedKeyframes({pvf::KeyRef{text, 10}});
    QApplication::processEvents();
    QApplication::processEvents();
    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
        objectTab->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-wholeframe-properties.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "properties-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "properties-test OK: auto whole-frame keys, selection priority, "
               "multi-select playhead write, static fallback, content/easing/"
               "note, screenshot"
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
        QStringLiteral("wholeframe-easing.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("整帧缓动");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
        return 1;

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("曲线");
    entity.start = 0;
    entity.end = 60;
    entity.content.kind = pvf::ContentKind::Text;
    entity.content.text.text = QStringLiteral("PVForge");
    for (int frame : {0, 10})
    {
        Keyframe key;
        key.frame = frame;
        key.content.insert(QStringLiteral("pixelSize"), 96.0);
        key.content.insert(QStringLiteral("lineHeight"), 1.0);
        entity.keyframes.append(key);
    }
    const pvf::Uuid uuid = entity.uuid;
    session->mutableProject().entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("easing setup"),
                                           &error))
        return 1;
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
    if (!live || !document || !panel)
        return 1;
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "keyframe-easing-group-test:" << message;
        return condition;
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "keyframe-easing-group-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };

    document->setSelection(uuid);
    live->setPlayhead(10);
    QApplication::processEvents();
    auto* easing =
        panel->findChild<QComboBox*>(QStringLiteral("keyframeEasingGroup"));
    if (!check(easing, QStringLiteral("whole-frame easing combo missing")))
        return 1;
    if (!check(easing->findData(QStringLiteral("bezier")) >= 0,
               QStringLiteral("bezier entry missing")))
        return 1;
    const int before = depth();
    easing->setCurrentIndex(easing->findData(QStringLiteral("easeinoutcubic")));
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, uuid, 10)
                   && keyAt(live, uuid, 10)->easing.type
                          == QStringLiteral("easeinoutcubic"),
               QStringLiteral("easing was not written to frame 10"))
        || !check(keyAt(live, uuid, 0)
                      && keyAt(live, uuid, 0)->easing.type
                             == QStringLiteral("linear"),
                  QStringLiteral("easing leaked to frame 0"))
        || !check(depth() == before + 1,
                  QStringLiteral("easing edit was not one commit")))
        return 1;

    // The first key owns an outgoing segment too: the combo stays enabled.
    live->setPlayhead(0);
    QApplication::processEvents();
    if (!check(easing->isEnabled(),
               QStringLiteral("first key should own an outgoing segment")))
        return 1;

    QString detail;
    if (!check(noSingleParameterUi(panel, &detail),
               QStringLiteral("single-parameter UI present: %1").arg(detail)))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "keyframe-easing-group-test OK: one whole-frame easing, "
               "one commit";
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
        QDir(temp.path()).filePath(QStringLiteral("wholeframe-html.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("整帧 HTML");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
        return 1;

    {
        QFile index(QDir(htmlSource.path()).filePath(QStringLiteral("index.html")));
        if (!index.open(QIODevice::WriteOnly))
            return 1;
        index.write("<!doctype html><html><body>PVForge</body></html>");
    }
    {
        QJsonObject variables;
        variables.insert(
            QStringLiteral("speed"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                        {QStringLiteral("default"), 2.5},
                        {QStringLiteral("min"), 0.0},
                        {QStringLiteral("max"), 10.0}});
        variables.insert(
            QStringLiteral("count"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                        {QStringLiteral("default"), 3.0},
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

    const auto imported = session->assets().importPvhtml(htmlSource.path(), &error);
    if (!imported)
        return 1;

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("HTML");
    entity.start = 0;
    entity.end = 120;
    entity.content.kind = pvf::ContentKind::Asset;
    entity.content.asset.type = pvf::AssetType::Html;
    entity.content.asset.hash = imported->hash;
    const pvf::Uuid uuid = entity.uuid;
    session->mutableProject().entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("html setup"),
                                           &error))
        return 1;
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
        return 1;
    QString lastError;
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [&lastError](const QString& message) {
                         lastError = message;
                     });
    QObject::connect(panel, &pvf::PropertiesPanel::sessionStatusMessage,
                     [&lastError](const QString& message) {
                         lastError = message;
                     });
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "html-vars-test:" << message;
        return condition;
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "html-vars-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };

    document->setSelection(uuid);
    live->setPlayhead(0);
    QApplication::processEvents();

    auto* htmlSection =
        panel->findChild<QWidget*>(QStringLiteral("sessionHtmlSection"));
    auto* speedSpin = panel->findChild<QDoubleSpinBox*>(
        QStringLiteral("sessionHtmlVarValue_var.speed"));
    auto* countSpin = panel->findChild<QDoubleSpinBox*>(
        QStringLiteral("sessionHtmlVarValue_var.count"));
    auto* labelValue = panel->findChild<QLabel*>(
        QStringLiteral("sessionHtmlVarValue_var.label"));
    auto* flagValue = panel->findChild<QLabel*>(
        QStringLiteral("sessionHtmlVarValue_var.flag"));
    if (!check(htmlSection && htmlSection->isVisible(),
               QStringLiteral("HTML section is not visible"))
        || !check(speedSpin && std::abs(speedSpin->value() - 2.5) < 1e-6,
                  QStringLiteral("numeric default not shown"))
        || !check(countSpin && countSpin->decimals() == 0,
                  QStringLiteral("integer decimals wrong"))
        || !check(labelValue && labelValue->text() == QStringLiteral("hello"),
                  QStringLiteral("string variable read-only wrong"))
        || !check(flagValue && flagValue->text() == QStringLiteral("true"),
                  QStringLiteral("bool variable read-only wrong")))
        return 1;
    QString detail;
    if (!check(noSingleParameterUi(panel, &detail),
               QStringLiteral("variable diamond still present: %1").arg(detail)))
        return 1;

    int before = depth();
    speedSpin->setValue(4.2);
    QMetaObject::invokeMethod(speedSpin, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, uuid, 0)
                   && std::abs(keyAt(live, uuid, 0)
                                   ->htmlVars.value(QStringLiteral("speed"))
                                   .toDouble()
                               - 4.2)
                          < 1e-6,
               QStringLiteral("speed did not enter key.htmlVars"))
        || !check(keyAt(live, uuid, 0)
                      && keyAt(live, uuid, 0)
                             ->htmlVars.value(QStringLiteral("count"))
                             .toDouble()
                             == 3.0,
                  QStringLiteral("new key did not fill manifest defaults"))
        || !check(depth() == before + 1,
                  QStringLiteral("variable edit was not one commit")))
        return 1;

    live->setPlayhead(10);
    QApplication::processEvents();
    countSpin->setValue(9.0);
    QMetaObject::invokeMethod(countSpin, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, uuid, 10)
                   && keyAt(live, uuid, 10)
                          ->htmlVars.value(QStringLiteral("count"))
                          .toDouble()
                      == 9.0,
               QStringLiteral("count did not key frame 10")))
        return 1;

    panel->setSelectedKeyframes({});
    live->setPlayhead(200);
    QApplication::processEvents();
    lastError.clear();
    before = depth();
    speedSpin->setValue(8.0);
    QMetaObject::invokeMethod(speedSpin, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(depth() == before,
               QStringLiteral("out-of-range HTML edit added a commit"))
        || !check(lastError.contains(QStringLiteral("范围")),
                  QStringLiteral("out-of-range HTML edit did not report")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "html-vars-test OK: no diamonds, numeric vars auto-key, "
               "manifest defaults, read-only strings";
    return 0;
}

int textLayoutTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("wholeframe-text-layout.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("文字排版");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
        return 1;

    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("标题");
    entity.start = 0;
    entity.end = 120;
    entity.content.kind = pvf::ContentKind::Text;
    entity.content.text.text = QStringLiteral("PVForge");
    entity.content.text.pixelSize = 96;
    const pvf::Uuid uuid = entity.uuid;
    session->mutableProject().entities.insert(uuid, entity);
    if (!session->document()->commitEntity(uuid, QStringLiteral("text setup"),
                                           &error))
        return 1;
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
        return 1;
    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "text-layout-test:" << message;
        return condition;
    };
    auto flush = [&] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "text-layout-test: flush failed" << flushError;
    };
    auto depth = [document] { return document->store()->undoDepth(); };

    document->setSelection(uuid);
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

    QTimer::singleShot(50, [&] {
        QDialog* dialog = findDialog();
        if (!dialog)
            return;
        if (auto* size = dialog->findChild<QSpinBox*>())
            size->setValue(123);
        if (auto* spacing = dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("layoutLetterSpacing")))
            spacing->setValue(4.0);
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
    if (!check(live->project().entities.value(uuid).content.text.pixelSize == 123,
               QStringLiteral("dialog size did not reach static content"))
        || !check(depth() == before + 1,
                  QStringLiteral("dialog accept was not one commit")))
        return 1;

    QTimer::singleShot(50, [&] {
        QDialog* dialog = findDialog();
        if (!dialog)
            return;
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
    if (!check(live->project().entities.value(uuid).content.text.pixelSize == 123,
               QStringLiteral("cancel changed static content"))
        || !check(depth() == before,
                  QStringLiteral("cancel added a commit")))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "text-layout-test OK: dialog write-back and cancel";
    return 0;
}

} // namespace

namespace pvf::tests {

void registerPropertiesTests(Registry& registry)
{
    registry.add(QStringLiteral("--properties-test"),
                 [](QApplication& app) { return propertiesTest(app); });
    registry.add(QStringLiteral("--keyframe-easing-group-test"),
                 [](QApplication& app) { return keyframeEasingGroupTest(app); });
    registry.add(QStringLiteral("--html-vars-test"),
                 [](QApplication& app) { return htmlVarsTest(app); });
    registry.add(QStringLiteral("--text-layout-test"),
                 [](QApplication& app) { return textLayoutTest(app); });
}

} // namespace pvf::tests
