#pragma once

// Shared helpers for the AssetLibraryPanel translation units. They used to be
// file-local in AssetLibraryPanel.cpp; the panel is split across several .cpp
// files now, so the small pure helpers and the list widget live here with
// external linkage. Behaviour is unchanged.

#include "common/Types.h"

#include <QColor>
#include <QListWidget>
#include <QMimeData>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

#include <functional>

class QListWidgetItem;

namespace pvf {
namespace assetlibdetail {

QString elidedPath(const QString& path);
QString assetTypeLabel(AssetType type);
QString shortHash(const AssetHash& hash);

// Assets that can replace an entity of this content kind, following
// docs/AI/ENTITY_MODEL.md 6.2.
bool assetCompatible(ContentKind kind, AssetType currentType, AssetType incoming);

QPixmap checkerPixmap(const QSize& size, const QString& text, const QColor& border);
QPixmap htmlPlaceholder(const QSize& size, const QString& text);

// The list must provide a custom MIME payload so the timeline can tell which
// template (or HTML asset) was dragged, instead of receiving plain text.
class AssetListWidget : public QListWidget
{
public:
    explicit AssetListWidget(QWidget* parent = nullptr);

    std::function<QMimeData*(const QString&)> sessionMimeFactory;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

} // namespace assetlibdetail
} // namespace pvf
