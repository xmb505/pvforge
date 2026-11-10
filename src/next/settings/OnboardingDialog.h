#pragma once

#include "next/settings/SettingsStore.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;

namespace pvf::next {

// First-run welcome dialog.  It only reports the user's choice; the shell
// writes settings.gitEnabled and settings.onboardingDone after accept().
class OnboardingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OnboardingDialog(const AppSettings& settings, QWidget* parent = nullptr);

    bool gitEnabled() const;

private:
    void refreshGitStatus();

    QCheckBox* m_gitEnabled = nullptr;
    QLabel* m_gitStatus = nullptr;
    QPushButton* m_gitDetect = nullptr;
};

} // namespace pvf::next
