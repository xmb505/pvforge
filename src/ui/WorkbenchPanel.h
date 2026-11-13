#pragma once

#include "core/BrandWall.h"
#include "core/EntityId.h"
#include "core/TextStyle.h"
#include "model/Content.h"
#include "model/Entity.h"

#include <QColor>
#include <QFrame>

class QCheckBox;
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

class EditorSession;

// Floating creation panel modelled after the Mine-imator workbench.
class WorkbenchPanel : public QFrame
{
    Q_OBJECT

public:
    // Session mode: the special page creates model::BrandWallContent through
    // EditorSession. Other legacy creation pages are not shown yet.
    explicit WorkbenchPanel(EditorSession* session, QWidget* parent = nullptr);

    void showAt(const QPoint& pos);
    void hidePanel();
    void setIgnoreMouseWidget(QWidget* widget) { m_ignoreWidget = widget; }
    // Session-mode refresh after an external (MCP) project change: re-reads
    // the wall summary and media choices from the document/assets.
    void refreshSession();
    // Session-mode test hook: selects a stored asset in the media page.
    void selectSessionMediaForTest(const QString& hash) { selectSessionMedia(hash); }

signals:
    void created(EntityId id);
    void sessionCreated(const Uuid& uuid);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    QWidget* buildSessionSpecialPage();
    void createSessionBrandWall();
    void updateSessionWallSummary();
    QWidget* buildMediaPage();
    QWidget* buildTextPage();
    QWidget* buildTextBoxPage();
    QWidget* buildVectorPage();
    QWidget* buildCameraPage();
    QWidget* buildSpecialPage();

    void createCurrent();
    void createSessionCurrent();
    bool createSessionMedia();
    bool createSessionText();
    bool createSessionVector();
    bool createSessionCamera();
    bool createSessionEntity(model::Entity entity, const QString& message);
    void refreshSessionMediaLibrary();
    void selectSessionMedia(const QString& hash);
    EntityId createSpecial();
    EntityId createTextBox();
    void updateSpecialPreview();
    void updateTextBoxPreview();
    TextStyle currentTextStyle() const;
    void pickMedia();
    void refreshMediaLibrary();
    void updateVectorFields();
    void selectMedia(int templateIndex);
    void updateCreateState();
    void setColorButton(QPushButton* button, const QColor& color);
    QColor pickColor(const QColor& initial, const QString& title);

    EditorSession* m_session = nullptr;
    QWidget* m_ignoreWidget = nullptr;
    QListWidget* m_types = nullptr;
    QStackedWidget* m_pages = nullptr;
    QPushButton* m_create = nullptr;

    // Selected media template.
    int m_mediaTemplateIndex = -1;
    QString m_mediaPath;
    QString m_mediaHash;
    QLabel* m_mediaPreview = nullptr;
    QLabel* m_mediaInfo = nullptr;
    QListWidget* m_mediaLibrary = nullptr;

    QLineEdit* m_textContent = nullptr;
    QFontComboBox* m_textFont = nullptr;    QSpinBox* m_textSize = nullptr;
    QPushButton* m_textColor = nullptr;
    QComboBox* m_textAlign = nullptr;
    QColor m_textColorValue{Qt::white};
    TextStyle m_textStyle;

    QLineEdit* m_cameraName = nullptr;

    QListWidget* m_textBoxTypes = nullptr;
    QComboBox* m_textBoxShape = nullptr;
    QLabel* m_textBoxPreview = nullptr;

    QListWidget* m_specialTypes = nullptr;
    QStackedWidget* m_specialForms = nullptr;
    QLabel* m_specialPreview = nullptr;
    QLineEdit* m_comicContent = nullptr;
    QSpinBox* m_comicSize = nullptr;
    QPushButton* m_comicColor = nullptr;
    QColor m_comicColorValue{30, 30, 36};
    QComboBox* m_comicDirection = nullptr;
    QCheckBox* m_comicBold = nullptr;
    QCheckBox* m_comicItalic = nullptr;
    QCheckBox* m_comicUnderline = nullptr;
    QPushButton* m_comicFill = nullptr;
    QPushButton* m_comicStroke = nullptr;
    QDoubleSpinBox* m_comicStrokeWidth = nullptr;
    QColor m_comicFillValue{Qt::white};
    QColor m_comicStrokeValue{24, 22, 28};
    QDoubleSpinBox* m_radialFlicker = nullptr;
    TextStyle m_comicStyle;
    QSpinBox* m_radialCount = nullptr;
    QDoubleSpinBox* m_radialThickness = nullptr;
    QPushButton* m_radialColor = nullptr;
    QColor m_radialColorValue{Qt::white};
    QSpinBox* m_flowCount = nullptr;
    QDoubleSpinBox* m_flowSpeed = nullptr;
    QDoubleSpinBox* m_flowAngle = nullptr;
    BrandWallParams m_wallParams;
    QVector<QImage> m_wallImages;
    QLabel* m_wallSummary = nullptr;
    model::BrandWallContent m_wallContent;
    QLabel* m_sessionWallPreview = nullptr;
    void updateWallSummary();
    QString m_wallLogoAPath;
    QString m_wallLogoBPath;
    QImage m_wallImageA;
    QImage m_wallImageB;
    QPushButton* m_flowColor = nullptr;
    QColor m_flowColorValue{Qt::white};
    QDoubleSpinBox* m_halftoneArea = nullptr;
    QDoubleSpinBox* m_halftoneSpacing = nullptr;
    QDoubleSpinBox* m_halftoneSize = nullptr;
    QDoubleSpinBox* m_halftoneAngle = nullptr;
    QDoubleSpinBox* m_halftoneGradient = nullptr;
    QDoubleSpinBox* m_halftoneMin = nullptr;
    QDoubleSpinBox* m_halftoneJitter = nullptr;
    QPushButton* m_halftoneColor = nullptr;
    QColor m_halftoneColorValue{Qt::white};

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
