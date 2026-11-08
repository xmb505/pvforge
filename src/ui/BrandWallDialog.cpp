#include "BrandWallDialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace pvf {

BrandWallPreview::BrandWallPreview(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
}

void BrandWallPreview::setWall(const BrandWallParams& params,
                               const QVector<QImage>& logos)
{
    m_params = params;
    m_logos = logos;
    update();
}

void BrandWallPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 36));
    if (m_params.width < 1 || m_params.height < 1)
        return;
    const QRectF bounds = BrandWall::bounds(m_params);
    const double scale =
        std::min((width() - 16.0) / bounds.width(), (height() - 16.0) / bounds.height());
    painter.save();
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(scale, scale);
    BrandWall::paint(painter, m_params, m_logos);
    painter.restore();
}

BrandWallDialog::BrandWallDialog(const BrandWallParams& params,
                                 const QVector<QImage>& images, QWidget* parent)
    : QDialog(parent)
    , m_params(params)
    , m_images(images)
{
    setWindowTitle(QStringLiteral("品牌瓷砖墙"));
    resize(860, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    auto* left = new QVBoxLayout;
    left->setSpacing(8);

    auto* form = new QGridLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);

    m_width = new QDoubleSpinBox(this);
    m_width->setRange(16, 8192);
    m_width->setValue(std::max(16.0, m_params.width));
    form->addWidget(new QLabel(QStringLiteral("宽度"), this), 0, 0);
    form->addWidget(m_width, 0, 1);

    m_height = new QDoubleSpinBox(this);
    m_height->setRange(16, 8192);
    m_height->setValue(std::max(16.0, m_params.height));
    form->addWidget(new QLabel(QStringLiteral("高度"), this), 0, 2);
    form->addWidget(m_height, 0, 3);

    m_columns = new QSpinBox(this);
    m_columns->setRange(1, 64);
    m_columns->setValue(std::clamp(m_params.columns, 1, 64));
    m_columns->setSuffix(QStringLiteral(" 列"));
    form->addWidget(new QLabel(QStringLiteral("密度"), this), 1, 0);
    form->addWidget(m_columns, 1, 1);

    m_rows = new QSpinBox(this);
    m_rows->setRange(1, 64);
    m_rows->setValue(std::clamp(m_params.rows, 1, 64));
    m_rows->setSuffix(QStringLiteral(" 行"));
    form->addWidget(m_rows, 1, 2);

    m_gap = new QDoubleSpinBox(this);
    m_gap->setRange(0, 64);
    m_gap->setValue(m_params.gap);
    m_gap->setSuffix(QStringLiteral(" px"));
    form->addWidget(new QLabel(QStringLiteral("间距"), this), 1, 4);
    form->addWidget(m_gap, 1, 5);

    m_opacity = new QDoubleSpinBox(this);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(m_params.logoOpacity * 100.0);
    m_opacity->setSuffix(QStringLiteral(" %"));
    form->addWidget(new QLabel(QStringLiteral("Logo 透明度"), this), 2, 0);
    form->addWidget(m_opacity, 2, 1);
    left->addLayout(form);

    m_slotLayout = new QVBoxLayout;
    m_slotLayout->setSpacing(4);
    auto* slotContent = new QWidget(this);
    slotContent->setLayout(m_slotLayout);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::StyledPanel);
    scroll->setWidget(slotContent);
    scroll->setMinimumHeight(150);
    left->addWidget(scroll, 1);

    auto* add = new QPushButton(QStringLiteral("添加 Logo"), this);
    connect(add, &QPushButton::clicked, this, [this] {
        const int index = m_slots.size();
        addSlot(QString(), QColor::fromHsv((index * 67) % 360, 120, 220), QImage());
        rebuild();
    });
    left->addWidget(add);
    body->addLayout(left, 1);

    m_preview = new BrandWallPreview(this);
    m_preview->setObjectName(QStringLiteral("wallPreview"));
    body->addWidget(m_preview, 1);
    root->addLayout(body, 1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // Seed the slot list from the wall being edited.
    const int count = std::max({int(m_params.logos.size()),
                                int(m_params.tileColors.size()), 1});
    for (int index = 0; index < count; ++index)
    {
        const QString path = m_params.logos.value(index);
        const QColor color = index < m_params.tileColors.size()
                                 ? QColor(m_params.tileColors[index])
                                 : QColor::fromHsv((index * 67) % 360, 120, 220);
        const QImage image = index < m_images.size() ? m_images[index]
                                                     : BrandWall::loadLogo(path);
        addSlot(path, color.isValid() ? color : QColor(220, 220, 226), image);
    }

    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this](double) { rebuild(); });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [this](double) { rebuild(); });
    connect(m_columns, &QSpinBox::valueChanged, this, [this](int) { rebuild(); });
    connect(m_rows, &QSpinBox::valueChanged, this, [this](int) { rebuild(); });
    connect(m_gap, &QDoubleSpinBox::valueChanged, this, [this](double) { rebuild(); });
    connect(m_opacity, &QDoubleSpinBox::valueChanged, this, [this](double) { rebuild(); });

    rebuild();
}

void BrandWallDialog::updateSlotButton(Slot& slot)
{
    if (slot.colorButton)
    {
        slot.colorButton->setText(slot.color.name().toUpper());
        const QString text = slot.color.lightness() > 140 ? QStringLiteral("#000")
                                                          : QStringLiteral("#fff");
        slot.colorButton->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; color: %2; }")
                .arg(slot.color.name(), text));
    }
    if (slot.logoButton)
    {
        slot.logoButton->setText(slot.path.isEmpty()
                                     ? QStringLiteral("选择 Logo…")
                                     : QFileInfo(slot.path).fileName());
        slot.logoButton->setToolTip(slot.path);
    }
}

void BrandWallDialog::addSlot(const QString& path, const QColor& color,
                              const QImage& image)
{
    Slot slot;
    slot.path = path;
    slot.color = color;
    slot.image = image;

    slot.row = new QWidget(this);
    auto* layout = new QHBoxLayout(slot.row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    slot.colorButton = new QPushButton(slot.row);
    slot.colorButton->setFixedWidth(86);
    connect(slot.colorButton, &QPushButton::clicked, this, [this, row = slot.row] {
        for (Slot& current : m_slots)
        {
            if (current.row != row)
                continue;
            const QColor chosen = QColorDialog::getColor(current.color, this,
                                                         QStringLiteral("瓷砖颜色"));
            if (!chosen.isValid())
                return;
            current.color = chosen;
            updateSlotButton(current);
            rebuild();
            return;
        }
    });
    layout->addWidget(slot.colorButton);

    slot.logoButton = new QPushButton(slot.row);
    connect(slot.logoButton, &QPushButton::clicked, this, [this, row = slot.row] {
        for (Slot& current : m_slots)
        {
            if (current.row != row)
                continue;
            const QString file = QFileDialog::getOpenFileName(
                this, QStringLiteral("选择 Logo 图片"), current.path,
                QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.svg)"));
            if (file.isEmpty())
                return;
            current.path = file;
            current.image = BrandWall::loadLogo(file);
            updateSlotButton(current);
            rebuild();
            return;
        }
    });
    layout->addWidget(slot.logoButton, 1);

    auto* remove = new QPushButton(QStringLiteral("×"), slot.row);
    remove->setFixedWidth(26);
    connect(remove, &QPushButton::clicked, this,
            [this, row = slot.row] { removeSlot(row); });
    layout->addWidget(remove);

    m_slots.append(slot);
    m_slotLayout->addWidget(slot.row);
    updateSlotButton(m_slots.last());
}

void BrandWallDialog::removeSlot(QWidget* row)
{
    for (int index = 0; index < m_slots.size(); ++index)
    {
        if (m_slots[index].row != row)
            continue;
        m_slotLayout->removeWidget(row);
        row->deleteLater();
        m_slots.removeAt(index);
        break;
    }
    if (m_slots.isEmpty())
        addSlot(QString(), QColor(220, 220, 226), QImage());
    rebuild();
}

void BrandWallDialog::rebuild()
{
    m_params.width = m_width->value();
    m_params.height = m_height->value();
    m_params.columns = m_columns->value();
    m_params.rows = m_rows->value();
    m_params.gap = m_gap->value();
    m_params.logoOpacity = m_opacity->value() / 100.0;
    m_params.logos.clear();
    m_params.tileColors.clear();
    m_images.clear();
    for (const Slot& slot : m_slots)
    {
        m_params.logos.append(slot.path);
        m_params.tileColors.append(slot.color.name(QColor::HexArgb));
        m_images.append(slot.image);
    }
    m_preview->setWall(m_params, m_images);
}

} // namespace pvf
