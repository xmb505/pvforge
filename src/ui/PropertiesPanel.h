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

private:
    struct Row
    {
        QString prop;
        QDoubleSpinBox* spin = nullptr;
        double factor = 1.0;   // display value * factor = stored value
    };

    void addRow(QFormLayout* form, const QString& label, const QString& prop, double lo,
                double hi, int decimals, double step, double factor = 1.0);
    void buildTextSection(QVBoxLayout* layout);
    void buildVectorSection(QVBoxLayout* layout);
    void buildMediaSection(QVBoxLayout* layout);
    void buildPivotSection(QVBoxLayout* layout);
    void applyData(const QString& key, const QVariant& value);
    void setColorButton(QPushButton* button, const QColor& color);
    QColor pickColor(const QColor& initial, const QString& title);

    Project* m_project = nullptr;
    EntityId m_entity = kInvalidEntity;
    int m_frame = 0;
    QLabel* m_title = nullptr;
    QPushButton* m_keyButton = nullptr;
    QVector<Row> m_rows;

    CollapsibleSection* m_transformSection = nullptr;
    CollapsibleSection* m_keySection = nullptr;
    CollapsibleSection* m_textSection = nullptr;
    CollapsibleSection* m_vectorSection = nullptr;
    CollapsibleSection* m_mediaSection = nullptr;
    CollapsibleSection* m_pivotSection = nullptr;
    QCheckBox* m_usePivot = nullptr;
    QDoubleSpinBox* m_pivotX = nullptr;
    QDoubleSpinBox* m_pivotY = nullptr;

    QLineEdit* m_textContent = nullptr;
    QFontComboBox* m_textFont = nullptr;
    QSpinBox* m_textSize = nullptr;
    QPushButton* m_textColorButton = nullptr;
    QComboBox* m_textAlign = nullptr;
    QColor m_textColorValue{Qt::white};

    QComboBox* m_vectorShape = nullptr;
    QDoubleSpinBox* m_vectorWidth = nullptr;
    QDoubleSpinBox* m_vectorHeight = nullptr;
    QPushButton* m_fillButton = nullptr;
    QPushButton* m_strokeButton = nullptr;
    QDoubleSpinBox* m_strokeWidth = nullptr;
    QColor m_fillValue{90, 170, 255};
    QColor m_strokeValue{Qt::white};

    QLabel* m_mediaInfo = nullptr;
};

} // namespace pvf
