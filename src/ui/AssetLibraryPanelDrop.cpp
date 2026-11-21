#include "AssetLibraryPanel.h"

#include "AssetLibraryPanelInternal.h"

#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "next/app/ProjectDocument.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QSet>
#include <QUrl>

namespace pvf {

namespace {

// The list's outbound drags always carry one of these formats. Checking them
// (plus the drag source) keeps a drag from the bin back onto the bin from
// starting a second import.
bool hasInternalAssetMime(const QMimeData* mime)
{
    if (!mime)
        return false;
    return mime->hasFormat(QStringLiteral("application/x-pvforge-asset"))
           || mime->hasFormat(
               QStringLiteral("application/x-pvforge-asset-template"))
           || mime->hasFormat(QStringLiteral("application/x-pvforge-asset-html"));
}

bool isOurDrag(const QMimeData* mime, const QObject* source, const QObject* list)
{
    return (list && source == list) || hasInternalAssetMime(mime);
}

QString normalizedPath(const QString& rawPath)
{
    const QFileInfo info(rawPath);
    if (info.exists())
    {
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty())
            return canonical;
    }
    const QString absolute = info.absoluteFilePath();
    return absolute.isEmpty() ? rawPath : absolute;
}

} // namespace

QString AssetLibraryPanel::externalImportBlockedReason() const
{
    if (!m_session || !m_session->document())
        return QStringLiteral("没有打开的工程，不能导入素材");
    const next::ProjectDocument* document = m_session->document();
    if (document->store() && document->store()->readOnly())
        return QStringLiteral("工程以只读方式打开：%1")
            .arg(document->store()->readOnlyReason());
    if (document->writesLocked())
        return QStringLiteral("导出进行中，工程暂时只读");
    if (!m_session->humanEditingEnabled() || !document->humanWritesEnabled())
        return QStringLiteral("AI 正在修改工程，请稍后再编辑，或先按“急停”");
    return {};
}

bool AssetLibraryPanel::externalDropAvailable(const QMimeData* mime,
                                              const QObject* source) const
{
    if (!mime || !mime->hasUrls() || isOurDrag(mime, source, m_list))
        return false;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile())
            return true;
    return false;
}

bool AssetLibraryPanel::handleExternalDragMove(QDragMoveEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!externalDropAvailable(mime, event->source()))
    {
        // Consume URL/internal drags we cannot take so the item view never
        // sees them; other event types keep their default handling.
        if (mime && (mime->hasUrls() || hasInternalAssetMime(mime)))
        {
            event->ignore();
            setExternalDragActive(false);
            return true;
        }
        return false;
    }

    const QString blocked = externalImportBlockedReason();
    if (!blocked.isEmpty())
    {
        // Ignore the drag (no-drop cursor) but explain once on entry; the drop
        // itself never arrives at a target that rejected the drag enter.
        event->ignore();
        setExternalDragActive(false);
        if (event->type() == QEvent::DragEnter)
            setStatus(blocked);
        return true;
    }

    event->acceptProposedAction();
    setExternalDragActive(true);
    return true;
}

bool AssetLibraryPanel::handleExternalDrop(QDropEvent* event)
{
    setExternalDragActive(false);
    const QMimeData* mime = event->mimeData();
    if (!externalDropAvailable(mime, event->source()))
    {
        if (mime && (mime->hasUrls() || hasInternalAssetMime(mime)))
        {
            event->ignore();
            return true;
        }
        return false;
    }

    const QString blocked = externalImportBlockedReason();
    if (!blocked.isEmpty())
    {
        event->ignore();
        setStatus(blocked);
        return true;
    }

    QStringList paths;
    QSet<QString> seen;
    for (const QUrl& url : mime->urls())
    {
        if (!url.isLocalFile())
            continue;
        const QString path = normalizedPath(url.toLocalFile());
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        paths.append(path);
    }
    if (paths.isEmpty())
    {
        event->ignore();
        setStatus(QStringLiteral("拖入的内容不是本地文件，未导入"));
        return true;
    }

    event->acceptProposedAction();
    importDroppedPaths(paths);
    return true;
}

void AssetLibraryPanel::importDroppedPaths(const QStringList& paths)
{
    if (!m_session || !m_session->document())
        return;

    const QString blocked = externalImportBlockedReason();
    if (!blocked.isEmpty())
    {
        setStatus(blocked);
        return;
    }

    QStringList pending;
    int skipped = 0;
    for (const QString& path : paths)
    {
        const auto known = m_droppedPathHashes.constFind(path);
        if (known != m_droppedPathHashes.cend())
        {
            // Skip only while the previously imported asset still exists; a
            // delete + re-drop must be able to restore it.
            if (m_session->assets().get(*known))
            {
                ++skipped;
                continue;
            }
            m_droppedPathHashes.erase(known);
        }
        pending.append(path);
    }

    if (pending.isEmpty())
    {
        setStatus(QStringLiteral("拖入的 %1 个素材已在素材箱中，未重复导入")
                      .arg(skipped));
        return;
    }

    // Files and pvhtml directories follow the same two pipelines as the
    // import buttons, so a mixed drop imports both instead of only the first.
    QStringList files;
    QStringList directories;
    for (const QString& path : pending)
    {
        if (QFileInfo(path).isDir())
            directories.append(path);
        else
            files.append(path);
    }

    QStringList errors;
    int imported = 0;
    const auto runBatch = [&](const QStringList& batch) {
        if (batch.isEmpty())
            return;
        QString error;
        QHash<QString, AssetHash> byPath;
        if (!importPaths(batch, &error, &byPath) && !error.isEmpty())
            errors.append(error);
        imported += byPath.size();
        for (auto it = byPath.cbegin(); it != byPath.cend(); ++it)
            m_droppedPathHashes.insert(it.key(), it.value());
    };
    runBatch(files);
    runBatch(directories);

    QString status;
    if (imported > 0)
        status = QStringLiteral("已从拖入导入 %1 个素材").arg(imported);
    if (skipped > 0)
    {
        if (!status.isEmpty())
            status += QStringLiteral("；");
        status += QStringLiteral("%1 个已在素材箱中，未重复导入").arg(skipped);
    }
    if (!errors.isEmpty())
    {
        if (!status.isEmpty())
            status += QStringLiteral("；");
        status += errors.join(QStringLiteral("；"));
    }
    if (!status.isEmpty())
        setStatus(status);
}

void AssetLibraryPanel::setExternalDragActive(bool active)
{
    if (m_externalDragActive == active)
        return;
    m_externalDragActive = active;
    if (m_list)
    {
        static_cast<assetlibdetail::AssetListWidget*>(m_list)
            ->setExternalDragHighlight(active);
    }
    update();
}

void AssetLibraryPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!handleExternalDragMove(event))
        QWidget::dragEnterEvent(event);
}

void AssetLibraryPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (!handleExternalDragMove(event))
        QWidget::dragMoveEvent(event);
}

void AssetLibraryPanel::dragLeaveEvent(QDragLeaveEvent* event)
{
    setExternalDragActive(false);
    QWidget::dragLeaveEvent(event);
}

void AssetLibraryPanel::dropEvent(QDropEvent* event)
{
    if (!handleExternalDrop(event))
        QWidget::dropEvent(event);
}

void AssetLibraryPanel::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (!m_externalDragActive)
        return;

    QPainter painter(this);
    const QPalette::ColorGroup group =
        isActiveWindow() ? QPalette::Active : QPalette::Inactive;
    QPen pen(palette().color(group, QPalette::Highlight));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
}

bool AssetLibraryPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (m_list && watched == m_list->viewport())
    {
        switch (event->type())
        {
        case QEvent::DragEnter:
            return handleExternalDragMove(static_cast<QDragEnterEvent*>(event));
        case QEvent::DragMove:
            return handleExternalDragMove(static_cast<QDragMoveEvent*>(event));
        case QEvent::DragLeave:
            setExternalDragActive(false);
            return true;
        case QEvent::Drop:
            return handleExternalDrop(static_cast<QDropEvent*>(event));
        case QEvent::Leave:
            hideHoverPreview();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace pvf
