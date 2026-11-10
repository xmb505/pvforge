#pragma once

#include "common/Types.h"
#include "model/Project.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace pvf::next {

// Modal options dialog for one MP4 export.  It owns a snapshot of the project
// so the camera range shown while the dialog is open cannot change underneath
// the user.
class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const model::Project& project,
                          const Uuid& preferredCamera,
                          QWidget* parent = nullptr);

    Uuid cameraUuid() const;
    int startFrame() const { return m_startFrame; }
    int endFrame() const { return m_endFrame; }
    QString outputPath() const;
    bool includeAudio() const;
    // True only when the user confirmed replacing an existing output file.
    // Defaults to false for a fresh dialog.
    bool overwriteExisting() const { return m_overwriteExisting; }

    // Absolute output frame rate (project fps * 1/2/4) and the integer
    // resolution multiplier (1/2/4).  Both default to 1x.
    int outputFps() const;
    int resolutionScale() const;

    QComboBox* cameraComboBoxForTest() const { return m_cameraCombo; }
    QComboBox* resolutionComboBoxForTest() const { return m_resolutionCombo; }
    QComboBox* fpsComboBoxForTest() const { return m_fpsCombo; }
    // Tests can skip the modal 4x warning.
    void setConfirmLargeExportForTest(bool skip) { m_skipLargeWarning = skip; }
    // Tests can answer the overwrite prompt instead of showing the modal.
    void setOverwriteDecisionForTest(bool confirm)
    {
        m_overwriteOverride = confirm;
        m_hasOverwriteOverride = true;
    }

private:
    void updateCameraInfo();
    void chooseOutputPath();
    void updateStartEnabled();
    bool confirmLargeExport();
    bool confirmOverwrite();

    model::Project m_project;
    int m_startFrame = 0;
    int m_endFrame = 0;
    bool m_skipLargeWarning = false;
    bool m_overwriteExisting = false;
    bool m_hasOverwriteOverride = false;
    bool m_overwriteOverride = false;

    QComboBox* m_cameraCombo = nullptr;
    QLabel* m_rangeLabel = nullptr;
    QComboBox* m_resolutionCombo = nullptr;
    QComboBox* m_fpsCombo = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QCheckBox* m_audioCheck = nullptr;
    QPushButton* m_startButton = nullptr;
};

} // namespace pvf::next
