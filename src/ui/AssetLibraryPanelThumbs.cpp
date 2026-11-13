#include "AssetLibraryPanel.h"

#include "AssetLibraryPanelInternal.h"

#include "app/EditorSession.h"
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
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

QPixmap AssetLibraryPanel::iconFor(const SessionEntry& entry)
{
    const QString key = QStringLiteral("%1|%2").arg(entry.hash, entry.missingReason);
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
        {
            pixmap = QPixmap(size);
            pixmap.fill(QColor(18, 20, 26));
            QPainter painter(&pixmap);
            QFont font = painter.font();
            font.setPixelSize(13);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor(QStringLiteral("#ffcf6b")));
            painter.drawText(pixmap.rect(), Qt::AlignCenter,
                             QStringLiteral("</>"));
            break;
        }
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
        return assetlibdetail::htmlPlaceholder(size, QStringLiteral("HTML"));
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
    if (m_hoverPreview)
        m_hoverPreview->hide();
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

    if (!m_hoverPreview)
    {
        m_hoverPreview = new QFrame(this, Qt::ToolTip);
        m_hoverPreview->setObjectName(QStringLiteral("assetHoverPreview"));
        m_hoverPreview->setAttribute(Qt::WA_ShowWithoutActivating);
        m_hoverPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_hoverPreview->setStyleSheet(QStringLiteral(
            "QFrame { background: #1e1f24; border: 1px solid #4a4d55; }"
            "QLabel { background: transparent; border: none; color: #d8dce4; }"));
        auto* hoverLayout = new QVBoxLayout(m_hoverPreview);
        hoverLayout->setContentsMargins(4, 4, 4, 4);
        hoverLayout->setSpacing(3);
        m_hoverImage = new QLabel(m_hoverPreview);
        m_hoverCaption = new QLabel(m_hoverPreview);
        m_hoverCaption->setWordWrap(true);
        m_hoverCaption->setMaximumWidth(240);
        hoverLayout->addWidget(m_hoverImage);
        hoverLayout->addWidget(m_hoverCaption);
    }

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

bool AssetLibraryPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::Leave)
        hideHoverPreview();
    return QWidget::eventFilter(watched, event);
}

} // namespace pvf
