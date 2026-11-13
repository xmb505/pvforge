#include "WorkbenchPanel.h"

#include "app/AppIcon.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "app/MainWindow.h"
#include "assets/AssetStore.h"
#include "assets/AssetTypeDetect.h"
#include "core/BrandWall.h"
#include "ui/BrandWallDialog.h"
#include "core/ComicBox.h"
#include "core/Lang.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "ui/TextLayoutDialog.h"
#include "model/Query.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "video2/VideoFrames.h"


#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

QString assetTypeLabelForWorkbench(AssetType type)
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

} // namespace


WorkbenchPanel::WorkbenchPanel(EditorSession* session, QWidget* parent)
    : QFrame(parent)
    , m_session(session)
{
    buildUi();
}

void WorkbenchPanel::buildUi()
{
    setObjectName(QStringLiteral("workbench"));
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "QFrame#workbench { background: palette(window); border: 1px solid palette(mid); }"));
    // The old wall is 640x560, but it must fit above the timeline in smaller
    // viewports. showAt() shrinks it to the viewport; the page stack scrolls
    // while the create button stays outside the scroll area.
    setMinimumSize(420, 300);
    resize(640, 560);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 0, 0, 170));
    setGraphicsEffect(shadow);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    m_types = new QListWidget(this);
    m_types->setObjectName(QStringLiteral("typeList"));
    m_types->setFixedWidth(170);
    m_types->setSelectionMode(QAbstractItemView::SingleSelection);
    QFont typeFont = m_types->font();
    typeFont.setPointSizeF(typeFont.pointSizeF() + 1.5);
    m_types->setFont(typeFont);

    auto* right = new QVBoxLayout();
    right->setSpacing(10);
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("typePages"));

    if (m_session)
    {
        // Keep the old wall's "covers the project" intent while using the
        // model's text-label fields. The model default is 200 px tiles.
        m_wallContent.logos = {QStringLiteral("PVForge"),
                               QStringLiteral("ACME")};
        const auto& settings = m_session->settings();
        const double fit =
            std::min((settings.width - (m_wallContent.columns - 1)
                                         * m_wallContent.spacing)
                         / double(m_wallContent.columns),
                     (settings.height - (m_wallContent.rows - 1)
                                          * m_wallContent.spacing)
                         / double(m_wallContent.rows));
        m_wallContent.logoSize =
            std::clamp(fit, 1.0, m_wallContent.logoSize);
        const QStringList typeNames = {
            Lang::t(QStringLiteral("workbench.media"), QStringLiteral("媒体")),
            Lang::t(QStringLiteral("workbench.text"), QStringLiteral("文字")),
            Lang::t(QStringLiteral("workbench.vector"), QStringLiteral("矢量")),
            Lang::t(QStringLiteral("workbench.special"), QStringLiteral("特殊")),
            Lang::t(QStringLiteral("workbench.camera"), QStringLiteral("摄像机")),
        };
        for (const QString& name : typeNames)
        {
            auto* item = new QListWidgetItem(name, m_types);
            item->setSizeHint(QSize(0, 42));
        }
        m_pages->addWidget(buildMediaPage());
        m_pages->addWidget(buildTextPage());
        m_pages->addWidget(buildVectorPage());
        m_pages->addWidget(buildSpecialPage());
        m_pages->addWidget(buildCameraPage());
    }
    else
    {
        const QStringList typeNames = {
            Lang::t(QStringLiteral("workbench.media"), QStringLiteral("媒体")),
            Lang::t(QStringLiteral("workbench.text"), QStringLiteral("文字")),
            QStringLiteral("文本框"),
            Lang::t(QStringLiteral("workbench.vector"), QStringLiteral("矢量")),
            Lang::t(QStringLiteral("workbench.special"), QStringLiteral("特殊")),
            Lang::t(QStringLiteral("workbench.camera"), QStringLiteral("摄像机")),
        };
        for (const QString& name : typeNames)
        {
            auto* item = new QListWidgetItem(name, m_types);
            item->setSizeHint(QSize(0, 42));
        }
        m_pages->addWidget(buildMediaPage());
        m_pages->addWidget(buildTextPage());
        m_pages->addWidget(buildTextBoxPage());
        m_pages->addWidget(buildVectorPage());
        m_pages->addWidget(buildSpecialPage());
        m_pages->addWidget(buildCameraPage());
    }
    root->addWidget(m_types);

    auto* pageScroll = new QScrollArea(this);
    pageScroll->setObjectName(QStringLiteral("workbenchPageScroll"));
    pageScroll->setWidgetResizable(true);
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pageScroll->setWidget(m_pages);

    right->addWidget(pageScroll, 1);

    m_create = new QPushButton(
        Lang::t(QStringLiteral("workbench.create"), QStringLiteral("创建")), this);
    m_create->setObjectName(QStringLiteral("createButton"));
    m_create->setMinimumHeight(38);
    right->addWidget(m_create);
    root->addLayout(right, 1);

    connect(m_types, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_pages->setCurrentIndex(row);
        updateCreateState();
    });
    connect(m_pages, &QStackedWidget::currentChanged, this,
            [this](int) { updateCreateState(); });
    connect(m_create, &QPushButton::clicked, this, &WorkbenchPanel::createCurrent);

    m_types->setCurrentRow(0);
    refreshMediaLibrary();
    updateCreateState();
}

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

void WorkbenchPanel::refreshSession()
{
    if (!m_session)
        return;
    refreshMediaLibrary();
    updateSessionWallSummary();
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

void WorkbenchPanel::showAt(const QPoint& pos)
{
    refreshMediaLibrary();
    if (m_session)
        updateSessionWallSummary();

    QWidget* parent = parentWidget();
    QSize panelSize = size();
    if (parent)
    {
        panelSize.setWidth(std::min(640, std::max(420, parent->width() - 16)));
        panelSize.setHeight(std::min(560, std::max(300, parent->height() - 16)));
    }
    resize(panelSize);

    QPoint target = pos;
    if (parent)
    {
        target.setX(std::clamp(target.x(), 8, std::max(8, parent->width() - width() - 8)));
        target.setY(std::clamp(target.y(), 8, std::max(8, parent->height() - height() - 8)));
    }

    if (qApp)
    {
        qApp->removeEventFilter(this);
        qApp->installEventFilter(this);
    }

    const QPoint start = target - QPoint(18, 0);
    move(start);
    show();
    raise();

    auto* animation = new QPropertyAnimation(this, "pos", this);
    animation->setDuration(160);
    animation->setStartValue(start);
    animation->setEndValue(target);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void WorkbenchPanel::hidePanel()
{
    if (!isVisible())
        return;
    if (qApp)
        qApp->removeEventFilter(this);
    hide();
}

bool WorkbenchPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (!isVisible())
        return QFrame::eventFilter(watched, event);

    if (event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape)
        {
            hidePanel();
            return false;
        }
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        // Dialogs and popups live outside the panel but belong to it.
        if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
            return QFrame::eventFilter(watched, event);

        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint global = mouse->globalPosition().toPoint();
        if (m_ignoreWidget
            && m_ignoreWidget->rect().contains(m_ignoreWidget->mapFromGlobal(global)))
            return QFrame::eventFilter(watched, event);
        if (!rect().contains(mapFromGlobal(global)))
            hidePanel();
    }

    return QFrame::eventFilter(watched, event);
}

void WorkbenchPanel::updateCreateState()
{
    const int page = m_pages ? m_pages->currentIndex() : 0;
    bool ready = true;
    if (m_session)
    {
        if (page == 0)
            ready = !m_mediaHash.isEmpty();
        else if (page == 3)
            ready = !m_wallContent.logos.isEmpty();
    }
    else if (page == 0)
        ready = !m_mediaPath.isEmpty();
    m_create->setEnabled(ready);
}

void WorkbenchPanel::refreshMediaLibrary()
{
    if (!m_mediaLibrary)
        return;

    const QSignalBlocker blocker(m_mediaLibrary);
    m_mediaLibrary->clear();

    if (m_session)
    {
        for (const assets::StoredAsset& asset : m_session->assets().list())
        {
            if (asset.type != AssetType::Image && asset.type != AssetType::Video
                && asset.type != AssetType::Audio)
                continue;
            auto* item = new QListWidgetItem(asset.name, m_mediaLibrary);
            item->setData(Qt::UserRole, asset.hash);
            item->setToolTip(asset.directory);

            const QStringList files = QDir(asset.directory).entryList(
                {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
            if (!files.isEmpty() && asset.type == AssetType::Image)
            {
                const QImage image(asset.directory + QLatin1Char('/')
                                   + files.first());
                if (!image.isNull())
                    item->setIcon(QIcon(QPixmap::fromImage(image).scaled(
                        40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            else if (!files.isEmpty() && asset.type == AssetType::Video)
            {
                video2::VideoFrames frames(
                    std::max(1, m_session->appSettings().videoProxyWidth), 4);
                video2::Request request;
                request.path = asset.directory + QLatin1Char('/') + files.first();
                request.sourceFrame = 0;
                request.fps = std::max(1, m_session->settings().fps);
                request.size = QSize(80, 80);
                const QImage frame = frames.frame(request);
                if (!frame.isNull())
                    item->setIcon(QIcon(QPixmap::fromImage(frame).scaled(
                        40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            if (asset.hash == m_mediaHash)
                m_mediaLibrary->setCurrentItem(item);
        }
        return;
    }

}

void WorkbenchPanel::selectMedia(int templateIndex)
{
    Q_UNUSED(templateIndex);
}

void WorkbenchPanel::selectSessionMedia(const QString& hash)
{
    if (!m_session || !m_mediaPreview || !m_mediaInfo)
        return;
    const auto asset = m_session->assets().get(hash);
    if (!asset)
        return;

    m_mediaHash = hash;
    m_mediaTemplateIndex = -1;
    const QStringList files = QDir(asset->directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    m_mediaPath = files.isEmpty() ? QString()
                                  : asset->directory + QLatin1Char('/') + files.first();

    const QSize previewSize(300, 140);
    QPixmap preview;
    if (asset->type == AssetType::Image && !m_mediaPath.isEmpty())
    {
        const QImage image(m_mediaPath);
        if (!image.isNull())
            preview = QPixmap::fromImage(image.scaled(
                previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else if (asset->type == AssetType::Video && !m_mediaPath.isEmpty())
    {
        video2::VideoFrames frames(
            std::max(1, m_session->appSettings().videoProxyWidth), 4);
        video2::Request request;
        request.path = m_mediaPath;
        request.sourceFrame = 0;
        request.fps = std::max(1, m_session->settings().fps);
        request.size = previewSize;
        const QImage frame = frames.frame(request);
        if (!frame.isNull())
            preview = QPixmap::fromImage(frame);
    }
    if (preview.isNull())
        m_mediaPreview->setText(assetTypeLabelForWorkbench(asset->type));
    else
        m_mediaPreview->setPixmap(preview);

    QString info = assetTypeLabelForWorkbench(asset->type)
                   + QStringLiteral(" · ") + asset->name;
    const double seconds =
        asset->metadata.value(QStringLiteral("durationSeconds")).toDouble(0.0);
    if (seconds > 0.0)
        info += QStringLiteral(" · %1 秒").arg(seconds, 0, 'f', 2);
    info += QStringLiteral("\n") + m_mediaPath;
    m_mediaInfo->setText(info);
    updateCreateState();
}

void WorkbenchPanel::pickMedia()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择媒体文件"), QString(),
        QStringLiteral("媒体文件 (*.png *.jpg *.jpeg *.webp *.bmp *.mp4 *.webm *.mov "
                       "*.mkv *.avi *.m4v *.wav *.mp3 *.flac *.ogg *.opus *.m4a "
                       "*.aac *.wma *.aiff *.aif *.mka);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    if (m_session)
    {
        const auto type = assets::detectType(path);
        QString error;
        std::optional<assets::StoredAsset> stored;
        if (type && *type == AssetType::Video)
        {
            const auto video = m_session->assets().importVideoWithAudio(path, &error);
            if (video)
                stored = video->video;
        }
        else if (type)
            stored = m_session->assets().importFile(path, &error);

        if (!stored)
        {
            QMessageBox::warning(this, QStringLiteral("无法导入"),
                                 error.isEmpty() ? QStringLiteral("导入失败") : error);
            return;
        }
        m_session->document()->commitProject(
            QStringLiteral("pvf: import asset \"%1\"").arg(stored->name), nullptr);
        refreshMediaLibrary();
        for (int row = 0; row < m_mediaLibrary->count(); ++row)
            if (m_mediaLibrary->item(row)->data(Qt::UserRole).toString()
                == stored->hash)
            {
                m_mediaLibrary->setCurrentRow(row);
                break;
            }
        selectSessionMedia(stored->hash);
        return;
    }

}

TextStyle WorkbenchPanel::currentTextStyle() const
{
    TextStyle style = m_textStyle;
    style.text = m_textContent->text().isEmpty() ? QStringLiteral("Text")
                                                  : m_textContent->text();
    style.fontFamily = m_textFont->currentFont().family();
    style.pixelSize = m_textSize->value();
    style.color = m_textColorValue;
    style.align = m_textAlign->currentData().toInt();
    return style;
}

void WorkbenchPanel::updateSpecialPreview()
{
    if (!m_specialPreview)
        return;

    const int row = m_specialTypes ? m_specialTypes->currentRow() : 0;
    QImage image(240, 150, QImage::Format_ARGB32);
    image.fill(QColor(30, 30, 36));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF centre(image.width() / 2.0, image.height() / 2.0);
    const auto fit = [&](const QRectF& bounds) {
        return std::min((image.width() - 16.0) / std::max(1.0, bounds.width()),
                         (image.height() - 16.0) / std::max(1.0, bounds.height()));
    };

    if (row == 0)
    {
        RadialParams params;
        params.count = m_radialCount->value();
        params.width = m_radialThickness->value();
        params.flicker = m_radialFlicker->value();
        params.color = m_radialColorValue;
        painter.save();
        painter.translate(centre);
        painter.scale(fit(LineDeco::radialBounds(params)),
                      fit(LineDeco::radialBounds(params)));
        LineDeco::paintRadial(painter, params, 0.0);
        painter.restore();
    }
    else if (row == 1)
    {
        FlowParams params;
        params.count = m_flowCount->value();
        params.speed = m_flowSpeed->value();
        params.angle = m_flowAngle->value();
        params.color = m_flowColorValue;
        painter.save();
        painter.translate(centre);
        painter.scale(fit(LineDeco::flowBounds(params)),
                      fit(LineDeco::flowBounds(params)));
        LineDeco::paintFlow(painter, params, 0.0);
        painter.restore();
    }
    else if (row == 2)
    {
        HalftoneParams params;
        params.area = m_halftoneArea->value();
        params.spacing = m_halftoneSpacing->value();
        params.dotSize = m_halftoneSize->value();
        params.angle = m_halftoneAngle->value();
        params.gradientAngle = m_halftoneGradient->value();
        params.minRatio = m_halftoneMin->value();
        params.jitter = m_halftoneJitter->value();
        params.color = m_halftoneColorValue;
        painter.save();
        painter.translate(centre);
        painter.scale(fit(LineDeco::halftoneBounds(params)),
                      fit(LineDeco::halftoneBounds(params)));
        LineDeco::paintHalftone(painter, params);
        painter.restore();
    }
    else
    {
        painter.save();
        painter.translate(centre);
        painter.scale(fit(BrandWall::bounds(m_wallParams)),
                      fit(BrandWall::bounds(m_wallParams)));
        BrandWall::paint(painter, m_wallParams, m_wallImages);
        painter.restore();
    }
    m_specialPreview->setPixmap(QPixmap::fromImage(image));
}

void WorkbenchPanel::updateTextBoxPreview()
{
    if (!m_textBoxPreview)
        return;

    const int row = m_textBoxTypes ? m_textBoxTypes->currentRow() : 0;
    int shape = m_textBoxShape ? m_textBoxShape->currentData().toInt() : 0;
    if (row == 1)
        shape = 1;   // Japanese hard rectangle
    else if (row == 2)
        shape = 3;   // explosion

    TextStyle style = m_comicStyle;
    style.text = m_comicContent->text().isEmpty() ? QStringLiteral("台词")
                                                   : m_comicContent->text();
    style.fontFamily = QFont().family();
    style.pixelSize = m_comicSize->value();
    style.color = m_comicColorValue;
    style.align = int(Qt::AlignCenter);
    BubbleStyle bubble;
    bubble.shape = shape;
    bubble.tail = row == 0;
    bubble.fill = m_comicFillValue;
    bubble.stroke = m_comicStrokeValue;
    bubble.strokeWidth = m_comicStrokeWidth ? m_comicStrokeWidth->value() : 6.0;
    const ComicBox box = ComicBox::build(style, bubble);
    const QRectF bounds = box.bounds();

    QImage image(280, 150, QImage::Format_ARGB32);
    image.fill(QColor(30, 30, 36));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double scale =
        std::min((image.width() - 16.0) / std::max(1.0, bounds.width()),
                 (image.height() - 16.0) / std::max(1.0, bounds.height()));
    painter.translate(image.width() / 2.0, image.height() / 2.0);
    painter.scale(scale, scale);
    box.paint(painter);
    m_textBoxPreview->setPixmap(QPixmap::fromImage(image));
}


void WorkbenchPanel::createSessionBrandWall()
{
    if (!m_session || !m_session->document())
        return;
    if (m_wallContent.logos.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("创建失败"),
                             QStringLiteral("品牌墙至少需要一个文字标签。"));
        return;
    }

    auto* document = m_session->document();
    const model::Project& project = m_session->project();
    const int fps = std::max(1, m_session->settings().fps);
    const int start = std::max(0, m_session->playhead());

    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("品牌瓷砖墙");
    entity.start = start;
    entity.end = start + fps;
    for (const model::Entity& other : project.entities)
    {
        if (other.start < entity.end && entity.start < other.end)
            entity.row = std::max(entity.row, other.row + 1);
    }
    entity.content.kind = ContentKind::BrandWall;
    entity.content.brandWall = m_wallContent;
    entity.transform.x = m_session->settings().width / 2.0;
    entity.transform.y = m_session->settings().height / 2.0;

    document->mutableProject().entities.insert(entity.uuid, entity);
    QString error;
    if (!document->commitEntity(entity.uuid,
                                QStringLiteral("pvf: create brand wall"),
                                &error))
    {
        document->mutableProject().entities.remove(entity.uuid);
        QMessageBox::warning(this, QStringLiteral("创建失败"), error);
        return;
    }

    document->setSelection(entity.uuid);
    emit sessionCreated(entity.uuid);
    hidePanel();
}

void WorkbenchPanel::createSessionCurrent()
{
    const int page = m_pages ? m_pages->currentIndex() : 0;
    switch (page)
    {
    case 0:
        createSessionMedia();
        break;
    case 1:
        createSessionText();
        break;
    case 2:
        createSessionVector();
        break;
    case 3:
        createSessionBrandWall();
        break;
    case 4:
        createSessionCamera();
        break;
    default:
        break;
    }
}

bool WorkbenchPanel::createSessionMedia()
{
    if (!m_session || !m_session->document() || m_mediaHash.isEmpty())
        return false;

    QString error;
    const QVector<Uuid> created = m_session->editActions()->addAssetEntities(
        m_mediaHash, std::max(0, m_session->playhead()), -1, Uuid(), &error);
    if (created.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("创建失败"),
                             error.isEmpty() ? QStringLiteral("素材无法使用")
                                             : error);
        return false;
    }
    m_session->document()->setSelection(created.first());
    emit sessionCreated(created.first());
    hidePanel();
    return true;
}

bool WorkbenchPanel::createSessionText()
{
    if (!m_session || !m_session->document())
        return false;

    const TextStyle style = currentTextStyle();
    model::Entity entity;
    entity.name = style.text.isEmpty() ? QStringLiteral("文字")
                                       : style.text.left(24);
    const int fps = std::max(1, m_session->settings().fps);
    entity.start = std::max(0, m_session->playhead());
    entity.end = entity.start + fps;
    entity.row = -1;
    entity.content.kind = ContentKind::Text;
    model::TextContent& text = entity.content.text;
    text.text = style.text;
    text.fontFamily = style.fontFamily;
    text.pixelSize = style.pixelSize;
    text.color = style.color.name(QColor::HexArgb);
    text.strokeColor = style.strokeColor.name(QColor::HexArgb);
    text.strokeWidth = style.strokeWidth;
    text.align = style.align;
    text.lineHeight = style.lineHeight;
    text.letterSpacing = style.letterSpacing;
    text.padding = style.padding;
    text.boxWidth = style.boxWidth;
    text.boxHeight = style.boxHeight;
    text.bold = style.bold;
    text.italic = style.italic;
    text.underline = style.underline;
    text.vertical = style.vertical;
    text.verticalRtl = style.verticalRtl;

    if (!createSessionEntity(entity, QStringLiteral("pvf: create text")))
        return false;
    return true;
}

bool WorkbenchPanel::createSessionVector()
{
    if (!m_session || !m_session->document())
        return false;

    const int shape = m_vectorShapes && m_vectorShapes->currentItem()
                          ? m_vectorShapes->currentItem()
                                ->data(Qt::UserRole)
                                .toInt()
                          : 0;
    model::Entity entity;
    entity.name = shape == 1 ? QStringLiteral("圆形")
                             : shape == 2 ? QStringLiteral("直线")
                                          : QStringLiteral("矩形");
    const int fps = std::max(1, m_session->settings().fps);
    entity.start = std::max(0, m_session->playhead());
    entity.end = entity.start + fps;
    entity.row = -1;
    entity.content.kind = ContentKind::Vector;
    model::VectorContent& vector = entity.content.vector;
    vector.shape = shape;
    vector.width = m_vectorWidth ? m_vectorWidth->value() : 400.0;
    vector.height = m_vectorHeight ? m_vectorHeight->value() : 300.0;
    vector.cornerRadius = m_vectorCorner ? m_vectorCorner->value() : 0.0;
    vector.sides = m_vectorSides ? m_vectorSides->value() : 5;
    vector.innerRatio = m_vectorInner ? m_vectorInner->value() : 0.45;
    vector.fillColor = m_fillValue.name(QColor::HexArgb);
    vector.strokeColor = m_strokeValue.name(QColor::HexArgb);
    vector.strokeWidth =
        m_vectorStrokeWidth ? m_vectorStrokeWidth->value() : 0.0;

    if (!createSessionEntity(entity, QStringLiteral("pvf: create vector")))
        return false;
    return true;
}

bool WorkbenchPanel::createSessionCamera()
{
    if (!m_session || !m_session->document())
        return false;

    QString name = m_cameraName ? m_cameraName->text().trimmed() : QString();
    if (name.isEmpty())
    {
        int count = 0;
        for (const model::Entity& other : m_session->project().entities)
            if (other.content.kind == ContentKind::Camera)
                ++count;
        name = QStringLiteral("摄像机 %1").arg(count + 1);
    }

    model::Entity entity;
    entity.name = name;
    const int fps = std::max(1, m_session->settings().fps);
    entity.start = std::max(0, m_session->playhead());
    entity.end = entity.start + fps;
    entity.row = -1;
    entity.content.kind = ContentKind::Camera;

    if (!createSessionEntity(entity, QStringLiteral("pvf: create camera")))
        return false;
    return true;
}

bool WorkbenchPanel::createSessionEntity(model::Entity entity,
                                         const QString& message)
{
    if (!m_session || !m_session->document())
        return false;

    auto* document = m_session->document();
    entity.uuid = model::newUuid();
    if (entity.row < 0)
        entity.row = model::nextAutoRow(m_session->project(), Uuid{},
                                        entity.start, entity.end);
    document->mutableProject().entities.insert(entity.uuid, entity);
    QString error;
    if (!document->commitEntity(entity.uuid, message, &error))
    {
        document->mutableProject().entities.remove(entity.uuid);
        QMessageBox::warning(this, QStringLiteral("创建失败"), error);
        return false;
    }

    document->setSelection(entity.uuid);
    emit sessionCreated(entity.uuid);
    hidePanel();
    return true;
}

void WorkbenchPanel::createCurrent()
{
    if (m_session)
    {
        createSessionCurrent();
        return;
    }
}

void WorkbenchPanel::setColorButton(QPushButton* button, const QColor& color)
{
    button->setText(color.name().toUpper());
    const QString text = color.lightness() > 140 ? QStringLiteral("#000")
                                                 : QStringLiteral("#fff");
    button->setStyleSheet(
        QStringLiteral("background-color:%1;color:%2;").arg(color.name(), text));
}

QColor WorkbenchPanel::pickColor(const QColor& initial, const QString& title)
{
    return QColorDialog::getColor(initial, this, title, QColorDialog::ShowAlphaChannel);
}

} // namespace pvf
