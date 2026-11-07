#include "AssetLibraryPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
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
    }
    return QString();
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
        QFont font(tmpl.data.value(QStringLiteral("font")).toString());
        font.setPixelSize(std::max(10, size.height() / 4));
        p.setFont(font);
        p.setPen(tmpl.data.value(QStringLiteral("color")).value<QColor>());
        p.drawText(pixmap.rect().adjusted(8, 8, -8, -8),
                   Qt::AlignCenter | Qt::TextWordWrap,
                   tmpl.data.value(QStringLiteral("text")).toString());
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
    return QIcon(pixmap);
}

} // namespace

AssetLibraryPanel::AssetLibraryPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_preview = new QLabel(QStringLiteral("素材库为空"), this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(150);
    m_preview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_preview);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("assetList"));
    m_list->setIconSize(QSize(40, 40));
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout();
    m_create = new QPushButton(QStringLiteral("创建实体"), this);
    m_create->setObjectName(QStringLiteral("assetCreate"));
    m_create->setEnabled(false);
    buttons->addWidget(m_create, 1);
    m_remove = new QPushButton(QStringLiteral("删除"), this);
    m_remove->setEnabled(false);
    buttons->addWidget(m_remove);
    layout->addLayout(buttons);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

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

    for (const Template& tmpl : m_project->templates())
    {
        auto* item = new QListWidgetItem(tmpl.name, m_list);
        item->setIcon(listIcon(tmpl));
        item->setToolTip(QStringLiteral("%1 · %2").arg(templateTypeName(tmpl.type), tmpl.path));
    }

    if (m_list->count() > 0)
        m_list->setCurrentRow(std::clamp(previous, 0, m_list->count() - 1));

    updatePreview();
}

int AssetLibraryPanel::selectedIndex() const
{
    return m_list->currentRow();
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
        return;
    }

    const Template& tmpl = templates[index];
    m_preview->setText(QString());
    m_preview->setPixmap(renderTemplate(tmpl, QSize(240, 150)));

    QString info = templateTypeName(tmpl.type);
    if (tmpl.type == EntityType::Video && tmpl.duration > 0.0)
        info += QStringLiteral(" · %1 秒").arg(tmpl.duration, 0, 'f', 2);
    if (!tmpl.path.isEmpty())
        info += QStringLiteral("\n") + tmpl.path;
    m_info->setText(info);
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
    if (index < 0 || index >= m_project->templates().size())
        return;
    m_project->removeTemplate(index);
}

} // namespace pvf
