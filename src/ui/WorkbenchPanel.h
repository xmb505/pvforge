#pragma once

#include "core/Project.h"

#include <QColor>
#include <QFrame>
#include <QImage>

class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace pvf {

// Floating creation panel modelled after the Mine-imator workbench.
class WorkbenchPanel : public QFrame
{
    Q_OBJECT

public:
    explicit WorkbenchPanel(Project* project, QWidget* parent = nullptr);

    void showAt(const QPoint& pos);
    void hidePanel();
    void setIgnoreMouseWidget(QWidget* widget) { m_ignoreWidget = widget; }

signals:
    void created(EntityId id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* buildMediaPage();
    QWidget* buildTextPage();
    QWidget* buildVectorPage();

    void createCurrent();
    void pickMedia();
    void refreshMediaLibrary();
    void updateVectorFields();
    void selectMedia(int templateIndex);
    void updateCreateState();
    void setColorButton(QPushButton* button, const QColor& color);
    QColor pickColor(const QColor& initial, const QString& title);

    Project* m_project = nullptr;
    QWidget* m_ignoreWidget = nullptr;
    QListWidget* m_types = nullptr;
    QStackedWidget* m_pages = nullptr;
    QPushButton* m_create = nullptr;

    // Selected media (image or video).
    EntityType m_mediaType = EntityType::Image;
    QString m_mediaPath;
    QImage m_mediaFrame;
    double m_mediaDuration = 0.0;
    QLabel* m_mediaPreview = nullptr;
    QLabel* m_mediaInfo = nullptr;
    QListWidget* m_mediaLibrary = nullptr;

    QLineEdit* m_textContent = nullptr;
    QFontComboBox* m_textFont = nullptr;
    QSpinBox* m_textSize = nullptr;
    QPushButton* m_textColor = nullptr;
    QComboBox* m_textAlign = nullptr;
    QColor m_textColorValue{Qt::white};

    QListWidget* m_vectorShapes = nullptr;
    QDoubleSpinBox* m_vectorCorner = nullptr;
    QSpinBox* m_vectorSides = nullptr;
    QDoubleSpinBox* m_vectorInner = nullptr;
    QDoubleSpinBox* m_vectorWidth = nullptr;
    QDoubleSpinBox* m_vectorHeight = nullptr;
    QPushButton* m_vectorFill = nullptr;
    QPushButton* m_vectorStroke = nullptr;
    QDoubleSpinBox* m_vectorStrokeWidth = nullptr;
    QColor m_fillValue{90, 170, 255};
    QColor m_strokeValue{Qt::white};
};

} // namespace pvf
