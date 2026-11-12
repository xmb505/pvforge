#include "AssetLibraryPanel.h"

#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "app/MainWindow.h"
#include "assets/AssetStore.h"
#include "assets/AssetTypeDetect.h"
#include "core/BrandWall.h"
#include "core/ComicBox.h"
#include "core/Lang.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "html/HtmlAsset.h"
#include "model/Query.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "video2/VideoExtractionQueue.h"
#include "video2/VideoFrames.h"

#if PVFORGE_LEGACY_SHELL
#include "media/MediaImporter.h"
#endif

#include <QAbstractItemView>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

#if PVFORGE_LEGACY_SHELL
QString templateTypeName(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QStringLiteral("图片");
    case EntityType::Video:
        return QStringLiteral("视频");
    case EntityType::Text:
        return QStringLiteral("文字");
    case EntityType::Vector:
        return QStringLiteral("矢量");
    case EntityType::Camera:
        return QStringLiteral("摄像机");
    case EntityType::Audio:
        return QStringLiteral("音频");
    case EntityType::Folder:
        return QStringLiteral("文件夹");
    case EntityType::ComicText:
        return QStringLiteral("漫画文本框");
    case EntityType::RadialLines:
        return QStringLiteral("放射状线条");
    case EntityType::FlowLines:
        return QStringLiteral("快速流过");
    case EntityType::BrandWall:
        return QStringLiteral("品牌瓷砖墙");
    case EntityType::HalftoneDots:
        return QStringLiteral("漫画点阵");
    case EntityType::HtmlLayer:
        return QStringLiteral("HTML 组件");
    }
    return QString();
}
#endif // PVFORGE_LEGACY_SHELL

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

bool isVisualAsset(AssetType type)
{
    return type == AssetType::Image || type == AssetType::Video
           || type == AssetType::Html;
}

// Assets that can replace an entity of this content kind, following
// docs/AI/ENTITY_MODEL.md 6.2.
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

#if PVFORGE_LEGACY_SHELL
QStringList htmlAssetPaths(const Project& project)
{
    QStringList paths;
    const QString directory = project.htmlAssetDirectory();
    if (!directory.isEmpty())
    {
        const QFileInfoList files =
            QDir(directory).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
        for (const QFileInfo& file : files)
            paths.append(file.absoluteFilePath());
    }
    for (const Entity& entity : project.entities())
    {
        if (entity.type != EntityType::HtmlLayer)
            continue;
        const QString path = QFileInfo(entity.htmlAssetPath()).absoluteFilePath();
        if (!path.isEmpty() && !paths.contains(path))
            paths.append(path);
    }
    paths.sort();
    return paths;
}

QString htmlAssetLabel(const QString& path)
{
    QString error;
    const html::HtmlAsset asset = html::HtmlAsset::parseFile(path, &error);
    if (error.isEmpty())
    {
        QString name = QFileInfo(path).completeBaseName();
        if (!asset.components.isEmpty())
        {
            const html::HtmlComponent& component = asset.components.first();
            name = component.name.isEmpty() ? component.id : component.name;
        }
        return QStringLiteral("%1 (%2×%3)")
            .arg(name)
            .arg(asset.logicalSize.width())
            .arg(asset.logicalSize.height());
    }
    return QFileInfo(path).completeBaseName();
}

QImage htmlAssetThumbnail(const Project& project, const QString& path, const QSize& size)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (const Entity& entity : project.entities())
    {
        if (entity.type != EntityType::HtmlLayer || entity.htmlImage.isNull())
            continue;
        if (QFileInfo(entity.htmlAssetPath()).absoluteFilePath() != absolute)
            continue;
        return entity.htmlImage.scaled(size, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
    }
    return QImage();
}
#endif // PVFORGE_LEGACY_SHELL

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

#if PVFORGE_LEGACY_SHELL
QPixmap renderTemplate(const Template& tmpl, const QSize& size)
{
    QPixmap pixmap(size);
    pixmap.fill(QColor(30, 30, 36));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (!tmpl.thumbnail.isNull())
    {
        const QImage scaled =
            tmpl.thumbnail.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((size.width() - scaled.width()) / 2,
                    (size.height() - scaled.height()) / 2, scaled);
        return pixmap;
    }

    if (tmpl.type == EntityType::Text)
    {
        const TextBlock block = TextBlock::measure(TextStyle::fromData(tmpl.data));
        const QSizeF textSize = block.size();
        if (textSize.width() > 1 && textSize.height() > 1)
        {
            const double scale =
                std::min((size.width() - 16.0) / textSize.width(),
                         (size.height() - 16.0) / textSize.height());
            p.save();
            p.translate(size.width() / 2.0, size.height() / 2.0);
            p.scale(scale, scale);
            block.paint(p, QPointF());
            p.restore();
        }
    }
    else if (tmpl.type == EntityType::ComicText)
    {
        const ComicBox box =
            ComicBox::build(TextStyle::fromData(tmpl.data),
                            BubbleStyle::fromData(tmpl.data));
        const QRectF bounds = box.bounds();
        const double scale =
            std::min(size.width() / std::max(1.0, bounds.width()),
                     size.height() / std::max(1.0, bounds.height())) * 0.9;
        p.save();
        p.translate(size.width() / 2.0, size.height() / 2.0);
        p.scale(scale, scale);
        box.paint(p);
        p.restore();
    }
    else if (tmpl.type == EntityType::RadialLines)
    {
        const RadialParams params = RadialParams::fromData(tmpl.data);
        const QRectF bounds = LineDeco::radialBounds(params);
        const double scale =
            std::min(size.width() / std::max(1.0, bounds.width()),
                     size.height() / std::max(1.0, bounds.height())) * 0.9;
        p.save();
        p.translate(size.width() / 2.0, size.height() / 2.0);
        p.scale(scale, scale);
        LineDeco::paintRadial(p, params, 0.0);
        p.restore();
    }
    else if (tmpl.type == EntityType::FlowLines)
    {
        const FlowParams params = FlowParams::fromData(tmpl.data);
        const QRectF bounds = LineDeco::flowBounds(params);
        const double scale =
            std::min(size.width() / std::max(1.0, bounds.width()),
                     size.height() / std::max(1.0, bounds.height())) * 0.9;
        p.save();
        p.translate(size.width() / 2.0, size.height() / 2.0);
        p.scale(scale, scale);
        LineDeco::paintFlow(p, params, 0.0);
        p.restore();
    }
    else if (tmpl.type == EntityType::BrandWall)
    {
        const BrandWallParams params = BrandWallParams::fromData(tmpl.data);
        QVector<QImage> logos;
        for (const QString& path : params.logos)
            logos.append(BrandWall::loadLogo(path, 256));
        const QRectF bounds = BrandWall::bounds(params);
        const double scale =
            std::min(size.width() / std::max(1.0, bounds.width()),
                     size.height() / std::max(1.0, bounds.height())) * 0.9;
        p.save();
        p.translate(size.width() / 2.0, size.height() / 2.0);
        p.scale(scale, scale);
        BrandWall::paint(p, params, logos);
        p.restore();
    }
    else if (tmpl.type == EntityType::HalftoneDots)
    {
        const HalftoneParams params = HalftoneParams::fromData(tmpl.data);
        const QRectF bounds = LineDeco::halftoneBounds(params);
        const double scale =
            std::min(size.width() / std::max(1.0, bounds.width()),
                     size.height() / std::max(1.0, bounds.height())) * 0.9;
        p.save();
        p.translate(size.width() / 2.0, size.height() / 2.0);
        p.scale(scale, scale);
        LineDeco::paintHalftone(p, params);
        p.restore();
    }
    else if (tmpl.type == EntityType::Vector)
    {
        const int shape = tmpl.data.value(QStringLiteral("shape"), 0).toInt();
        const QColor fill = tmpl.data.value(QStringLiteral("fill")).value<QColor>();
        p.setBrush(fill.isValid() ? fill : QColor(90, 170, 255));
        p.setPen(Qt::NoPen);
        const QRectF rect(size.width() * 0.2, size.height() * 0.2, size.width() * 0.6,
                          size.height() * 0.6);
        if (shape == 1)
            p.drawEllipse(rect);
        else if (shape == 2)
            p.drawLine(QPointF(rect.left(), rect.center().y()),
                       QPointF(rect.right(), rect.center().y()));
        else
            p.drawRect(rect);
    }
    else
    {
        p.setPen(QColor(160, 160, 170));
        p.drawText(pixmap.rect(), Qt::AlignCenter, templateTypeName(tmpl.type));
    }
    return pixmap;
}

QIcon listIcon(const Template& tmpl)
{
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (!tmpl.thumbnail.isNull())
    {
        const QImage scaled = tmpl.thumbnail.scaled(pixmap.size(), Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation);
        p.drawImage((pixmap.width() - scaled.width()) / 2,
                    (pixmap.height() - scaled.height()) / 2, scaled);
        return QIcon(pixmap);
    }

    if (tmpl.type == EntityType::Text)
    {
        QFont font = p.font();
        font.setPixelSize(22);
        font.setBold(true);
        p.setFont(font);
        p.setPen(QColor(220, 220, 228));
        p.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("T"));
    }
    else if (tmpl.type == EntityType::Vector)
    {
        p.setBrush(tmpl.data.value(QStringLiteral("fill")).value<QColor>());
        p.setPen(Qt::NoPen);
        const QRectF rect(8, 8, 24, 24);
        if (tmpl.data.value(QStringLiteral("shape"), 0).toInt() == 1)
            p.drawEllipse(rect);
        else
            p.drawRect(rect);
    }
    else if (tmpl.type == EntityType::Audio)
    {
        p.setPen(QColor(160, 200, 240));
        QFont font = p.font();
        font.setPixelSize(22);
        p.setFont(font);
        p.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("♪"));
    }
    else if (tmpl.type == EntityType::ComicText)
    {
        p.setPen(QPen(QColor(240, 240, 245), 2));
        p.setBrush(QColor(255, 255, 255));
        p.drawRoundedRect(6, 10, 28, 18, 6, 6);
        QPolygonF spike;
        spike << QPointF(10, 26) << QPointF(16, 36) << QPointF(20, 26);
        p.drawPolygon(spike);
    }
    else if (tmpl.type == EntityType::RadialLines)
    {
        p.setPen(QPen(QColor(240, 240, 245), 2, Qt::SolidLine, Qt::RoundCap));
        for (int index = 0; index < 8; ++index)
        {
            const double angle = index * M_PI / 4.0;
            p.drawLine(QPointF(20 + 6 * std::cos(angle), 20 + 6 * std::sin(angle)),
                       QPointF(20 + 16 * std::cos(angle), 20 + 16 * std::sin(angle)));
        }
    }
    else if (tmpl.type == EntityType::FlowLines)
    {
        p.setPen(QPen(QColor(240, 240, 245), 2, Qt::SolidLine, Qt::RoundCap));
        for (int index = 0; index < 4; ++index)
        {
            const double offset = 8 + index * 8;
            p.drawLine(QPointF(4, offset + 12), QPointF(36, offset - 4));
        }
    }
    else if (tmpl.type == EntityType::BrandWall)
    {
        p.setPen(Qt::NoPen);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 2; ++column)
            {
                p.setBrush((row + column) % 2 ? QColor(120, 130, 200)
                                              : QColor(220, 220, 230));
                p.drawRect(QRectF(4 + column * 16, 4 + row * 16, 16, 16));
            }
    }
    else if (tmpl.type == EntityType::HalftoneDots)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(220, 220, 228));
        for (int row = 0; row < 5; ++row)
            for (int column = 0; column < 5; ++column)
            {
                const double radius =
                    1.2 + 2.2 * (1.0 - (row + column) / 8.0);
                p.drawEllipse(
                    QPointF(8 + column * 6, 8 + row * 6), radius, radius);
            }
    }
    return QIcon(pixmap);
}
#endif // PVFORGE_LEGACY_SHELL

// The list must provide a custom MIME payload so the timeline can tell which
// template (or HTML asset) was dragged, instead of receiving plain text.
class AssetListWidget : public QListWidget
{
public:
    explicit AssetListWidget(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
    }

    std::function<QMimeData*(const QString&)> sessionMimeFactory;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
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
};

} // namespace

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

#if PVFORGE_LEGACY_SHELL
AssetLibraryPanel::AssetLibraryPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    buildUi();
    connect(m_project, &Project::changed, this, &AssetLibraryPanel::refresh);
    refresh();
}
#endif

AssetLibraryPanel::AssetLibraryPanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_sessionMode(true)
{
    buildUi();
    buildSessionUi();
    if (m_session && m_session->document())
    {
        connect(m_session->document(), &next::Document::changed, this,
                &AssetLibraryPanel::refresh);
    }
    refresh();
    enqueueMissingVideoExtractions();
}

AssetLibraryPanel::~AssetLibraryPanel()
{
    if (m_extractionQueue)
        m_extractionQueue->cancelAll();
}

void AssetLibraryPanel::setCompactMode(bool compact)
{
    m_compact = compact;
    if (!m_layout)
        return;

    m_layout->setContentsMargins(compact ? 4 : 10, compact ? 4 : 10,
                                 compact ? 4 : 10, compact ? 4 : 10);
    m_layout->setSpacing(compact ? 4 : 8);
    if (m_header)
        m_header->setVisible(!compact);
    // The preview was already hidden; the info line is the long path/hash
    // text that would eat the timeline height.
    if (m_info)
        m_info->setVisible(!compact);
    if (m_list)
    {
        m_list->setMinimumHeight(compact ? 48 : 140);
        m_list->setIconSize(compact ? QSize(30, 30) : QSize(40, 40));
    }
    setMinimumWidth(compact ? 0 : minimumWidth());
}

void AssetLibraryPanel::buildUi()
{
    setObjectName(QStringLiteral("assetPanel"));
    m_layout = new QVBoxLayout(this);
    auto* layout = m_layout;
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // Folder-like header: current location plus list/icon switch. Compact
    // mode hides this whole row to keep the timeline asset bin shallow.
    m_header = new QWidget(this);
    auto* pathRow = new QHBoxLayout(m_header);
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->setSpacing(6);
    auto* folderIcon = new QLabel(m_header);
    folderIcon->setPixmap(style()->standardIcon(QStyle::SP_DirIcon).pixmap(16, 16));
    pathRow->addWidget(folderIcon);
    auto* pathLabel = new QLabel(QStringLiteral("全部素材"), m_header);
    QFont pathFont = pathLabel->font();
    pathFont.setBold(true);
    pathLabel->setFont(pathFont);
    pathRow->addWidget(pathLabel);
    pathRow->addStretch(1);
    m_viewButton = new QToolButton(m_header);
    m_viewButton->setObjectName(QStringLiteral("assetViewToggle"));
    m_viewButton->setCheckable(true);
    m_viewButton->setText(QStringLiteral("▦"));
    m_viewButton->setToolTip(QStringLiteral("切换到图标视图"));
    m_viewButton->setFixedSize(26, 26);
    pathRow->addWidget(m_viewButton);
    layout->addWidget(m_header);
    connect(m_viewButton, &QToolButton::toggled, this,
            [this](bool iconView) { setIconView(iconView); });

    m_preview = new QLabel(QStringLiteral("素材库为空"), this);
    m_preview->setObjectName(QStringLiteral("assetPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(150);
    m_preview->setMinimumWidth(0);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_preview->setFrameShape(QFrame::StyledPanel);
    // The large preview is replaced by the hover popup; keeping it hidden
    // leaves more room for the folder list.
    m_preview->hide();

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("assetSearch"));
    m_search->setMinimumWidth(0);
    m_search->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_search->setPlaceholderText(
        Lang::t(QStringLiteral("search.assets"), QStringLiteral("搜索素材…")));
    m_search->setClearButtonEnabled(true);

    auto* searchRow = new QHBoxLayout();
    searchRow->addWidget(m_search, 1);
    auto* importButton = new QToolButton(this);
    importButton->setObjectName(QStringLiteral("assetImport"));
    importButton->setText(QStringLiteral("+"));
    importButton->setToolTip(QStringLiteral("导入素材文件（图片 / 视频 / 音频 / zip）"));
    importButton->setFixedSize(26, 26);
    searchRow->addWidget(importButton);
    auto* htmlImportButton = new QToolButton(this);
    htmlImportButton->setObjectName(QStringLiteral("assetImportHtml"));
    htmlImportButton->setText(QStringLiteral("H"));
    htmlImportButton->setToolTip(QStringLiteral("导入 .pvhtml 目录（zip 用 + 导入）"));
    htmlImportButton->setFixedSize(26, 26);
    searchRow->addWidget(htmlImportButton);
    layout->addLayout(searchRow);

    if (m_sessionMode)
    {
        auto* tagRow = new QHBoxLayout();
        tagRow->setSpacing(6);
        auto* tagLabel = new QLabel(QStringLiteral("标签"), this);
        tagLabel->setObjectName(QStringLiteral("assetTagLabel"));
        tagRow->addWidget(tagLabel);
        m_tagFilter = new QComboBox(this);
        m_tagFilter->setObjectName(QStringLiteral("assetTagFilter"));
        m_tagFilter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_tagFilter->addItem(QStringLiteral("全部标签"), QString());
        tagRow->addWidget(m_tagFilter, 1);
        layout->addLayout(tagRow);
        connect(m_tagFilter, &QComboBox::currentIndexChanged, this,
                [this](int) { applySessionFilter(); });
        connect(importButton, &QToolButton::clicked, this,
                &AssetLibraryPanel::chooseSessionImportFiles);
        connect(htmlImportButton, &QToolButton::clicked, this,
                &AssetLibraryPanel::chooseSessionImportHtml);
    }
#if PVFORGE_LEGACY_SHELL
    else
    {
        connect(htmlImportButton, &QToolButton::clicked, this,
                &AssetLibraryPanel::importHtmlAsset);
        connect(importButton, &QToolButton::clicked, this, [this] {
            const QStringList files = QFileDialog::getOpenFileNames(
                this, QStringLiteral("导入素材"), QString(),
                QStringLiteral("所有文件 (*)"));
            if (files.isEmpty())
                return;

            auto* mainWindow = qobject_cast<MainWindow*>(window());
            if (!mainWindow)
                return;

            mainWindow->importMediaFiles(
                files, [this](const QVector<MediaImportResult>& results) {
                    int imported = 0;
                    QStringList failed;
                    for (const MediaImportResult& result : results)
                    {
                        if (!result.error.isEmpty()
                            || result.importedPath.isEmpty())
                        {
                            failed << QStringLiteral("%1（%2）")
                                          .arg(QFileInfo(result.sourcePath)
                                                   .fileName(),
                                               result.error);
                            continue;
                        }
                        if (m_project->addImportedMedia(result))
                            ++imported;
                    }

                    if (!failed.isEmpty())
                    {
                        QMessageBox::warning(
                            this, QStringLiteral("部分文件未导入"),
                            QStringLiteral("已导入 %1 个素材。\n\n以下文件被跳过：\n%2")
                                .arg(imported)
                                .arg(failed.join(QStringLiteral("\n"))));
                    }
                });
        });
    }
#endif // PVFORGE_LEGACY_SHELL

    auto* list = new AssetListWidget(this);
    m_list = list;
    list->sessionMimeFactory = [this](const QString& hash) {
        return assetMimeData(hash);
    };
    m_list->setObjectName(QStringLiteral("assetList"));
    m_list->setIconSize(QSize(40, 40));
    m_list->setAlternatingRowColors(true);
    m_list->setMinimumHeight(140);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setWordWrap(false);
    m_list->setMouseTracking(true);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setDefaultDropAction(Qt::CopyAction);
    m_list->viewport()->installEventFilter(this);
    connect(m_list, &QListWidget::itemEntered, this,
            &AssetLibraryPanel::showHoverPreview);
    if (m_sessionMode)
    {
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_list, &QListWidget::customContextMenuRequested, this,
                &AssetLibraryPanel::showSessionContextMenu);
    }
    layout->addWidget(m_list, 1);

    if (m_sessionMode)
    {
        m_progressArea = new QWidget(this);
        m_progressArea->setObjectName(QStringLiteral("assetExtractionArea"));
        m_progressLayout = new QVBoxLayout(m_progressArea);
        m_progressLayout->setContentsMargins(0, 0, 0, 0);
        m_progressLayout->setSpacing(3);
        m_progressArea->hide();
        layout->addWidget(m_progressArea);
    }

    // Legacy visible workflow: 创建实体 / 删除. Dragging an asset to the
    // timeline remains the second entry point.
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_create = new QPushButton(QStringLiteral("创建实体"), this);
    m_create->setObjectName(QStringLiteral("assetCreate"));
    m_create->setEnabled(false);
    buttons->addWidget(m_create, 1);
    m_remove = new QPushButton(QStringLiteral("删除"), this);
    m_remove->setObjectName(QStringLiteral("assetRemove"));
    if (m_sessionMode)
    {
        m_remove->setToolTip(
            QStringLiteral("只删除素材库条目，引用它的实体显示“素材缺失”"));
        connect(m_create, &QPushButton::clicked, this,
                &AssetLibraryPanel::createSelectedSession);
        connect(m_remove, &QPushButton::clicked, this,
                &AssetLibraryPanel::removeSelectedSession);
    }
#if PVFORGE_LEGACY_SHELL
    else
    {
        m_remove->setToolTip(
            QStringLiteral("只删除素材模板，不影响场景中已创建的实体"));
        connect(m_create, &QPushButton::clicked, this,
                &AssetLibraryPanel::createSelected);
        connect(m_remove, &QPushButton::clicked, this,
                &AssetLibraryPanel::removeSelected);
    }
#endif
    m_remove->setEnabled(false);
    buttons->addWidget(m_remove);
    layout->addLayout(buttons);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("assetInfo"));
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::PlainText);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setMinimumWidth(0);
    m_info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_info);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) {
        if (m_sessionMode)
            applySessionFilter();
#if PVFORGE_LEGACY_SHELL
        else
            rebuild();
#endif
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) {
        if (m_sessionMode)
            updateSessionSelection();
#if PVFORGE_LEGACY_SHELL
        else
            updatePreview();
#endif
    });
}

void AssetLibraryPanel::buildSessionUi()
{
    m_extractionQueue = new video2::VideoExtractionQueue(this);
    connect(m_extractionQueue, &video2::VideoExtractionQueue::started, this,
            [this](const QString& hash, int total) {
                ensureProgressRow(hash);
                updateProgressRow(hash, 0, total);
            });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::progress, this,
            [this](const QString& hash, int done, int total) {
                updateProgressRow(hash, done, total);
            });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::finished, this,
            [this](const QString& hash) { finishProgressRow(hash); });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::failed, this,
            [this](const QString& hash, const QString& error) {
                failProgressRow(hash, error);
            });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::cancelled, this,
            [this](const QString& hash) { removeProgressRow(hash); });
}

void AssetLibraryPanel::refresh()
{
    if (m_sessionMode)
    {
        rebuildSessionEntries();
        return;
    }
#if PVFORGE_LEGACY_SHELL
    QStringList signature;
    for (const Template& tmpl : m_project->templates())
        signature << QStringLiteral("%1|%2|%3").arg(int(tmpl.type)).arg(tmpl.name, tmpl.path);
    for (const QString& path : htmlAssetPaths(*m_project))
        signature << QStringLiteral("html|%1").arg(path);

    if (signature == m_signature)
    {
        updatePreview();
        return;
    }

    m_signature = signature;
    rebuild();
#endif
}

void AssetLibraryPanel::rebuildSessionEntries()
{
    m_entries.clear();
    if (!m_session)
    {
        applySessionFilter();
        return;
    }

    QSet<AssetHash> seen;
    auto addStored = [this, &seen](const assets::StoredAsset& asset) {
        if (asset.hash.isEmpty() || seen.contains(asset.hash))
            return;
        seen.insert(asset.hash);

        SessionEntry entry;
        entry.hash = asset.hash;
        entry.name = asset.name.isEmpty() ? shortHash(asset.hash) : asset.name;
        entry.directory = asset.directory;
        entry.type = asset.type;
        entry.tags = asset.tags;
        entry.hasAudio =
            !asset.metadata.value(QStringLiteral("audioHash")).toString().isEmpty();
        entry.width = asset.metadata.value(QStringLiteral("width")).toInt(0);
        entry.height = asset.metadata.value(QStringLiteral("height")).toInt(0);
        entry.seconds =
            asset.metadata.value(QStringLiteral("durationSeconds")).toDouble(0.0);
        entry.frames =
            asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
        entry.missing = assets::missingContent(asset);
        if (entry.missing)
            entry.missingReason = QStringLiteral("内容缺失");

        const QStringList contentFiles = QDir(asset.directory).entryList(
            {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
        if (!contentFiles.isEmpty())
            entry.content = asset.directory + QLatin1Char('/') + contentFiles.first();
        m_entries.append(entry);
    };

    for (const assets::StoredAsset& asset : m_session->assets().listIncludingMissing())
        addStored(asset);

    // Entities keep their hash after the store entry is gone. Keep one
    // offline row per referenced hash so re-importing the same content
    // restores it in place.
    QSet<AssetHash> missingSeen;
    for (const model::Entity& entity : m_session->project().entities)
    {
        if (entity.content.kind != ContentKind::Asset)
            continue;
        const AssetHash& hash = entity.content.asset.hash;
        if (hash.isEmpty() || seen.contains(hash) || missingSeen.contains(hash))
            continue;
        missingSeen.insert(hash);
        SessionEntry entry;
        entry.hash = hash;
        entry.name = shortHash(hash);
        entry.type = entity.content.asset.type;
        entry.missing = true;
        entry.missingReason = QStringLiteral("目录缺失");
        m_entries.append(entry);
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const SessionEntry& a, const SessionEntry& b) {
                  if (a.missing != b.missing)
                      return !a.missing;
                  const int byName =
                      QString::compare(a.name, b.name, Qt::CaseInsensitive);
                  if (byName != 0)
                      return byName < 0;
                  return a.hash < b.hash;
              });

    QStringList signature;
    for (const SessionEntry& entry : m_entries)
        signature << QStringLiteral("%1|%2|%3|%4|%5")
                         .arg(entry.hash)
                         .arg(entry.name)
                         .arg(entry.tags.join(QLatin1Char(',')))
                         .arg(entry.missingReason)
                         .arg(entry.missing ? 1 : 0);
    const bool changed = signature != m_sessionSignature;
    m_sessionSignature = signature;

    if (changed)
        rebuildSessionTagFilter();
    applySessionFilter();
}

void AssetLibraryPanel::rebuildSessionTagFilter()
{
    if (!m_tagFilter)
        return;

    const QString current = m_tagFilter->currentData().toString();
    QStringList tags;
    for (const SessionEntry& entry : m_entries)
        for (const QString& tag : entry.tags)
        {
            const bool exists =
                std::any_of(tags.cbegin(), tags.cend(), [&tag](const QString& seen) {
                    return QString::compare(seen, tag, Qt::CaseInsensitive) == 0;
                });
            if (!exists)
                tags.append(tag);
        }
    std::sort(tags.begin(), tags.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    const QSignalBlocker blocker(m_tagFilter);
    m_tagFilter->clear();
    m_tagFilter->addItem(QStringLiteral("全部标签"), QString());
    for (const QString& tag : tags)
        m_tagFilter->addItem(tag, tag);
    const int index = current.isEmpty() ? 0 : m_tagFilter->findData(current);
    m_tagFilter->setCurrentIndex(index > 0 ? index : 0);
}

void AssetLibraryPanel::applySessionFilter()
{
    if (!m_list)
        return;

    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const QString tagFilter =
        m_tagFilter ? m_tagFilter->currentData().toString() : QString();
    const QString selected = selectedHash();

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const SessionEntry& entry : m_entries)
    {
        if (!filter.isEmpty())
        {
            const bool matchesName =
                entry.name.contains(filter, Qt::CaseInsensitive);
            const bool matchesHash =
                entry.hash.contains(filter, Qt::CaseInsensitive);
            const bool matchesTag = std::any_of(
                entry.tags.cbegin(), entry.tags.cend(), [&filter](const QString& tag) {
                    return tag.contains(filter, Qt::CaseInsensitive);
                });
            if (!matchesName && !matchesHash && !matchesTag)
                continue;
        }
        if (!tagFilter.isEmpty()
            && std::none_of(entry.tags.cbegin(), entry.tags.cend(),
                            [&tagFilter](const QString& tag) {
                                return QString::compare(tag, tagFilter,
                                                        Qt::CaseInsensitive)
                                       == 0;
                            }))
            continue;
        m_list->addItem(makeSessionItem(entry));
    }

    int restore = m_list->count() > 0 ? 0 : -1;
    if (!selected.isEmpty())
    {
        for (int row = 0; row < m_list->count(); ++row)
            if (m_list->item(row)->data(Qt::UserRole).toString() == selected)
            {
                restore = row;
                break;
            }
    }
    m_list->setCurrentRow(restore);
    updateSessionSelection();
}

QListWidgetItem* AssetLibraryPanel::makeSessionItem(const SessionEntry& entry)
{
    auto* item = new QListWidgetItem;
    item->setIcon(QIcon(iconFor(entry)));
    item->setSizeHint(QSize(0, 52));

    const QString firstLine =
        entry.missing ? QStringLiteral("%1（素材缺失）").arg(entry.name) : entry.name;
    QString second = assetTypeLabel(entry.type);
    if (entry.type == AssetType::Image || entry.type == AssetType::Html)
    {
        if (entry.width > 0 && entry.height > 0)
            second += QStringLiteral(" · %1×%2").arg(entry.width).arg(entry.height);
    }
    else if (entry.type == AssetType::Video)
    {
        if (entry.width > 0 && entry.height > 0)
            second += QStringLiteral(" · %1×%2").arg(entry.width).arg(entry.height);
        if (entry.seconds > 0.0)
            second += QStringLiteral(" · %1 秒").arg(entry.seconds, 0, 'f', 2);
    }
    else if (entry.type == AssetType::Audio && entry.seconds > 0.0)
    {
        second += QStringLiteral(" · %1 秒").arg(entry.seconds, 0, 'f', 2);
    }
    if (entry.hasAudio)
        second += QStringLiteral(" · 含音频");
    if (!entry.tags.isEmpty())
    {
        QStringList decorated;
        for (const QString& tag : entry.tags)
            decorated.append(QLatin1Char('#') + tag);
        second += QStringLiteral(" · ") + decorated.join(QStringLiteral(", "));
    }
    item->setText(firstLine + QLatin1Char('\n') + second);

    item->setData(Qt::UserRole, entry.hash);
    item->setData(Qt::UserRole + 1, 2);
    item->setData(Qt::UserRole + 2, entry.missingReason);
    if (entry.missing)
        item->setForeground(QColor(QStringLiteral("#ff9a9a")));

    QString tooltip = QStringLiteral("名称：%1\n类型：%2\n哈希：%3")
                          .arg(entry.name, assetTypeLabel(entry.type), entry.hash);
    if (entry.frames > 0)
        tooltip += QStringLiteral("\n帧数：%1").arg(entry.frames);
    tooltip += QStringLiteral("\n标签：%1").arg(
        entry.tags.isEmpty() ? QStringLiteral("（无）")
                             : entry.tags.join(QStringLiteral(", ")));
    if (!entry.directory.isEmpty())
        tooltip += QStringLiteral("\n路径：%1").arg(entry.directory);
    if (entry.missing)
        tooltip += QStringLiteral("\n\n%1：实体仍引用该哈希。重新导入相同内容即可恢复。")
                       .arg(entry.missingReason);
    else if (entry.hasAudio)
        tooltip += QStringLiteral("\n\n已拆出 WAV 音频素材。");
    item->setToolTip(tooltip);
    return item;
}

void AssetLibraryPanel::updateSessionSelection()
{
    const QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    const QString hash = item ? item->data(Qt::UserRole).toString() : QString();
    const bool missing =
        item && !item->data(Qt::UserRole + 2).toString().isEmpty();
    if (m_create)
        m_create->setEnabled(item && !missing);
    if (m_remove)
        m_remove->setEnabled(
            item && m_session
            && m_session->assets().getIncludingMissing(hash).has_value());
    if (!item)
    {
        if (m_info)
        {
            m_info->setText(m_entries.isEmpty() ? QStringLiteral("素材库为空")
                                                : QStringLiteral("没有匹配的素材"));
            m_info->setToolTip(QString());
        }
        return;
    }

    const auto found = std::find_if(
        m_entries.cbegin(), m_entries.cend(),
        [&hash](const SessionEntry& entry) { return entry.hash == hash; });
    if (found == m_entries.cend())
        return;

    QString info = assetTypeLabel(found->type);
    if (found->width > 0 && found->height > 0)
        info += QStringLiteral(" · %1×%2").arg(found->width).arg(found->height);
    if (found->seconds > 0.0)
        info += QStringLiteral(" · %1 秒").arg(found->seconds, 0, 'f', 2);
    if (found->missing)
        info += QStringLiteral(" · %1").arg(found->missingReason);
    if (!found->tags.isEmpty())
        info += QStringLiteral("\n标签：%1").arg(found->tags.join(QStringLiteral(", ")));
    if (!found->directory.isEmpty())
        info += QStringLiteral("\n") + elidedPath(found->directory);
    info += QStringLiteral("\n") + found->hash;
    if (m_info)
    {
        m_info->setText(info);
        m_info->setToolTip(found->directory.isEmpty() ? found->hash
                                                      : found->directory);
    }
}

QPixmap AssetLibraryPanel::iconFor(const SessionEntry& entry)
{
    const QString key = QStringLiteral("%1|%2").arg(entry.hash, entry.missingReason);
    if (const auto cached = m_iconCache.constFind(key); cached != m_iconCache.cend())
        return *cached;

    const QSize size(48, 48);
    QPixmap pixmap;
    if (entry.missing)
    {
        pixmap = checkerPixmap(size, QStringLiteral("缺失"),
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
                pixmap = checkerPixmap(size, QStringLiteral("坏图"),
                                       QColor(QStringLiteral("#ff6b6b")));
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
        return checkerPixmap(size, QStringLiteral("素材缺失"),
                             QColor(QStringLiteral("#ff6b6b")));

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
        return htmlPlaceholder(size, QStringLiteral("HTML"));
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

QString AssetLibraryPanel::selectedHash() const
{
    if (!m_list)
        return {};
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

int AssetLibraryPanel::assetCount() const
{
    return m_list ? m_list->count() : 0;
}

QString AssetLibraryPanel::assetNameAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    const QString text = m_list->item(index)->text();
    return text.section(QLatin1Char('\n'), 0, 0);
}

QString AssetLibraryPanel::assetHashAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(Qt::UserRole).toString();
}

QStringList AssetLibraryPanel::assetTagsAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    const QString hash = m_list->item(index)->data(Qt::UserRole).toString();
    for (const SessionEntry& entry : m_entries)
        if (entry.hash == hash)
            return entry.tags;
    return {};
}

bool AssetLibraryPanel::assetMissingAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return false;
    return !m_list->item(index)->data(Qt::UserRole + 2).toString().isEmpty();
}

QString AssetLibraryPanel::assetMissingReasonAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(Qt::UserRole + 2).toString();
}

QPixmap AssetLibraryPanel::assetThumbnailAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->icon().pixmap(48, 48);
}

void AssetLibraryPanel::setSearchText(const QString& text)
{
    if (!m_search)
        return;
    m_search->setText(text);
}

QString AssetLibraryPanel::searchText() const
{
    return m_search ? m_search->text() : QString();
}

QStringList AssetLibraryPanel::tagFilters() const
{
    QStringList tags;
    if (!m_tagFilter)
        return tags;
    for (int index = 1; index < m_tagFilter->count(); ++index)
        tags.append(m_tagFilter->itemData(index).toString());
    return tags;
}

void AssetLibraryPanel::setTagFilter(const QString& tag)
{
    if (!m_tagFilter)
        return;
    const int index = m_tagFilter->findData(tag);
    m_tagFilter->setCurrentIndex(index > 0 ? index : 0);
}

QString AssetLibraryPanel::tagFilter() const
{
    return m_tagFilter ? m_tagFilter->currentData().toString() : QString();
}

bool AssetLibraryPanel::importPaths(const QStringList& paths, QString* error)
{
    if (error)
        error->clear();
    if (!m_session)
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }

    QStringList imported;
    QStringList failures;
    QStringList warnings;
    bool audioSplit = false;

    for (const QString& rawPath : paths)
    {
        const QFileInfo info(rawPath);
        if (!info.exists())
        {
            failures << QStringLiteral("%1：文件不存在").arg(rawPath);
            continue;
        }

        const QString path = info.absoluteFilePath();
        const auto type = assets::detectType(path);
        QString importError;
        std::optional<assets::StoredAsset> stored;

        const bool isBundle =
            info.isDir()
            || info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0
            || (type && *type == AssetType::Html);
        if (isBundle)
        {
            stored = m_session->assets().importPvhtml(path, &importError);
        }
        else if (!type)
        {
            failures << QStringLiteral("%1：不支持的素材类型").arg(info.fileName());
            continue;
        }
        else if (*type == AssetType::Video)
        {
            const auto video =
                m_session->assets().importVideoWithAudio(path, &importError);
            if (video)
            {
                stored = video->video;
                if (video->audio)
                    audioSplit = true;
                if (!video->audioError.isEmpty())
                    warnings << QStringLiteral("%1：音频拆分失败（%2）")
                                    .arg(info.fileName(), video->audioError);
            }
        }
        else
        {
            stored = m_session->assets().importFile(path, &importError);
        }

        if (!stored)
        {
            failures << QStringLiteral("%1：%2")
                            .arg(info.fileName(),
                                 importError.isEmpty() ? QStringLiteral("导入失败")
                                                       : importError);
            continue;
        }
        imported.append(stored->name);
        if (stored->type == AssetType::Video)
            enqueueVideoExtraction(*stored);
    }

    if (!imported.isEmpty())
    {
        refresh();
        QString message = imported.size() == 1
                              ? QStringLiteral("pvf: import asset \"%1\"")
                                    .arg(imported.first())
                              : QStringLiteral("pvf: import assets \"%1\"")
                                    .arg(imported.join(QStringLiteral("\", \"")));
        QString commitError;
        if (!m_session->document()->commitProject(message, &commitError))
            failures << QStringLiteral("提交失败：%1").arg(commitError);
        emit assetsChanged();

        QString status = imported.size() == 1
                             ? QStringLiteral("已导入素材 \"%1\"").arg(imported.first())
                             : QStringLiteral("已导入 %1 个素材").arg(imported.size());
        if (audioSplit)
            status += QStringLiteral("，已拆出 WAV 音频素材");
        if (!warnings.isEmpty())
            status += QStringLiteral("；") + warnings.join(QStringLiteral("；"));
        if (failures.isEmpty())
            setStatus(status);
    }

    if (!failures.isEmpty())
    {
        const QString text = failures.join(QLatin1Char('\n'));
        if (error)
            *error = text;
        setStatus(text);
        return false;
    }
    return true;
}

bool AssetLibraryPanel::renameAsset(const AssetHash& hash, const QString& name,
                                    QString* error)
{
    if (!m_session)
        return false;
    QString storeError;
    if (!m_session->assets().rename(hash, name, &storeError))
    {
        if (error)
            *error = storeError;
        setStatus(storeError);
        return false;
    }
    const QString trimmed = name.trimmed();
    m_session->document()->commitProject(
        QStringLiteral("pvf: rename asset \"%1\"").arg(trimmed), nullptr);
    refresh();
    emit assetsChanged();
    setStatus(QStringLiteral("已重命名素材为 \"%1\"").arg(trimmed));
    return true;
}

bool AssetLibraryPanel::setAssetTags(const AssetHash& hash,
                                     const QStringList& tags, QString* error)
{
    if (!m_session)
        return false;
    QString storeError;
    if (!m_session->assets().setTags(hash, tags, &storeError))
    {
        if (error)
            *error = storeError;
        setStatus(storeError);
        return false;
    }
    m_session->document()->commitProject(
        QStringLiteral("pvf: edit asset tags \"%1\"").arg(hash.left(8)), nullptr);
    refresh();
    emit assetsChanged();
    setStatus(QStringLiteral("已更新素材标签"));
    return true;
}

bool AssetLibraryPanel::deleteAsset(const AssetHash& hash, QString* error)
{
    if (!m_session)
        return false;
    QString name = shortHash(hash);
    if (const auto asset = m_session->assets().getIncludingMissing(hash))
        name = asset->name;
    QString storeError;
    if (!m_session->assets().remove(hash, &storeError))
    {
        if (error)
            *error = storeError;
        setStatus(storeError);
        return false;
    }
    m_session->document()->commitProject(
        QStringLiteral("pvf: delete asset \"%1\"").arg(name), nullptr);
    refresh();
    emit assetsChanged();
    setStatus(QStringLiteral("已删除素材 \"%1\"；场景实体保留并显示素材缺失")
                  .arg(name));
    return true;
}

bool AssetLibraryPanel::reimportMissing(const AssetHash& hash, const QString& path,
                                        QString* error)
{
    if (!m_session)
        return false;
    QString importError;
    if (!importPaths({path}, &importError))
    {
        if (error)
            *error = importError;
        return false;
    }
    if (!m_session->assets().get(hash))
    {
        const QString message =
            QStringLiteral("导入的内容与缺失素材不同（期望 %1）").arg(hash.left(8));
        if (error)
            *error = message;
        setStatus(message);
        return false;
    }
    setStatus(QStringLiteral("已恢复素材 %1").arg(hash.left(8)));
    return true;
}

bool AssetLibraryPanel::replaceSelectedEntityWith(const AssetHash& hash,
                                                  QString* error)
{
    if (!m_session || !m_session->document())
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }
    const Uuid selected = m_session->document()->selection();
    if (selected.isEmpty())
    {
        if (error)
            *error = QStringLiteral("未选择实体");
        return false;
    }
    if (!m_session->editActions()->replaceAsset(selected, hash, error))
        return false;
    setStatus(QStringLiteral("已更换素材"));
    emit assetsChanged();
    return true;
}

QMimeData* AssetLibraryPanel::assetMimeData(const AssetHash& hash) const
{
    if (!m_session || hash.isEmpty() || !m_session->assets().get(hash))
        return nullptr;
    auto* data = new QMimeData;
    data->setData(QStringLiteral("application/x-pvforge-asset"), hash.toUtf8());
    data->setText(hash);
    return data;
}

void AssetLibraryPanel::setStatus(const QString& text)
{
    emit statusMessage(text);
}

void AssetLibraryPanel::chooseSessionImportFiles()
{
    const QString filter = QStringLiteral(
        "支持的素材 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg "
        "*.avif *.heic *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg *.wav "
        "*.mp3 *.ogg *.flac *.aac *.m4a *.opus *.zip);;"
        "图片 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg *.avif *.heic);;"
        "视频 (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg);;"
        "音频 (*.wav *.mp3 *.ogg *.flac *.aac *.m4a *.opus);;"
        "PVForge HTML (*.zip);;所有文件 (*)");
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("导入素材"), m_lastImportDir, filter);
    if (paths.isEmpty())
        return;
    m_lastImportDir = QFileInfo(paths.first()).absolutePath();
    QString error;
    if (!importPaths(paths, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetLibraryPanel::chooseSessionImportHtml()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 .pvhtml 目录"), m_lastImportDir,
        QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;
    m_lastImportDir = QFileInfo(directory).absolutePath();
    QString error;
    if (!importPaths({directory}, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetLibraryPanel::renameSessionSelected()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const auto asset = m_session ? m_session->assets().get(hash) : std::nullopt;
    if (!asset)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("重命名素材"), QStringLiteral("素材名称："),
        QLineEdit::Normal, asset->name, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == asset->name)
        return;
    QString error;
    if (!renameAsset(hash, name, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("重命名失败"), error);
}

void AssetLibraryPanel::editSessionTags()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const auto asset = m_session ? m_session->assets().get(hash) : std::nullopt;
    if (!asset)
        return;
    bool ok = false;
    QString text = QInputDialog::getText(
        this, QStringLiteral("编辑标签"),
        QStringLiteral("标签（用逗号分隔，最多 32 个）："), QLineEdit::Normal,
        asset->tags.join(QStringLiteral(", ")), &ok);
    if (!ok)
        return;
    text.replace(QChar(0xFF0C), QLatin1Char(','));
    QStringList tags;
    for (const QString& part : text.split(QLatin1Char(',')))
    {
        const QString tag = part.trimmed();
        if (!tag.isEmpty())
            tags.append(tag);
    }
    QString error;
    if (!setAssetTags(hash, tags, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("编辑标签失败"), error);
}

void AssetLibraryPanel::deleteSessionSelected()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const auto asset = m_session ? m_session->assets().get(hash) : std::nullopt;
    const QString name = asset ? asset->name : shortHash(hash);
    const int refs = referenceCount(hash);
    QString text = QStringLiteral("确定删除素材 \"%1\"？").arg(name);
    if (refs > 0)
        text += QStringLiteral("\n\n%1 个实体正在引用它。删除后实体会显示“素材缺失”，"
                               "重新导入相同内容即可恢复。")
                    .arg(refs);
    if (QMessageBox::question(this, QStringLiteral("删除素材"), text,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes)
        return;
    QString error;
    if (!deleteAsset(hash, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("删除素材"), error);
}

void AssetLibraryPanel::reimportSessionSelected()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("重新导入缺失素材"), m_lastImportDir,
        QStringLiteral("所有文件 (*)"));
    if (path.isEmpty())
        return;
    m_lastImportDir = QFileInfo(path).absolutePath();
    QString error;
    if (!reimportMissing(hash, path, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("恢复素材失败"), error);
}

void AssetLibraryPanel::replaceSelectedEntityFromMenu()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    QString error;
    if (!replaceSelectedEntityWith(hash, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetLibraryPanel::showSessionContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_list ? m_list->itemAt(pos) : nullptr;
    if (!item)
        return;
    m_list->setCurrentItem(item);
    const bool missing = !item->data(Qt::UserRole + 2).toString().isEmpty();
    const QString hash = item->data(Qt::UserRole).toString();

    bool canReplace = false;
    if (m_session && m_session->document())
    {
        const Uuid selected = m_session->document()->selection();
        const auto it = m_session->project().entities.constFind(selected);
        if (it != m_session->project().entities.constEnd())
        {
            AssetType current = AssetType::Image;
            if (it->content.kind == ContentKind::Asset)
                current = it->content.asset.type;
            if (assetCompatible(it->content.kind, current, entryTypeForHash(hash)))
                canReplace = !missing;
        }
    }

    QMenu menu(this);
    QAction* renameAction = menu.addAction(QStringLiteral("重命名…"));
    QAction* tagsAction = menu.addAction(QStringLiteral("编辑标签…"));
    QAction* deleteAction = menu.addAction(QStringLiteral("删除…"));
    QAction* replaceAction = menu.addAction(QStringLiteral("替换选中实体…"));
    QAction* reimportAction = menu.addAction(QStringLiteral("重新导入…"));
    const bool hasStoreEntry =
        m_session && m_session->assets().getIncludingMissing(hash).has_value();
    renameAction->setEnabled(!missing);
    tagsAction->setEnabled(!missing);
    deleteAction->setEnabled(!missing || hasStoreEntry);
    replaceAction->setEnabled(canReplace);
    reimportAction->setVisible(missing);

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == renameAction)
        renameSessionSelected();
    else if (chosen == tagsAction)
        editSessionTags();
    else if (chosen == deleteAction)
        deleteSessionSelected();
    else if (chosen == replaceAction)
        replaceSelectedEntityFromMenu();
    else if (chosen == reimportAction)
        reimportSessionSelected();
}

AssetType AssetLibraryPanel::entryTypeForHash(const AssetHash& hash) const
{
    for (const SessionEntry& entry : m_entries)
        if (entry.hash == hash)
            return entry.type;
    return AssetType::Image;
}

int AssetLibraryPanel::referenceCount(const AssetHash& hash) const
{
    if (!m_session)
        return 0;
    int count = 0;
    for (const model::Entity& entity : m_session->project().entities)
        if (entity.content.kind == ContentKind::Asset
            && entity.content.asset.hash == hash)
            ++count;
    return count;
}

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

QString AssetLibraryPanel::displayNameFor(const AssetHash& hash) const
{
    if (m_session)
        if (const auto asset = m_session->assets().getIncludingMissing(hash))
            return asset->name.isEmpty() ? shortHash(hash) : asset->name;
    return shortHash(hash);
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
#if PVFORGE_LEGACY_SHELL
    else if (item->data(Qt::UserRole + 1).toInt() == 0)
    {
        const int index = item->data(Qt::UserRole).toInt();
        const auto& templates = m_project->templates();
        if (index >= 0 && index < templates.size())
            preview = renderTemplate(templates[index], QSize(240, 150));
    }
    else
    {
        const QString path = item->data(Qt::UserRole + 2).toString();
        preview = QPixmap::fromImage(
            htmlAssetThumbnail(*m_project, path, QSize(240, 150)));
    }
#endif
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

#if PVFORGE_LEGACY_SHELL
void AssetLibraryPanel::rebuild()
{
    hideHoverPreview();
    const int previousTemplate = selectedIndex();
    const QString previousHtml = selectedHtmlAsset();
    const QSignalBlocker blocker(m_list);
    m_list->clear();

    const QString filter = m_search->text().trimmed();
    const auto& templates = m_project->templates();
    for (int i = 0; i < templates.size(); ++i)
    {
        const Template& tmpl = templates[i];
        if (!filter.isEmpty() && !tmpl.name.contains(filter, Qt::CaseInsensitive))
            continue;
        auto* item = new QListWidgetItem(tmpl.name, m_list);
        item->setData(Qt::UserRole, i);
        item->setData(Qt::UserRole + 1, 0);
        item->setIcon(listIcon(tmpl));
    }

    // TODO(M6): drag-to-create from the library; the HTML list currently
    // creates a layer with the button or a double click.
    const QStringList htmlPaths = htmlAssetPaths(*m_project);
    for (const QString& path : htmlPaths)
    {
        const QString label = htmlAssetLabel(path);
        if (!filter.isEmpty() && !label.contains(filter, Qt::CaseInsensitive))
            continue;
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, -1);
        item->setData(Qt::UserRole + 1, 1);
        item->setData(Qt::UserRole + 2, path);
        const QImage image = htmlAssetThumbnail(*m_project, path, QSize(40, 40));
        item->setIcon(image.isNull() ? QIcon(htmlPlaceholder(QSize(40, 40), QStringLiteral("H")))
                                     : QIcon(QPixmap::fromImage(image)));
    }

    int restore = m_list->count() > 0 ? 0 : -1;
    for (int row = 0; row < m_list->count(); ++row)
    {
        const QListWidgetItem* item = m_list->item(row);
        if (item->data(Qt::UserRole + 1).toInt() == 0
            && item->data(Qt::UserRole).toInt() == previousTemplate)
            restore = row;
        else if (item->data(Qt::UserRole + 1).toInt() == 1
                 && item->data(Qt::UserRole + 2).toString() == previousHtml)
            restore = row;
    }
    m_list->setCurrentRow(restore);

    updatePreview();
}

int AssetLibraryPanel::selectedIndex() const
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item || item->data(Qt::UserRole + 1).toInt() != 0)
        return -1;
    return item->data(Qt::UserRole).toInt();
}

QString AssetLibraryPanel::selectedHtmlAsset() const
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item || item->data(Qt::UserRole + 1).toInt() != 1)
        return QString();
    return item->data(Qt::UserRole + 2).toString();
}

void AssetLibraryPanel::updatePreview()
{
    const int index = selectedIndex();
    const QString htmlPath = selectedHtmlAsset();
    const auto& templates = m_project->templates();
    const bool validTemplate = index >= 0 && index < templates.size();
    const bool validHtml = !htmlPath.isEmpty();

    m_create->setEnabled(validTemplate || validHtml);
    m_remove->setEnabled(validTemplate);

    if (!validTemplate && !validHtml)
    {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(QStringLiteral("素材库为空"));
        m_info->clear();
        m_info->setToolTip(QString());
        return;
    }

    if (validHtml)
    {
        QString error;
        const html::HtmlAsset asset = html::HtmlAsset::parseFile(htmlPath, &error);
        const QImage image = htmlAssetThumbnail(*m_project, htmlPath, QSize(240, 150));
        m_preview->setText(QString());
        if (!image.isNull())
            m_preview->setPixmap(QPixmap::fromImage(image));
        else
            m_preview->setPixmap(
                htmlPlaceholder(QSize(240, 150), QStringLiteral("HTML")));

        QString info = templateTypeName(EntityType::HtmlLayer);
        if (error.isEmpty())
            info += QStringLiteral(" · %1×%2 · %3 个变量")
                        .arg(asset.logicalSize.width())
                        .arg(asset.logicalSize.height())
                        .arg(asset.variableKeys().size());
        else
            info += QStringLiteral(" · 无法解析：%1").arg(error);
        info += QStringLiteral("\n") + elidedPath(htmlPath);
        m_info->setText(info);
        m_info->setToolTip(htmlPath);
        return;
    }

    const Template& tmpl = templates[index];
    m_preview->setText(QString());
    m_preview->setPixmap(renderTemplate(tmpl, QSize(240, 150)));

    QString info = templateTypeName(tmpl.type);
    if ((tmpl.type == EntityType::Video || tmpl.type == EntityType::Audio)
        && tmpl.duration > 0.0)
        info += QStringLiteral(" · %1 秒").arg(tmpl.duration, 0, 'f', 2);
    if (!tmpl.path.isEmpty())
        info += QStringLiteral("\n") + elidedPath(tmpl.path);
    m_info->setText(info);
    m_info->setToolTip(tmpl.path);
}

void AssetLibraryPanel::createSelected()
{
    const QString htmlPath = selectedHtmlAsset();
    if (!htmlPath.isEmpty())
    {
        Entity* entity = m_project->addHtmlLayer(htmlPath);
        if (!entity)
        {
            QMessageBox::warning(
                this, QStringLiteral("无法创建 HTML 层"),
                QStringLiteral("工程里最多只能有两个 HTML 层，或资产无法解析。"));
            return;
        }
        emit entityCreated(entity->id);
        return;
    }

    const int index = selectedIndex();
    const auto& templates = m_project->templates();
    if (index < 0 || index >= templates.size())
        return;

    if (Entity* e = m_project->createFromTemplate(templates[index]))
        emit entityCreated(e->id);
}

void AssetLibraryPanel::importHtmlAsset()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入 HTML 组件"), QString(),
        QStringLiteral("PVForge HTML (*.json);;所有文件 (*)"));
    if (file.isEmpty())
        return;

    QString error;
    const QString imported = m_project->importHtmlAsset(file, &error);
    if (imported.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("导入 HTML 失败"), error);
        return;
    }

    Entity* entity = m_project->addHtmlLayer(imported);
    if (!entity)
    {
        QMessageBox::warning(
            this, QStringLiteral("无法创建 HTML 层"),
            QStringLiteral("工程里最多只能有两个 HTML 层，或资产无法解析。"));
        return;
    }
    emit entityCreated(entity->id);
}

QVector<int> AssetLibraryPanel::mediaTemplateIndices(const Project& project,
                                                   EntityType type)
{
    QVector<int> indices;
    const auto& templates = project.templates();
    for (int index = 0; index < int(templates.size()); ++index)
        if (templates[index].type == type && !templates[index].path.isEmpty())
            indices.append(index);
    return indices;
}
#endif // PVFORGE_LEGACY_SHELL

QString AssetLibraryPanel::pickAsset(QWidget* parent, EditorSession* session,
                                     ContentKind contentKind, AssetType currentType,
                                     const AssetHash& currentHash)
{
    if (!session)
        return QString();

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("更换素材"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* hint = new QLabel(
        QStringLiteral("从素材库选择，或导入新文件。只显示与当前实体兼容的素材。"),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* list = new QListWidget(&dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(96, 72));
    list->setGridSize(QSize(136, 110));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMinimumSize(420, 260);

    auto addAsset = [&list, contentKind, currentType, currentHash](const assets::StoredAsset& asset) {
        if (!assetCompatible(contentKind, currentType, asset.type))
            return;
        QPixmap pixmap(96, 72);
        pixmap.fill(QColor(18, 20, 26));
        if (asset.type == AssetType::Image)
        {
            const QStringList files = QDir(asset.directory).entryList(
                {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
            if (!files.isEmpty())
            {
                QImageReader reader(asset.directory + QLatin1Char('/')
                                    + files.first());
                reader.setAutoTransform(true);
                const QImage image = reader.read();
                if (!image.isNull())
                {
                    const QImage scaled = image.scaled(
                        pixmap.size(), Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
                    QPainter painter(&pixmap);
                    painter.drawImage((pixmap.width() - scaled.width()) / 2,
                                      (pixmap.height() - scaled.height()) / 2,
                                      scaled);
                }
            }
        }
        else if (asset.type == AssetType::Video)
        {
            QPainter painter(&pixmap);
            painter.setPen(QPen(QColor(127, 209, 255), 2));
            painter.drawRect(QRect(24, 22, 48, 28));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(127, 209, 255));
            QPolygon play;
            play << QPoint(42, 28) << QPoint(42, 44) << QPoint(58, 36);
            painter.drawPolygon(play);
        }
        else if (asset.type == AssetType::Audio)
        {
            QPainter painter(&pixmap);
            painter.setPen(QPen(QColor(127, 209, 255), 2));
            for (int x = 16; x < 80; x += 6)
            {
                const int height = 8 + (x * 37 % 40);
                painter.drawLine(QPoint(x, 36 - height / 2),
                                 QPoint(x, 36 + height / 2));
            }
        }
        else
        {
            QPainter painter(&pixmap);
            QFont font = painter.font();
            font.setPixelSize(20);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor(255, 207, 107));
            painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("</>"));
        }

        auto* item = new QListWidgetItem(QIcon(pixmap), asset.name, list);
        item->setData(Qt::UserRole, asset.hash);
        QString tooltip = QStringLiteral("%1 · %2\n%3")
                              .arg(assetTypeLabel(asset.type), asset.name,
                                   asset.hash);
        if (asset.hash == currentHash)
            tooltip += QStringLiteral("\n（当前素材）");
        item->setToolTip(tooltip);
    };

    for (const assets::StoredAsset& asset : session->assets().list())
        addAsset(asset);
    if (list->count() > 0)
        list->setCurrentRow(0);
    layout->addWidget(list, 1);

    auto* buttons = new QHBoxLayout();
    auto* importButton = new QPushButton(QStringLiteral("导入新文件…"), &dialog);
    buttons->addWidget(importButton);
    buttons->addStretch(1);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    buttons->addWidget(cancelButton);
    auto* useButton = new QPushButton(QStringLiteral("使用选中素材"), &dialog);
    useButton->setDefault(true);
    useButton->setEnabled(list->count() > 0);
    buttons->addWidget(useButton);
    layout->addLayout(buttons);

    QString importedHash;
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(useButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [&dialog](QListWidgetItem*) { dialog.accept(); });
    QObject::connect(importButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("更换素材"), QString(),
            QStringLiteral("支持的素材 (*.png *.jpg *.jpeg *.gif *.bmp *.webp "
                           "*.svg *.mp4 *.mov *.mkv *.webm *.avi *.wav *.mp3 "
                           "*.ogg *.flac *.aac *.m4a *.zip);;所有文件 (*)"));
        if (path.isEmpty())
            return;

        const QFileInfo info(path);
        const auto type = assets::detectType(path);
        QString importError;
        std::optional<assets::StoredAsset> stored;
        const bool isBundle =
            info.isDir()
            || info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0
            || (type && *type == AssetType::Html);
        if (isBundle)
            stored = session->assets().importPvhtml(path, &importError);
        else if (type && *type == AssetType::Video)
        {
            const auto video = session->assets().importVideoWithAudio(path,
                                                                      &importError);
            if (video)
                stored = video->video;
        }
        else if (type)
            stored = session->assets().importFile(path, &importError);

        if (!stored)
        {
            QMessageBox::warning(&dialog, QStringLiteral("导入素材失败"),
                                 importError.isEmpty() ? QStringLiteral("导入失败")
                                                       : importError);
            return;
        }
        if (!assetCompatible(contentKind, currentType, stored->type))
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("素材类型不兼容"),
                QStringLiteral("不能把当前实体换成 %1 素材")
                    .arg(assetTypeLabel(stored->type)));
            return;
        }
        session->document()->commitProject(
            QStringLiteral("pvf: import asset \"%1\"").arg(stored->name), nullptr);
        importedHash = stored->hash;
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted)
        return QString();
    if (!importedHash.isEmpty())
        return importedHash;

    const QListWidgetItem* item = list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

#if PVFORGE_LEGACY_SHELL
QString AssetLibraryPanel::pickMedia(QWidget* parent, Project* project,
                                     EntityType type, const QString& filter)
{
    if (!project)
        return QString();

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("更换素材"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* hint = new QLabel(QStringLiteral("从素材库选择，或导入新文件"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* list = new QListWidget(&dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(96, 72));
    list->setGridSize(QSize(136, 110));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMinimumSize(420, 260);
    for (int index : mediaTemplateIndices(*project, type))
    {
        const Template& tmpl = project->templates()[index];
        auto* item = new QListWidgetItem(listIcon(tmpl), tmpl.name, list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(QStringLiteral("%1 · %2")
                             .arg(templateTypeName(tmpl.type), tmpl.path));
    }
    if (list->count() > 0)
        list->setCurrentRow(0);
    layout->addWidget(list, 1);

    auto* buttons = new QHBoxLayout();
    auto* importButton = new QPushButton(QStringLiteral("导入新文件…"), &dialog);
    buttons->addWidget(importButton);
    buttons->addStretch(1);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    buttons->addWidget(cancelButton);
    auto* useButton = new QPushButton(QStringLiteral("使用选中素材"), &dialog);
    useButton->setDefault(true);
    useButton->setEnabled(list->count() > 0);
    buttons->addWidget(useButton);
    layout->addLayout(buttons);

    QString imported;
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(useButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [&dialog](QListWidgetItem*) { dialog.accept(); });
    QObject::connect(importButton, &QPushButton::clicked, &dialog, [&] {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("更换素材"), QString(), filter);
        if (!file.isEmpty())
        {
            imported = file;
            dialog.accept();
        }
    });

    if (dialog.exec() != QDialog::Accepted)
        return QString();
    if (!imported.isEmpty())
        return imported;

    const QListWidgetItem* item = list->currentItem();
    if (!item)
        return QString();
    const int index = item->data(Qt::UserRole).toInt();
    const auto& templates = project->templates();
    if (index < 0 || index >= int(templates.size()))
        return QString();
    return templates[index].path;
}

void AssetLibraryPanel::removeSelected()
{
    if (!selectedHtmlAsset().isEmpty())
        return;   // content-addressed HTML assets are shared; leave them on disk

    const int index = selectedIndex();
    const auto& templates = m_project->templates();
    if (index < 0 || index >= templates.size())
        return;

    const QString templateId = templates[index].id;
    QVector<EntityId> instances;
    for (const Entity& e : m_project->entities())
        if (e.templateId == templateId)
            instances.append(e.id);

    if (!instances.isEmpty())
    {
        QMessageBox box(QMessageBox::Question, QStringLiteral("删除素材"),
                        QStringLiteral("该素材在场景中还有 %1 个实例。\n要同时删除它们吗？")
                            .arg(instances.size()),
                        QMessageBox::NoButton, this);
        auto* both =
            box.addButton(QStringLiteral("同时删除实例"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("只删模板"), QMessageBox::RejectRole);
        auto* cancel = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        box.setDefaultButton(both);
        box.exec();

        if (box.clickedButton() == cancel)
            return;
        if (box.clickedButton() != both)
        {
            m_project->removeTemplate(index);
            return;
        }
    }

    // Remove the template first so its index stays valid while entities go.
    m_project->removeTemplate(index);
    for (EntityId id : instances)
        m_project->removeEntity(id);
}
#endif // PVFORGE_LEGACY_SHELL

void AssetLibraryPanel::createSelectedSession()
{
    if (!m_session || !m_session->editActions())
        return;
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;

    QString error;
    const QVector<Uuid> created = m_session->editActions()->addAssetEntities(
        hash, m_session->playhead(), -1, Uuid(), &error);
    if (created.isEmpty())
    {
        setStatus(error.isEmpty() ? QStringLiteral("无法创建实体") : error);
        return;
    }
    if (m_session->document())
        m_session->document()->setSelection(created.first());
    const auto asset = m_session->assets().get(hash);
    const QString name = asset && !asset->name.isEmpty() ? asset->name : hash;
    setStatus(QStringLiteral("已创建实体 \"%1\"").arg(name));
}

void AssetLibraryPanel::removeSelectedSession()
{
    deleteSessionSelected();
}

} // namespace pvf
