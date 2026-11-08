#include "AssetLibraryPanel.h"

#include "app/MainWindow.h"
#include "core/BrandWall.h"
#include "core/ComicBox.h"
#include "core/Lang.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "media/MediaImporter.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace pvf {

namespace {

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
    }
    return QString();
}

QString elidedPath(const QString& path)
{
    // Paths have no spaces to wrap at; show both ends and let the tooltip
    // carry the full value. The label itself is width-ignored so a long path
    // can never widen the whole right sidebar.
    return QFontMetrics(QFont()).elidedText(path, Qt::ElideMiddle, 240);
}

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
    return QIcon(pixmap);
}

} // namespace

AssetLibraryPanel::AssetLibraryPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setObjectName(QStringLiteral("assetPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_preview = new QLabel(QStringLiteral("素材库为空"), this);
    m_preview->setObjectName(QStringLiteral("assetPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(150);
    m_preview->setMinimumWidth(0);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_preview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_preview);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("assetSearch"));
    m_search->setMinimumWidth(0);
    m_search->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_search->setPlaceholderText(Lang::t(QStringLiteral("search.assets"), QStringLiteral("搜索素材…")));
    m_search->setClearButtonEnabled(true);

    auto* searchRow = new QHBoxLayout();
    searchRow->addWidget(m_search, 1);
    auto* importButton = new QToolButton(this);
    importButton->setObjectName(QStringLiteral("assetImport"));
    importButton->setText(QStringLiteral("+"));
    importButton->setToolTip(QStringLiteral("导入素材文件（图片 / 视频 / 音频）"));
    importButton->setFixedSize(26, 26);
    searchRow->addWidget(importButton);
    layout->addLayout(searchRow);

    connect(importButton, &QToolButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("导入素材"), QString(), QStringLiteral("所有文件 (*)"));
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
                    if (!result.error.isEmpty() || result.importedPath.isEmpty())
                    {
                        failed << QStringLiteral("%1（%2）")
                                      .arg(QFileInfo(result.sourcePath).fileName(),
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

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("assetList"));
    m_list->setIconSize(QSize(40, 40));
    m_list->setAlternatingRowColors(true);
    m_list->setMinimumHeight(140);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setWordWrap(false);
    layout->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout();
    m_create = new QPushButton(QStringLiteral("创建实体"), this);
    m_create->setObjectName(QStringLiteral("assetCreate"));
    m_create->setEnabled(false);
    buttons->addWidget(m_create, 1);
    m_remove = new QPushButton(QStringLiteral("删除"), this);
    m_remove->setToolTip(QStringLiteral("只删除素材模板，不影响场景中已创建的实体"));
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
        rebuild();
    });
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { updatePreview(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { createSelected(); });
    connect(m_create, &QPushButton::clicked, this, &AssetLibraryPanel::createSelected);
    connect(m_remove, &QPushButton::clicked, this, &AssetLibraryPanel::removeSelected);
    connect(m_project, &Project::changed, this, &AssetLibraryPanel::refresh);

    refresh();
}

void AssetLibraryPanel::refresh()
{
    QStringList signature;
    for (const Template& tmpl : m_project->templates())
        signature << QStringLiteral("%1|%2|%3").arg(int(tmpl.type)).arg(tmpl.name, tmpl.path);

    if (signature == m_signature)
    {
        updatePreview();
        return;
    }

    m_signature = signature;
    rebuild();
}

void AssetLibraryPanel::rebuild()
{
    const int previous = selectedIndex();
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
        item->setIcon(listIcon(tmpl));
        item->setToolTip(QStringLiteral("%1 · %2").arg(templateTypeName(tmpl.type), tmpl.path));
    }

    int restore = m_list->count() > 0 ? 0 : -1;
    for (int row = 0; row < m_list->count(); ++row)
        if (m_list->item(row)->data(Qt::UserRole).toInt() == previous)
            restore = row;
    m_list->setCurrentRow(restore);

    updatePreview();
}

int AssetLibraryPanel::selectedIndex() const
{
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void AssetLibraryPanel::updatePreview()
{
    const int index = selectedIndex();
    const auto& templates = m_project->templates();
    const bool valid = index >= 0 && index < templates.size();

    m_create->setEnabled(valid);
    m_remove->setEnabled(valid);

    if (!valid)
    {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(QStringLiteral("素材库为空"));
        m_info->clear();
        m_info->setToolTip(QString());
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
    const int index = selectedIndex();
    const auto& templates = m_project->templates();
    if (index < 0 || index >= templates.size())
        return;

    if (Entity* e = m_project->createFromTemplate(templates[index]))
        emit entityCreated(e->id);
}

void AssetLibraryPanel::removeSelected()
{
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

} // namespace pvf
