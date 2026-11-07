#pragma once

#include "core/Project.h"

#include <QWidget>

class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;

namespace pvf {

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

private:
    struct Row
    {
        QString prop;
        QDoubleSpinBox* spin = nullptr;
        double factor = 1.0;   // display value * factor = stored value
    };

    void addRow(QFormLayout* form, const QString& label, const QString& prop,
                double lo, double hi, int decimals, double step, double factor = 1.0);

    Project* m_project = nullptr;
    EntityId m_entity = kInvalidEntity;
    int m_frame = 0;
    QLabel* m_title = nullptr;
    QPushButton* m_keyButton = nullptr;
    QVector<Row> m_rows;
};

} // namespace pvf
