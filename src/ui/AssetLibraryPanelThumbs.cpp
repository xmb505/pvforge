#include "AssetLibraryPanel.h"

#include "AssetLibraryPanelInternal.h"

#include "app/EditorSession.h"
#include "next/app/MediaServices.h"
#include "video2/VideoFrames.h"

#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QImageReader>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

namespace {

// One render size for every HTML thumbnail: the hover card's 240x150.  Asking
// Chrome for the 48x48 list icon instead cached a 48x27 frame that the hover
// card then upscaled five times.  Both views now downscale this sharp base
// image on demand.
constexpr QSize kHtmlThumbRenderSize(240, 150);

// Shared by the list icon and the hover card: the real 0-frame render when the
// cache has it, the HTML placeholder until then (and forever when Chrome is
// unavailable, because htmlThumbnail() never returns an image then).
QPixmap htmlThumbPixmap(const QImage& frame, const QSize& size,
                        const QString& caption)
{
    if (frame.isNull())
        return assetlibdetail::htmlPlaceholder(size, caption);

    QPixmap pixmap(size);
    pixmap.fill(QColor(18, 20, 26));
    QPainter painter(&pixmap);
    const QImage scaled =
        frame.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawImage((size.width() - scaled.width()) / 2,
                      (size.height() - scaled.height()) / 2, scaled);
    return pixmap;
}

} // namespace

QString AssetLibraryPanel::iconCacheKey(const SessionEntry& entry) const
{
    return QStringLiteral("%1|%2").arg(entry.hash, entry.missingReason);
}

QPixmap AssetLibraryPanel::iconFor(const SessionEntry& entry)
{
    const QString key = iconCacheKey(entry);
    if (const auto cached = m_iconCache.constFind(key); cached != m_iconCache.cend())
        return *cached;

    const QSize size(48, 48);
    QPixmap pixmap;
    if (entry.missing)
    {
        pixmap = assetlibdetail::checkerPixmap(size, QStringLiteral("缺失"),
                                               QColor(QStringLiteral("#ff6b6b")));
    }
    else
    {
        switch (entry.type)
        {
        case AssetType::Image:
        {
            QImage image;
            if (!entry.content.isEmpty())
            {
                QImageReader reader(entry.content);
                reader.setAutoTransform(true);
                image = reader.read();
            }
            if (image.isNull())
                pixmap = assetlibdetail::checkerPixmap(
                    size, QStringLiteral("坏图"), QColor(QStringLiteral("#ff6b6b")));
            else
            {
                pixmap = QPixmap(size);
                pixmap.fill(QColor(18, 20, 26));
                QPainter painter(&pixmap);
                const QImage scaled = image.scaled(size, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
                painter.drawImage((size.width() - scaled.width()) / 2,
                                  (size.height() - scaled.height()) / 2, scaled);
            }
            break;
        }
        case AssetType::Video:
        {
            QImage frame;
            if (!entry.content.isEmpty())
            {
                video2::VideoFrames frames(
                    m_session ? m_session->appSettings().videoProxyWidth : 960, 8);
                video2::Request request;
                request.path = entry.content;
                request.sourceFrame = 0;
                request.fps = m_session ? std::max(1, m_session->settings().fps) : 30;
                request.size = QSize(96, 96);
                request.proxy = true;
                frame = frames.frame(request);
            }
            pixmap = QPixmap(size);
            pixmap.fill(QColor(18, 20, 26));
            QPainter painter(&pixmap);
            if (!frame.isNull())
            {
                const QImage scaled = frame.scaled(size, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
                painter.drawImage((size.width() - scaled.width()) / 2,
                                  (size.height() - scaled.height()) / 2, scaled);
            }
            painter.setPen(QPen(QColor(127, 209, 255), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRect(9, 12, 30, 20));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(127, 209, 255));
            QPolygon play;
            play << QPoint(21, 16) << QPoint(21, 28) << QPoint(31, 22);
            painter.drawPolygon(play);
            break;
        }
        case AssetType::Audio:
        {
            pixmap = QPixmap(size);
            pixmap.fill(QColor(18, 20, 26));
            QPainter painter(&pixmap);
            static const int heights[] = {6, 12, 20, 10, 24, 14, 8, 18, 11};
            painter.setPen(Qt::NoPen);
            for (int i = 0; i < 9; ++i)
            {
                const int height = heights[i];
                painter.fillRect(QRect(6 + i * 4, 24 - height / 2, 3, height),
                                 QColor(QStringLiteral("#7fd1ff")));
            }
            break;
        }
        case AssetType::Html:
            pixmap = htmlThumbPixmap(htmlThumbnail(entry, kHtmlThumbRenderSize),
                                     size, QStringLiteral("HTML"));
            break;
        }
    }

    if (!pixmap.isNull())
    {
        QPainter painter(&pixmap);
        painter.setPen(QPen(QColor(74, 77, 85), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    }
    m_iconCache.insert(key, pixmap);
    return pixmap;
}

QPixmap AssetLibraryPanel::previewFor(const SessionEntry& entry, const QSize& size)
{
    if (entry.missing)
        return assetlibdetail::checkerPixmap(
            size, QStringLiteral("素材缺失"), QColor(QStringLiteral("#ff6b6b")));

    if (entry.type == AssetType::Image && !entry.content.isEmpty())
    {
        QImageReader reader(entry.content);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull())
            return QPixmap::fromImage(image.scaled(
                size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (entry.type == AssetType::Video && !entry.content.isEmpty())
    {
        video2::VideoFrames frames(
            m_session ? m_session->appSettings().videoProxyWidth : 960, 8);
        video2::Request request;
        request.path = entry.content;
        request.sourceFrame = 0;
        request.fps = m_session ? std::max(1, m_session->settings().fps) : 30;
        request.size = size;
        request.proxy = true;
        const QImage frame = frames.frame(request);
        if (!frame.isNull())
            return QPixmap::fromImage(frame);
    }
    if (entry.type == AssetType::Html)
        return htmlThumbPixmap(htmlThumbnail(entry, kHtmlThumbRenderSize), size,
                               QStringLiteral("HTML"));
    if (entry.type == AssetType::Audio)
    {
        QPixmap pixmap(size);
        pixmap.fill(QColor(18, 20, 26));
        QPainter painter(&pixmap);
        painter.setPen(QPen(QColor(127, 209, 255), 2));
        const int mid = size.height() / 2;
        for (int x = 8; x < size.width() - 8; x += 6)
        {
            const int height = 6 + (x * 37 % std::max(8, size.height() - 20));
            painter.drawLine(QPoint(x, mid - height / 2),
                             QPoint(x, mid + height / 2));
        }
        return pixmap;
    }
    return iconFor(entry);
}

QImage AssetLibraryPanel::htmlThumbnail(const SessionEntry& entry,
                                        const QSize& target)
{
#ifdef PVFORGE_TEST_HOOKS
    if (m_htmlThumbnailForTest)
    {
        const auto cached = m_htmlThumbs.constFind(entry.hash);
        if (cached != m_htmlThumbs.constEnd())
            return *cached;
        const QImage image = m_htmlThumbnailForTest(entry.hash, target);
        if (!image.isNull())
            m_htmlThumbs.insert(entry.hash, image);
        return image;
    }
#endif
    if (!m_session || entry.missing || entry.hash.isEmpty())
        return {};

    const auto cached = m_htmlThumbs.constFind(entry.hash);
    if (cached != m_htmlThumbs.constEnd())
        return *cached;

    next::MediaServices* media = m_session->media();
    if (!media)
        return {};

    // Never subscribes twice: the reload happens on every finished frame.
    if (!m_htmlFrameConnection)
    {
        m_htmlFrameConnection = connect(media, &next::MediaServices::htmlFrameReady,
                                        this, [this] { refreshHtmlThumbnails(); });
    }

    const QImage image = media->htmlThumbnail(entry.hash, target);
    if (!image.isNull())
        m_htmlThumbs.insert(entry.hash, image);
    return image;
}

void AssetLibraryPanel::refreshHtmlThumbnails()
{
    // htmlFrameReady fires for every rendered frame while an HTML project
    // plays.  Clearing the whole icon cache and rebuilding the list here meant
    // a full rebuild 30 times a second (video icons decode frame 0 in
    // iconFor()), so only the rows that just gained a fresh frame are touched.
    QVector<const SessionEntry*> finished;
    for (const SessionEntry& entry : m_entries)
    {
        if (entry.type != AssetType::Html || entry.missing)
            continue;
        if (m_htmlThumbs.contains(entry.hash))
            continue; // already shows its frame: nothing to redo
        if (!htmlThumbnail(entry, kHtmlThumbRenderSize).isNull())
            finished.append(&entry);
    }
    if (finished.isEmpty())
        return; // no new frame: O(HTML entries), no rebuild, no cache drop

#ifdef PVFORGE_TEST_HOOKS
    ++m_htmlThumbRefreshCount;
#endif

    QListWidgetItem* hoverItem = m_lastHoverIndex >= 0 && m_list
                                     && m_lastHoverIndex < m_list->count()
                                 ? m_list->item(m_lastHoverIndex)
                                 : nullptr;
    bool hoverDirty = false;
    for (const SessionEntry* entry : finished)
    {
        // Only this entry's memoised icon is stale; the rest of the cache
        // (images, videos, audio) stays warm.
        m_iconCache.remove(iconCacheKey(*entry));
        if (m_list)
        {
            for (int row = 0; row < m_list->count(); ++row)
            {
                QListWidgetItem* item = m_list->item(row);
                if (item->data(Qt::UserRole).toString() != entry->hash)
                    continue;
                item->setIcon(QIcon(iconFor(*entry)));
                hoverDirty = hoverDirty || item == hoverItem;
                break;
            }
        }
    }

    // The hover card is not part of the list, so it is refreshed on its own and
    // only when it is showing a row that just finished rendering.
    if (hoverDirty && m_hoverPreview && m_hoverPreview->isVisible())
        showHoverPreview(hoverItem);
}

#ifdef PVFORGE_TEST_HOOKS
void AssetLibraryPanel::setHtmlThumbnailRendererForTest(
    HtmlThumbnailRenderer renderer)
{
    m_htmlThumbnailForTest = std::move(renderer);
    m_htmlThumbs.clear();
    m_iconCache.clear();
    // Rows already hold the placeholder icon; rebuild them against the new
    // renderer so the assertion sees the hook's answer.
    applySessionFilter();
    m_htmlFrameConnection = QMetaObject::Connection();
}

QListWidgetItem* AssetLibraryPanel::assetItemAtForTest(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return nullptr;
    return m_list->item(index);
}

int AssetLibraryPanel::iconCacheSizeForTest() const
{
    return m_iconCache.size();
}

void AssetLibraryPanel::showHoverPreviewAtForTest(int index)
{
    if (!m_list || index < 0 || index >= m_list->count())
        return;
    showHoverPreview(m_list->item(index));
}
#endif

void AssetLibraryPanel::setIconView(bool iconView)
{
    m_iconView = iconView;
    if (iconView)
    {
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(48, 48));
        m_list->setGridSize(m_sessionMode ? QSize(112, 92) : QSize(80, 84));
        m_list->setWordWrap(true);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setSpacing(2);
        m_list->setAlternatingRowColors(false);
        m_viewButton->setText(QStringLiteral("☰"));
        m_viewButton->setToolTip(QStringLiteral("切换到列表视图"));
    }
    else
    {
        m_list->setViewMode(QListView::ListMode);
        m_list->setIconSize(QSize(40, 40));
        m_list->setGridSize(QSize());
        m_list->setWordWrap(false);
        m_list->setSpacing(0);
        m_list->setAlternatingRowColors(true);
        m_viewButton->setText(QStringLiteral("▦"));
        m_viewButton->setToolTip(QStringLiteral("切换到图标视图"));
    }
}

void AssetLibraryPanel::hideHoverPreview()
{
    m_lastHoverIndex = -1;
    if (m_hoverPreview)
        m_hoverPreview->hide();
}

void AssetLibraryPanel::ensureHoverPreview()
{
    if (m_hoverPreview)
        return;

    // Native tool-tip look: StyledPanel plus the tool-tip palette roles, so
    // the card follows the system light/dark theme instead of a fixed dark
    // sheet (the old #1e1f24 frame).
    m_hoverPreview = new QFrame(this, Qt::ToolTip);
    m_hoverPreview->setObjectName(QStringLiteral("assetHoverPreview"));
    m_hoverPreview->setAttribute(Qt::WA_ShowWithoutActivating);
    m_hoverPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hoverPreview->setFrameShape(QFrame::StyledPanel);
    m_hoverPreview->setFrameShadow(QFrame::Plain);
    m_hoverPreview->setAutoFillBackground(true);

    QPalette palette = m_hoverPreview->palette();
    const QPalette appPalette = QGuiApplication::palette();
    palette.setColor(QPalette::Window, appPalette.color(QPalette::ToolTipBase));
    palette.setColor(QPalette::WindowText, appPalette.color(QPalette::ToolTipText));
    // Mid is the same separator role a styled panel uses for its border.
    palette.setColor(QPalette::Mid, appPalette.color(QPalette::Mid));
    m_hoverPreview->setPalette(palette);

    auto* hoverLayout = new QVBoxLayout(m_hoverPreview);
    hoverLayout->setContentsMargins(4, 4, 4, 4);
    hoverLayout->setSpacing(3);
    m_hoverImage = new QLabel(m_hoverPreview);
    m_hoverCaption = new QLabel(m_hoverPreview);
    m_hoverCaption->setWordWrap(true);
    m_hoverCaption->setMaximumWidth(240);
    for (QLabel* label : {m_hoverImage, m_hoverCaption})
    {
        label->setForegroundRole(QPalette::WindowText);
        label->setBackgroundRole(QPalette::NoRole);
    }
    hoverLayout->addWidget(m_hoverImage);
    hoverLayout->addWidget(m_hoverCaption);
}

void AssetLibraryPanel::showHoverPreview(QListWidgetItem* item)
{
    if (!item)
    {
        hideHoverPreview();
        return;
    }

    QPixmap preview;
    if (m_sessionMode)
    {
        const QString hash = item->data(Qt::UserRole).toString();
        const auto found = std::find_if(
            m_entries.cbegin(), m_entries.cend(),
            [&hash](const SessionEntry& entry) { return entry.hash == hash; });
        if (found != m_entries.cend())
            preview = previewFor(*found, QSize(240, 150));
    }
    if (preview.isNull())
    {
        hideHoverPreview();
        return;
    }

    ensureHoverPreview();

    if (m_list)
        m_lastHoverIndex = m_list->row(item);
    m_hoverImage->setPixmap(preview);
    m_hoverCaption->setText(item->text());
    m_hoverPreview->adjustSize();

    QPoint pos = QCursor::pos() + QPoint(18, 18);
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos()))
    {
        const QRect area = screen->availableGeometry();
        if (pos.x() + m_hoverPreview->width() > area.right())
            pos.setX(QCursor::pos().x() - m_hoverPreview->width() - 18);
        if (pos.y() + m_hoverPreview->height() > area.bottom())
            pos.setY(QCursor::pos().y() - m_hoverPreview->height() - 18);
    }
    m_hoverPreview->move(pos);
    m_hoverPreview->show();
    m_hoverPreview->raise();
}

} // namespace pvf
