#include "LegacyTestSupport.h"
#include "PropertiesSpecialTests.h"
#include "TestRegistry.h"

namespace {

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

} // namespace

namespace pvf::tests {

void registerPropertiesTests(Registry& registry)
{
    registry.add(QStringLiteral("--properties-test"),
                 [](QApplication& app) { return propertiesTest(app); });
    registry.add(QStringLiteral("--keyframe-easing-group-test"),
                 [](QApplication& app) { return keyframeEasingGroupTest(app); });
    registry.add(QStringLiteral("--properties-special-test"),
                 [](QApplication& app) { return propertiesSpecialTest(app); });
    registry.add(QStringLiteral("--html-vars-test"),
                 [](QApplication& app) { return htmlVarsTest(app); });
    registry.add(QStringLiteral("--text-layout-test"),
                 [](QApplication& app) { return textLayoutTest(app); });
    registry.add(QStringLiteral("--brandwall-create-test"),
                 [](QApplication& app) { return brandWallCreateTest(app); });
    registry.add(QStringLiteral("--special-test"),
                 [](QApplication& app) { return runPropertiesSpecialTest(app); });
}

} // namespace pvf::tests
