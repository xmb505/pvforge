// Properties-panel regression tests for audio entities.
//
// Audio clips have no stage transform: selecting one must hide the transform,
// pivot and solver pages, drop the duplicate media-section volume, and keep a
// single dB gain control plus the basic/keyframe/asset fields.  The model keeps
// the existing linear content.volume multiplier.

#include "TestApp.h"
#include "TestRegistry.h"

namespace {

const pvf::model::Keyframe* keyAt(const pvf::EditorSession* session,
                                  const pvf::Uuid& uuid, int frame)
{
    const auto it = session->project().entities.constFind(uuid);
    if (it == session->project().entities.constEnd())
        return nullptr;
    return pvf::model::keyframeAt(it.value().keyframes, frame);
}

double keyVolume(const pvf::model::Keyframe* key)
{
    return key ? key->content.value(QStringLiteral("volume")).toDouble() : -1.0;
}

bool visible(QWidget* widget)
{
    return widget && widget->isVisible();
}

int propertiesAudioTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool ok, const QString& label) {
        if (!ok)
        {
            qCritical() << "properties-audio-test:" << label;
            ++failures;
        }
        return ok;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("audio-properties.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Audio Properties");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "properties-audio-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity audio;
    audio.uuid = pvf::model::newUuid();
    audio.name = QStringLiteral("音频");
    audio.start = 0;
    audio.end = 60;
    audio.row = 0;
    audio.content.kind = pvf::ContentKind::Asset;
    audio.content.asset.type = pvf::AssetType::Audio;
    audio.content.asset.hash = QStringLiteral("audiohash");
    audio.content.asset.volume = 1.0;
    const pvf::Uuid audioUuid = audio.uuid;

    pvf::model::Entity vector;
    vector.uuid = pvf::model::newUuid();
    vector.name = QStringLiteral("矢量");
    vector.start = 0;
    vector.end = 60;
    vector.row = 1;
    vector.content.kind = pvf::ContentKind::Vector;
    vector.content.vector.width = 200.0;
    vector.content.vector.height = 100.0;
    const pvf::Uuid vectorUuid = vector.uuid;

    project.entities.insert(audioUuid, audio);
    project.entities.insert(vectorUuid, vector);
    if (!session->document()->commitEntities(
            {audioUuid, vectorUuid}, QStringLiteral("pvf: audio properties setup"),
            &error))
    {
        qCritical() << "properties-audio-test: setup commit failed" << error;
        return 1;
    }

    pvf::MainWindow window(session.release());
    window.resize(1200, 900);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window.editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window.propertiesForTest();
    if (!live || !document || !panel)
    {
        qCritical() << "properties-audio-test: no session properties panel";
        return 1;
    }
    const auto flushSession = [live] {
        QString flushError;
        live->flush(&flushError);
        return flushError;
    };
    const auto select = [&](const pvf::Uuid& uuid) {
        document->setSelection(uuid);
        live->setPlayhead(0);
        QApplication::processEvents();
        QApplication::processEvents();
    };

    auto* transformSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("transformSection"));
    auto* pivotSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("pivotSection"));
    auto* solverSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("solverSection"));
    auto* basicSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("basicSection"));
    auto* keySection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("keySection"));
    auto* mediaSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("mediaSection"));
    auto* audioSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("audioSection"));
    if (!check(transformSection && pivotSection && solverSection && basicSection
                   && keySection && mediaSection && audioSection,
               QStringLiteral("properties sections missing")))
        return 1;

    select(audioUuid);
    if (!check(document->selection() == audioUuid,
               QStringLiteral("audio selection lost")))
        return 1;
    // Basic info lives behind its collapsible header by default; expand it so
    // the retained parent/range fields can be inspected.
    basicSection->setExpanded(true);
    QApplication::processEvents();

    // 1) No stage transform/pivot/solver content for an audio clip.
    check(!transformSection->isVisible(),
          QStringLiteral("transform section visible for audio"));
    check(!pivotSection->isVisible(),
          QStringLiteral("pivot section visible for audio"));
    check(!solverSection->isVisible(),
          QStringLiteral("solver section visible for audio"));
    check(visible(basicSection) && visible(keySection) && visible(mediaSection)
              && visible(audioSection),
          QStringLiteral("kept audio sections hidden"));

    // Basic stage-space controls follow the hidden transform page.  There is no
    // separate EntitySpace widget in this tree; the parent combo is the only
    // coordinate-space selector and must stay visible for audio.
    auto* inherit = panel->findChild<QCheckBox*>(
        QStringLiteral("entityInheritVisibility"));
    auto* stageVisible =
        panel->findChild<QCheckBox*>(QStringLiteral("entityVisible"));
    auto* physics = panel->findChild<QCheckBox*>(
        QStringLiteral("entityPhysicsInteract"));
    auto* parentCombo =
        panel->findChild<QComboBox*>(QStringLiteral("parentCombo"));
    auto* xRow = panel->findChild<QDoubleSpinBox*>(QStringLiteral("row_x"));
    check(inherit && !inherit->isVisible(),
          QStringLiteral("inherit visibility visible for audio"));
    check(stageVisible && !stageVisible->isVisible(),
          QStringLiteral("stage visible checkbox visible for audio"));
    check(physics && !physics->isVisible(),
          QStringLiteral("physics checkbox visible for audio"));
    check(xRow && !xRow->isVisible(),
          QStringLiteral("X transform row visible for audio"));
    check(visible(parentCombo),
          QStringLiteral("parent combo hidden for audio"));

    // 2) Exactly one dB gain control; the media-section duplicate is gone.
    check(!panel->findChild<QDoubleSpinBox*>(QStringLiteral("assetVolume")),
          QStringLiteral("duplicate media volume still exists"));
    const QList<QDoubleSpinBox*> gains =
        panel->findChildren<QDoubleSpinBox*>(QStringLiteral("audioVolume"));
    if (!check(gains.size() == 1, QStringLiteral("audio gain is not unique")))
        return 1;
    QDoubleSpinBox* gain = gains.first();
    check(gain->suffix() == QStringLiteral(" dB"),
          QStringLiteral("audio gain suffix is not dB"));
    check(gain->minimum() == -60.0 && gain->maximum() == 24.0,
          QStringLiteral("audio gain range is not -60..+24 dB"));
    check(gain->decimals() == 1 && gain->singleStep() == 0.5,
          QStringLiteral("audio gain is not 0.1 dB / 0.5 step"));
    check(std::abs(gain->value() - 0.0) < 1e-6,
          QStringLiteral("linear volume 1.0 is not 0.0 dB"));

    // 3) Keyframe page keeps its status/easing/note entry points and drops the
    //    solver conversion shortcut for audio.
    auto* status =
        panel->findChild<QLabel*>(QStringLiteral("keyframeStatus"));
    auto* easing = panel->findChild<QToolButton*>(
        QStringLiteral("keyframeEasingGroup"));
    auto* note = panel->findChild<QLineEdit*>(QStringLiteral("keyframeNote"));
    auto* convert = panel->findChild<QPushButton*>(
        QStringLiteral("solverConvertButton"));
    auto* convertHint =
        panel->findChild<QLabel*>(QStringLiteral("solverConvertHint"));
    check(visible(status), QStringLiteral("keyframe status hidden"));
    check(visible(easing), QStringLiteral("keyframe easing entry hidden"));
    check(visible(note), QStringLiteral("keyframe note hidden"));
    check(convert && !convert->isVisible(),
          QStringLiteral("solver convert button visible for audio"));
    check(convertHint && !convertHint->isVisible(),
          QStringLiteral("solver convert hint visible for audio"));

    // 4) +6 dB writes the linear gain 10^(6/20) into one whole-frame key, with
    //    exactly one undo entry.
    flushSession();
    const int depthBefore = document->store()->undoDepth();
    gain->setValue(6.0);
    QApplication::processEvents();
    flushSession();
    const pvf::model::Keyframe* boosted = keyAt(live, audioUuid, 0);
    const double expectedBoost = std::pow(10.0, 6.0 / 20.0);
    check(boosted && std::abs(keyVolume(boosted) - expectedBoost) < 1e-3,
          QStringLiteral("+6 dB did not write linear volume 1.995"));
    check(document->store()->undoDepth() == depthBefore + 1,
          QStringLiteral("+6 dB was not one commit"));
    if (boosted)
        qInfo() << "properties-audio-test: +6 dB -> volume" << keyVolume(boosted);

    document->undo();
    QApplication::processEvents();
    select(audioUuid);
    check(std::abs(gain->value() - 0.0) < 1e-6,
          QStringLiteral("undo did not restore 0.0 dB"));

    // 5) -60 dB is the mute floor: the stored linear gain is exactly 0.
    gain->setValue(-60.0);
    QApplication::processEvents();
    flushSession();
    const pvf::model::Keyframe* muted = keyAt(live, audioUuid, 0);
    check(muted && keyVolume(muted) == 0.0,
          QStringLiteral("-60 dB did not write volume 0.0"));
    check(std::abs(gain->value() + 60.0) < 1e-6,
          QStringLiteral("-60 dB did not stay at the mute floor"));

    // Keyframe note editing stays available on the audio key.
    check(easing->isEnabled(),
          QStringLiteral("keyframe easing disabled on the audio key"));
    note->setText(QStringLiteral("副歌第一拍"));
    QMetaObject::invokeMethod(note, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    flushSession();
    const pvf::model::Keyframe* noted = keyAt(live, audioUuid, 0);
    check(noted && noted->note == QStringLiteral("副歌第一拍"),
          QStringLiteral("audio keyframe note was not written"));

    // 6) Switching back to a visual entity restores every hidden section.
    select(vectorUuid);
    check(transformSection->isVisible() && pivotSection->isVisible()
              && solverSection->isVisible(),
          QStringLiteral("visual entity sections did not come back"));
    check(!audioSection->isVisible(),
          QStringLiteral("audio section still visible for a vector"));
    check(visible(inherit) && visible(stageVisible) && visible(physics),
          QStringLiteral("visual entity basic controls did not come back"));
    check(visible(convert) && visible(convertHint),
          QStringLiteral("solver convert shortcut did not come back"));

    if (failures == 0)
        qInfo() << "properties-audio-test: ok";
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerPropertiesAudioTests(Registry& registry)
{
    registry.add(QStringLiteral("--properties-audio-test"),
                 [](QApplication& app) { return propertiesAudioTest(app); });
}

} // namespace pvf::tests
