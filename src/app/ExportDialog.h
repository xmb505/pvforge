#pragma once

#include "common/Types.h"
#include "model/Project.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace pvf {

// Maps an export2::Result error code (output_exists, encoder_unavailable,
// invalid_crf, ...) to a message that can be shown in the UI.
QString friendlyExportError(const QString& error);

// Collects the MP4 export settings for one camera.  The layout follows the
// legacy dialog; the size/quality controls now map onto export2::Options.
class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const model::Project& project,
                          const Uuid& preferredCamera = Uuid(),
                          const QString& projectRoot = QString(),
                          QWidget* parent = nullptr);

    Uuid cameraUuid() const;
    int startFrame() const { return m_startFrame; }
    int endFrame() const { return m_endFrame; }
    QString outputPath() const;
    bool includeAudio() const;
    int outputFps() const;
    int resolutionScale() const;
    int crf() const;
    QString encoder() const;
    QString preset() const;
    // True only when the user confirmed replacing an existing file.
    bool overwriteExisting() const { return m_overwriteExisting; }

    // Test hooks: the modal prompts can be answered without a dialog.
    void setConfirmLargeExportForTest(bool skip) { m_skipLargeWarning = skip; }
    void setLargeExportDecisionForTest(bool confirm)
    {
        m_largeOverride = confirm;
        m_hasLargeOverride = true;
    }
    void setOverwriteDecisionForTest(bool confirm)
    {
        m_overwriteOverride = confirm;
        m_hasOverwriteOverride = true;
    }
    QComboBox* cameraComboBoxForTest() const { return m_camera; }
    QComboBox* resolutionComboBoxForTest() const { return m_resolution; }
    QComboBox* fpsComboBoxForTest() const { return m_fps; }
    QSpinBox* crfSpinBoxForTest() const { return m_crf; }
    QComboBox* encoderComboBoxForTest() const { return m_encoder; }
    QComboBox* presetComboBoxForTest() const { return m_preset; }
    QPushButton* startButtonForTest() const { return m_start; }

public slots:
    void accept() override;

private:
    void browse();
    void updateCameraInfo();
    void updateSummary();
    void updateStartEnabled();
    bool confirmOverwrite();
    bool confirmLargeExport();
    QString defaultOutputPath() const;

    model::Project m_project;
    QString m_projectRoot;
    int m_startFrame = 0;
    int m_endFrame = 0;
    bool m_skipLargeWarning = false;
    bool m_hasLargeOverride = false;
    bool m_largeOverride = false;
    bool m_overwriteExisting = false;
    bool m_hasOverwriteOverride = false;
    bool m_overwriteOverride = false;

    QLineEdit* m_path = nullptr;
    QComboBox* m_camera = nullptr;
    QLabel* m_range = nullptr;
    QComboBox* m_resolution = nullptr;
    QComboBox* m_fps = nullptr;
    QSpinBox* m_crf = nullptr;
    QComboBox* m_encoder = nullptr;
    QComboBox* m_preset = nullptr;
    QCheckBox* m_audio = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_start = nullptr;
};

} // namespace pvf
