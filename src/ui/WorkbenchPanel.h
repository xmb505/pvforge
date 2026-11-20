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
class QShortcut;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace pvf {

class EditorSession;

// Floating creation panel modelled after the Mine-imator workbench.
//
// The panel always carries a slim 22 px header (title + float/dock + close).
// Docked it is a QFrame child of the viewport; floating it is the same widget
// reparented to the main window as a native Qt::Window.  The host owns the
// parent/geometry transitions; the panel only swaps its chrome and reports
// user intent through signals.
class WorkbenchPanel : public QFrame
{
    Q_OBJECT

public:
    // Session mode creation panel: media, text, vector and camera.  Every
    // creation goes through EditorSession so it is one undo step.
    explicit WorkbenchPanel(EditorSession* session, QWidget* parent = nullptr);

    // Show docked at an explicit viewport rectangle.  Installs the click-away
    // filter while docked and makes sure no stale app filter survives a float.
    void showDocked(const QRect& rect);
    void hidePanel();
    void setIgnoreMouseWidget(QWidget* widget) { m_ignoreWidget = widget; }
    // Floating chrome: drop the drop shadow / QFrame border and swap the
    // header button to "dock"; docked restores the old look.
    void setFloatingChrome(bool floating);
    bool isFloating() const { return m_floating; }
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
    void dockRequested();
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
    QShortcut* m_escapeShortcut = nullptr;
    bool m_floating = false;
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
