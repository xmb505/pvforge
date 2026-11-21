#pragma once

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
class QToolButton;

namespace pvf {

class EditorSession;

// Creation panel modelled after the Mine-imator workbench.
//
// The panel always carries a slim 22 px header (title + close) and is a QFrame
// child of the viewport.  It follows the stage: closing only hides it and it
// never becomes a separate native window.
class WorkbenchPanel : public QFrame
{
    Q_OBJECT

public:
    // Session mode creation panel: media, text, vector and camera.  Every
    // creation goes through EditorSession so it is one undo step.
    explicit WorkbenchPanel(EditorSession* session, QWidget* parent = nullptr);

    // Show at an explicit viewport rectangle and install the click-away
    // filter.  The panel is always a child of the stage.
    void showDocked(const QRect& rect);
    void hidePanel();
    void setIgnoreMouseWidget(QWidget* widget) { m_ignoreWidget = widget; }
    // Session-mode refresh after an external (MCP) project change: re-reads
    // the wall summary and media choices from the document/assets.
    void refreshSession();
#ifdef PVFORGE_TEST_HOOKS
    // Session-mode test hook: selects a stored asset in the media page.
    void selectSessionMediaForTest(const QString& hash) { selectSessionMedia(hash); }
#endif

signals:
    void created(EntityId id);
    void sessionCreated(const Uuid& uuid);
    // User intent from the built-in header.
    void hideRequested();
    void visibilityChanged(bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildHeader();
    void buildUi();
    QWidget* buildMediaPage();
    QWidget* buildTextPage();
    QWidget* buildVectorPage();
    QWidget* buildCameraPage();

    void createCurrent();
    void createSessionCurrent();
    bool createSessionMedia();
    bool createSessionText();
    bool createSessionVector();
    bool createSessionCamera();
    bool createSessionEntity(model::Entity entity, const QString& message);
    void selectSessionMedia(const QString& hash);
    TextStyle currentTextStyle() const;
    void pickMedia();
    void refreshMediaLibrary();
    void updateVectorFields();
    void updateCreateState();
    void setColorButton(QPushButton* button, const QColor& color);
    QColor pickColor(const QColor& initial, const QString& title);

    EditorSession* m_session = nullptr;
    QWidget* m_ignoreWidget = nullptr;
    QWidget* m_header = nullptr;
    QLabel* m_title = nullptr;
    QToolButton* m_closeButton = nullptr;
    QListWidget* m_types = nullptr;
    QStackedWidget* m_pages = nullptr;
    QPushButton* m_create = nullptr;

    // Selected media.
    QString m_mediaPath;
    QString m_mediaHash;
    QLabel* m_mediaPreview = nullptr;
    QLabel* m_mediaInfo = nullptr;
    QListWidget* m_mediaLibrary = nullptr;

    QLineEdit* m_textContent = nullptr;
    QFontComboBox* m_textFont = nullptr;
    QSpinBox* m_textSize = nullptr;
    QPushButton* m_textColor = nullptr;
    QComboBox* m_textAlign = nullptr;
    QColor m_textColorValue{Qt::white};
    TextStyle m_textStyle;

    QLineEdit* m_cameraName = nullptr;

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
