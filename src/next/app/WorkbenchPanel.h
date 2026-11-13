#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace pvf::next {

class WorkbenchServices;

// Non-modal debug page for the workbench frame cache.  Purely a view over
// WorkbenchServices; the window rebuilds the service on project/camera/settings
// changes and calls setServices() again.
class WorkbenchPanel : public QWidget
{
    Q_OBJECT

public:
    explicit WorkbenchPanel(QWidget* parent = nullptr);

    void setServices(WorkbenchServices* services);
    WorkbenchServices* servicesForTest() const { return m_services; }

    // Test accessors: the same strings the panel shows.
    QString statusTextForTest() const;
    QString statsTextForTest() const;
    QPushButton* rescanButtonForTest() const { return m_rescan; }
    QPushButton* clearButtonForTest() const { return m_clear; }
    void refreshForTest() { refresh(); }

public slots:
    void refresh();

signals:
    // Emitted after the clear button emptied the service cache, so the window
    // can refresh the viewport and the status bar.  `ok` is false while files
    // are still on disk.
    void cacheCleared(bool ok);

private:
    WorkbenchServices* m_services = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_stats = nullptr;
    QPushButton* m_rescan = nullptr;
    QPushButton* m_clear = nullptr;
    QTimer* m_timer = nullptr;
};

} // namespace pvf::next
