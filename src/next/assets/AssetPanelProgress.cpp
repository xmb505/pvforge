#include "next/assets/AssetPanel.h"

#include "AssetPanelInternal.h"
#include "video2/VideoExtractionQueue.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

namespace pvf::next {

// One row in the bottom "video extraction" strip. Deliberately plain widgets:
// the panel owns queue lifetime and row removal, the row only renders state.
class AssetPanel::ProgressRow final : public QFrame
{
public:
    ProgressRow(const AssetHash& hash, const QString& name, QWidget* parent)
        : QFrame(parent)
        , m_hash(hash)
    {
        setObjectName(QStringLiteral("VideoExtractionRow"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 5, 6, 6);
        layout->setSpacing(3);

        auto* top = new QHBoxLayout;
        top->setSpacing(6);

        m_name = new QLabel(name, this);
        m_name->setObjectName(QStringLiteral("VideoExtractionName"));
        m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_name->setToolTip(name);
        top->addWidget(m_name, 1);

        m_text = new QLabel(QStringLiteral("抽帧 0/0"), this);
        m_text->setObjectName(QStringLiteral("VideoExtractionText"));
        QFont small = m_text->font();
        small.setPixelSize(11);
        m_text->setFont(small);
        top->addWidget(m_text);

        m_cancel = new QPushButton(QStringLiteral("取消"), this);
        m_cancel->setObjectName(QStringLiteral("VideoExtractionCancel"));
        m_cancel->setCursor(Qt::PointingHandCursor);
        m_cancel->setFixedHeight(18);
        top->addWidget(m_cancel);

        layout->addLayout(top);

        m_bar = new QProgressBar(this);
        m_bar->setObjectName(QStringLiteral("VideoExtractionBar"));
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(5);
        m_bar->setRange(0, 100);
        m_bar->setValue(0);
        layout->addWidget(m_bar);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("VideoExtractionError"));
        m_error->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_error->hide();
        layout->addWidget(m_error);
    }

    AssetHash hash() const { return m_hash; }
    QPushButton* cancelButton() const { return m_cancel; }
    bool failed() const { return m_failed; }
    bool finished() const { return m_finished; }

    QString statusText() const
    {
        return m_failed ? m_error->text() : m_text->text();
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
        m_bar->hide();
        m_text->hide();
        m_error->setText(QStringLiteral("抽帧失败：%1").arg(error));
        m_error->setToolTip(error);
        m_error->show();
        m_cancel->setText(QStringLiteral("关闭"));
        m_cancel->setEnabled(true);
    }

private:
    AssetHash m_hash;
    QLabel* m_name = nullptr;
    QLabel* m_text = nullptr;
    QLabel* m_error = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_cancel = nullptr;
    int m_total = 0;
    bool m_failed = false;
    bool m_finished = false;
};

int AssetPanel::progressRowCount() const
{
    return m_progressRows.size();
}

QString AssetPanel::progressTextAt(int index) const
{
    if (index < 0 || index >= m_progressRows.size())
        return {};
    return m_progressRows.at(index)->statusText();
}

QPushButton* AssetPanel::progressCancelButtonAt(int index) const
{
    if (index < 0 || index >= m_progressRows.size())
        return nullptr;
    return m_progressRows.at(index)->cancelButton();
}

int AssetPanel::progressRowIndex(const AssetHash& hash) const
{
    for (int i = 0; i < m_progressRows.size(); ++i)
    {
        if (m_progressRows.at(i)->hash() == hash)
            return i;
    }
    return -1;
}

QString AssetPanel::displayNameFor(const AssetHash& hash) const
{
    for (const Entry& entry : m_entries)
    {
        if (entry.hash == hash)
            return entry.name;
    }
    if (m_document)
    {
        if (const auto asset = m_document->assets().get(hash))
            return asset->name.isEmpty() ? hash.left(8) : asset->name;
    }
    return hash.left(8);
}

void AssetPanel::ensureProgressRow(const AssetHash& hash)
{
    if (!m_progressArea || progressRowIndex(hash) >= 0)
        return;

    auto* row = new ProgressRow(hash, displayNameFor(hash), m_progressArea);
    m_progressLayout->addWidget(row);
    m_progressRows.append(row);
    connect(row->cancelButton(), &QPushButton::clicked, this, [this, hash]() {
        const int index = progressRowIndex(hash);
        if (index < 0)
            return;
        ProgressRow* current = m_progressRows.at(index);
        if (current->failed())
        {
            removeProgressRow(hash);
            return;
        }
        current->cancelButton()->setEnabled(false);
        if (m_extractionQueue)
            m_extractionQueue->cancel(hash);
    });
    m_progressArea->show();
}

void AssetPanel::updateProgressRow(const AssetHash& hash, int done, int total)
{
    ensureProgressRow(hash);
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;
    m_progressRows.at(index)->setProgress(done, total);
}

void AssetPanel::finishProgressRow(const AssetHash& hash)
{
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;

    ProgressRow* row = m_progressRows.at(index);
    row->setFinished();

    // Keep the completed row for a moment so the user sees 100%, then drop it.
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

void AssetPanel::failProgressRow(const AssetHash& hash, const QString& error)
{
    ensureProgressRow(hash);
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;
    m_progressRows.at(index)->setFailed(error);
}

void AssetPanel::removeProgressRow(const AssetHash& hash)
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

void AssetPanel::setVideoExtractionOptions(int maxWidth, qint64 diskBudgetBytes)
{
    if (maxWidth > 0)
        m_videoExtractionWidth = maxWidth;
    if (diskBudgetBytes > 0)
        m_videoExtractionDiskBudget = diskBudgetBytes;
}

int AssetPanel::videoExtractionWidth() const
{
    return m_videoExtractionWidth;
}

qint64 AssetPanel::videoExtractionDiskBudget() const
{
    return m_videoExtractionDiskBudget;
}

bool AssetPanel::buildExtractionJob(const assets::StoredAsset& asset,
                                    video2::ExtractionJob* job) const
{
    if (!m_document || !job || asset.type != AssetType::Video)
        return false;

    const int fps = m_document->project().settings.fps;
    if (fps <= 0)
        return false;

    const QStringList contentFiles = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (contentFiles.isEmpty())
        return false;

    const QString projectRoot =
        QFileInfo(m_document->assets().assetsDirectory()).absolutePath();

    job->assetHash = asset.hash;
    job->videoPath = asset.directory + QLatin1Char('/') + contentFiles.first();
    job->projectFps = fps;
    job->maxWidth = m_videoExtractionWidth;
    job->diskBudget = m_videoExtractionDiskBudget;
    job->cacheRoot = QDir(projectRoot).filePath(QStringLiteral(".cache/video"));
    return true;
}

void AssetPanel::enqueueVideoExtraction(const assets::StoredAsset& asset)
{
    if (!m_extractionQueue)
        return;

    video2::ExtractionJob job;
    if (!buildExtractionJob(asset, &job))
        return;
    m_extractionQueue->enqueue(job);
}

void AssetPanel::enqueueMissingVideoExtractions()
{
    if (!m_document || !m_extractionQueue)
        return;

    int queued = 0;
    for (const assets::StoredAsset& asset : m_document->assets().list())
    {
        if (asset.type != AssetType::Video)
            continue;
        if (m_extractionQueue->isRunning(asset.hash)
            || m_extractionQueue->isQueued(asset.hash))
            continue;

        video2::ExtractionJob job;
        if (!buildExtractionJob(asset, &job))
            continue;
        m_extractionQueue->enqueue(job);
        // enqueue() silently skips jobs with a complete cache index.
        if (m_extractionQueue->isQueued(asset.hash)
            || m_extractionQueue->isRunning(asset.hash))
            ++queued;
    }

    if (queued > 0)
        setStatus(tr("已排队补抽 %1 个视频的帧").arg(queued));
}

} // namespace pvf::next
