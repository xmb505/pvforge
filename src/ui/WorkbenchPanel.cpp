#include "WorkbenchPanel.h"

#include "app/AppIcon.h"
#include "app/MainWindow.h"
#include "core/Lang.h"
#include "media/MediaImporter.h"
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
#include <QIcon>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPropertyAnimation>
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
        QStringLiteral("支持图片与视频，导入后自动识别并加入素材库。"), page);
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
        if (!tmpl || (tmpl->type != EntityType::Image && tmpl->type != EntityType::Video))
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

    m_mediaType = tmpl->type;
    m_mediaPath = tmpl->path;
    m_mediaName = tmpl->name;
    m_mediaFrame = tmpl->thumbnail;
    m_mediaDuration = tmpl->duration;

    if (!tmpl->thumbnail.isNull())
        m_mediaPreview->setPixmap(QPixmap::fromImage(tmpl->thumbnail)
                                      .scaled(300, 140, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    else
        m_mediaPreview->setText(QStringLiteral("无预览"));

    const QString typeName = tmpl->type == EntityType::Video ? QStringLiteral("视频")
                                                             : QStringLiteral("图片");
    QString info = typeName + QStringLiteral(" · ") + tmpl->name;
    if (tmpl->type == EntityType::Video && tmpl->duration > 0.0)
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
                       "*.mkv *.avi *.m4v);;所有文件 (*)"));
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

            if (!m_project->addMediaTemplate(result.importedPath, result.type,
                                             result.thumbnail, result.duration,
                                             result.displayName))
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

void WorkbenchPanel::createCurrent()
{
    const int page = m_pages->currentIndex();
    EntityId id = kInvalidEntity;

    if (page == 0)
    {
        if (m_mediaPath.isEmpty())
            return;

        if (m_mediaType == EntityType::Video)
        {
            double duration = m_mediaDuration;
            QString error;
            const QImage frame = decodeVideoFirstFrame(m_mediaPath, &error, &duration);
            if (Entity* e = m_project->addVideo(m_mediaPath,
                                                frame.isNull() ? m_mediaFrame : frame,
                                                duration, m_mediaName))
                id = e->id;
        }
        else
        {
            if (Entity* e = m_project->addImage(m_mediaPath, m_mediaName))
                id = e->id;
        }
    }
    else if (page == 1)
    {
        Entity* e = m_project->addText(
            m_textContent->text().isEmpty() ? QStringLiteral("Text") : m_textContent->text(),
            m_textFont->currentFont().family(), m_textSize->value(), m_textColorValue,
            m_textAlign->currentData().toInt());
        if (e)
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
