#include "next/app/WorkbenchPanel.h"

#include "next/app/WorkbenchServices.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace pvf::next {
namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB")
            .arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB")
            .arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace

WorkbenchPanel::WorkbenchPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("workbenchPanel"));

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("workbenchStatusLabel"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);

    m_stats = new QLabel(this);
    m_stats->setObjectName(QStringLiteral("workbenchStatsLabel"));
    m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stats->setWordWrap(true);

    m_rescan = new QPushButton(tr("重新扫描"), this);
    m_rescan->setObjectName(QStringLiteral("workbenchRescanButton"));
    connect(m_rescan, &QPushButton::clicked, this, [this] {
        if (m_services)
            m_services->startScanNow();
    });

    m_clear = new QPushButton(tr("清空缓存"), this);
    m_clear->setObjectName(QStringLiteral("workbenchClearButton"));
    connect(m_clear, &QPushButton::clicked, this, [this] {
        if (!m_services)
            return;
        m_services->clearCache();
        refresh();
        emit cacheCleared();
    });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_rescan);
    buttons->addWidget(m_clear);
    buttons->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_stats);
    layout->addLayout(buttons);
    layout->addStretch(1);

    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &WorkbenchPanel::refresh);
    m_timer->start();

    refresh();
}

void WorkbenchPanel::setServices(WorkbenchServices* services)
{
    m_services = services;
    refresh();
}

QString WorkbenchPanel::statusTextForTest() const
{
    return m_status ? m_status->text() : QString();
}

QString WorkbenchPanel::statsTextForTest() const
{
    return m_stats ? m_stats->text() : QString();
}

void WorkbenchPanel::refresh()
{
    if (!m_services || !m_services->enabled()) {
        m_status->setText(tr("工作台缓存未启用"));
        m_stats->clear();
        m_rescan->setEnabled(false);
        m_clear->setEnabled(false);
        return;
    }

    m_rescan->setEnabled(true);
    m_clear->setEnabled(true);

    const QString camera = m_services->cameraUuid().isEmpty()
                               ? tr("全画布")
                               : m_services->cameraUuid().left(8);
    const QSize target = m_services->targetSize();
    QString status = tr("状态：启用 · 比例 %1% · 扩展 %2% · 目标 %3 × %4\n相机：%5")
                         .arg(m_services->scalePercent())
                         .arg(m_services->expandPercent())
                         .arg(target.width())
                         .arg(target.height())
                         .arg(camera);
    if (!m_services->cacheValid()) {
        status += tr("\n缓存不可用：%1").arg(m_services->cacheError());
    } else {
        const QString directory = m_services->cacheDirectory();
        status += tr("\n目录：%1")
                      .arg(directory.isEmpty()
                               ? tr("（首次访问帧时创建）")
                               : directory);
    }
    m_status->setText(status);

    const workbench::CacheStats cache = m_services->stats();
    const WorkbenchPrewarmProgress progress = m_services->progress();

    QString stats =
        tr("总帧 %1 · 已缓存 %2 · 命中 %3 · 失效 %4\n磁盘 %5 / %6")
            .arg(m_services->totalFrames())
            .arg(cache.entries)
            .arg(cache.hits)
            .arg(cache.misses)
            .arg(formatBytes(cache.diskBytes))
            .arg(formatBytes(cache.budgetBytes));

    if (progress.scanning) {
        stats += tr("\n手动扫描：%1 / %2（当前帧 %3）%4")
                     .arg(progress.done)
                     .arg(progress.total)
                     .arg(progress.frame)
                     .arg(progress.waitingForHtml
                              ? tr(" · 等待 HTML")
                              : (progress.paused ? tr(" · 播放中暂停")
                                                 : QString()));
    } else {
        stats += tr("\n手动扫描：空闲");
    }
    stats += tr("\n本次：写入 %1 · 命中跳过 %2 · 跳过 %3 · 失败 %4")
                 .arg(progress.cached)
                 .arg(progress.hits)
                 .arg(progress.skipped)
                 .arg(progress.failed);

    m_stats->setText(stats);
}

} // namespace pvf::next
