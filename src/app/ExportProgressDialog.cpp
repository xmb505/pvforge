#include "ExportProgressDialog.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

QString formatRemaining(double seconds)
{
    const int total = int(std::ceil(std::max(0.0, seconds)));
    if (total >= 60)
    {
        return QStringLiteral("%1:%2")
            .arg(total / 60)
            .arg(total % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1 秒").arg(total);
}

} // namespace

ExportProgressDialog::ExportProgressDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("导出视频"));
    setWindowModality(Qt::WindowModal);
    setMinimumSize(560, 400);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_preview = new QLabel(QStringLiteral("等待第一帧…"), this);
    m_preview->setObjectName(QStringLiteral("exportPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(512, 288);
    m_preview->setStyleSheet(
        QStringLiteral("background:#000; color:#777; border:1px solid #333;"));
    layout->addWidget(m_preview, 1);

    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(true);
    layout->addWidget(m_bar);
    auto* bottom = new QHBoxLayout();
    m_status = new QLabel(QStringLiteral("正在准备…"), this);
    m_status->setObjectName(QStringLiteral("exportStatus"));
    bottom->addWidget(m_status, 1);
    m_cancel = new QPushButton(QStringLiteral("取消"), this);
    m_cancel->setObjectName(QStringLiteral("exportCancelButton"));
    connect(m_cancel, &QPushButton::clicked, this,
            &ExportProgressDialog::requestCancel);
    bottom->addWidget(m_cancel);
    layout->addLayout(bottom);
}

void ExportProgressDialog::setPreview(const QImage& image)
{
    m_image = image;
    updatePreview();
}

void ExportProgressDialog::setProgress(int done, int total)
{
    m_bar->setMaximum(std::max(1, total));
    m_bar->setValue(std::clamp(done, 0, std::max(1, total)));
    if (m_cancelling)
        return;

    if (done >= total)
    {
        m_status->setText(QStringLiteral("正在编码视频…"));
        return;
    }

    // Smooth the throughput over ~250 ms windows so bursty rendering and
    // pipe backpressure do not make the estimate jump every frame.
    if (done > 0)
    {
        if (!m_clock.isValid())
            m_clock.start();
        const qint64 now = m_clock.elapsed();
        if (m_lastSampleMs < 0)
        {
            m_lastSampleMs = now;
            m_lastDone = done;
        }
        else if (now - m_lastSampleMs >= 250)
        {
            const double seconds = (now - m_lastSampleMs) / 1000.0;
            const double instant = (done - m_lastDone) / std::max(0.001, seconds);
            m_rate = m_rate > 0.0 ? m_rate * 0.6 + instant * 0.4 : instant;
            m_lastSampleMs = now;
            m_lastDone = done;
        }
    }

    if (m_rate > 0.05)
    {
        const double remaining = (total - done) / m_rate;
        m_status->setText(QStringLiteral("%1 / %2 帧 · %3 fps · 剩余 %4")
                              .arg(done)
                              .arg(total)
                              .arg(m_rate, 0, 'f', 1)
                              .arg(formatRemaining(remaining)));
    }
    else
    {
        m_status->setText(QStringLiteral("%1 / %2 帧").arg(done).arg(total));
    }
}

void ExportProgressDialog::finish()
{
    m_finished = true;
    close();
}

void ExportProgressDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updatePreview();
}

void ExportProgressDialog::closeEvent(QCloseEvent* event)
{
    if (m_finished)
    {
        event->accept();
        return;
    }
    event->ignore();
    requestCancel();
}

void ExportProgressDialog::reject()
{
    if (m_finished)
    {
        QDialog::reject();
        return;
    }
    requestCancel();
}

void ExportProgressDialog::updatePreview()
{
    if (m_image.isNull())
        return;
    const QSize size = m_preview->size();
    if (size.width() < 16 || size.height() < 16)
        return;
    m_preview->setPixmap(QPixmap::fromImage(
        m_image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void ExportProgressDialog::requestCancel()
{
    if (m_cancelling || m_finished)
        return;
    m_cancelling = true;
    m_cancel->setEnabled(false);
    m_status->setText(QStringLiteral("正在取消…"));
    emit cancelRequested();
}

} // namespace pvf
