#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QImage>

class QLabel;
class QProgressBar;
class QPushButton;
class QCloseEvent;
class QResizeEvent;

namespace pvf {

// Modal window shown while VideoExporter runs: live frame preview, progress
// and a cancel button. Closing the window is treated as a cancel request.
class ExportProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportProgressDialog(QWidget* parent = nullptr);

    void setPreview(const QImage& image);
    void setProgress(int done, int total);
    // export2 reports the ETA directly; prefer this overload during export.
    void setProgress(int done, int total, double etaSeconds);
    void finish();

signals:
    void cancelRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private:
    void updatePreview();
    void requestCancel();

    QLabel* m_preview = nullptr;
    QProgressBar* m_bar = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_cancel = nullptr;
    QImage m_image;
    QElapsedTimer m_clock;
    qint64 m_lastSampleMs = -1;
    int m_lastDone = 0;
    double m_rate = 0.0;
    bool m_finished = false;
    bool m_cancelling = false;
};

} // namespace pvf
