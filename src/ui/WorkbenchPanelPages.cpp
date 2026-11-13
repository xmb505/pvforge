#include "WorkbenchPanel.h"

#include "app/AppIcon.h"
#include "app/EditorSession.h"
#include "core/BrandWall.h"
#include "core/Lang.h"
#include "core/TextStyle.h"
#include "ui/BrandWallDialog.h"
#include "ui/TextLayoutDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pvf {

QWidget* WorkbenchPanel::buildMediaPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_mediaPreview = new QLabel(QStringLiteral("尚未选择素材"), page);
    m_mediaPreview->setAlignment(Qt::AlignCenter);
    m_mediaPreview->setMinimumHeight(140);
    m_mediaPreview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_mediaPreview);

    auto* pick = new QPushButton(
        Lang::t(QStringLiteral("workbench.chooseMedia"), QStringLiteral("选择媒体文件…")), page);
    connect(pick, &QPushButton::clicked, this, &WorkbenchPanel::pickMedia);
    layout->addWidget(pick);

    auto* libraryLabel = new QLabel(QStringLiteral("素材库"), page);
    QFont labelFont = libraryLabel->font();
    labelFont.setBold(true);
    libraryLabel->setFont(labelFont);
    layout->addWidget(libraryLabel);

    m_mediaLibrary = new QListWidget(page);
    m_mediaLibrary->setObjectName(QStringLiteral("mediaLibrary"));
    m_mediaLibrary->setIconSize(QSize(40, 40));
    m_mediaLibrary->setAlternatingRowColors(true);
    m_mediaLibrary->setMinimumHeight(120);
    layout->addWidget(m_mediaLibrary, 1);

    m_mediaInfo = new QLabel(
        QStringLiteral("支持图片、视频与音频，导入后自动识别并加入素材库。"), page);
    m_mediaInfo->setWordWrap(true);
    layout->addWidget(m_mediaInfo);

    connect(m_mediaLibrary, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                if (m_session)
                    selectSessionMedia(item->data(Qt::UserRole).toString());
                else
                    selectMedia(item->data(Qt::UserRole).toInt());
            });

    return page;
}

QWidget* WorkbenchPanel::buildTextPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(10);

    m_textContent = new QLineEdit(QStringLiteral("Text"), page);
    m_textContent->setObjectName(QStringLiteral("textContent"));
    form->addRow(QStringLiteral("内容"), m_textContent);

    m_textFont = new QFontComboBox(page);
    form->addRow(QStringLiteral("字体"), m_textFont);

    m_textSize = new QSpinBox(page);
    m_textSize->setRange(8, 512);
    m_textSize->setValue(96);
    m_textSize->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("字号"), m_textSize);

    m_textColor = new QPushButton(page);
    setColorButton(m_textColor, m_textColorValue);
    connect(m_textColor, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_textColorValue, QStringLiteral("文字颜色"));
        if (color.isValid())
        {
            m_textColorValue = color;
            setColorButton(m_textColor, color);
        }
    });
    form->addRow(QStringLiteral("颜色"), m_textColor);

    m_textAlign = new QComboBox(page);
    m_textAlign->addItem(QStringLiteral("居中"), int(Qt::AlignCenter));
    m_textAlign->addItem(QStringLiteral("左对齐"), int(Qt::AlignLeft | Qt::AlignVCenter));
    m_textAlign->addItem(QStringLiteral("右对齐"), int(Qt::AlignRight | Qt::AlignVCenter));
    form->addRow(QStringLiteral("对齐"), m_textAlign);

    auto* layoutButton = new QPushButton(QStringLiteral("排版…"), page);
    layoutButton->setObjectName(QStringLiteral("textLayoutButton"));
    layoutButton->setToolTip(QStringLiteral("打开文字排版编辑器：换行宽度、行距、内边距"));
    connect(layoutButton, &QPushButton::clicked, this, [this] {
        TextLayoutDialog dialog(currentTextStyle(), this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_textStyle = dialog.style();
        const QSignalBlocker contentBlocker(m_textContent);
        m_textContent->setText(m_textStyle.text);
        const QSignalBlocker fontBlocker(m_textFont);
        m_textFont->setCurrentFont(QFont(m_textStyle.fontFamily));
        const QSignalBlocker sizeBlocker(m_textSize);
        m_textSize->setValue(m_textStyle.pixelSize);
        m_textColorValue = m_textStyle.color;
        setColorButton(m_textColor, m_textColorValue);
        const QSignalBlocker alignBlocker(m_textAlign);
        for (int index = 0; index < m_textAlign->count(); ++index)
        {
            if ((m_textAlign->itemData(index).toInt() & Qt::AlignHorizontal_Mask) ==
                (m_textStyle.align & Qt::AlignHorizontal_Mask))
            {
                m_textAlign->setCurrentIndex(index);
                break;
            }
        }
    });
    form->addRow(layoutButton);

    return page;
}

QWidget* WorkbenchPanel::buildCameraPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(10);

    m_cameraName = new QLineEdit(page);
    m_cameraName->setObjectName(QStringLiteral("cameraName"));
    m_cameraName->setPlaceholderText(QStringLiteral("留空自动命名"));
    form->addRow(QStringLiteral("名称"), m_cameraName);

    auto* hint = new QLabel(
        QStringLiteral("创建后自动选中并设为活动摄像机。\n"
                       "在视口里拖边框或标题条移动，拖四角缩放，顶边圆点旋转。\n"
                       "监视器显示这台摄像机的最终画面。"),
        page);
    hint->setWordWrap(true);
    form->addRow(hint);

    return page;
}

QWidget* WorkbenchPanel::buildTextBoxPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_comicStyle.padding = 18.0;
    m_comicStyle.pixelSize = 72;
    m_comicStyle.color = m_comicColorValue;

    m_textBoxTypes = new QListWidget(page);
    m_textBoxTypes->setObjectName(QStringLiteral("textBoxTypes"));
    const QStringList names = {
        QStringLiteral("普通文本框"),
        QStringLiteral("日式超绝硬直角"),
        QStringLiteral("爆炸文本框"),
    };
    for (const QString& name : names)
        m_textBoxTypes->addItem(name);
    m_textBoxTypes->setMaximumHeight(96);
    layout->addWidget(m_textBoxTypes);

    m_textBoxPreview = new QLabel(page);
    m_textBoxPreview->setObjectName(QStringLiteral("textBoxPreview"));
    m_textBoxPreview->setAlignment(Qt::AlignCenter);
    m_textBoxPreview->setMinimumHeight(120);
    m_textBoxPreview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_textBoxPreview);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(8);

    m_textBoxShape = new QComboBox(page);
    m_textBoxShape->setObjectName(QStringLiteral("textBoxShape"));
    m_textBoxShape->addItem(QStringLiteral("云朵"), 2);
    m_textBoxShape->addItem(QStringLiteral("矩形"), 1);
    m_textBoxShape->addItem(QStringLiteral("椭圆"), 0);
    m_textBoxShape->setCurrentIndex(2);
    form->addRow(QStringLiteral("外形"), m_textBoxShape);

    m_comicContent = new QLineEdit(QStringLiteral("台词"), page);
    m_comicContent->setObjectName(QStringLiteral("comicContent"));
    form->addRow(QStringLiteral("内容"), m_comicContent);

    m_comicSize = new QSpinBox(page);
    m_comicSize->setRange(12, 400);
    m_comicSize->setValue(72);
    m_comicSize->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("字号"), m_comicSize);

    m_comicColor = new QPushButton(page);
    setColorButton(m_comicColor, m_comicColorValue);
    connect(m_comicColor, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_comicColorValue, QStringLiteral("文字颜色"));
        if (color.isValid())
        {
            m_comicColorValue = color;
            setColorButton(m_comicColor, color);
            updateTextBoxPreview();
        }
    });
    form->addRow(QStringLiteral("文字颜色"), m_comicColor);

    m_comicDirection = new QComboBox(page);
    m_comicDirection->addItem(QStringLiteral("横排"), 0);
    m_comicDirection->addItem(QStringLiteral("竖排 · 从右到左"), 1);
    m_comicDirection->addItem(QStringLiteral("竖排 · 从左到右"), 2);
    connect(m_comicDirection, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_comicStyle.vertical = index > 0;
        m_comicStyle.verticalRtl = index != 2;
        updateTextBoxPreview();
    });
    form->addRow(QStringLiteral("排版方向"), m_comicDirection);

    auto* styleRow = new QWidget(page);
    auto* styleLayout = new QHBoxLayout(styleRow);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->setSpacing(10);
    m_comicBold = new QCheckBox(QStringLiteral("加粗"), styleRow);
    m_comicItalic = new QCheckBox(QStringLiteral("斜体"), styleRow);
    m_comicUnderline = new QCheckBox(QStringLiteral("下划线"), styleRow);
    connect(m_comicBold, &QCheckBox::toggled, this, [this](bool on) {
        m_comicStyle.bold = on;
        updateTextBoxPreview();
    });
    connect(m_comicItalic, &QCheckBox::toggled, this, [this](bool on) {
        m_comicStyle.italic = on;
        updateTextBoxPreview();
    });
    connect(m_comicUnderline, &QCheckBox::toggled, this, [this](bool on) {
        m_comicStyle.underline = on;
        updateTextBoxPreview();
    });
    styleLayout->addWidget(m_comicBold);
    styleLayout->addWidget(m_comicItalic);
    styleLayout->addWidget(m_comicUnderline);
    styleLayout->addStretch(1);
    form->addRow(QStringLiteral("字形"), styleRow);

    m_comicFill = new QPushButton(page);
    setColorButton(m_comicFill, m_comicFillValue);
    connect(m_comicFill, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_comicFillValue, QStringLiteral("填充颜色"));
        if (color.isValid())
        {
            m_comicFillValue = color;
            setColorButton(m_comicFill, color);
            updateTextBoxPreview();
        }
    });
    form->addRow(QStringLiteral("填充颜色"), m_comicFill);

    m_comicStroke = new QPushButton(page);
    setColorButton(m_comicStroke, m_comicStrokeValue);
    connect(m_comicStroke, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_comicStrokeValue, QStringLiteral("边框颜色"));
        if (color.isValid())
        {
            m_comicStrokeValue = color;
            setColorButton(m_comicStroke, color);
            updateTextBoxPreview();
        }
    });
    form->addRow(QStringLiteral("边框颜色"), m_comicStroke);

    m_comicStrokeWidth = new QDoubleSpinBox(page);
    m_comicStrokeWidth->setRange(0, 60);
    m_comicStrokeWidth->setValue(6);
    m_comicStrokeWidth->setSuffix(QStringLiteral(" px"));
    connect(m_comicStrokeWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double) { updateTextBoxPreview(); });
    form->addRow(QStringLiteral("边框粗细"), m_comicStrokeWidth);

    auto* layoutButton = new QPushButton(QStringLiteral("排版…"), page);
    layoutButton->setObjectName(QStringLiteral("comicLayoutButton"));
    layoutButton->setToolTip(QStringLiteral("换行宽度、行距、内边距"));
    connect(layoutButton, &QPushButton::clicked, this, [this] {
        TextStyle seed = m_comicStyle;
        seed.text = m_comicContent->text();
        seed.pixelSize = m_comicSize->value();
        seed.color = m_comicColorValue;
        TextLayoutDialog dialog(seed, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_comicStyle = dialog.style();
        const QSignalBlocker contentBlocker(m_comicContent);
        m_comicContent->setText(m_comicStyle.text);
        const QSignalBlocker sizeBlocker(m_comicSize);
        m_comicSize->setValue(m_comicStyle.pixelSize);
        m_comicColorValue = m_comicStyle.color;
        setColorButton(m_comicColor, m_comicColorValue);
        updateTextBoxPreview();
    });
    form->addRow(layoutButton);

    layout->addLayout(form, 1);

    connect(m_textBoxTypes, &QListWidget::currentRowChanged, this, [this, form](int row) {
        if (m_textBoxShape)
        {
            m_textBoxShape->setVisible(row == 0);
            if (auto* label = qobject_cast<QLabel*>(form->labelForField(m_textBoxShape)))
                label->setVisible(row == 0);
        }
        updateTextBoxPreview();
    });
    connect(m_textBoxShape, &QComboBox::currentIndexChanged, this,
            [this](int) { updateTextBoxPreview(); });
    connect(m_comicContent, &QLineEdit::textChanged, this,
            [this](const QString&) { updateTextBoxPreview(); });
    connect(m_comicSize, &QSpinBox::valueChanged, this,
            [this](int) { updateTextBoxPreview(); });

    m_textBoxTypes->setCurrentRow(0);
    return page;
}

QWidget* WorkbenchPanel::buildSpecialPage()
{
    if (m_session)
        return buildSessionSpecialPage();

    return new QWidget(this);
}

QWidget* WorkbenchPanel::buildSessionSpecialPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* hint = new QLabel(
        QStringLiteral("品牌瓷砖墙使用文字标签，不使用图片或 SVG 贴图。"
                       "每个瓷砖显示 logos 列表里的一个标签。"),
        page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_wallSummary = new QLabel(page);
    m_wallSummary->setObjectName(QStringLiteral("sessionWallSummary"));
    m_wallSummary->setWordWrap(true);
    layout->addWidget(m_wallSummary);

    m_sessionWallPreview = new QLabel(page);
    m_sessionWallPreview->setObjectName(QStringLiteral("sessionWallPreview"));
    m_sessionWallPreview->setAlignment(Qt::AlignCenter);
    m_sessionWallPreview->setMinimumHeight(180);
    m_sessionWallPreview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_sessionWallPreview, 1);

    auto* editWall = new QPushButton(QStringLiteral("配置瓷砖墙…"), page);
    editWall->setObjectName(QStringLiteral("wallEditButton"));
    connect(editWall, &QPushButton::clicked, this, [this] {
        BrandWallDialog dialog(m_wallContent, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_wallContent = dialog.content();
        updateSessionWallSummary();
        updateCreateState();
    });
    layout->addWidget(editWall);

    updateSessionWallSummary();
    return page;
}

void WorkbenchPanel::updateSessionWallSummary()
{
    if (!m_wallSummary)
        return;
    const double width = m_wallContent.columns * m_wallContent.logoSize
        + std::max(0, m_wallContent.columns - 1) * m_wallContent.spacing;
    const double height = m_wallContent.rows * m_wallContent.logoSize
        + std::max(0, m_wallContent.rows - 1) * m_wallContent.spacing;
    m_wallSummary->setText(
        QStringLiteral("%1 个标签 · %2×%3 瓷砖 · %4×%5 px")
            .arg(m_wallContent.logos.size())
            .arg(m_wallContent.columns)
            .arg(m_wallContent.rows)
            .arg(int(std::lround(width)))
            .arg(int(std::lround(height))));
    if (m_sessionWallPreview)
        m_sessionWallPreview->setPixmap(
            renderBrandWallPreview(m_wallContent, m_sessionWallPreview->size()));
}

void WorkbenchPanel::updateWallSummary()
{
    if (!m_wallSummary)
        return;
    m_wallSummary->setText(
        QStringLiteral("%1 个 Logo · %2×%3 瓷砖 · %4×%5 px")
            .arg(m_wallParams.logos.size())
            .arg(m_wallParams.columns)
            .arg(m_wallParams.rows)
            .arg(int(m_wallParams.width))
            .arg(int(m_wallParams.height)));
}

QWidget* WorkbenchPanel::buildVectorPage(){
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_vectorShapes = new QListWidget(page);
    m_vectorShapes->setObjectName(QStringLiteral("vectorShapes"));
    m_vectorShapes->setViewMode(QListView::IconMode);
    m_vectorShapes->setMovement(QListView::Static);
    m_vectorShapes->setResizeMode(QListView::Adjust);
    m_vectorShapes->setIconSize(QSize(36, 36));
    m_vectorShapes->setGridSize(QSize(62, 62));
    m_vectorShapes->setFixedHeight(132);

    const struct
    {
        int shape;
        const char* name;
    } shapeEntries[] = {{0, "矩形"},   {3, "圆角矩形"}, {1, "椭圆"},
                        {2, "直线"},   {4, "三角形"},   {5, "心形"},
                        {6, "星形"},   {7, "多边形"},   {8, "箭头"}};
    for (const auto& entry : shapeEntries)
    {
        auto* item = new QListWidgetItem(makeShapeIcon(entry.shape),
                                         QString::fromUtf8(entry.name), m_vectorShapes);
        item->setData(Qt::UserRole, entry.shape);
    }
    m_vectorShapes->setCurrentRow(0);
    layout->addWidget(m_vectorShapes);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(6);

    m_vectorWidth = new QDoubleSpinBox(page);
    m_vectorWidth->setRange(1, 20000);
    m_vectorWidth->setValue(400);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("宽度"), m_vectorWidth);

    m_vectorHeight = new QDoubleSpinBox(page);
    m_vectorHeight->setRange(1, 20000);
    m_vectorHeight->setValue(300);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("高度"), m_vectorHeight);

    m_vectorCorner = new QDoubleSpinBox(page);
    m_vectorCorner->setRange(0, 2000);
    m_vectorCorner->setValue(60);
    m_vectorCorner->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("圆角"), m_vectorCorner);

    m_vectorSides = new QSpinBox(page);
    m_vectorSides->setRange(3, 64);
    m_vectorSides->setValue(5);
    form->addRow(QStringLiteral("边数"), m_vectorSides);

    m_vectorInner = new QDoubleSpinBox(page);
    m_vectorInner->setRange(0.05, 1.0);
    m_vectorInner->setSingleStep(0.05);
    m_vectorInner->setDecimals(2);
    m_vectorInner->setValue(0.45);
    form->addRow(QStringLiteral("内径比"), m_vectorInner);

    m_vectorFill = new QPushButton(page);
    setColorButton(m_vectorFill, m_fillValue);
    connect(m_vectorFill, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_fillValue, QStringLiteral("填充颜色"));
        if (color.isValid())
        {
            m_fillValue = color;
            setColorButton(m_vectorFill, color);
        }
    });
    form->addRow(QStringLiteral("填充"), m_vectorFill);

    m_vectorStroke = new QPushButton(page);
    setColorButton(m_vectorStroke, m_strokeValue);
    connect(m_vectorStroke, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_strokeValue, QStringLiteral("描边颜色"));
        if (color.isValid())
        {
            m_strokeValue = color;
            setColorButton(m_vectorStroke, color);
        }
    });
    form->addRow(QStringLiteral("描边"), m_vectorStroke);

    m_vectorStrokeWidth = new QDoubleSpinBox(page);
    m_vectorStrokeWidth->setRange(0, 200);
    m_vectorStrokeWidth->setValue(0);
    m_vectorStrokeWidth->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("描边宽度"), m_vectorStrokeWidth);

    layout->addLayout(form);

    connect(m_vectorShapes, &QListWidget::currentRowChanged, this,
            [this](int) { updateVectorFields(); });
    updateVectorFields();
    return page;
}

void WorkbenchPanel::updateVectorFields()
{
    const int shape = m_vectorShapes && m_vectorShapes->currentItem()
                          ? m_vectorShapes->currentItem()->data(Qt::UserRole).toInt()
                          : 0;
    if (m_vectorCorner)
        m_vectorCorner->setEnabled(shape == 3);
    if (m_vectorSides)
        m_vectorSides->setEnabled(shape == 6 || shape == 7);
    if (m_vectorInner)
        m_vectorInner->setEnabled(shape == 6);
}

} // namespace pvf
