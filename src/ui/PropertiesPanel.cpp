#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Content.h"
#include "model/Easing.h"
#include "model/Entity.h"
#include "model/Query.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"
#include "next/properties/SolverPreviewWidget.h"
#include "next/properties/SpecialContentEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace pvf {

using namespace ui::detail;

PropertiesPanel::PropertiesPanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_sessionMode(true)
{
    buildUi();
    buildSessionUi();

    if (m_session && m_session->document())
    {
        auto* document = m_session->document();
        connect(document, &next::Document::selectionChanged, this, [this] {
            const Uuid selected = m_session->document()->selection();
            if (selected != m_sessionUuid)
            {
                m_sessionUuid = selected;
                refresh();
            }
        });
        connect(document, &next::Document::playheadChanged, this, [this] {
            m_frame = m_session->playhead();
            // Scrubbing/playback only touches frame-dependent widgets; a full
            // refresh() here rebuilt every solver card and re-ran the solver.
            refreshPlayheadOnly();
        });
        connect(document, &next::Document::changed, this, [this] {
            if (!m_editing)
                refresh();
        });
        if (auto* playback = m_session->playback())
        {
            connect(playback,
                    &next::PlaybackController::playbackStateChanged, this,
                    [this](bool playing) { setPlaybackActive(playing); });
            m_playbackActive = playback->isPlaying();
            if (m_solverPreview)
                m_solverPreview->setPlaybackActive(m_playbackActive);
        }
    }
    refresh();
}

const model::Entity* PropertiesPanel::sessionEntity() const
{
    if (!m_session || m_sessionUuid.isEmpty())
        return nullptr;
    const auto& entities = m_session->project().entities;
    const auto it = entities.constFind(m_sessionUuid);
    return it == entities.constEnd() ? nullptr : &it.value();
}

bool PropertiesPanel::sessionEditable() const
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !m_session)
        return false;
    return !next::isEntityLocked(m_session->project(), entity->uuid);
}

int PropertiesPanel::sessionRelativeFrame(const model::Entity& entity) const
{
    const int playhead = m_session ? m_session->playhead() : m_frame;
    return playhead - entity.start;
}

void PropertiesPanel::reportSessionError(const QString& message)
{
    emit sessionEditFailed(message.isEmpty() ? QStringLiteral("未知错误") : message);
}

bool PropertiesPanel::runSessionEdit(
    const QString& message, const std::function<bool(model::Entity&)>& mutate,
    bool ignoreLock)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return false;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateEntity(
        m_sessionUuid, message, mutate, &error, ignoreLock);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
    return ok;
}

bool PropertiesPanel::runSessionContentEdit(
    const QString& message, const std::function<bool(model::Content&)>& mutate)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return false;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateContent(
        m_sessionUuid, message, mutate, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
    return ok;
}

bool PropertiesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::FocusIn)
    {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        for (const SolverCard& card : m_solverCards)
        {
            if (card.box == watched
                || (widget && card.box && card.box->isAncestorOf(widget)))
            {
                selectSolver(card.id);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PropertiesPanel::setColorButton(QPushButton* button, const QColor& color)
{
    button->setText(color.name().toUpper());
    const QString text = color.lightness() > 140 ? QStringLiteral("#000")
                                                 : QStringLiteral("#fff");
    button->setStyleSheet(
        QStringLiteral("background-color:%1;color:%2;").arg(color.name(), text));
}

QColor PropertiesPanel::pickColor(const QColor& initial, const QString& title)
{
    return QColorDialog::getColor(initial, this, title, QColorDialog::ShowAlphaChannel);
}

void PropertiesPanel::setEntity(EntityId id)
{
    m_entity = id;
    refresh();
}

void PropertiesPanel::setFrame(int frame)
{
    m_frame = frame;
    refresh();
}

void PropertiesPanel::setSelectedUuid(const Uuid& uuid)
{
    if (!m_sessionMode)
        return;
    if (m_sessionUuid == uuid)
    {
        refresh();
        return;
    }
    // A different entity owns different solver ids: drop the focus and the
    // mini preview binding before the rebind in refreshSession().
    if (m_solverPreview)
        m_solverPreview->clearSolver();
    m_selectedSolverId.clear();
    m_sessionUuid = uuid;
    refresh();
}

void PropertiesPanel::setAutoKey(bool on)
{
    if (m_autoKey == on)
        return;
    m_autoKey = on;
    if (m_autoKeyCheck)
    {
        const QSignalBlocker blocker(m_autoKeyCheck);
        m_autoKeyCheck->setChecked(on);
    }
}

void PropertiesPanel::setPlaybackActive(bool active)
{
    if (m_playbackActive == active)
        return;
    m_playbackActive = active;
    if (!m_solverPreview)
        return;
    m_solverPreview->setPlaybackActive(active);
    if (active)
        return;
    // Pause: refresh the trajectory exactly once, after the session's queued
    // re-warm has run.  The zero-delay timer registered here fires behind the
    // precompute timer registered by EditorSession on the same signal.
    QTimer::singleShot(0, m_solverPreview, [preview = m_solverPreview] {
        preview->refreshPreview();
    });
}

void PropertiesPanel::refreshPlayheadOnly()
{
    if (!m_sessionMode || m_editing || !m_session)
        return;
    const model::Entity* entity = sessionEntity();
    if (!entity)
        return;

    const bool camera = entity->content.kind == ContentKind::Camera;
    const int relative = sessionRelativeFrame(*entity);
    const bool inRange = relative >= 0 && relative < entity->duration();
    const bool editable = sessionEditable();

    m_keyButton->setEnabled(editable && inRange);

    for (Row& row : m_rows)
    {
        {
            const QSignalBlocker blocker(row.spin);
            double value = currentTrackValue(*entity, row.prop, relative,
                                             isStepTrack(row.prop))
                / row.factor;
            if (camera && row.prop == QLatin1String("scaleX"))
                value = std::abs(value);
            if (!row.spin->hasFocus())
                row.spin->setValue(value);
        }
        if (!row.key)
            continue;

        const bool hasKey = inRange && hasKeyAt(*entity, row.prop, relative);
        const bool blocked =
            inRange && trackBlockedAt(*entity, row.prop, relative);
        row.key->setText(hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
        row.key->setToolTip(
            blocked
                ? QStringLiteral("该帧由求解段驱动，不能写入普通关键帧")
                : hasKey
                    ? QStringLiteral("删除当前帧的关键帧")
                    : QStringLiteral("在当前帧打关键帧"));
        row.key->setEnabled(editable && inRange && !blocked);
    }

    setSpinIfIdle(m_pivotX, currentTrackValue(*entity, QStringLiteral("pivotX"),
                                              relative, false));
    setSpinIfIdle(m_pivotY, currentTrackValue(*entity, QStringLiteral("pivotY"),
                                              relative, false));

    // Track rows and the group easing control are frame-dependent.  The
    // structure check inside reloadTrackRows() is cheap and never rebuilds on
    // a playhead tick.
    reloadTrackRows(*entity);
    reloadSessionHtmlValues(*entity);
    // Create/convert depend on whether the playhead sits on the last key.
    reloadSolverConvert(*entity);

    // The mini preview only needs a repaint for its current dot; the
    // trajectory samples are already cached.  During playback it stays
    // frozen, so don't even repaint it.
    if (!m_playbackActive && m_solverPreview)
        m_solverPreview->update();
}

void PropertiesPanel::refreshSession()
{
    if (m_editing)
        return;
    const model::Entity* entity = sessionEntity();
    const bool valid = entity != nullptr;
    if (!valid)
    {
        m_title->setText(QStringLiteral("未选择对象"));
        for (CollapsibleSection* section :
             {m_basicSection, m_transformSection, m_keySection, m_textSection,
              m_comicSection, m_linesSection, m_wallSection, m_halftoneSection,
              m_specialSection, m_sessionHtmlSection,
              m_vectorSection, m_mediaSection, m_audioSection, m_pivotSection,
              m_cameraSection, m_solverSection})
        {
            if (section)
                section->hide();
        }
        m_specialUuid.clear();
        if (m_solverPreview)
        {
            m_solverPreview->clearSolver();
            m_solverPreview->hide();
        }
        return;
    }

    const bool editable = sessionEditable();
    const ContentKind kind = entity->content.kind;
    const bool camera = kind == ContentKind::Camera;
    const bool text = kind == ContentKind::Text;
    const bool vector = kind == ContentKind::Vector;
    const bool asset = kind == ContentKind::Asset;
    const bool audio = asset && entity->content.asset.type == AssetType::Audio;
    const bool special = next::SpecialContentEditor::handles(kind);

    m_title->setText(entity->name + QStringLiteral(" · ")
                     + contentKindLabel(kind));

    m_basicSection->show();
    m_transformSection->show();
    m_keySection->show();
    m_pivotSection->setVisible(!audio);
    m_textSection->setVisible(text);
    m_vectorSection->setVisible(vector);
    m_mediaSection->setVisible(asset);
    m_audioSection->hide();
    m_comicSection->hide();
    m_linesSection->hide();
    m_wallSection->hide();
    m_halftoneSection->hide();
    m_specialSection->setVisible(special);
    m_cameraSection->setVisible(camera);
    m_solverSection->show();

    m_basicSection->setEnabled(true);
    m_transformSection->setEnabled(editable);
    m_keySection->setEnabled(true);
    m_pivotSection->setEnabled(editable);
    m_textSection->setEnabled(editable);
    m_vectorSection->setEnabled(editable);
    m_mediaSection->setEnabled(editable);
    m_specialSection->setEnabled(editable);
    m_sessionHtmlSection->setEnabled(editable);
    m_cameraSection->setEnabled(true);
    m_solverSection->setEnabled(editable);

    if (!m_basicName->hasFocus())
    {
        const QSignalBlocker blocker(m_basicName);
        m_basicName->setText(entity->name);
    }
    setSpinIfIdle(m_basicStart, entity->start);
    setSpinIfIdle(m_basicEnd, entity->end);
    setSpinIfIdle(m_basicRow, entity->row);
    {
        const QSignalBlocker blocker(m_basicHidden);
        m_basicHidden->setChecked(entity->hidden);
    }
    {
        const QSignalBlocker blocker(m_basicLocked);
        m_basicLocked->setChecked(entity->locked);
    }
    {
        const QSignalBlocker blocker(m_basicVisible);
        m_basicVisible->setChecked(entity->visible);
    }
    {
        const QSignalBlocker blocker(m_basicInherit);
        m_basicInherit->setChecked(entity->inheritVisibility);
    }
    {
        const QSignalBlocker blocker(m_basicPhysics);
        m_basicPhysics->setChecked(entity->physicsInteract);
    }
    m_basicName->setEnabled(editable);
    m_basicStart->setEnabled(editable);
    m_basicEnd->setEnabled(editable);
    m_basicRow->setEnabled(editable);
    m_basicHidden->setEnabled(editable);
    m_basicLocked->setEnabled(true);
    m_basicVisible->setEnabled(editable);
    m_basicInherit->setEnabled(editable);
    m_basicPhysics->setEnabled(editable);

    {
        const QSignalBlocker blocker(m_parent);
        m_parent->clear();
        m_parent->addItem(QStringLiteral("工作台（世界坐标）"), QString());
        const model::Project& project = m_session->project();
        std::function<void(const Uuid&, int)> appendChildren =
            [&](const Uuid& parent, int depth) {
                for (const Uuid& uuid : model::childrenOf(project, parent))
                {
                    if (uuid == entity->uuid
                        || isDescendant(project, uuid, entity->uuid))
                        continue;
                    const model::Entity& candidate = project.entities[uuid];
                    if (camera
                        && candidate.content.kind == ContentKind::Camera)
                        continue;
                    if (candidate.content.kind == ContentKind::Asset
                        && candidate.content.asset.type == AssetType::Audio)
                        continue;
                    const QString prefix = QStringLiteral("  ").repeated(depth);
                    const QString typePrefix =
                        candidate.content.kind == ContentKind::Empty
                            ? QStringLiteral("[空] ")
                        : candidate.content.kind == ContentKind::Camera
                            ? QStringLiteral("[摄像机] ")
                            : QString();
                    m_parent->addItem(prefix + typePrefix + candidate.name,
                                      uuid);
                    appendChildren(uuid, depth + 1);
                }
            };
        appendChildren(Uuid(), 0);
        const int index = m_parent->findData(entity->parentUuid);
        m_parent->setCurrentIndex(index >= 0 ? index : 0);
    }
    m_parent->setEnabled(editable);
    m_parentRow->show();
    if (m_parentLabel)
        m_parentLabel->show();

    const int relative = sessionRelativeFrame(*entity);
    const bool inRange = relative >= 0 && relative < entity->duration();
    m_keyButton->setEnabled(editable && inRange);

    for (Row& row : m_rows)
    {
        const bool hiddenRow =
            camera && (row.prop == QLatin1String("scaleY")
                       || row.prop == QLatin1String("opacity"));
        // Hide the whole form row: a hidden spin box used to leave its key
        // diamond dangling below the camera framing row.
        if (row.key && row.key->parentWidget())
            row.key->parentWidget()->setVisible(!hiddenRow);
        row.spin->setVisible(!hiddenRow);
        row.label->setVisible(!hiddenRow);
        if (row.key)
            row.key->setVisible(!hiddenRow);
        {
            const QSignalBlocker blocker(row.spin);
            if (camera && row.prop == QLatin1String("scaleX"))
            {
                row.label->setText(QStringLiteral("取景比例"));
                row.spin->setRange(5.0, 100.0);
                row.spin->setDecimals(1);
                row.spin->setSingleStep(1.0);
                row.spin->setSuffix(QStringLiteral("%"));
                row.spin->setToolTip(
                    QStringLiteral("摄像机取景比例，绝对值限制 0.05–1.0"
                                   "（5%–100%）"));
                row.factor = 0.01;
            }
            else if (row.prop == QLatin1String("scaleX"))
            {
                row.label->setText(row.baseLabel);
                row.spin->setRange(-100.0, 100.0);
                row.spin->setDecimals(3);
                row.spin->setSingleStep(0.01);
                row.spin->setSuffix(QString());
                row.spin->setToolTip(QString());
                row.factor = 1.0;
            }
            double value = currentTrackValue(*entity, row.prop, relative,
                                             isStepTrack(row.prop))
                / row.factor;
            if (camera && row.prop == QLatin1String("scaleX"))
                value = std::abs(value);
            if (!row.spin->hasFocus())
                row.spin->setValue(value);
        }
        row.spin->setEnabled(editable && !hiddenRow);

        if (row.key)
        {
            const bool hasKey = inRange && hasKeyAt(*entity, row.prop, relative);
            const bool blocked =
                inRange && trackBlockedAt(*entity, row.prop, relative);
            row.key->setText(hasKey ? QStringLiteral("◆")
                                    : QStringLiteral("◇"));
            row.key->setToolTip(
                blocked
                    ? QStringLiteral("该帧由求解段驱动，不能写入普通关键帧")
                    : hasKey
                        ? QStringLiteral("删除当前帧的关键帧")
                        : QStringLiteral("在当前帧打关键帧"));
            row.key->setEnabled(editable && inRange && !blocked);
        }
    }

    m_usePivot->hide();
    setSpinIfIdle(m_pivotX, currentTrackValue(*entity, QStringLiteral("pivotX"),
                                              relative, false));
    setSpinIfIdle(m_pivotY, currentTrackValue(*entity, QStringLiteral("pivotY"),
                                              relative, false));
    m_pivotX->setEnabled(editable);
    m_pivotY->setEnabled(editable);

    if (text)
    {
        const model::TextContent& content = entity->content.text;
        if (!m_textContent->hasFocus())
        {
            const QSignalBlocker blocker(m_textContent);
            m_textContent->setText(content.text);
        }
        if (!m_textFont->hasFocus())
        {
            const QSignalBlocker blocker(m_textFont);
            m_textFont->setCurrentFont(
                content.fontFamily.isEmpty() ? QFont()
                                             : QFont(content.fontFamily));
        }
        setSpinIfIdle(m_textSize, content.pixelSize);
        m_textColorValue = QColor(content.color);
        setColorButton(m_textColorButton, m_textColorValue);
        setSpinIfIdle(m_textStrokeWidth, content.strokeWidth);
        m_textStrokeColorValue = QColor(content.strokeColor);
        setColorButton(m_textStrokeColorButton, m_textStrokeColorValue);
        {
            const QSignalBlocker blocker(m_textAlign);
            selectComboData(m_textAlign, content.align);
        }
        {
            const QSignalBlocker blocker(m_textDirection);
            m_textDirection->setCurrentIndex(
                content.vertical ? (content.verticalRtl ? 1 : 2) : 0);
        }
        {
            const QSignalBlocker blocker(m_textBold);
            m_textBold->setChecked(content.bold);
        }
        {
            const QSignalBlocker blocker(m_textItalic);
            m_textItalic->setChecked(content.italic);
        }
        {
            const QSignalBlocker blocker(m_textUnderline);
            m_textUnderline->setChecked(content.underline);
        }
        setSpinIfIdle(m_textLineHeight, content.lineHeight);
        setSpinIfIdle(m_textLetterSpacing, content.letterSpacing);
        setSpinIfIdle(m_textPadding, content.padding);
        setSpinIfIdle(m_textBoxWidth, content.boxWidth);
        setSpinIfIdle(m_textBoxHeight, content.boxHeight);
    }

    if (vector)
    {
        const model::VectorContent& content = entity->content.vector;
        {
            const QSignalBlocker blocker(m_vectorShape);
            selectComboData(m_vectorShape, content.shape);
        }
        setSpinIfIdle(m_vectorCorner, content.cornerRadius);
        setSpinIfIdle(m_vectorSides, content.sides);
        setSpinIfIdle(m_vectorInner, content.innerRatio);
        setSpinIfIdle(m_vectorWidth, content.width);
        setSpinIfIdle(m_vectorHeight, content.height);
        m_fillValue = QColor(content.fillColor);
        setColorButton(m_fillButton, m_fillValue);
        m_strokeValue = QColor(content.strokeColor);
        setColorButton(m_strokeButton, m_strokeValue);
        setSpinIfIdle(m_strokeWidth, content.strokeWidth);
        m_vectorCorner->setEnabled(editable && content.shape == 3);
        m_vectorSides->setEnabled(editable
                                  && (content.shape == 6 || content.shape == 7));
        m_vectorInner->setEnabled(editable && content.shape == 6);
    }

    if (asset)
    {
        const model::AssetContent& content = entity->content.asset;
        m_assetHash->setText(content.hash);
        m_assetType->setText(assetTypeLabel(content.type));

        int frames = 0;
        const auto stored = m_session->assets().getIncludingMissing(content.hash);
        if (stored)
            frames = stored->metadata.value(QStringLiteral("durationFrames")).toInt(0);
        m_assetSourceIn->setMaximum(frames > 0 ? std::max(0, frames - 1)
                                               : 1000000);
        setSpinIfIdle(m_assetSourceIn, content.sourceIn);
        setSpinIfIdle(m_assetVolume, content.volume);
        m_assetSourceIn->setEnabled(editable);
        m_assetVolume->setEnabled(editable);
        m_replaceSource->show();
        m_replaceSource->setEnabled(editable);

        QString info = assetTypeLabel(content.type);
        if (!stored)
            info += QStringLiteral(" · 素材缺失");
        else if (!stored->name.isEmpty())
            info += QStringLiteral(" · ") + stored->name;
        if (frames > 0)
            info += QStringLiteral(" · %1 帧").arg(frames);
        m_mediaInfo->setText(info);
        m_mediaInfo->setToolTip(content.hash);
    }

    if (special && m_specialEditor)
    {
        const bool reload = m_specialUuid != entity->uuid
            || m_specialEditor->kind() != kind;
        m_specialUuid = entity->uuid;
        QWidget* focus = QApplication::focusWidget();
        if (reload || !focus || !m_specialEditor->isAncestorOf(focus))
            m_specialEditor->setContent(entity->content);
    }
    else
    {
        m_specialUuid.clear();
    }

    reloadSessionHtml(*entity);

    reloadTrackRows(*entity);
    updateSolverCards(*entity);
    for (SolverCard& card : m_solverCards)
        card.box->setEnabled(editable);
    reloadSolverConvert(*entity);
    m_solverEffectCombo->setEnabled(editable);
    m_solverTargetCombo->setEnabled(editable);
    m_solverEndSpin->setEnabled(editable);

    {
        const QSignalBlocker blocker(m_autoKeyCheck);
        m_autoKeyCheck->setChecked(m_autoKey);
    }
}

void PropertiesPanel::refresh()
{
    if (m_sessionMode)
    {
        refreshSession();
        return;
    }
}

} // namespace pvf
