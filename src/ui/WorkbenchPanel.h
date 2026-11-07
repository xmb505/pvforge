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

// Floating creation panel modelled after the Mine-imator workbench:
// type list on the left, per-type form on the right, Create at the bottom.
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
    QWidget* buildImagePage();
    QWidget* buildVideoPage();
    QWidget* buildTextPage();
    QWidget* buildVectorPage();

    void createCurrent();
    void pickImage();
    void pickVideo();
    void updateCreateState();
    void setColorButton(QPushButton* button, const QColor& color);
    QColor pickColor(const QColor& initial, const QString& title);

    Project* m_project = nullptr;
    QWidget* m_ignoreWidget = nullptr;
    QListWidget* m_types = nullptr;
    QStackedWidget* m_pages = nullptr;
    QPushButton* m_create = nullptr;

    QString m_imagePath;
    QLabel* m_imagePreview = nullptr;
    QLabel* m_imageInfo = nullptr;

    QString m_videoPath;
    QImage m_videoFrame;
    QLabel* m_videoPreview = nullptr;
    QLabel* m_videoInfo = nullptr;

    QLineEdit* m_textContent = nullptr;
    QFontComboBox* m_textFont = nullptr;
    QSpinBox* m_textSize = nullptr;
    QPushButton* m_textColor = nullptr;
    QComboBox* m_textAlign = nullptr;
    QColor m_textColorValue{Qt::white};

    QComboBox* m_vectorShape = nullptr;
    QDoubleSpinBox* m_vectorWidth = nullptr;
    QDoubleSpinBox* m_vectorHeight = nullptr;
    QPushButton* m_vectorFill = nullptr;
    QPushButton* m_vectorStroke = nullptr;
    QDoubleSpinBox* m_vectorStrokeWidth = nullptr;
    QColor m_fillValue{90, 170, 255};
    QColor m_strokeValue{Qt::white};
};

} // namespace pvf
