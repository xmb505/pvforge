#pragma once

#include "core/BrandWall.h"

#include <QDialog>
#include <QVector>

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace pvf {

// Live preview of a brand wall, used inside the wall editor.
class BrandWallPreview : public QWidget
{
    Q_OBJECT

public:
    explicit BrandWallPreview(QWidget* parent = nullptr);
    void setWall(const BrandWallParams& params, const QVector<QImage>& logos);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    BrandWallParams m_params;
    QVector<QImage> m_logos;
};

// Edits a brand wall: any number of logo slots, each with its own tile
// colour, plus tile size, gap, opacity and wall size.
class BrandWallDialog : public QDialog
{
    Q_OBJECT

public:
    BrandWallDialog(const BrandWallParams& params, const QVector<QImage>& images,
                    QWidget* parent = nullptr);

    BrandWallParams params() const { return m_params; }
    QVector<QImage> images() const { return m_images; }

private:
    struct Slot
    {
        QWidget* row = nullptr;
        QPushButton* colorButton = nullptr;
        QPushButton* logoButton = nullptr;
        QString path;
        QColor color;
        QImage image;
    };

    void addSlot(const QString& path, const QColor& color, const QImage& image);
    void removeSlot(QWidget* row);
    void rebuild();
    void updateSlotButton(Slot& slot);

    QVector<Slot> m_slots;
    QVBoxLayout* m_slotLayout = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QSpinBox* m_columns = nullptr;
    QSpinBox* m_rows = nullptr;
    QDoubleSpinBox* m_gap = nullptr;
    QDoubleSpinBox* m_opacity = nullptr;
    BrandWallPreview* m_preview = nullptr;
    BrandWallParams m_params;
    QVector<QImage> m_images;
};

} // namespace pvf
