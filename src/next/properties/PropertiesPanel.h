#pragma once

#include "next/core/Document.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

namespace pvf::next {

class SolverPreviewWidget;

// Right side panel for one selected entity.  It only mutates the model through
// Document::mutableProject() and then calls commitEntity() once per finished
// edit (Enter, focus out, checkbox toggle, combo selection, color dialog).
// Widgets are reloaded with m_loading set, so a refresh never writes the
// previous entity's values into the newly selected one.
class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanel(Document* document, QWidget* parent = nullptr);

    // Auto-key is on by default: numeric transform/content edits also insert or
    // update a keyframe at the playhead's entity-relative frame.
    void setAutoKey(bool on);
    bool autoKey() const;

    // Writes easing to the keyframe at the current playhead frame on `track`
    // (entity-relative). Returns false when there is no key or the save failed.
    // The custom-bezier dialog calls this after collecting its four handles.
    bool setTrackEasing(const QString& track, const model::EasingSpec& easing);

    // Replaces the selected entity's asset with `hash` in one commit. Only
    // assets from the same compatibility group are accepted (image/video/html
    // or audio). Timed assets trim the entity end to the available material
    // length and drop keyframes past the new duration; still images and HTML
    // keep the current duration. sourceIn is clamped into the new asset.
    // Used by the "更换素材…" dialog and by tests.
    bool replaceAsset(const AssetHash& hash, QString* error = nullptr);

public slots:
    void refresh();

private:
    struct KeyframeRow
    {
        QString track;
        bool step = false;
        bool hasKey = false;
        QLabel* name = nullptr;
        QPushButton* diamond = nullptr;
        QDoubleSpinBox* value = nullptr;
        QComboBox* easing = nullptr;
    };

    // One row of the HTML manifest variables section. Numeric variables get a
    // double spin box and a keyframe diamond backed by the `var.<name>` track;
    // boolean/string variables are read-only in this first version.
    struct HtmlVariableRow
    {
        QString name;
        QString track;
        bool numeric = false;
        bool inRange = false;
        double defaultValue = 0.0;
        double minimum = -1000000000.0;
        double maximum = 1000000000.0;
        double step = 0.01;
        int decimals = 3;
        QString readOnlyText;
        QPushButton* diamond = nullptr;
        QDoubleSpinBox* value = nullptr;
        QLabel* readOnlyValue = nullptr;
    };

    struct SolverCard
    {
        Uuid id;
        SolverEffect effect = SolverEffect::Gravity;
        QGroupBox* box = nullptr;
        QSpinBox* start = nullptr;
        QSpinBox* end = nullptr;
        QDoubleSpinBox* velocityX = nullptr;
        QDoubleSpinBox* velocityY = nullptr;
        SolverPreviewWidget* preview = nullptr;
        QPushButton* remove = nullptr;
        QHash<QString, QWidget*> params;
    };

    void buildUi();
    void buildBasicSection();
    void buildTransformSection();
    void buildKeyframeSection();
    void buildContentSection();
    void buildHtmlVariablesSection();
    void buildSolverSection();

    void reloadBasic(const model::Entity& entity);
    void reloadTransform(const model::Entity& entity);
    void reloadKeyframes(const model::Entity& entity);
    void rebuildKeyframeRows(const model::Entity& entity);
    void reloadContent(const model::Entity& entity);
    void reloadHtmlVariables(const model::Entity& entity);
    void rebuildHtmlVariableRows();
    void updateSolverCards(const model::Entity& entity);
    void rebuildSolverCards(const Uuid& entityUuid,
                            const QVector<model::SolverSegment>& solvers);
    void fillSolverCard(SolverCard& card, const Uuid& entityUuid,
                        const model::SolverSegment& solver);
    void reloadSolverCreate(const model::Entity& entity);
    void applySolverCardSelection();
    SolverPreviewWidget* solverPreview(const Uuid& solverId) const;
    void commitSolverEdit(const Uuid& solverId, const QString& what,
                          const std::function<bool(model::Entity&)>& mutate);

    void createSolverFromPlayhead();
    void deleteSolverSegment(const Uuid& solverId);
    SolverEffect selectedSolverEffect() const;

    void chooseReplacementAsset();
    // Opens the modal TextLayoutDialog and writes the whole edited
    // model::TextContent back in one commit ("pvf: edit text layout").
    void openTextLayoutDialog();
    // Decoded 320 px thumbnail cached by content hash; assets are immutable so
    // the cache never needs invalidation. Returns a null pixmap for
    // non-image/missing content.
    QPixmap cachedAssetThumbnail(const AssetHash& hash,
                                 const assets::StoredAsset& asset);

    void toggleKeyframe(const QString& track,
                        std::optional<double> fallback = std::nullopt);
    void editKeyframeValue(const QString& track, double value);
    void applyAutoKey(model::Entity& entity, const QString& track, double value);
    void promptCustomEasing(const QString& track);

    void applyEditingState();
    void applyKeyframeEnabledState();
    void applySolverEnabledState();

    model::Entity* selectedEntity() const;
    bool editable() const;

    bool commitEdit(const QString& what,
                    const std::function<bool(model::Entity&)>& mutate,
                    bool ignoreLock = false);
    void bindTransform(QDoubleSpinBox* spin, const QString& field,
                       double model::Transform::*member);
    void bindCheckBox(QCheckBox* box, bool model::Entity::*member, const QString& field);
    void addEditable(QWidget* widget);

    void setStatus(const QString& text);
    void showSaveError(const QString& error);

    Document* m_document = nullptr;
    bool m_loading = false;
    bool m_committing = false;
    Uuid m_lastUuid;

    QLabel* m_title = nullptr;
    QLabel* m_placeholder = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QLabel* m_status = nullptr;

    // Basic
    QLineEdit* m_nameEdit = nullptr;
    QSpinBox* m_startSpin = nullptr;
    QSpinBox* m_endSpin = nullptr;
    QSpinBox* m_rowSpin = nullptr;
    QCheckBox* m_hiddenCheck = nullptr;
    QCheckBox* m_lockedCheck = nullptr;
    QCheckBox* m_visibleCheck = nullptr;
    QCheckBox* m_inheritCheck = nullptr;
    QCheckBox* m_physicsCheck = nullptr;

    // Transform
    QDoubleSpinBox* m_xSpin = nullptr;
    QDoubleSpinBox* m_ySpin = nullptr;
    QDoubleSpinBox* m_rotationSpin = nullptr;
    QDoubleSpinBox* m_scaleXSpin = nullptr;
    QDoubleSpinBox* m_scaleYSpin = nullptr;
    QDoubleSpinBox* m_pivotXSpin = nullptr;
    QDoubleSpinBox* m_pivotYSpin = nullptr;
    QDoubleSpinBox* m_opacitySpin = nullptr;

    // Keyframes
    QGroupBox* m_keyframeGroup = nullptr;
    QGridLayout* m_keyframeGrid = nullptr;
    QCheckBox* m_autoKeyCheck = nullptr;
    QVector<KeyframeRow> m_keyframeRows;
    Uuid m_keyframeUuid;
    bool m_autoKey = true;

    // Content
    QGroupBox* m_contentGroup = nullptr;
    QWidget* m_emptyPage = nullptr;
    QWidget* m_textPage = nullptr;
    QWidget* m_vectorPage = nullptr;
    QWidget* m_assetPage = nullptr;
    QWidget* m_cameraPage = nullptr;

    QLineEdit* m_textEdit = nullptr;
    QFontComboBox* m_textFont = nullptr;
    QSpinBox* m_textPixelSize = nullptr;
    QPushButton* m_textColorButton = nullptr;
    QComboBox* m_textAlign = nullptr;
    QDoubleSpinBox* m_textLineHeight = nullptr;
    QDoubleSpinBox* m_textLetterSpacing = nullptr;
    QDoubleSpinBox* m_textStrokeWidth = nullptr;
    QPushButton* m_textLayoutButton = nullptr;
    QColor m_textColorValue = Qt::white;

    QComboBox* m_vectorShape = nullptr;
    QDoubleSpinBox* m_vectorWidth = nullptr;
    QDoubleSpinBox* m_vectorHeight = nullptr;
    QDoubleSpinBox* m_vectorCorner = nullptr;
    QPushButton* m_vectorFillButton = nullptr;
    QPushButton* m_vectorStrokeButton = nullptr;
    QDoubleSpinBox* m_vectorStrokeWidth = nullptr;
    QColor m_vectorFillValue = Qt::white;
    QColor m_vectorStrokeValue = Qt::black;

    QLineEdit* m_assetHash = nullptr;
    QComboBox* m_assetType = nullptr;
    QSpinBox* m_assetSourceIn = nullptr;
    QDoubleSpinBox* m_assetVolume = nullptr;
    QPushButton* m_assetReplaceButton = nullptr;
    QLabel* m_assetPreview = nullptr;
    bool m_assetMissing = false;
    QHash<AssetHash, QPixmap> m_assetThumbnails;

    // HTML manifest variables (docs/ENTITY_MODEL.md 6.4)
    QGroupBox* m_htmlGroup = nullptr;
    QLabel* m_htmlStatus = nullptr;
    QLabel* m_htmlEmpty = nullptr;
    QGridLayout* m_htmlGrid = nullptr;
    QVector<HtmlVariableRow> m_htmlRows;
    Uuid m_htmlUuid;
    AssetHash m_htmlHash;
    QJsonObject m_htmlVariableDefs;
    bool m_htmlAssetPresent = false;
    bool m_htmlTimeDependent = true;

    // Solvers
    QLabel* m_solverEmpty = nullptr;
    QWidget* m_solverHost = nullptr;
    QVBoxLayout* m_solverCardsLayout = nullptr;
    QComboBox* m_solverEffectCombo = nullptr;
    QComboBox* m_solverTargetCombo = nullptr;
    QSpinBox* m_solverEndSpin = nullptr;
    QPushButton* m_solverCreateButton = nullptr;
    Uuid m_selectedSolverId;
    QVector<SolverCard> m_solverCards;

    QVector<QWidget*> m_editable;
};

} // namespace pvf::next
