#pragma once

#include "app/EditActions.h"
#include "common/Types.h"
#include "core/EntityId.h"
#include "model/Easing.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QFontComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

namespace pvf {

namespace model {
struct Content;
struct EasingSpec;
struct Entity;
struct Keyframe;
struct SolverSegment;
} // namespace model

namespace next {
class SolverPreviewWidget;
} // namespace next

class CollapsibleSection;
class EditorSession;

class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    // Session mode: reads model::Entity and writes through EditActions.
    explicit PropertiesPanel(EditorSession* session, QWidget* parent = nullptr);

    void refresh();

    void setSelectedUuid(const Uuid& uuid);
    Uuid selectedUuid() const { return m_sessionUuid; }
    bool sessionMode() const { return true; }
    Uuid selectedSolverId() const { return m_selectedSolverId; }

    // Timeline selection.  A single selected whole-frame key takes priority
    // for property edits; a multi-selection rewrites only the edited field on
    // every selected key of this entity and commits once.
    void setSelectedKeyframes(const QVector<KeyRef>& selection);
    int selectedKeyframeCount() const { return int(m_selectedKeys.size()); }

    // Writes one whole-frame easing: the selected key, every selected key of
    // this entity (one commit), or the playhead key.  No key -> status hint.
    void applyKeyframeEasing(const model::EasingSpec& spec);

    // Playback freeze for the mini solver preview.
    void setPlaybackActive(bool active);
#ifdef PVFORGE_TEST_HOOKS
    bool playbackActiveForTest() const { return m_playbackActive; }
    int activeEditFrameForTest() const;
    // Replaces the modal bezier dialog so the custom path stays testable.
    using EasingCustomEditor =
        std::function<bool(const model::EasingSpec& current,
                           model::EasingSpec* result)>;
    void setEasingCustomEditorForTest(EasingCustomEditor editor);
#endif

signals:
    void sessionEditFailed(const QString& message);
    // Informational status (out-of-range playhead etc.), not an edit failure.
    void sessionStatusMessage(const QString& message);
    void sessionReplaceSourceRequested(const Uuid& uuid);
    // "等比缩放" toggled; the shell mirrors it onto the viewport gizmo.
    void scaleLinkChanged(bool enabled);
    // A solver card was activated; the viewport keeps that trajectory visible.
    void solverSelected(const Uuid& entityUuid, const Uuid& solverId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // One numeric whole-frame animation parameter.  No per-parameter diamond:
    // editing any animation value always writes the whole frame.
    struct Row
    {
        QString prop;
        QString baseLabel;
        QLabel* label = nullptr;
        QDoubleSpinBox* spin = nullptr;
        double factor = 1.0; // display value * factor = stored value
    };

    // One manifest variable row.  Numeric variables are animation parameters
    // stored in Keyframe::htmlVars; non-numeric variables stay read-only.
    struct SessionHtmlRow
    {
        QString name;
        bool numeric = false;
        double defaultValue = 0.0;
        double minimum = -1000000000.0;
        double maximum = 1000000000.0;
        double step = 0.01;
        int decimals = 3;
        QString readOnlyText;
        QLabel* nameLabel = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QLabel* readOnlyValue = nullptr;
    };

    struct SolverCard
    {
        Uuid id;
        SolverEffect effect = SolverEffect::Gravity;
        QGroupBox* box = nullptr;
        QSpinBox* start = nullptr;
        QLabel* endInfo = nullptr;
        QCheckBox* manualEnd = nullptr;
        QWidget* endRow = nullptr;
        QSpinBox* end = nullptr;
        QToolButton* advanced = nullptr;
        QWidget* velocityRow = nullptr;
        QDoubleSpinBox* velocityX = nullptr;
        QDoubleSpinBox* velocityY = nullptr;
        QPushButton* remove = nullptr;
        QHash<QString, QWidget*> params;
    };

    void buildUi();
    void buildSessionUi();
    void buildBasicSection(QVBoxLayout* layout);
    void buildCameraSection(QVBoxLayout* layout);
    void buildSolverSection(QVBoxLayout* layout);

    void addRow(QFormLayout* form, const QString& label, const QString& prop,
                double lo, double hi, int decimals, double step,
                double factor = 1.0);
    void buildTextSection(QVBoxLayout* layout);
    void buildVectorSection(QVBoxLayout* layout);
    void buildMediaSection(QVBoxLayout* layout);
    void buildAudioSection(QVBoxLayout* layout);
    void buildPivotSection(QVBoxLayout* layout);
    void buildSessionHtmlSection(QVBoxLayout* layout);
    void applyData(const QString& key, const QVariant& value);
    void applyAudioTrimStart(int playheadFrame);
    void applyAudioTrimEnd(int playheadFrame);
    void setColorButton(QPushButton* button, const QColor& color);
    QColor pickColor(const QColor& initial, const QString& title);

    // ---- session mode ----------------------------------------------------
    void refreshSession();
    void refreshPlayheadOnly();
    void refreshKeyframeStatus(const model::Entity& entity);
    void reloadSessionHtmlValues(const model::Entity& entity);
    const model::Entity* sessionEntity() const;
    bool sessionEditable() const;
    int playheadRelative(const model::Entity& entity) const;
    // Every key this entity should receive the edit: one selected key, all
    // selected keys of this entity (ascending), or the playhead when nothing
    // is selected.  Other entities' selected keys are ignored.
    QVector<int> activeTargetKeyFrames(const model::Entity& entity) const;
    // Value/control display frame.  With several targets the playhead wins
    // only when it is itself selected; otherwise the earliest selected frame is
    // used, so an unselected playhead never supplies the displayed values.
    int activeDisplayKeyFrame(const model::Entity& entity) const;
    int activeKeyFrame(const model::Entity& entity) const;
    // Applies one whole-frame field to all target keys (>= 2) in one commit.
    // Frames inside an explicit solver segment are skipped.
    bool applyBatchKeyframeEdit(
        const QString& message,
        const std::function<void(model::Keyframe&)>& mutate);

    bool runSessionEdit(const QString& message,
                        const std::function<bool(model::Entity&)>& mutate,
                        bool ignoreLock = false);
    bool runSessionContentEdit(const QString& message,
                               const std::function<bool(model::Content&)>& mutate);
    // Pauses playback and returns the frame the edit targets.  Out-of-range
    // frames are returned verbatim; callers write static values instead.
    int prepareAnimationEdit(const model::Entity& entity);
    bool writeAnimationValue(const QString& property, double value,
                             bool boolean = false,
                             bool linkedScale = false);
    bool writeHtmlVariable(const QString& name, double value);
    void deleteActiveKeyframe();
    void reloadSessionHtml(const model::Entity& entity);
    void rebuildSessionHtmlRows(const QJsonObject& variables);
    void editSessionHtmlVariable(const QString& name, double value);
    void applySessionData(const QString& key, const QVariant& value);
    void applySessionParent(const Uuid& parent);
    void applySessionAsset(const QString& field, const QVariant& value);
    void copyTransformParams();
    void pasteTransformParams();
    void resetTransformParams();
    void updateSolverCards(const model::Entity& entity);
    void updateSolverPreview(const model::Entity& entity,
                             const QVector<model::SolverSegment>& solvers);
    void rebuildSolverCards(const Uuid& entityUuid,
                            const QVector<model::SolverSegment>& solvers);
    void fillSolverCard(SolverCard& card, const Uuid& entityUuid,
                        const model::SolverSegment& solver);
    void reloadSolverCreate(const model::Entity& entity);
    void reloadSolverConvert(const model::Entity& entity);
    bool canConvertCurrentKey(const model::Entity& entity,
                              QString* reason = nullptr) const;
    void openKeyframeEasingMenu();
    void promptGroupBezier();
    void showSolverConvertMenu();
    void convertSolverFromKey(SolverEffect effect);
    void cancelCurrentSolver();
    void createSolverFromPlayhead();
    void selectSolver(const Uuid& solverId);
    void syncSolverFocus(const QVector<model::SolverSegment>& solvers);
    void updateSolver(const Uuid& solverId, const QString& message,
                      const std::function<bool(model::SolverSegment&)>& mutate);
    void deleteSolver(const Uuid& solverId);
    void reportSessionError(const QString& message);

    EditorSession* m_session = nullptr;
    Uuid m_sessionUuid;
    QVector<KeyRef> m_selectedKeys;
    bool m_editing = false;
    bool m_playbackActive = false;
    int m_frame = 0;

    QLabel* m_title = nullptr;
    QWidget* m_parentRow = nullptr;
    QLabel* m_parentLabel = nullptr;
    QComboBox* m_parent = nullptr;
    QVector<Row> m_rows;
    QVBoxLayout* m_rootLayout = nullptr;
    QVBoxLayout* m_keyLayout = nullptr;
    QFormLayout* m_textForm = nullptr;
    QVBoxLayout* m_mediaLayout = nullptr;

    CollapsibleSection* m_transformSection = nullptr;
    CollapsibleSection* m_keySection = nullptr;
    CollapsibleSection* m_textSection = nullptr;
    CollapsibleSection* m_vectorSection = nullptr;
    CollapsibleSection* m_mediaSection = nullptr;
    CollapsibleSection* m_audioSection = nullptr;
    CollapsibleSection* m_pivotSection = nullptr;

    CollapsibleSection* m_sessionHtmlSection = nullptr;
    QLabel* m_sessionHtmlInfo = nullptr;
    QLabel* m_sessionHtmlWarning = nullptr;
    QLabel* m_sessionHtmlEmpty = nullptr;
    QWidget* m_sessionHtmlVariables = nullptr;
    QVBoxLayout* m_sessionHtmlVariablesLayout = nullptr;
    QVector<SessionHtmlRow> m_sessionHtmlRows;
    Uuid m_sessionHtmlUuid;
    AssetHash m_sessionHtmlHash;
    bool m_sessionHtmlPresent = false;
    bool m_sessionHtmlTimeDependent = true;

    QCheckBox* m_usePivot = nullptr;
    QDoubleSpinBox* m_pivotX = nullptr;
    QDoubleSpinBox* m_pivotY = nullptr;

    QLineEdit* m_textContent = nullptr;
    QFontComboBox* m_textFont = nullptr;
    QSpinBox* m_textSize = nullptr;
    QPushButton* m_textColorButton = nullptr;
    QPushButton* m_textStrokeColorButton = nullptr;
    QDoubleSpinBox* m_textStrokeWidth = nullptr;
    QComboBox* m_textAlign = nullptr;
    QComboBox* m_textDirection = nullptr;
    QCheckBox* m_textBold = nullptr;
    QCheckBox* m_textItalic = nullptr;
    QCheckBox* m_textUnderline = nullptr;
    QColor m_textColorValue{Qt::white};
    QColor m_textStrokeColorValue{Qt::black};
    QDoubleSpinBox* m_textLineHeight = nullptr;
    QDoubleSpinBox* m_textLetterSpacing = nullptr;
    QDoubleSpinBox* m_textPadding = nullptr;
    QDoubleSpinBox* m_textBoxWidth = nullptr;
    QDoubleSpinBox* m_textBoxHeight = nullptr;

    QComboBox* m_vectorShape = nullptr;
    QDoubleSpinBox* m_vectorCorner = nullptr;
    QSpinBox* m_vectorSides = nullptr;
    QDoubleSpinBox* m_vectorInner = nullptr;
    QDoubleSpinBox* m_vectorWidth = nullptr;
    QDoubleSpinBox* m_vectorHeight = nullptr;
    QPushButton* m_fillButton = nullptr;
    QPushButton* m_strokeButton = nullptr;
    QDoubleSpinBox* m_strokeWidth = nullptr;
    QColor m_fillValue{90, 170, 255};
    QColor m_strokeValue{Qt::white};

    QLabel* m_mediaInfo = nullptr;
    QPushButton* m_replaceSource = nullptr;
    QLineEdit* m_assetHash = nullptr;
    QLabel* m_assetType = nullptr;
    QSpinBox* m_assetSourceIn = nullptr;
    QDoubleSpinBox* m_assetVolume = nullptr;
    QSpinBox* m_audioStart = nullptr;
    QDoubleSpinBox* m_audioTrimStart = nullptr;
    QDoubleSpinBox* m_audioTrimEnd = nullptr;
    QDoubleSpinBox* m_audioFadeIn = nullptr;
    QDoubleSpinBox* m_audioFadeOut = nullptr;
    QSpinBox* m_audioVolume = nullptr;

    // Basic entity fields (session only).
    CollapsibleSection* m_basicSection = nullptr;
    QLineEdit* m_basicName = nullptr;
    QSpinBox* m_basicStart = nullptr;
    QSpinBox* m_basicEnd = nullptr;
    QSpinBox* m_basicRow = nullptr;
    QCheckBox* m_basicHidden = nullptr;
    QCheckBox* m_basicLocked = nullptr;
    QCheckBox* m_basicVisible = nullptr;
    QCheckBox* m_basicInherit = nullptr;
    QCheckBox* m_basicPhysics = nullptr;
    QCheckBox* m_basicFollow = nullptr;

    // Whole-frame keyframe status + metadata.
    QWidget* m_keyStatusRow = nullptr;
    QLabel* m_keyStatus = nullptr;
    QPushButton* m_keyDelete = nullptr;
    QToolButton* m_keyEasingButton = nullptr;
    QLineEdit* m_keyNote = nullptr;
    QPushButton* m_convertSolverButton = nullptr;
    QPushButton* m_cancelSolverButton = nullptr;
    QLabel* m_convertSolverHint = nullptr;
#ifdef PVFORGE_TEST_HOOKS
    EasingCustomEditor m_easingCustomEditor;
#endif

    // Transform section MI operations.  The clipboard holds one whole-frame
    // animation payload (public fields + content + htmlVars).
    QCheckBox* m_transformLink = nullptr;
    QJsonObject m_transformClipboard;
    bool m_hasTransformClipboard = false;

    // Content pages that have no legacy widget.
    CollapsibleSection* m_cameraSection = nullptr;
    QLabel* m_cameraInfo = nullptr;

    // Solvers (session only).
    CollapsibleSection* m_solverSection = nullptr;
    QLabel* m_solverEmpty = nullptr;
    QWidget* m_solverHost = nullptr;
    QVBoxLayout* m_solverCardsLayout = nullptr;
    QComboBox* m_solverEffectCombo = nullptr;
    QComboBox* m_solverTargetCombo = nullptr;
    QLabel* m_solverEndAuto = nullptr;
    QCheckBox* m_solverManualEnd = nullptr;
    QWidget* m_solverEndRow = nullptr;
    QSpinBox* m_solverEndSpin = nullptr;
    QPushButton* m_solverCreateButton = nullptr;
    QVector<SolverCard> m_solverCards;
    Uuid m_selectedSolverId;
    next::SolverPreviewWidget* m_solverPreview = nullptr;
};

} // namespace pvf
