#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;

namespace pvf {

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Used by Help -> About so both entry points show the same page.
    void showAboutPage();

protected:
    void accept() override;

private:
    QWidget* createGeneralPage();
    QWidget* createEditorPage();
    QWidget* createProjectPage();
    QWidget* createTimelinePage();
    QWidget* createAssetsPage();
    QWidget* createDecodePage();
    QWidget* createAboutPage();
    int addPage(const QString& title, QWidget* page, const QStringList& iconNames);

    QListWidget* m_categories = nullptr;
    QStackedWidget* m_pages = nullptr;
    int m_aboutIndex = 0;

    QComboBox* m_language = nullptr;
    QSpinBox* m_snapPixels = nullptr;
    QCheckBox* m_edgeCenterSnap = nullptr;
    QCheckBox* m_edgeMidpointSnap = nullptr;
    QCheckBox* m_angle0 = nullptr;
    QCheckBox* m_angle90 = nullptr;
    QCheckBox* m_angle180 = nullptr;
    QCheckBox* m_angle270 = nullptr;
    QCheckBox* m_autoKey = nullptr;
    QCheckBox* m_audioScrub = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_fps = nullptr;
    QDoubleSpinBox* m_timelineZoom = nullptr;
    QCheckBox* m_packAssets = nullptr;
    QPlainTextEdit* m_aboutText = nullptr;
};

} // namespace pvf
