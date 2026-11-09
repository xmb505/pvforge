#pragma once

#include "next/core/Document.h"

#include <QColor>
#include <QHash>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

namespace pvf::next {

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

public slots:
    void refresh();

private:
    struct SolverCard
    {
        Uuid id;
        SolverEffect effect = SolverEffect::Gravity;
        QGroupBox* box = nullptr;
        QSpinBox* start = nullptr;
        QSpinBox* end = nullptr;
        QDoubleSpinBox* velocityX = nullptr;
        QDoubleSpinBox* velocityY = nullptr;
        QHash<QString, QWidget*> params;
    };

    void buildUi();
    void buildBasicSection();
    void buildTransformSection();
    void buildContentSection();
    void buildSolverSection();

    void reloadBasic(const model::Entity& entity);
    void reloadTransform(const model::Entity& entity);
    void reloadContent(const model::Entity& entity);
    void updateSolverCards(const model::Entity& entity);
    void rebuildSolverCards(const QVector<model::SolverSegment>& solvers);
    void fillSolverCard(SolverCard& card, const model::SolverSegment& solver);

    void applyEditingState();
    void applySolverEnabledState();

    model::Entity* selectedEntity() const;
    bool editable() const;

    void commitEdit(const QString& what,
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

    // Solvers
    QLabel* m_solverEmpty = nullptr;
    QWidget* m_solverHost = nullptr;
    QVBoxLayout* m_solverCardsLayout = nullptr;
    QVector<SolverCard> m_solverCards;

    QVector<QWidget*> m_editable;
};

} // namespace pvf::next
