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

    QComboBox* cameraComboBoxForTest() const { return m_cameraCombo; }

private:
    void updateCameraInfo();
    void chooseOutputPath();
    void updateStartEnabled();

    model::Project m_project;
    int m_startFrame = 0;
    int m_endFrame = 0;

    QComboBox* m_cameraCombo = nullptr;
    QLabel* m_rangeLabel = nullptr;
    QLabel* m_resolutionLabel = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QCheckBox* m_audioCheck = nullptr;
    QPushButton* m_startButton = nullptr;
};

} // namespace pvf::next
