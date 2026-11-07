#include "WorkbenchPanel.h"

#include "core/Lang.h"

#include "media/VideoDecoder.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
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
    setFixedSize(560, 460);

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
        Lang::t(QStringLiteral("workbench.image"), QStringLiteral("图片")),
        Lang::t(QStringLiteral("workbench.video"), QStringLiteral("视频")),
        Lang::t(QStringLiteral("workbench.text"), QStringLiteral("文字")),
        Lang::t(QStringLiteral("workbench.vector"), QStringLiteral("矢量")),
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
    m_pages->addWidget(buildImagePage());
    m_pages->addWidget(buildVideoPage());
    m_pages->addWidget(buildTextPage());
    m_pages->addWidget(buildVectorPage());
    right->addWidget(m_pages, 1);

    m_create = new QPushButton(Lang::t(QStringLiteral("workbench.create"), QStringLiteral("创建")), this);
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
    updateCreateState();
}

QWidget* WorkbenchPanel::buildImagePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_imagePreview = new QLabel(QStringLiteral("尚未选择图片"), page);
    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->setMinimumHeight(210);
    m_imagePreview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_imagePreview);

    auto* pick = new QPushButton(Lang::t(QStringLiteral("workbench.selectImage"), QStringLiteral("选择图片…")), page);
    connect(pick, &QPushButton::clicked, this, &WorkbenchPanel::pickImage);
    layout->addWidget(pick);

    m_imageInfo = new QLabel(QStringLiteral("支持 PNG / JPG / WebP / BMP"), page);
    m_imageInfo->setWordWrap(true);
    layout->addWidget(m_imageInfo);
    layout->addStretch(1);
    return page;
}

QWidget* WorkbenchPanel::buildVideoPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_videoPreview = new QLabel(QStringLiteral("尚未选择视频"), page);
    m_videoPreview->setAlignment(Qt::AlignCenter);
    m_videoPreview->setMinimumHeight(210);
    m_videoPreview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_videoPreview);

    auto* pick = new QPushButton(Lang::t(QStringLiteral("workbench.selectVideo"), QStringLiteral("选择视频…")), page);
    connect(pick, &QPushButton::clicked, this, &WorkbenchPanel::pickVideo);
    layout->addWidget(pick);

    m_videoInfo = new QLabel(
        QStringLiteral("支持 MP4 / WebM / MOV / MKV，导入后先显示首帧。逐帧播放后续版本接入。"),
        page);
    m_videoInfo->setWordWrap(true);
    layout->addWidget(m_videoInfo);
    layout->addStretch(1);
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

    return page;
}

QWidget* WorkbenchPanel::buildVectorPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(10);

    m_vectorShape = new QComboBox(page);
    m_vectorShape->addItem(QStringLiteral("矩形"), 0);
    m_vectorShape->addItem(QStringLiteral("圆形"), 1);
    m_vectorShape->addItem(QStringLiteral("直线"), 2);
    form->addRow(QStringLiteral("形状"), m_vectorShape);

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

    return page;
}

void WorkbenchPanel::showAt(const QPoint& pos)
{
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
        ready = !m_imagePath.isEmpty();
    else if (page == 1)
        ready = !m_videoPath.isEmpty();
    m_create->setEnabled(ready);
}

void WorkbenchPanel::createCurrent()
{
    const int page = m_pages->currentIndex();
    EntityId id = kInvalidEntity;

    if (page == 0)
    {
        if (Entity* e = m_project->addImage(m_imagePath))
            id = e->id;
    }
    else if (page == 1)
    {
        if (Entity* e = m_project->addVideo(m_videoPath, m_videoFrame, m_videoDuration))
            id = e->id;
    }
    else if (page == 2)
    {
        Entity* e = m_project->addText(
            m_textContent->text().isEmpty() ? QStringLiteral("Text") : m_textContent->text(),
            m_textFont->currentFont().family(), m_textSize->value(), m_textColorValue,
            m_textAlign->currentData().toInt());
        if (e)
            id = e->id;
    }
    else if (page == 3)
    {
        Entity* e = m_project->addVector(
            m_vectorShape->currentData().toInt(), m_vectorWidth->value(),
            m_vectorHeight->value(), m_fillValue, m_strokeValue,
            m_vectorStrokeWidth->value());
        if (e)
            id = e->id;
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

void WorkbenchPanel::pickImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择图片"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QImage image(path);
    if (image.isNull())
    {
        QMessageBox::warning(this, QStringLiteral("无法读取图片"),
                             QStringLiteral("文件无法解码：\n%1").arg(path));
        return;
    }

    // With packing enabled the file is copied into the project folder.
    const QString imported = m_project->importAsset(path);
    m_imagePath = imported;
    m_imagePreview->setPixmap(QPixmap::fromImage(image.scaled(
        300, 205, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_imageInfo->setText(QStringLiteral("%1 · %2×%3")
                             .arg(QFileInfo(imported).fileName())
                             .arg(image.width())
                             .arg(image.height()));
    updateCreateState();
}

void WorkbenchPanel::pickVideo()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择视频"), QString(),
        QStringLiteral("视频 (*.mp4 *.webm *.mov *.mkv *.avi *.m4v);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    double duration = 0.0;
    const QImage frame = decodeVideoFirstFrame(path, &error, &duration);
    QApplication::restoreOverrideCursor();

    if (frame.isNull())
    {
        QMessageBox::warning(this, QStringLiteral("无法读取视频"), error);
        return;
    }

    const QString imported = m_project->importAsset(path);
    m_videoPath = imported;
    m_videoFrame = frame;
    m_videoDuration = duration;
    m_videoPreview->setPixmap(QPixmap::fromImage(frame.scaled(
        300, 205, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_videoInfo->setText(QStringLiteral("%1 · %2×%3 · 首帧")
                             .arg(QFileInfo(imported).fileName())
                             .arg(frame.width())
                             .arg(frame.height()));
    updateCreateState();
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
