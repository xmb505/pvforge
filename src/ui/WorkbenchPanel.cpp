#include "WorkbenchPanel.h"

#include "app/AppIcon.h"
#include "app/MainWindow.h"
#include "core/ComicBox.h"
#include "core/Lang.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "ui/TextLayoutDialog.h"
#include "media/MediaImporter.h"

#include <QApplication>
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace pvf {

WorkbenchPanel::WorkbenchPanel(Project* project, QWidget* parent)
    : QFrame(parent)
    , m_project(project)
{
    setObjectName(QStringLiteral("workbench"));
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "QFrame#workbench { background: palette(window); border: 1px solid palette(mid); }"));
    setFixedSize(640, 560);

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

    const QStringList typeNames = {
        Lang::t(QStringLiteral("workbench.media"), QStringLiteral("媒体")),
        Lang::t(QStringLiteral("workbench.text"), QStringLiteral("文字")),
        Lang::t(QStringLiteral("workbench.vector"), QStringLiteral("矢量")),
        Lang::t(QStringLiteral("workbench.camera"), QStringLiteral("摄像机")),
        Lang::t(QStringLiteral("workbench.special"), QStringLiteral("特殊")),
    };
    for (const QString& name : typeNames)
    {
        auto* item = new QListWidgetItem(name, m_types);
        item->setSizeHint(QSize(0, 42));
    }
    root->addWidget(m_types);

    auto* right = new QVBoxLayout();
    right->setSpacing(10);
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("typePages"));
    m_pages->addWidget(buildMediaPage());
    m_pages->addWidget(buildTextPage());
    m_pages->addWidget(buildVectorPage());
    m_pages->addWidget(buildCameraPage());
    m_pages->addWidget(buildSpecialPage());
    right->addWidget(m_pages, 1);

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

QWidget* WorkbenchPanel::buildSpecialPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_specialTypes = new QListWidget(page);
    m_specialTypes->setObjectName(QStringLiteral("specialTypes"));
    const QStringList names = {
        QStringLiteral("漫画文本框 · 圆球尖尾"),
        QStringLiteral("漫画文本框 · 矩形"),
        QStringLiteral("漫画文本框 · 爆炸"),
        QStringLiteral("放射状线条"),
        QStringLiteral("快速流过"),
    };
    for (const QString& name : names)
        m_specialTypes->addItem(name);
    m_specialTypes->setMaximumHeight(140);
    layout->addWidget(m_specialTypes);

    m_specialPreview = new QLabel(page);
    m_specialPreview->setObjectName(QStringLiteral("specialPreview"));
    m_specialPreview->setAlignment(Qt::AlignCenter);
    m_specialPreview->setMinimumHeight(150);
    m_specialPreview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_specialPreview);

    m_specialForms = new QStackedWidget(page);

    auto* comic = new QWidget(m_specialForms);
    auto* comicForm = new QFormLayout(comic);
    comicForm->setContentsMargins(0, 0, 0, 0);
    comicForm->setVerticalSpacing(8);
    m_comicContent = new QLineEdit(QStringLiteral("台词"), comic);
    m_comicContent->setObjectName(QStringLiteral("comicContent"));
    comicForm->addRow(QStringLiteral("内容"), m_comicContent);
    m_comicSize = new QSpinBox(comic);
    m_comicSize->setRange(12, 400);
    m_comicSize->setValue(72);
    m_comicSize->setSuffix(QStringLiteral(" px"));
    comicForm->addRow(QStringLiteral("字号"), m_comicSize);
    m_comicColor = new QPushButton(comic);
    setColorButton(m_comicColor, m_comicColorValue);
    connect(m_comicColor, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_comicColorValue, QStringLiteral("文字颜色"));
        if (color.isValid())
        {
            m_comicColorValue = color;
            setColorButton(m_comicColor, color);
            updateSpecialPreview();
        }
    });
    comicForm->addRow(QStringLiteral("文字颜色"), m_comicColor);
    m_specialForms->addWidget(comic);

    auto* radial = new QWidget(m_specialForms);
    auto* radialForm = new QFormLayout(radial);
    radialForm->setContentsMargins(0, 0, 0, 0);
    radialForm->setVerticalSpacing(8);
    m_radialCount = new QSpinBox(radial);
    m_radialCount->setRange(4, 200);
    m_radialCount->setValue(48);
    radialForm->addRow(QStringLiteral("线条数"), m_radialCount);
    m_radialThickness = new QDoubleSpinBox(radial);
    m_radialThickness->setRange(1, 120);
    m_radialThickness->setValue(18);
    radialForm->addRow(QStringLiteral("线宽"), m_radialThickness);
    m_radialColor = new QPushButton(radial);
    setColorButton(m_radialColor, m_radialColorValue);
    connect(m_radialColor, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_radialColorValue, QStringLiteral("线条颜色"));
        if (color.isValid())
        {
            m_radialColorValue = color;
            setColorButton(m_radialColor, color);
            updateSpecialPreview();
        }
    });
    radialForm->addRow(QStringLiteral("颜色"), m_radialColor);
    m_specialForms->addWidget(radial);

    auto* flow = new QWidget(m_specialForms);
    auto* flowForm = new QFormLayout(flow);
    flowForm->setContentsMargins(0, 0, 0, 0);
    flowForm->setVerticalSpacing(8);
    m_flowCount = new QSpinBox(flow);
    m_flowCount->setRange(2, 200);
    m_flowCount->setValue(26);
    flowForm->addRow(QStringLiteral("线条数"), m_flowCount);
    m_flowSpeed = new QDoubleSpinBox(flow);
    m_flowSpeed->setRange(0, 6000);
    m_flowSpeed->setValue(900);
    m_flowSpeed->setSuffix(QStringLiteral(" px/s"));
    flowForm->addRow(QStringLiteral("速度"), m_flowSpeed);
    m_flowAngle = new QDoubleSpinBox(flow);
    m_flowAngle->setRange(-180, 180);
    m_flowAngle->setValue(0);
    m_flowAngle->setSuffix(QStringLiteral("°"));
    flowForm->addRow(QStringLiteral("角度"), m_flowAngle);
    m_flowColor = new QPushButton(flow);
    setColorButton(m_flowColor, m_flowColorValue);
    connect(m_flowColor, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_flowColorValue, QStringLiteral("线条颜色"));
        if (color.isValid())
        {
            m_flowColorValue = color;
            setColorButton(m_flowColor, color);
            updateSpecialPreview();
        }
    });
    flowForm->addRow(QStringLiteral("颜色"), m_flowColor);
    m_specialForms->addWidget(flow);

    layout->addWidget(m_specialForms, 1);

    connect(m_specialTypes, &QListWidget::currentRowChanged, this, [this](int) {
        if (m_specialForms)
        {
            const int row = m_specialTypes->currentRow();
            m_specialForms->setCurrentIndex(row <= 2 ? 0 : (row == 3 ? 1 : 2));
        }
        updateSpecialPreview();
    });
    connect(m_comicContent, &QLineEdit::textChanged, this,
            [this](const QString&) { updateSpecialPreview(); });
    connect(m_comicSize, &QSpinBox::valueChanged, this,
            [this](int) { updateSpecialPreview(); });

    m_specialTypes->setCurrentRow(0);
    return page;
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

    QWidget* parent = parentWidget();
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
    if (page == 0)
        ready = !m_mediaPath.isEmpty();
    m_create->setEnabled(ready);
}

void WorkbenchPanel::refreshMediaLibrary()
{
    if (!m_mediaLibrary)
        return;

    const QSignalBlocker blocker(m_mediaLibrary);
    m_mediaLibrary->clear();

    for (int i = 0; i < m_project->templateCount(); ++i)
    {
        const Template* tmpl = m_project->templateAt(i);
        if (!tmpl
            || (tmpl->type != EntityType::Image && tmpl->type != EntityType::Video
                && tmpl->type != EntityType::Audio))
            continue;

        auto* item = new QListWidgetItem(tmpl->name, m_mediaLibrary);
        item->setData(Qt::UserRole, i);
        item->setToolTip(tmpl->path);
        if (!tmpl->thumbnail.isNull())
            item->setIcon(QIcon(QPixmap::fromImage(tmpl->thumbnail)
                                    .scaled(40, 40, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation)));
    }
}

void WorkbenchPanel::selectMedia(int templateIndex)
{
    const Template* tmpl = m_project->templateAt(templateIndex);
    if (!tmpl)
        return;

    m_mediaPath = tmpl->path;
    m_mediaTemplateIndex = templateIndex;

    if (!tmpl->thumbnail.isNull())
        m_mediaPreview->setPixmap(QPixmap::fromImage(tmpl->thumbnail)
                                      .scaled(300, 140, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    else
        m_mediaPreview->setText(QStringLiteral("无预览"));

    QString typeName;
    if (tmpl->type == EntityType::Video)
        typeName = QStringLiteral("视频");
    else if (tmpl->type == EntityType::Audio)
        typeName = QStringLiteral("音频");
    else
        typeName = QStringLiteral("图片");
    QString info = typeName + QStringLiteral(" · ") + tmpl->name;
    if ((tmpl->type == EntityType::Video || tmpl->type == EntityType::Audio)
        && tmpl->duration > 0.0)
        info += QStringLiteral(" · %1 秒").arg(tmpl->duration, 0, 'f', 2);
    info += QStringLiteral("\n") + tmpl->path;
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

    auto* mainWindow = qobject_cast<MainWindow*>(window());
    if (!mainWindow)
        return;

    mainWindow->importMediaFiles(
        {path}, [this](const QVector<MediaImportResult>& results) {
            if (results.isEmpty())
                return;
            const MediaImportResult& result = results.first();
            if (!result.error.isEmpty() || result.importedPath.isEmpty())
            {
                QMessageBox::warning(this, QStringLiteral("无法导入"), result.error);
                return;
            }

            if (!m_project->addImportedMedia(result))
                return;

            refreshMediaLibrary();
            for (int i = 0; i < m_project->templateCount(); ++i)
            {
                const Template* tmpl = m_project->templateAt(i);
                if (!tmpl || tmpl->path != result.importedPath)
                    continue;
                for (int row = 0; row < m_mediaLibrary->count(); ++row)
                {
                    if (m_mediaLibrary->item(row)->data(Qt::UserRole).toInt() == i)
                    {
                        m_mediaLibrary->setCurrentRow(row);
                        break;
                    }
                }
                selectMedia(i);
                break;
            }
        });
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

    if (row <= 2)
    {
        TextStyle style;
        style.text = m_comicContent->text().isEmpty() ? QStringLiteral("台词")
                                                       : m_comicContent->text();
        style.fontFamily = QFont().family();
        style.pixelSize = m_comicSize->value();
        style.color = m_comicColorValue;
        style.align = int(Qt::AlignCenter);
        style.padding = 16.0;
        BubbleStyle bubble;
        bubble.shape = row;
        const ComicBox box = ComicBox::build(style, bubble);
        painter.save();
        painter.translate(centre);
        painter.scale(fit(box.bounds()), fit(box.bounds()));
        box.paint(painter);
        painter.restore();
    }
    else if (row == 3)
    {
        RadialParams params;
        params.count = m_radialCount->value();
        params.width = m_radialThickness->value();
        params.color = m_radialColorValue;
        painter.save();
        painter.translate(centre);
        painter.scale(fit(LineDeco::radialBounds(params)),
                      fit(LineDeco::radialBounds(params)));
        LineDeco::paintRadial(painter, params, 0.0);
        painter.restore();
    }
    else
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
    m_specialPreview->setPixmap(QPixmap::fromImage(image));
}

EntityId WorkbenchPanel::createSpecial()
{
    const int row = m_specialTypes ? m_specialTypes->currentRow() : 0;
    Entity* entity = nullptr;

    if (row <= 2)
    {
        TextStyle style;
        style.text = m_comicContent->text().isEmpty() ? QStringLiteral("台词")
                                                       : m_comicContent->text();
        style.fontFamily = QFont().family();
        style.pixelSize = m_comicSize->value();
        style.color = m_comicColorValue;
        style.align = int(Qt::AlignCenter);
        style.padding = 18.0;
        BubbleStyle bubble;
        bubble.shape = row;
        QVariantMap data;
        style.writeTo(data);
        bubble.writeTo(data);
        entity = m_project->addEntity(EntityType::ComicText,
                                      QStringLiteral("漫画文本框"), data);
    }
    else if (row == 3)
    {
        RadialParams params;
        params.count = m_radialCount->value();
        params.width = m_radialThickness->value();
        params.color = m_radialColorValue;
        QVariantMap data;
        params.writeTo(data);
        entity = m_project->addEntity(EntityType::RadialLines,
                                      QStringLiteral("放射状线条"), data);
    }
    else
    {
        FlowParams params;
        params.count = m_flowCount->value();
        params.speed = m_flowSpeed->value();
        params.angle = m_flowAngle->value();
        params.color = m_flowColorValue;
        QVariantMap data;
        params.writeTo(data);
        entity = m_project->addEntity(EntityType::FlowLines,
                                      QStringLiteral("快速流过"), data);
    }
    return entity ? entity->id : kInvalidEntity;
}

void WorkbenchPanel::createCurrent()
{
    const int page = m_pages->currentIndex();
    EntityId id = kInvalidEntity;

    if (page == 0)
    {
        if (m_mediaPath.isEmpty())
            return;

        if (const Template* tmpl = m_project->templateAt(m_mediaTemplateIndex))
        {
            if (Entity* e = m_project->createFromTemplate(*tmpl))
                id = e->id;
        }
    }
    else if (page == 1)
    {
        if (Entity* e = m_project->addText(currentTextStyle()))
            id = e->id;
    }
    else if (page == 2)
    {
        const int shape = m_vectorShapes->currentItem()
                              ? m_vectorShapes->currentItem()->data(Qt::UserRole).toInt()
                              : 0;
        Entity* e = m_project->addVector(shape, m_vectorWidth->value(),
                                         m_vectorHeight->value(), m_fillValue,
                                         m_strokeValue, m_vectorStrokeWidth->value());
        if (e)
        {
            e->data[QStringLiteral("cornerRadius")] = m_vectorCorner->value();
            e->data[QStringLiteral("sides")] = m_vectorSides->value();
            e->data[QStringLiteral("innerRatio")] = m_vectorInner->value();
            m_project->notifyChanged();
            id = e->id;
        }
    }
    else if (page == 3)
    {
        QString name = m_cameraName ? m_cameraName->text().trimmed() : QString();
        if (name.isEmpty())
        {
            int count = 0;
            for (const Entity& e : m_project->entities())
                if (e.isCamera())
                    ++count;
            name = QStringLiteral("摄像机 %1").arg(count + 1);
        }
        if (Entity* e = m_project->addCamera(name))
            id = e->id;
    }
    else if (page == 4)
    {
        id = createSpecial();
    }

    if (id == kInvalidEntity)
    {
        QMessageBox::warning(this, QStringLiteral("创建失败"),
                             QStringLiteral("实体创建失败，请检查所选素材。"));
        return;
    }

    emit created(id);
    hidePanel();
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
