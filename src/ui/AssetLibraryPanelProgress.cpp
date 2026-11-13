#include "AssetLibraryPanel.h"

#include "AssetLibraryPanelInternal.h"

#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "video2/VideoExtractionQueue.h"

#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

// One row in the bottom video-extraction strip. The panel owns the queue
// lifetime and row removal; the row only renders state.
class AssetLibraryPanel::ProgressRow final : public QFrame
{
public:
    ProgressRow(const AssetHash& hash, const QString& name, QWidget* parent)
        : QFrame(parent)
        , m_hash(hash)
    {
        setObjectName(QStringLiteral("assetExtractionRow"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 4, 6, 5);
        layout->setSpacing(2);

        auto* top = new QHBoxLayout;
        top->setSpacing(6);
        m_name = new QLabel(name, this);
        m_name->setObjectName(QStringLiteral("assetExtractionName"));
        m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_name->setToolTip(name);
        top->addWidget(m_name, 1);

        m_text = new QLabel(QStringLiteral("抽帧 0/0"), this);
        m_text->setObjectName(QStringLiteral("assetExtractionText"));
        QFont small = m_text->font();
        small.setPixelSize(10);
        m_text->setFont(small);
        top->addWidget(m_text);

        m_cancel = new QPushButton(QStringLiteral("取消"), this);
        m_cancel->setObjectName(QStringLiteral("assetExtractionCancel"));
        m_cancel->setCursor(Qt::PointingHandCursor);
        m_cancel->setFixedHeight(18);
        top->addWidget(m_cancel);
        layout->addLayout(top);

        m_bar = new QProgressBar(this);
        m_bar->setObjectName(QStringLiteral("assetExtractionBar"));
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(4);
        m_bar->setRange(0, 100);
        layout->addWidget(m_bar);
    }

    AssetHash hash() const { return m_hash; }
    QPushButton* cancelButton() const { return m_cancel; }
    bool finished() const { return m_finished; }
    bool failed() const { return m_failed; }

    QString statusText() const
    {
        return m_failed ? m_error : m_text->text();
    }

    void setProgress(int done, int total)
    {
        m_total = total;
        m_finished = false;
        if (total > 0)
        {
            m_text->setText(QStringLiteral("抽帧 %1/%2").arg(done).arg(total));
            m_bar->setRange(0, total);
            m_bar->setValue(done);
        }
        else
        {
            m_text->setText(QStringLiteral("抽帧 %1").arg(done));
            m_bar->setRange(0, 0);
        }
        m_text->setToolTip(m_text->text());
    }

    void setFinished()
    {
        if (m_total > 0)
            setProgress(m_total, m_total);
        m_finished = true;
        m_text->setText(QStringLiteral("抽帧完成"));
        m_bar->setRange(0, 1);
        m_bar->setValue(1);
        m_cancel->hide();
    }

    void setFailed(const QString& error)
    {
        m_failed = true;
        m_error = error;
        m_bar->hide();
        m_text->setText(QStringLiteral("抽帧失败：%1").arg(error));
        m_text->setToolTip(error);
        m_cancel->setText(QStringLiteral("关闭"));
        m_cancel->setEnabled(true);
    }

private:
    AssetHash m_hash;
    QLabel* m_name = nullptr;
    QLabel* m_text = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_cancel = nullptr;
    int m_total = 0;
    bool m_failed = false;
    bool m_finished = false;
    QString m_error;
};

void AssetLibraryPanel::enqueueVideoExtraction(const assets::StoredAsset& asset)
{
    if (!m_extractionQueue || !m_session || asset.type != AssetType::Video)
        return;
    const QStringList contentFiles = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (contentFiles.isEmpty())
        return;
    video2::ExtractionJob job;
    job.assetHash = asset.hash;
    job.videoPath = asset.directory + QLatin1Char('/') + contentFiles.first();
    job.projectFps = std::max(1, m_session->settings().fps);
    job.maxWidth = std::max(1, m_session->appSettings().videoProxyWidth);
    job.cacheRoot = QDir(m_session->root()).filePath(QStringLiteral(".cache/video"));
    job.diskBudget = 2LL * 1024 * 1024 * 1024;
    m_extractionQueue->enqueue(job);
}

void AssetLibraryPanel::enqueueMissingVideoExtractions()
{
    if (!m_extractionQueue || !m_session)
        return;
    for (const assets::StoredAsset& asset : m_session->assets().list())
    {
        if (asset.type != AssetType::Video)
            continue;
        if (m_extractionQueue->isRunning(asset.hash)
            || m_extractionQueue->isQueued(asset.hash))
            continue;
        enqueueVideoExtraction(asset);
    }
}

void AssetLibraryPanel::ensureProgressRow(const AssetHash& hash)
{
    if (!m_progressLayout || progressRowIndex(hash) >= 0)
        return;
    auto* row = new ProgressRow(hash, displayNameFor(hash), m_progressArea);
    connect(row->cancelButton(), &QPushButton::clicked, this, [this, hash, row]() {
        if (row->finished() || row->failed() || m_extractionQueue == nullptr)
            removeProgressRow(hash);
        else
            m_extractionQueue->cancel(hash);
    });
    m_progressLayout->addWidget(row);
    m_progressRows.append(row);
    if (m_progressArea)
        m_progressArea->show();
}

void AssetLibraryPanel::updateProgressRow(const AssetHash& hash, int done, int total)
{
    ensureProgressRow(hash);
    const int index = progressRowIndex(hash);
    if (index >= 0)
        m_progressRows.at(index)->setProgress(done, total);
}

void AssetLibraryPanel::finishProgressRow(const AssetHash& hash)
{
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;
    ProgressRow* row = m_progressRows.at(index);
    row->setFinished();
    QPointer<ProgressRow> guard(row);
    QTimer::singleShot(2000, this, [this, hash, guard]() {
        if (!guard)
            return;
        const int current = progressRowIndex(hash);
        if (current >= 0 && m_progressRows.at(current) == guard.data()
            && guard->finished())
            removeProgressRow(hash);
    });
}

void AssetLibraryPanel::failProgressRow(const AssetHash& hash, const QString& error)
{
    ensureProgressRow(hash);
    const int index = progressRowIndex(hash);
    if (index >= 0)
        m_progressRows.at(index)->setFailed(error);
}

void AssetLibraryPanel::removeProgressRow(const AssetHash& hash)
{
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;
    ProgressRow* row = m_progressRows.takeAt(index);
    if (m_progressLayout)
        m_progressLayout->removeWidget(row);
    row->hide();
    row->deleteLater();
    if (m_progressRows.isEmpty() && m_progressArea)
        m_progressArea->hide();
}

int AssetLibraryPanel::progressRowIndex(const AssetHash& hash) const
{
    for (int index = 0; index < m_progressRows.size(); ++index)
        if (m_progressRows.at(index)->hash() == hash)
            return index;
    return -1;
}

int AssetLibraryPanel::progressRowCount() const
{
    return int(m_progressRows.size());
}

QString AssetLibraryPanel::progressTextAt(int index) const
{
    if (index < 0 || index >= m_progressRows.size())
        return {};
    return m_progressRows.at(index)->statusText();
}

QPushButton* AssetLibraryPanel::progressCancelButtonAt(int index) const
{
    if (index < 0 || index >= m_progressRows.size())
        return nullptr;
    return m_progressRows.at(index)->cancelButton();
}

} // namespace pvf
