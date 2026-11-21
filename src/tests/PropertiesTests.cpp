#include "TestApp.h"
#include "TestRegistry.h"
#include "ui/EasingPicker.h"

#include <QFormLayout>
#include <QJsonArray>
#include <QMouseEvent>

namespace {

using pvf::model::Keyframe;

// Drives the MI easing grid exactly like a user: click the button with the
// current-curve preview, then the tile inside the popup.  Returns false when
// the control is missing or disabled (no keyframe under the playhead).
pvf::EasingPicker* openEasingGrid(pvf::PropertiesPanel* panel)
{
    auto* button =
        panel->findChild<QToolButton*>(QStringLiteral("keyframeEasingGroup"));
    if (!button || !button->isEnabled())
        return nullptr;
    button->click();
    QApplication::processEvents();
    const auto pickers = panel->findChildren<pvf::EasingPicker*>();
    for (pvf::EasingPicker* picker : pickers)
        if (picker->isVisible())
            return picker;
    return pickers.isEmpty() ? nullptr : pickers.last();
}

bool chooseEasing(pvf::PropertiesPanel* panel, const QString& type)
{
    pvf::EasingPicker* picker = openEasingGrid(panel);
    if (!picker)
        return false;
    const QRect tile = picker->tileRect(type);
    if (!tile.isValid())
        return false;
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(tile.center()),
                      QPointF(picker->mapToGlobal(tile.center())),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(picker, &press);
    QApplication::processEvents();
    return true;
}

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

    // HiDPI button icons must be rasterised at DPR=2, not a 1x bitmap that Qt
    // upscales on a scaled display.
    {
        pvf::model::EasingSpec spec;
        spec.type = QStringLiteral("easeinoutcubic");
        const QPixmap preview = pvf::EasingPicker::curvePreview(
            spec, QPalette(), QSize(40, 22), 3, 2.0);
        if (preview.size() != QSize(80, 44)
            || !qFuzzyCompare(preview.devicePixelRatio(), 2.0))
        {
            qCritical() << "properties-test: HiDPI easing preview is wrong"
                        << preview.size() << preview.devicePixelRatio();
            return 1;
        }
    }

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

    // Multi-selection: the edited field is absolutely overwritten on every
    // selected key, while every other field keeps that key's own value.
    {
        auto& textEntity = live->mutableProject().entities[text];
        Keyframe first = *pvf::model::keyframeAt(textEntity.keyframes, 0);
        first.frame = 0;
        first.x = 0.0;
        first.rotation = 0.0;
        first.note = QStringLiteral("第一帧");
        Keyframe second = *pvf::model::keyframeAt(textEntity.keyframes, 10);
        second.frame = 10;
        second.x = 50.0;
        second.rotation = 45.0;
        second.note = QStringLiteral("第二帧");
        Keyframe third;
        third.frame = 20;
        third.x = 100.0;
        third.rotation = 90.0;
        third.note = QStringLiteral("第三帧");
        pvf::model::upsertKeyframe(textEntity.keyframes, first);
        pvf::model::upsertKeyframe(textEntity.keyframes, second);
        pvf::model::upsertKeyframe(textEntity.keyframes, third);
        if (!document->commitEntity(text, QStringLiteral("batch setup"), &error))
            return 1;
        flush();
    }
    panel->setSelectedKeyframes(
        {pvf::KeyRef{text, 0}, pvf::KeyRef{text, 10}, pvf::KeyRef{text, 20}});
    live->setPlayhead(10);
    QApplication::processEvents();
    if (!check(status->text().contains(QStringLiteral("正在批量编辑"))
                   && status->text().contains(QStringLiteral("3 个关键帧")),
               QStringLiteral("batch status text wrong: %1").arg(status->text())))
        return 1;
    // An unselected playhead must not supply the displayed baseline: frame 7
    // interpolates x=35, but the earliest selected key (frame 0) has x=0.
    xSpin->clearFocus();
    live->setPlayhead(7);
    QApplication::processEvents();
    if (!check(std::abs(xSpin->value()) < 1e-6,
               QStringLiteral("batch display used the unselected playhead value: %1")
                   .arg(xSpin->value())))
        return 1;
    live->setPlayhead(10);
    QApplication::processEvents();
    before = depth();
    xSpin->setValue(200.0);
    QApplication::processEvents();
    flush();
    if (!check(countKeys(live, text) == 3,
               QStringLiteral("batch x edit added or removed keys"))
        || !check(keyAt(live, text, 0) && keyAt(live, text, 10)
                      && keyAt(live, text, 20)
                      && std::abs(keyAt(live, text, 0)->x - 200.0) < 1e-6
                      && std::abs(keyAt(live, text, 10)->x - 200.0) < 1e-6
                      && std::abs(keyAt(live, text, 20)->x - 200.0) < 1e-6,
                  QStringLiteral("batch x did not rewrite all selected keys"))
        || !check(std::abs(keyAt(live, text, 0)->rotation) < 1e-6
                      && std::abs(keyAt(live, text, 10)->rotation - 45.0) < 1e-6
                      && std::abs(keyAt(live, text, 20)->rotation - 90.0) < 1e-6,
                  QStringLiteral("batch x clobbered per-key rotations"))
        || !check(keyAt(live, text, 0)->note == QStringLiteral("第一帧")
                      && keyAt(live, text, 10)->note == QStringLiteral("第二帧")
                      && keyAt(live, text, 20)->note == QStringLiteral("第三帧"),
                  QStringLiteral("batch x clobbered per-key notes"))
        || !check(depth() == before + 1,
                  QStringLiteral("batch x edit was not one commit")))
        return 1;

    document->undo();
    QApplication::processEvents();
    if (!check(keyAt(live, text, 0)
                   && std::abs(keyAt(live, text, 0)->x) < 1e-6
                   && std::abs(keyAt(live, text, 10)->x - 50.0) < 1e-6
                   && std::abs(keyAt(live, text, 20)->x - 100.0) < 1e-6,
               QStringLiteral("one undo did not restore the batch edit")))
        return 1;
    document->redo();
    QApplication::processEvents();

    const int beforeEasing = depth();
    if (!check(chooseEasing(panel, QStringLiteral("easeinoutquad")),
               QStringLiteral("batch easing grid could not choose a preset")))
        return 1;
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, text, 0)
                   && keyAt(live, text, 0)->easing.type
                          == QStringLiteral("easeinoutquad")
                   && keyAt(live, text, 10)->easing.type
                          == QStringLiteral("easeinoutquad")
                   && keyAt(live, text, 20)->easing.type
                          == QStringLiteral("easeinoutquad"),
               QStringLiteral("batch easing did not reach all selected keys"))
        || !check(depth() == beforeEasing + 1,
                  QStringLiteral("batch easing was not one commit")))
        return 1;
    document->undo();
    QApplication::processEvents();
    if (!check(keyAt(live, text, 0)
                   && keyAt(live, text, 0)->easing.type
                          == QStringLiteral("linear")
                   && keyAt(live, text, 10)->easing.type
                          == QStringLiteral("linear")
                   && keyAt(live, text, 20)->easing.type
                          == QStringLiteral("linear"),
               QStringLiteral("one undo did not restore the batch easing")))
        return 1;
    document->redo();
    QApplication::processEvents();

    auto* batchNote =
        panel->findChild<QLineEdit*>(QStringLiteral("keyframeNote"));
    if (!check(batchNote, QStringLiteral("batch note widget missing")))
        return 1;
    const int beforeNote = depth();
    batchNote->setText(QStringLiteral("批量注释"));
    QMetaObject::invokeMethod(batchNote, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, text, 0)->note == QStringLiteral("批量注释")
                   && keyAt(live, text, 10)->note == QStringLiteral("批量注释")
                   && keyAt(live, text, 20)->note == QStringLiteral("批量注释"),
               QStringLiteral("batch note did not reach all selected keys"))
        || !check(depth() == beforeNote + 1,
                  QStringLiteral("batch note was not one commit")))
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
    auto* easingButton =
        panel->findChild<QToolButton*>(QStringLiteral("keyframeEasingGroup"));
    auto* note = panel->findChild<QLineEdit*>(QStringLiteral("keyframeNote"));
    if (!check(easingButton && note,
               QStringLiteral("easing/note widgets missing")))
        return 1;
    if (!check(!panel->findChild<QComboBox*>(
                   QStringLiteral("keyframeEasingGroup")),
               QStringLiteral("plain-text easing combo still present")))
        return 1;
    before = depth();
    if (!check(chooseEasing(panel, QStringLiteral("easeinoutcubic")),
               QStringLiteral("easing grid could not choose a preset")))
        return 1;
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

    // The delete button removes the whole multi-selection in one commit.
    panel->setSelectedKeyframes(
        {pvf::KeyRef{text, 10}, pvf::KeyRef{text, 20}});
    QApplication::processEvents();
    before = depth();
    deleteKey->click();
    QApplication::processEvents();
    flush();
    if (!check(!keyAt(live, text, 10) && !keyAt(live, text, 20),
               QStringLiteral("batch delete did not remove every selected key"))
        || !check(depth() == before + 1,
                  QStringLiteral("batch delete was not one commit")))
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

    // Audio gain is an animated content parameter.  The panel edits dB; the
    // model keeps the linear multiplier in key.content.volume.
    document->setSelection(audio);
    live->setPlayhead(5);
    QApplication::processEvents();
    auto* audioVolume =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("audioVolume"));
    if (!check(audioVolume, QStringLiteral("audio gain widget missing")))
        return 1;
    audioVolume->setValue(-6.0);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, audio, 5)
                   && std::abs(keyAt(live, audio, 5)
                                   ->content.value(QStringLiteral("volume"))
                                   .toDouble()
                               - std::pow(10.0, -6.0 / 20.0))
                          < 1e-6,
               QStringLiteral("audio gain did not create a content key")))
        return 1;

    // Linked scale preserves the entity's current aspect ratio and is one
    // commit, not two independent scale writes.
    {
        auto& setup = live->mutableProject();
        setup.entities[vector].keyframes.clear();
        setup.entities[vector].transform.scaleX = 1.6;
        setup.entities[vector].transform.scaleY = 0.9;
        QString scaleError;
        if (!document->commitEntity(vector,
                                    QStringLiteral("pvf: scale setup"),
                                    &scaleError))
        {
            qCritical() << "properties-test: scale setup failed" << scaleError;
            return 1;
        }
        flush();
        document->setSelection(vector);
        live->setPlayhead(10);
        QApplication::processEvents();

        auto* scaleX =
            panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_scaleX"));
        auto* scaleY =
            panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_scaleY"));
        auto* link =
            panel->findChild<QCheckBox*>(QStringLiteral("transformLink"));
        if (!check(scaleX && scaleY && link
                       && link->text() == QStringLiteral("等比缩放"),
                   QStringLiteral("linked scale widgets missing/wrong label")))
            return 1;
        link->setChecked(true);
        const int scaleDepth = depth();
        scaleX->setValue(3.2);
        QApplication::processEvents();
        flush();
        const Keyframe* scaleKey = keyAt(live, vector, 0);
        if (!check(scaleKey
                       && std::abs(scaleKey->scaleX - 3.2) < 1e-6
                       && std::abs(scaleKey->scaleY - 1.8) < 1e-6,
                   QStringLiteral("linked scale did not preserve 16:9: %1/%2 "
                                  "link=%3 spin=%4")
                       .arg(scaleKey ? scaleKey->scaleX : -1)
                       .arg(scaleKey ? scaleKey->scaleY : -1)
                       .arg(link->isChecked())
                       .arg(scaleX->value()))
            || !check(depth() == scaleDepth + 1,
                      QStringLiteral("linked scale was not one commit")))
            return 1;
        if (!document->undo())
            return 1;
        const auto& restored = live->project().entities.value(vector);
        if (!check(std::abs(restored.transform.scaleX - 1.6) < 1e-6
                       && std::abs(restored.transform.scaleY - 0.9) < 1e-6,
                   QStringLiteral("one undo did not restore both scale axes")))
            return 1;
        link->setChecked(false);
    }

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

    // Easing is one whole-frame control (visual grid, not a text combo); no
    // per-track easing combos.
    if (!check(!panel->findChild<QComboBox*>(
                   QStringLiteral("trackEasing_x")),
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
    auto* easingButton =
        panel->findChild<QToolButton*>(QStringLiteral("keyframeEasingGroup"));
    if (!check(easingButton,
               QStringLiteral("whole-frame easing button missing")))
        return 1;
    if (!check(!panel->findChild<QComboBox*>(
                   QStringLiteral("keyframeEasingGroup")),
               QStringLiteral("plain-text easing combo still present")))
        return 1;
    if (!check(!easingButton->icon().isNull(),
               QStringLiteral("current easing curve preview missing")))
        return 1;

    // The whole preset table is a curve grid, grouped in/out/in-out, with
    // linear, instant and the custom bezier row.
    pvf::EasingPicker* picker = openEasingGrid(panel);
    if (!check(picker, QStringLiteral("easing grid popup missing")))
        return 1;
    if (!check(picker->tileRect(QStringLiteral("linear")).isValid()
                   && picker->tileRect(QStringLiteral("instant")).isValid()
                   && picker->tileRect(QStringLiteral("easeinquad")).isValid()
                   && picker->tileRect(QStringLiteral("easeoutsine")).isValid()
                   && picker->tileRect(QStringLiteral("easeinoutbounce"))
                          .isValid()
                   && picker->tileRect(QStringLiteral("bezier")).isValid(),
               QStringLiteral("easing grid is missing presets")))
        return 1;
    picker->window()->close();
    QApplication::processEvents();

    const int before = depth();
    if (!check(chooseEasing(panel, QStringLiteral("easeinoutcubic")),
               QStringLiteral("easing grid could not choose a preset")))
        return 1;
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

    document->undo();
    QApplication::processEvents();
    if (!check(keyAt(live, uuid, 10)
                   && keyAt(live, uuid, 10)->easing.type
                          == QStringLiteral("linear"),
               QStringLiteral("one undo did not roll back the easing")))
        return 1;
    document->redo();
    QApplication::processEvents();

    // Custom bezier row: the modal dialog is replaced by the test hook, so the
    // full grid -> customRequested -> apply wiring is exercised.
    panel->setEasingCustomEditorForTest(
        [](const pvf::model::EasingSpec& current,
           pvf::model::EasingSpec* result) {
            Q_UNUSED(current);
            result->type = QStringLiteral("bezier");
            result->inX = 0.2;
            result->inY = 0.0;
            result->outX = 0.35;
            result->outY = 1.0;
            return true;
        });
    const int beforeCustom = depth();
    if (!check(chooseEasing(panel, QStringLiteral("bezier")),
               QStringLiteral("custom bezier row missing")))
        return 1;
    QApplication::processEvents();
    flush();
    const Keyframe* customKey = keyAt(live, uuid, 10);
    if (!check(customKey && customKey->easing.type == QStringLiteral("bezier")
                   && std::abs(customKey->easing.inX - 0.2) < 1e-6
                   && std::abs(customKey->easing.outX - 0.35) < 1e-6,
               QStringLiteral("custom bezier was not written to frame 10"))
        || !check(depth() == beforeCustom + 1,
                  QStringLiteral("custom bezier was not one commit")))
        return 1;
    document->undo();
    QApplication::processEvents();
    if (!check(keyAt(live, uuid, 10)
                   && keyAt(live, uuid, 10)->easing.type
                          == QStringLiteral("easeinoutcubic"),
               QStringLiteral("one undo did not roll back the custom bezier")))
        return 1;
    document->redo();
    QApplication::processEvents();

    // Multi-selection: one custom bezier choice rewrites every selected key in
    // one commit, and one undo restores them.
    panel->setSelectedKeyframes(
        {pvf::KeyRef{uuid, 0}, pvf::KeyRef{uuid, 10}});
    live->setPlayhead(10);
    QApplication::processEvents();
    const int beforeBatchCustom = depth();
    if (!check(chooseEasing(panel, QStringLiteral("bezier")),
               QStringLiteral("batch custom bezier row missing")))
        return 1;
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, uuid, 0)
                   && keyAt(live, uuid, 0)->easing.type
                          == QStringLiteral("bezier")
                   && keyAt(live, uuid, 10)
                   && keyAt(live, uuid, 10)->easing.type
                          == QStringLiteral("bezier"),
               QStringLiteral("batch custom bezier missed a selected key"))
        || !check(depth() == beforeBatchCustom + 1,
                  QStringLiteral("batch custom bezier was not one commit")))
        return 1;
    document->undo();
    QApplication::processEvents();
    if (!check(keyAt(live, uuid, 0)
                   && keyAt(live, uuid, 0)->easing.type
                          == QStringLiteral("linear")
                   && keyAt(live, uuid, 10)
                   && keyAt(live, uuid, 10)->easing.type
                          == QStringLiteral("bezier"),
               QStringLiteral("one undo did not roll back the batch bezier")))
        return 1;

    // The first key owns an outgoing segment too: the control stays enabled.
    panel->setSelectedKeyframes({pvf::KeyRef{uuid, 0}});
    live->setPlayhead(0);
    QApplication::processEvents();
    if (!check(easingButton->isEnabled(),
               QStringLiteral("first key should own an outgoing segment")))
        return 1;

    // An empty playhead frame disables the grid and explains why.
    panel->setSelectedKeyframes({});
    live->setPlayhead(30);
    QApplication::processEvents();
    if (!check(!easingButton->isEnabled(),
               QStringLiteral("empty frame should disable the easing grid"))
        || !check(easingButton->text().contains(QStringLiteral("无关键帧")),
                  QStringLiteral("empty frame hint missing")))
        return 1;

    QString detail;
    if (!check(noSingleParameterUi(panel, &detail),
               QStringLiteral("single-parameter UI present: %1").arg(detail)))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "keyframe-easing-group-test OK: visual curve grid, single/"
               "batch custom bezier, undo";
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
    // Closed range [0, 120]: the end frame itself is writable...
    live->setPlayhead(120);
    QApplication::processEvents();
    before = depth();
    speedSpin->setValue(7.0);
    QMetaObject::invokeMethod(speedSpin, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(keyAt(live, uuid, 120)
                   && keyAt(live, uuid, 120)
                          ->htmlVars.value(QStringLiteral("speed"))
                          .toDouble()
                      == 7.0,
               QStringLiteral("HTML edit did not key the closed end frame"))
        || !check(depth() == before + 1,
                  QStringLiteral("end-frame HTML edit was not one commit")))
        return 1;

    // ...but frame == duration (end + 1) is outside and must not commit.
    live->setPlayhead(121);
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

    live->setPlayhead(200);
    QApplication::processEvents();
    lastError.clear();
    before = depth();
    speedSpin->setValue(8.0);
    QMetaObject::invokeMethod(speedSpin, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(depth() == before,
               QStringLiteral("far out-of-range HTML edit added a commit"))
        || !check(lastError.contains(QStringLiteral("范围")),
                  QStringLiteral("far out-of-range HTML edit did not report")))
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
