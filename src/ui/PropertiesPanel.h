#pragma once

#include "common/Types.h"
#include "core/EntityId.h"

#if PVFORGE_LEGACY_SHELL
#include "core/Project.h"
#endif
#include "html/HtmlAsset.h"

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
class QStackedWidget;
class QVBoxLayout;

namespace pvf {

namespace model {
struct Content;
struct EasingSpec;
struct Entity;
struct SolverSegment;
} // namespace model

namespace next {
class SpecialContentEditor;
class SolverPreviewWidget;
} // namespace next

class CollapsibleSection;
class EditorSession;

// Retired core::Project DTOs referenced by the legacy shell declarations below.
struct EasingSpec;
struct Entity;

class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
#if PVFORGE_LEGACY_SHELL
    // Legacy Project mode, kept for the old shell and its self-tests.
    explicit PropertiesPanel(Project* project, QWidget* parent = nullptr);
#endif
    // Session mode: reads model::Entity and writes through EditActions.
    explicit PropertiesPanel(EditorSession* session, QWidget* parent = nullptr);

    void setEntity(EntityId id);
    void setFrame(int frame);
    void refresh();

    // Session mode selection.  The panel keeps the Uuid, not a legacy id.
    void setSelectedUuid(const Uuid& uuid);
    Uuid selectedUuid() const { return m_sessionUuid; }
    bool sessionMode() const { return m_sessionMode; }
    void setAutoKey(bool on);
    bool autoKey() const { return m_autoKey; }
    Uuid selectedSolverId() const { return m_selectedSolverId; }

    // Playback freeze for the mini solver preview.  The panel connects this
    // to EditorSession::playback(); MainWindow may also forward it.
    void setPlaybackActive(bool active);
    bool playbackActiveForTest() const { return m_playbackActive; }

signals:
    void valueEdited(EntityId id, const QString& prop, double value);
    void pivotEdited(EntityId id, double ax, double ay);
    void parentEdited(EntityId id, EntityId parent);
    void keyframeEasingEdited(EntityId id, int frame, const EasingSpec& spec);
    // Data fields that are not part of the animated tracks; written as one
    // undo step by the main window.
    void dataEdited(EntityId id, const QVariantMap& data);
    void keyframeToggled(EntityId id, const QString& prop, double frame, bool remove);
    void replaceSourceRequested(EntityId id);

    // Session mode reports failures to the shell status bar and mirrors the
    // auto-key toggle with MainWindow's action.
    void sessionEditFailed(const QString& message);
    void sessionReplaceSourceRequested(const Uuid& uuid);
    void autoKeyToggled(bool on);
    // A solver card was activated; the viewport keeps that trajectory visible.
    void solverSelected(const Uuid& entityUuid, const Uuid& solverId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row
    {
        QString prop;
        QString baseLabel;
        QLabel* label = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QToolButton* key = nullptr;
        double factor = 1.0;   // display value * factor = stored value
    };

    struct HtmlRow
    {
        QString prop;
        QLabel* label = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QToolButton* key = nullptr;
    };

    // One manifest variable row in session mode. Numeric variables get a spin
    // box and a diamond backed by `var.<name>`; boolean/string variables are
    // read-only and show their manifest default.
    struct SessionHtmlRow
    {
        QString name;
        QString track;
        bool numeric = false;
        double defaultValue = 0.0;
        double minimum = -1000000000.0;
        double maximum = 1000000000.0;
        double step = 0.01;
        int decimals = 3;
        QString readOnlyText;
        QLabel* nameLabel = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QToolButton* key = nullptr;
        QLabel* readOnlyValue = nullptr;
    };

    struct TrackRow
    {
        QString track;
        QString label;
        bool step = false;
        double minimum = -1000000000.0;
        double maximum = 1000000000.0;
        double singleStep = 1.0;
        int decimals = 2;
        QString suffix;
        QLabel* name = nullptr;
        QToolButton* diamond = nullptr;
        QDoubleSpinBox* value = nullptr;
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

    void addRow(QFormLayout* form, const QString& label, const QString& prop, double lo,
                double hi, int decimals, double step, double factor = 1.0);
    struct DataRow
    {
        QString key;
        QDoubleSpinBox* spin = nullptr;
        QSpinBox* count = nullptr;
        QPushButton* color = nullptr;
    };

    void buildTextSection(QVBoxLayout* layout);
    void buildComicSection(QVBoxLayout* layout);
    void buildLinesSection(QVBoxLayout* layout);
    void buildBrandWallSection(QVBoxLayout* layout);
    void buildHalftoneSection(QVBoxLayout* layout);
    void buildHtmlSection(QVBoxLayout* layout);
    void buildSpecialSection(QVBoxLayout* layout);
    void buildSessionHtmlSection(QVBoxLayout* layout);
    void rebuildHtmlRows(const html::HtmlAsset& asset);
    void addHtmlVariableRow(QFormLayout* form, const html::HtmlComponent& component,
                            const html::HtmlVariable& variable);
    void addDataRow(QFormLayout* form, const QString& label, const QString& key, double lo,
                    double hi, int decimals, double step);
    void addDataCount(QFormLayout* form, const QString& label, const QString& key, int lo,
                      int hi);
    void addDataColor(QFormLayout* form, const QString& label, const QString& key,
                      const QColor& initial);
    void emitDataEdit(const QString& key, const QVariant& value);
    void buildVectorSection(QVBoxLayout* layout);
    void buildMediaSection(QVBoxLayout* layout);
    void buildAudioSection(QVBoxLayout* layout);
    void buildPivotSection(QVBoxLayout* layout);
    void applyData(const QString& key, const QVariant& value);
    void applyAudioTrimStart(int playheadFrame);
    void applyAudioTrimEnd(int playheadFrame);
    void setColorButton(QPushButton* button, const QColor& color);
    QColor pickColor(const QColor& initial, const QString& title);

    // ---- Session mode ----------------------------------------------------
    void refreshSession();
    // Playhead-only update: frame-dependent spins/labels/keys plus the mini
    // preview's current dot.  Never rebuilds solver cards or re-evaluates the
    // solver preview.
    void refreshPlayheadOnly();
    void reloadSessionHtmlValues(const model::Entity& entity);
    const model::Entity* sessionEntity() const;
    bool sessionEditable() const;
    int sessionRelativeFrame(const model::Entity& entity) const;
    bool runSessionEdit(const QString& message,
                        const std::function<bool(model::Entity&)>& mutate,
                        bool ignoreLock = false);
    bool runSessionContentEdit(const QString& message,
                               const std::function<bool(model::Content&)>& mutate);
    void commitSpecialContentEdit();
    void reloadSessionHtml(const model::Entity& entity);
    void rebuildSessionHtmlRows(const QJsonObject& variables);
    void editSessionHtmlVariable(const QString& track, double value);
    void toggleSessionHtmlKey(const QString& track, double fallback);
    void sessionSetTrackValue(const QString& track, double value);
    void sessionSetKeyValue(const QString& track, double value);
    void sessionToggleKey(const QString& track);
    void sessionAddTransformKeys();
    void sessionSetGroupEasing(const model::EasingSpec& easing);
    void sessionPromptGroupBezier();
    void applySessionData(const QString& key, const QVariant& value);
    void applySessionParent(const Uuid& parent);
    void applySessionAsset(const QString& field, const QVariant& value);
    void rebuildTrackRows(const model::Entity& entity);
    void reloadTrackRows(const model::Entity& entity);
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
    void showSolverConvertMenu();
    void convertSolverFromKey(SolverEffect effect);
    void createSolverFromPlayhead();
    void selectSolver(const Uuid& solverId);
    void syncSolverFocus(const QVector<model::SolverSegment>& solvers);
    void updateSolver(const Uuid& solverId, const QString& message,
                      const std::function<bool(model::SolverSegment&)>& mutate);
    void deleteSolver(const Uuid& solverId);
    void reportSessionError(const QString& message);

#if PVFORGE_LEGACY_SHELL
    Project* m_project = nullptr;
#endif
    EditorSession* m_session = nullptr;
    bool m_sessionMode = false;
    Uuid m_sessionUuid;
    bool m_autoKey = true;
    bool m_editing = false;
    bool m_playbackActive = false;
    EntityId m_entity = kInvalidEntity;
    int m_frame = 0;
    QLabel* m_title = nullptr;
    QPushButton* m_keyButton = nullptr;
    QPushButton* m_easeButton = nullptr;
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
    CollapsibleSection* m_comicSection = nullptr;
    CollapsibleSection* m_linesSection = nullptr;
    CollapsibleSection* m_wallSection = nullptr;
    CollapsibleSection* m_halftoneSection = nullptr;
    CollapsibleSection* m_htmlSection = nullptr;
    QLabel* m_htmlInfo = nullptr;
    QLabel* m_htmlWarning = nullptr;
    QWidget* m_htmlVariables = nullptr;
    QVBoxLayout* m_htmlVariablesLayout = nullptr;
    QPushButton* m_htmlReplace = nullptr;
    QVector<HtmlRow> m_htmlRows;
    QString m_htmlAssetPath;
    EntityId m_htmlRowsEntity = kInvalidEntity;
    QString m_htmlRowsStamp;
    html::HtmlAsset m_htmlAssetCache;
    QString m_htmlAssetCachePath;
    QString m_htmlAssetCacheError;
    QString m_htmlAssetCacheStamp;
    bool m_htmlAssetCacheValid = false;

    // Session-mode special content and HTML manifest pages.
    CollapsibleSection* m_specialSection = nullptr;
    next::SpecialContentEditor* m_specialEditor = nullptr;
    Uuid m_specialUuid;
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

    QLabel* m_wallInfo = nullptr;
    QVector<DataRow> m_dataRows;
    QHash<QString, QColor> m_dataColors;
    QComboBox* m_comicShape = nullptr;
    QCheckBox* m_comicTail = nullptr;
    QStackedWidget* m_linesStack = nullptr;
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

    // Keyframe track rows (session only).  Easing is one control for the
    // merged keyframe group at the current frame, matching the legacy UI.
    QCheckBox* m_autoKeyCheck = nullptr;
    QComboBox* m_groupEasing = nullptr;
    QWidget* m_trackHost = nullptr;
    QFormLayout* m_trackForm = nullptr;
    QVector<TrackRow> m_trackRows;
    Uuid m_trackRowsEntity;
    ContentKind m_trackRowsKind = ContentKind::Empty;

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
    QPushButton* m_convertSolverButton = nullptr;
    QLabel* m_convertSolverHint = nullptr;
    QVector<SolverCard> m_solverCards;
    Uuid m_selectedSolverId;
    // Mini trajectory window: points + path + initial-velocity arrow only.
    next::SolverPreviewWidget* m_solverPreview = nullptr;
};

} // namespace pvf
