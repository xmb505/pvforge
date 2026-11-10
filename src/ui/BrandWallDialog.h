#pragma once

#include "core/BrandWall.h"
#include "model/Content.h"

#include <QDialog>
#include <QPixmap>
#include <QVector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace pvf {

// Renders a text-label brand wall through render2 so the dialog preview and
// the viewport show the same artwork.
QPixmap renderBrandWallPreview(const model::BrandWallContent& content,
                               const QSize& size);

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
    // Text-label wall for the new model: no image/SVG textures, every tile
    // shows one of the labels in `logos`.
    explicit BrandWallDialog(const model::BrandWallContent& content,
                             QWidget* parent = nullptr);

    BrandWallParams params() const { return m_params; }
    QVector<QImage> images() const { return m_images; }
    model::BrandWallContent content() const;

private:
    void buildContentUi();
    void refreshContentPreview();
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

    // Text-label mode (model::BrandWallContent).
    model::BrandWallContent m_content;
    QPlainTextEdit* m_contentLogos = nullptr;
    QSpinBox* m_contentColumns = nullptr;
    QSpinBox* m_contentRows = nullptr;
    QDoubleSpinBox* m_contentSpacing = nullptr;
    QDoubleSpinBox* m_contentLogoSize = nullptr;
    QLineEdit* m_contentColors = nullptr;
    QCheckBox* m_contentAlternate = nullptr;
    QDoubleSpinBox* m_contentAngle = nullptr;
    QLineEdit* m_contentBackground = nullptr;
    QSpinBox* m_contentSeed = nullptr;
    QLabel* m_contentSummary = nullptr;
    QLabel* m_contentPreview = nullptr;
};

} // namespace pvf
