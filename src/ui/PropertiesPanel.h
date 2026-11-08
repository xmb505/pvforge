#pragma once

#include "core/Project.h"

#include <QColor>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QStackedWidget;
class QVBoxLayout;

namespace pvf {

class CollapsibleSection;

class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanel(Project* project, QWidget* parent = nullptr);

    void setEntity(EntityId id);
    void setFrame(int frame);
    void refresh();

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

    Project* m_project = nullptr;
    EntityId m_entity = kInvalidEntity;
    int m_frame = 0;
    QLabel* m_title = nullptr;
    QPushButton* m_keyButton = nullptr;
    QPushButton* m_easeButton = nullptr;
    QWidget* m_parentRow = nullptr;
    QLabel* m_parentLabel = nullptr;
    QComboBox* m_parent = nullptr;
    QVector<Row> m_rows;

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
    QComboBox* m_textAlign = nullptr;
    QComboBox* m_textDirection = nullptr;
    QCheckBox* m_textBold = nullptr;
    QCheckBox* m_textItalic = nullptr;
    QCheckBox* m_textUnderline = nullptr;
    QColor m_textColorValue{Qt::white};

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
    QSpinBox* m_audioStart = nullptr;
    QDoubleSpinBox* m_audioTrimStart = nullptr;
    QDoubleSpinBox* m_audioTrimEnd = nullptr;
    QSpinBox* m_audioVolume = nullptr;
};

} // namespace pvf
