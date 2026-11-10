#include "BrandWallDialog.h"

#include "assets/AssetStore.h"
#include "model/Uuid.h"
#include "render2/Render.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

// renderFrame only asks the world for poses when a solver is active. The
// preview entity has none, so an empty world is enough.
class PreviewWorld final : public anim::World
{
public:
    solver::Pose poseOf(const Uuid&, int) const override { return {}; }
    QPainterPath shapeOf(const Uuid&, int) const override { return {}; }
    bool physicsInteract(const Uuid&, int) const override { return false; }
    QVector<Uuid> physicsEntities(int) const override { return {}; }
};

QStringList splitContentList(const QString& raw)
{
    QStringList out;
    const QStringList parts =
        raw.split(QRegularExpression(QStringLiteral("[,\\n]")),
                  Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        const QString value = part.trimmed();
        if (!value.isEmpty())
            out.append(value);
    }
    return out;
}

} // namespace

QPixmap renderBrandWallPreview(const model::BrandWallContent& content,
                               const QSize& size)
{
    if (size.isEmpty())
        return {};
    QPixmap pixmap(size);
    pixmap.fill(QColor(30, 30, 36));
    if (content.columns <= 0 || content.rows <= 0 || content.logoSize <= 0.0)
        return pixmap;

    const double spacing = std::max(0.0, content.spacing);
    const double width = content.columns * content.logoSize
        + std::max(0, content.columns - 1) * spacing;
    const double height = content.rows * content.logoSize
        + std::max(0, content.rows - 1) * spacing;
    const double extent = std::max(1.0, std::hypot(width, height) * 1.08);

    model::Project project;
    project.settings.width = int(std::ceil(extent));
    project.settings.height = int(std::ceil(extent));
    project.settings.background = QStringLiteral("#1e1e24");

    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("品牌墙预览");
    entity.start = 0;
    entity.end = 1;
    entity.content.kind = ContentKind::BrandWall;
    entity.content.brandWall = content;
    project.entities.insert(entity.uuid, entity);

    assets::AssetStore assets{QString()};
    PreviewWorld world;
    anim::Cache cache;
    render2::Options options;
    options.projectFrame = 0.0;
    options.outputSize = size;
    options.background = QColor(30, 30, 36);
    options.worldRect = QRectF(-extent / 2.0, -extent / 2.0, extent, extent);
    const QImage image =
        render2::renderFrame(project, assets, options, world, cache);
    return QPixmap::fromImage(image);
}

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

BrandWallDialog::BrandWallDialog(const model::BrandWallContent& content,
                                 QWidget* parent)
    : QDialog(parent)
    , m_content(content)
{
    buildContentUi();
}

void BrandWallDialog::buildContentUi()
{
    setWindowTitle(QStringLiteral("品牌瓷砖墙 · 文字标签"));
    resize(820, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    auto* formHost = new QWidget(this);
    auto* form = new QFormLayout(formHost);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_contentLogos = new QPlainTextEdit(formHost);
    m_contentLogos->setObjectName(QStringLiteral("brandWallContentLogos"));
    m_contentLogos->setMinimumHeight(110);
    m_contentLogos->setPlainText(m_content.logos.join(QLatin1Char('\n')));
    m_contentLogos->setToolTip(
        QStringLiteral("每行一个标签，也可用逗号分隔；最多 64 个，每项 1..128 字。"));
    form->addRow(QStringLiteral("Logo 标签"), m_contentLogos);

    m_contentColumns = new QSpinBox(formHost);
    m_contentColumns->setObjectName(QStringLiteral("brandWallContentColumns"));
    m_contentColumns->setRange(1, 64);
    m_contentColumns->setValue(std::clamp(m_content.columns, 1, 64));
    m_contentColumns->setSuffix(QStringLiteral(" 列"));
    form->addRow(QStringLiteral("列数"), m_contentColumns);

    m_contentRows = new QSpinBox(formHost);
    m_contentRows->setObjectName(QStringLiteral("brandWallContentRows"));
    m_contentRows->setRange(1, 64);
    m_contentRows->setValue(std::clamp(m_content.rows, 1, 64));
    m_contentRows->setSuffix(QStringLiteral(" 行"));
    form->addRow(QStringLiteral("行数"), m_contentRows);

    m_contentSpacing = new QDoubleSpinBox(formHost);
    m_contentSpacing->setObjectName(QStringLiteral("brandWallContentSpacing"));
    m_contentSpacing->setRange(0.0, 512.0);
    m_contentSpacing->setDecimals(2);
    m_contentSpacing->setSingleStep(1.0);
    m_contentSpacing->setSuffix(QStringLiteral(" px"));
    m_contentSpacing->setValue(m_content.spacing);
    form->addRow(QStringLiteral("间距"), m_contentSpacing);

    m_contentLogoSize = new QDoubleSpinBox(formHost);
    m_contentLogoSize->setObjectName(QStringLiteral("brandWallContentLogoSize"));
    m_contentLogoSize->setRange(1.0, 1024.0);
    m_contentLogoSize->setDecimals(2);
    m_contentLogoSize->setSingleStep(1.0);
    m_contentLogoSize->setSuffix(QStringLiteral(" px"));
    m_contentLogoSize->setValue(m_content.logoSize);
    form->addRow(QStringLiteral("Logo 尺寸"), m_contentLogoSize);

    m_contentColors = new QLineEdit(formHost);
    m_contentColors->setObjectName(QStringLiteral("brandWallContentColors"));
    m_contentColors->setText(m_content.colors.join(QStringLiteral(", ")));
    m_contentColors->setToolTip(
        QStringLiteral("逗号分隔，#rgb / #rrggbb / #aarrggbb；至少一个。"));
    form->addRow(QStringLiteral("瓷砖颜色"), m_contentColors);

    m_contentAlternate = new QCheckBox(QStringLiteral("棋盘交替"), formHost);
    m_contentAlternate->setObjectName(
        QStringLiteral("brandWallContentAlternate"));
    m_contentAlternate->setChecked(m_content.alternate);
    form->addRow(m_contentAlternate);

    m_contentAngle = new QDoubleSpinBox(formHost);
    m_contentAngle->setObjectName(QStringLiteral("brandWallContentAngle"));
    m_contentAngle->setRange(-360.0, 360.0);
    m_contentAngle->setDecimals(2);
    m_contentAngle->setSingleStep(1.0);
    m_contentAngle->setSuffix(QStringLiteral(" °"));
    m_contentAngle->setValue(m_content.angle);
    form->addRow(QStringLiteral("旋转"), m_contentAngle);

    m_contentBackground = new QLineEdit(formHost);
    m_contentBackground->setObjectName(
        QStringLiteral("brandWallContentBackground"));
    m_contentBackground->setText(m_content.background);
    m_contentBackground->setPlaceholderText(QStringLiteral("空 = 透明"));
    m_contentBackground->setToolTip(
        QStringLiteral("留空透明；填写 #rgb / #rrggbb / #aarrggbb。"));
    form->addRow(QStringLiteral("背景色"), m_contentBackground);

    m_contentSeed = new QSpinBox(formHost);
    m_contentSeed->setObjectName(QStringLiteral("brandWallContentSeed"));
    m_contentSeed->setRange(-2147483647, 2147483647);
    m_contentSeed->setValue(m_content.seed);
    form->addRow(QStringLiteral("随机种子"), m_contentSeed);

    body->addWidget(formHost, 1);

    auto* previewHost = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewHost);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(6);
    m_contentPreview = new QLabel(previewHost);
    m_contentPreview->setObjectName(QStringLiteral("brandWallContentPreview"));
    m_contentPreview->setAlignment(Qt::AlignCenter);
    m_contentPreview->setFixedSize(360, 360);
    m_contentPreview->setFrameShape(QFrame::StyledPanel);
    previewLayout->addWidget(m_contentPreview);
    m_contentSummary = new QLabel(previewHost);
    m_contentSummary->setObjectName(QStringLiteral("brandWallContentSummary"));
    m_contentSummary->setWordWrap(true);
    m_contentSummary->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_contentSummary);
    previewLayout->addStretch(1);
    body->addWidget(previewHost, 1);

    root->addLayout(body, 1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    const auto changed = [this](auto) { refreshContentPreview(); };
    connect(m_contentLogos, &QPlainTextEdit::textChanged, this,
            [this] { refreshContentPreview(); });
    connect(m_contentColumns, &QSpinBox::valueChanged, this, changed);
    connect(m_contentRows, &QSpinBox::valueChanged, this, changed);
    connect(m_contentSpacing, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_contentLogoSize, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_contentColors, &QLineEdit::textChanged, this,
            [this] { refreshContentPreview(); });
    connect(m_contentAlternate, &QCheckBox::toggled, this,
            [this] { refreshContentPreview(); });
    connect(m_contentAngle, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_contentBackground, &QLineEdit::textChanged, this,
            [this] { refreshContentPreview(); });
    connect(m_contentSeed, &QSpinBox::valueChanged, this, changed);

    refreshContentPreview();
}

model::BrandWallContent BrandWallDialog::content() const
{
    model::BrandWallContent out = m_content;
    if (!m_contentLogos)
        return out;
    out.logos = splitContentList(m_contentLogos->toPlainText());
    out.columns = m_contentColumns->value();
    out.rows = m_contentRows->value();
    out.spacing = m_contentSpacing->value();
    out.logoSize = m_contentLogoSize->value();
    out.colors = splitContentList(m_contentColors->text());
    out.alternate = m_contentAlternate->isChecked();
    out.angle = m_contentAngle->value();
    out.background = m_contentBackground->text().trimmed();
    out.seed = m_contentSeed->value();
    return out;
}

void BrandWallDialog::refreshContentPreview()
{
    if (!m_contentPreview)
        return;
    const model::BrandWallContent wall = content();
    m_contentPreview->setPixmap(
        renderBrandWallPreview(wall, m_contentPreview->size()));
    if (m_contentSummary)
    {
        const double width = wall.columns * wall.logoSize
            + std::max(0, wall.columns - 1) * wall.spacing;
        const double height = wall.rows * wall.logoSize
            + std::max(0, wall.rows - 1) * wall.spacing;
        m_contentSummary->setText(
            QStringLiteral("%1 个标签 · %2×%3 · %4×%5 px")
                .arg(wall.logos.size())
                .arg(wall.columns)
                .arg(wall.rows)
                .arg(int(std::lround(width)))
                .arg(int(std::lround(height))));
    }
}

} // namespace pvf
