#pragma once

#include "render/VideoExporter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace pvf {

class Project;

// Collects the MP4 export settings. The dialog only reads the project; the
// actual rendering is driven by VideoExporter from MainWindow.
class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const Project& project, QWidget* parent = nullptr);

    ExportOptions options() const;

public slots:
    void accept() override;

private:
    void browse();
    void updateSummary();
    QString defaultOutputPath() const;
    int exportFrames() const;

    const Project& m_project;
    QLineEdit* m_path = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QComboBox* m_view = nullptr;
    QComboBox* m_quality = nullptr;
    QComboBox* m_encoder = nullptr;
    QCheckBox* m_audio = nullptr;
    QLabel* m_summary = nullptr;
};

} // namespace pvf
