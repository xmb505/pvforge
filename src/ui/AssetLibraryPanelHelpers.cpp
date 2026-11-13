#include "AssetLibraryPanelInternal.h"

#include <QFont>
#include <QFontMetrics>
#include <QListWidgetItem>
#include <QPainter>

#include <algorithm>

namespace pvf {
namespace assetlibdetail {

QString elidedPath(const QString& path)
{
    // Paths have no spaces to wrap at; show both ends and let the tooltip
    // carry the full value. The label itself is width-ignored so a long path
    // can never widen the whole right sidebar.
    return QFontMetrics(QFont()).elidedText(path, Qt::ElideMiddle, 240);
}

QString assetTypeLabel(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("图片");
    case AssetType::Video:
        return QStringLiteral("视频");
    case AssetType::Audio:
        return QStringLiteral("音频");
    case AssetType::Html:
        return QStringLiteral("HTML");
    }
    return QString();
}

QString shortHash(const AssetHash& hash)
{
    return hash.left(8);
}

namespace {

bool isVisualAsset(AssetType type)
{
    return type == AssetType::Image || type == AssetType::Video
           || type == AssetType::Html;
}

} // namespace

bool assetCompatible(ContentKind kind, AssetType currentType, AssetType incoming)
{
    switch (kind)
    {
    case ContentKind::Empty:
        return true;
    case ContentKind::Asset:
        if (isVisualAsset(currentType))
            return isVisualAsset(incoming);
        if (currentType == AssetType::Audio)
            return incoming == AssetType::Audio;
        return false;
    case ContentKind::Camera:
    case ContentKind::Text:
    case ContentKind::Vector:
    case ContentKind::ComicText:
    case ContentKind::RadialLines:
    case ContentKind::FlowLines:
    case ContentKind::HalftoneDots:
    case ContentKind::BrandWall:
        return false;
    }
    return false;
}

QPixmap checkerPixmap(const QSize& size, const QString& text, const QColor& border)
{
    QPixmap pixmap(size);
    pixmap.fill(QColor(22, 24, 30));
    QPainter painter(&pixmap);
    const int cell = 8;
    for (int y = 0; y < size.height(); y += cell)
        for (int x = 0; x < size.width(); x += cell)
        {
            if (((x / cell) + (y / cell)) % 2 == 0)
                painter.fillRect(QRect(x, y, cell, cell), QColor(32, 34, 42));
        }
    painter.setPen(QPen(border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    QFont font = painter.font();
    font.setPixelSize(std::max(9, std::min(size.width(), size.height()) / 4));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(border);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    return pixmap;
}

QPixmap htmlPlaceholder(const QSize& size, const QString& text)
{
    QPixmap pixmap(size);
    pixmap.fill(QColor(24, 30, 42));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(120, 180, 255), 1, Qt::DashLine));
    p.drawRect(pixmap.rect().adjusted(1, 1, -2, -2));
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(std::max(9, std::min(size.width(), size.height()) / 4));
    p.setFont(font);
    p.setPen(QColor(205, 222, 255));
    p.drawText(pixmap.rect(), Qt::AlignCenter, text);
    return pixmap;
}

AssetListWidget::AssetListWidget(QWidget* parent)
    : QListWidget(parent)
{
}

QMimeData* AssetListWidget::mimeData(const QList<QListWidgetItem*>& items) const
{
    auto* mime = new QMimeData;
    if (items.isEmpty())
        return mime;
    QListWidgetItem* item = items.first();
    const int kind = item->data(Qt::UserRole + 1).toInt();
    if (kind == 2)
    {
        if (sessionMimeFactory)
        {
            delete mime;
            mime = sessionMimeFactory(item->data(Qt::UserRole).toString());
            if (!mime)
                mime = new QMimeData;
            else
                mime->setText(item->text());
        }
    }
    else if (kind == 0)
    {
        mime->setData(QStringLiteral("application/x-pvforge-asset-template"),
                      QByteArray::number(item->data(Qt::UserRole).toInt()));
    }
    else
    {
        mime->setData(QStringLiteral("application/x-pvforge-asset-html"),
                      item->data(Qt::UserRole + 2).toString().toUtf8());
    }
    if (!mime->hasText())
        mime->setText(item->text());
    return mime;
}

} // namespace assetlibdetail
} // namespace pvf
