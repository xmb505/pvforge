#include "WorkbenchPanel.h"

#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "core/Lang.h"
#include "model/Query.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"

#include <QApplication>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

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

void WorkbenchPanel::refreshSession()
{
    if (!m_session)
        return;
    refreshMediaLibrary();
    updateSessionWallSummary();
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
