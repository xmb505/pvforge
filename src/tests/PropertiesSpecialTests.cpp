#include "PropertiesSpecialTests.h"

#include "LegacyTestSupport.h"

namespace {

// Count pixels that are visibly lit; used as a cheap "did anything draw" probe.
int ink(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) > 40)
                ++count;
    return count;
}

struct SpecialHarness
{
    pvf::Project& project;
    int& failures;

    QImage render(int frame) const
    {
        QImage image(400, 300, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        QTransform worldToScreen;
        worldToScreen.scale(0.5, 0.5);
        worldToScreen.translate(200, 150);
        pvf::SceneRenderer::drawEntities(painter, project, frame, worldToScreen,
                                         false);
        return image;
    }
};

// Comic balloons in all four shapes, each rendered on its own.
void checkComicShapes(SpecialHarness& h)
{
    int& failures = h.failures;
    pvf::Project* project = &h.project;

    for (int shape = 0; shape < 4; ++shape)
    {
        pvf::TextStyle style;
        style.text = QStringLiteral("台词\n第二行");
        style.pixelSize = 48;
        style.padding = 14;
        style.color = QColor(30, 30, 36);
        QVariantMap data;
        style.writeTo(data);
        pvf::BubbleStyle bubble;
        bubble.shape = shape;
        bubble.tail = shape == 0 || shape == 2;
        bubble.writeTo(data);

        pvf::Entity* comic = project->addEntity(pvf::EntityType::ComicText,
                                                QStringLiteral("漫画文本框"), data);
        if (!comic)
        {
            qCritical() << "special-test: comic creation failed";
            ++failures;
            continue;
        }
        comic->values[QStringLiteral("x")] = -200.0 + shape * 200.0;
        // Render this balloon alone.
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = other.id != comic->id;
        const QRectF bounds = pvf::SceneRenderer::entityLocalBounds(*comic);
        if (bounds.width() < 10 || bounds.height() < 10)
        {
            qCritical() << "special-test: comic bounds too small" << bounds;
            ++failures;
        }
        QImage shot(400, 300, QImage::Format_ARGB32);
        shot.fill(Qt::black);
        {
            QPainter painter(&shot);
            QTransform worldToScreen;
            worldToScreen.scale(0.5, 0.5);
            worldToScreen.translate(400, 300);
            pvf::SceneRenderer::drawEntities(painter, *project, 0, worldToScreen,
                                             false);
        }
        shot.save(QStringLiteral("/tmp/pvforge_comic_%1.png").arg(shape));
        if (ink(shot) <= 0)
        {
            qCritical() << "special-test: comic shape" << shape << " drew nothing";
            ++failures;
        }
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = false;
    }
}

// Vertical tategaki inside an ellipse: the whole text block has to fit, not
// just its centre line.
void checkVerticalTextBalloon(int& failures)
{
    pvf::TextStyle style;
    style.text = QStringLiteral("我看旁边的小日子\n过得不错的\n霓虹酱");
    style.pixelSize = 48;
    style.color = QColor(30, 30, 36);
    style.padding = 18;
    style.vertical = true;
    pvf::BubbleStyle bubble;
    bubble.shape = 0;
    bubble.tail = true;

    const pvf::ComicBox box = pvf::ComicBox::build(style, bubble);
    const pvf::TextBlock block = pvf::TextBlock::measure(style);
    if (box.bodySize().width() < block.contentSize().width() * 1.35
        || box.bodySize().height() < block.contentSize().height() * 1.35)
    {
        qCritical() << "special-test: ellipse too tight for vertical text"
                    << box.bodySize() << block.contentSize();
        ++failures;
    }

    pvf::TextStyle spacedStyle = style;
    spacedStyle.letterSpacing = 12;
    if (pvf::TextBlock::measure(spacedStyle).contentSize().height()
        <= block.contentSize().height() + 1.0)
    {
        qCritical() << "special-test: vertical letter spacing did not expand";
        ++failures;
    }

    QImage image(420, 800, QImage::Format_ARGB32);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(image.width() / 2.0, image.height() / 2.0);
        box.paint(painter);
    }
    image.save(QStringLiteral("/tmp/pvf_comic_vertical.png"));
    if (ink(image) <= 0)
    {
        qCritical() << "special-test: vertical balloon drew nothing";
        ++failures;
    }
}

// Balloon bounds must include half the stroke, or the rasterised image clips
// the outline at its top/bottom extremes.
void checkBalloonBounds(int& failures)
{
    QVariantMap data;
    data.insert(QStringLiteral("text"), QStringLiteral("真是个变态啊"));
    data.insert(QStringLiteral("size"), 96);
    const pvf::ComicBox box = pvf::ComicBox::build(
        pvf::TextStyle::fromData(data), pvf::BubbleStyle::fromData(data));

    QImage canvas(1600, 1600, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.translate(800, 800);
        box.paint(painter);
    }
    const QRectF bounds = box.bounds();
    const QRect area(int(std::floor(bounds.left())) + 800,
                     int(std::floor(bounds.top())) + 800,
                     int(std::ceil(bounds.width())),
                     int(std::ceil(bounds.height())));
    int edge = 0;
    const QRect edgeRect = area.adjusted(0, 0, -1, -1);
    for (int x = edgeRect.left(); x <= edgeRect.right(); ++x)
    {
        if (qAlpha(canvas.pixel(x, edgeRect.top())) > 0)
            ++edge;
        if (qAlpha(canvas.pixel(x, edgeRect.bottom())) > 0)
            ++edge;
    }
    for (int y = edgeRect.top(); y <= edgeRect.bottom(); ++y)
    {
        if (qAlpha(canvas.pixel(edgeRect.left(), y)) > 0)
            ++edge;
        if (qAlpha(canvas.pixel(edgeRect.right(), y)) > 0)
            ++edge;
    }
    canvas.save(QStringLiteral("/tmp/pvf_bubble_bounds.png"));
    if (edge >= 50)
    {
        qCritical() << "special-test: balloon stroke clipped at bounds"
                    << edge << bounds;
        ++failures;
    }
}

// Glyph outline: stroke colour appears around the fill.
void checkGlyphOutline(int& failures)
{
    pvf::TextStyle style;
    style.text = QStringLiteral("描边");
    style.pixelSize = 64;
    style.color = Qt::white;
    style.strokeColor = QColor(220, 30, 60);
    style.strokeWidth = 6;
    QImage image(320, 200, QImage::Format_ARGB32);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        pvf::TextBlock::measure(style).paint(painter, QPointF(160, 100));
    }
    image.save(QStringLiteral("/tmp/pvf_text_stroke.png"));
    int fill = 0;
    int outline = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > 200 && color.green() > 200 && color.blue() > 200)
                ++fill;
            else if (color.red() > 150 && color.green() < 100)
                ++outline;
        }
    if (fill == 0 || outline == 0)
    {
        qCritical() << "special-test: text outline missing" << fill << outline;
        ++failures;
    }
}

// Radial rays.
void checkRadialLines(SpecialHarness& h)
{
    int& failures = h.failures;
    pvf::Project* project = &h.project;
    const auto render = [&h](int frame) { return h.render(frame); };

    pvf::RadialParams params;
    params.count = 32;
    params.radius = 200;
    params.width = 12;
    QVariantMap data;
    params.writeTo(data);
    pvf::Entity* radial = project->addEntity(pvf::EntityType::RadialLines,
                                             QStringLiteral("放射状线条"), data);
    if (!radial)
    {
        qCritical() << "special-test: radial creation failed";
        ++failures;
    }
    else
    {
        radial->values[QStringLiteral("x")] = 260.0;
        const QImage first = render(0);
        if (ink(first) <= 0)
        {
            qCritical() << "special-test: radial lines drew nothing";
            ++failures;
        }
        if (first != render(0))
        {
            qCritical() << "special-test: radial lines are not deterministic";
            ++failures;
        }
        if (first == render(8))
        {
            qCritical() << "special-test: radial flicker does not animate";
            ++failures;
        }
    }
}

// Flow streaks move with the frame.
void checkFlowLines(SpecialHarness& h)
{
    int& failures = h.failures;
    pvf::Project* project = &h.project;
    const auto render = [&h](int frame) { return h.render(frame); };

    pvf::FlowParams params;
    params.count = 12;
    params.length = 300;
    params.spacing = 60;
    params.area = 900;
    params.speed = 900;
    QVariantMap data;
    params.writeTo(data);
    pvf::Entity* flow = project->addEntity(pvf::EntityType::FlowLines,
                                           QStringLiteral("快速流过"), data);
    if (!flow)
    {
        qCritical() << "special-test: flow creation failed";
        ++failures;
    }
    else
    {
        flow->values[QStringLiteral("x")] = 0.0;
        flow->values[QStringLiteral("y")] = 200.0;
        const QImage first = render(0);
        if (ink(first) <= 0)
        {
            qCritical() << "special-test: flow lines drew nothing";
            ++failures;
        }
        if (first == render(30))
        {
            qCritical() << "special-test: flow lines do not animate";
            ++failures;
        }
    }
}

// Halftone screentone: a dot grid that fades along the gradient angle.
void checkHalftone(SpecialHarness& h)
{
    int& failures = h.failures;
    pvf::Project* project = &h.project;
    const auto render = [&h](int frame) { return h.render(frame); };

    pvf::HalftoneParams params;
    params.area = 600;
    params.spacing = 16;
    params.dotSize = 8;
    QVariantMap data;
    params.writeTo(data);
    pvf::Entity* halftone = project->addEntity(pvf::EntityType::HalftoneDots,
                                               QStringLiteral("漫画点阵"), data);
    if (!halftone)
    {
        qCritical() << "special-test: halftone creation failed";
        ++failures;
    }
    else
    {
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = other.id != halftone->id;
        const QImage shot = render(0);
        const QImage again = render(0);
        if (halftone->halftonePixmap.isNull()
            || halftone->halftoneData.value(QStringLiteral("spacing")).toDouble()
                   != 16.0)
        {
            qCritical() << "special-test: halftone pattern was not cached";
            ++failures;
        }
        else
        {
            // A second frame reuses the raster; a parameter edit rebuilds it.
            const qint64 cached = halftone->halftonePixmap.cacheKey();
            render(0);
            if (halftone->halftonePixmap.cacheKey() != cached)
            {
                qCritical() << "special-test: halftone cache rebuilt every frame";
                ++failures;
            }
            halftone->data[QStringLiteral("spacing")] = 24.0;
            render(0);
            if (halftone->halftonePixmap.cacheKey() == cached
                || halftone->halftoneData.value(QStringLiteral("spacing"))
                           .toDouble()
                       != 24.0)
            {
                qCritical() << "special-test: halftone cache not invalidated";
                ++failures;
            }
        }
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = false;
        shot.save(QStringLiteral("/tmp/pvf_halftone.png"));

        const auto density = [&](int top, int bottom) {
            int count = 0;
            for (int y = top; y < bottom; ++y)
                for (int x = 0; x < shot.width(); ++x)
                    if (qGray(shot.pixel(x, y)) > 40)
                        ++count;
            return count;
        };
        if (ink(shot) <= 0)
        {
            qCritical() << "special-test: halftone drew nothing";
            ++failures;
        }
        else if (density(0, shot.height() / 2)
                 <= density(shot.height() / 2, shot.height()))
        {
            qCritical() << "special-test: halftone gradient does not fade";
            ++failures;
        }
        if (shot != again)
        {
            qCritical() << "special-test: halftone is not deterministic";
            ++failures;
        }
    }
}

// Brand tile wall: a checkerboard of two logos.
void checkBrandWall(SpecialHarness& h)
{
    int& failures = h.failures;
    pvf::Project* project = &h.project;
    const auto render = [&h](int frame) { return h.render(frame); };

    const QString logoA =
        QDir::tempPath() + QStringLiteral("/pvf_wall_logo_a.png");
    const QString logoB =
        QDir::tempPath() + QStringLiteral("/pvf_wall_logo_b.svg");
    QImage a(96, 96, QImage::Format_ARGB32);
    a.fill(Qt::transparent);
    {
        QPainter p(&a);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(10, 10, 76, 76);
    }
    if (!a.save(logoA))
    {
        qCritical() << "special-test: wall logo not saved";
        ++failures;
    }
    // The second slot is an SVG at a tiny intrinsic size; it has to be
    // rasterised at a usable resolution.
    QFile svgFile(logoB);
    if (svgFile.open(QIODevice::WriteOnly))
    {
        svgFile.write(
            "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' "
            "viewBox='0 0 24 24'><path d='M4 4 L20 20 M20 4 L4 20' "
            "stroke='white' stroke-width='3' stroke-linecap='round'/></svg>");
        svgFile.close();
    }
    const QImage svgLogo = pvf::BrandWall::loadLogo(logoB);
    if (svgLogo.isNull()
        || std::max(svgLogo.width(), svgLogo.height()) != 1024)
    {
        qCritical() << "special-test: svg logo not rasterised" << svgLogo.size();
        ++failures;
    }
    pvf::BrandWallParams wallParams;
    wallParams.width = 800;
    wallParams.height = 450;
    wallParams.columns = 8;
    wallParams.rows = 5;
    wallParams.gap = 6;
    wallParams.logoOpacity = 0.9;
    wallParams.logos = QStringList{logoA, logoB};
    wallParams.tileColors =
        QStringList{QColor(210, 70, 160).name(QColor::HexArgb),
                    QColor(232, 232, 238).name(QColor::HexArgb)};
    pvf::Entity* wall = project->addBrandWall(wallParams);
    if (!wall)
    {
        qCritical() << "special-test: brand wall creation failed";
        ++failures;
    }
    else if (std::abs(pvf::SceneRenderer::entityLocalBounds(*wall).width() - 800.0)
                 > 0.01
             || wall->wallLogos.size() != 2 || wall->wallLogos[0].isNull()
             || wall->wallLogos[1].isNull())
    {
        qCritical() << "special-test: brand wall bounds or logos wrong"
                    << pvf::SceneRenderer::entityLocalBounds(*wall);
        ++failures;
    }
    else
    {
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = other.id != wall->id;
        const QImage shot = render(0);
        shot.save(QStringLiteral("/tmp/pvforge_brandwall.png"));
        for (pvf::Entity& other : project->mutableEntities())
            other.hidden = false;
        if (ink(shot) <= 0)
        {
            qCritical() << "special-test: brand wall drew nothing";
            ++failures;
        }
        // Checkerboard: neighbouring tiles use different slots, also
        // across rows (the old row * columns indexing broke this).
        const QColor tile00 = shot.pixelColor(96, 94);
        const QColor tile01 = shot.pixelColor(146, 94);
        const QColor tile10 = shot.pixelColor(96, 139);
        if (tile00 == tile01 || tile00 == tile10)
        {
            qCritical() << "special-test: brand wall is not a checkerboard"
                        << tile00 << tile01 << tile10;
            ++failures;
        }
    }
}

// Japanese vertical text: columns run right to left.
void checkVerticalColumns(int& failures)
{
    pvf::TextStyle style;
    style.text = QStringLiteral("日式竖排\n从右到左");
    style.pixelSize = 48;
    style.vertical = true;
    const pvf::TextBlock block = pvf::TextBlock::measure(style);
    if (block.contentSize().width() <= 0
        || block.contentSize().height() <= block.contentSize().width())
    {
        qCritical() << "special-test: vertical text is not column shaped"
                    << block.contentSize();
        ++failures;
    }
}

// The special types survive a save and load round trip.
void checkRoundTrip(pvf::Project& project, int& failures)
{
    const QString path =
        QDir::tempPath() + QStringLiteral("/pvforge_special_test.pvproj");
    QString error;
    if (!project.save(path, &error))
    {
        qCritical() << "special-test: save failed" << error;
        ++failures;
    }
    else
    {
        pvf::Project loaded;
        if (!loaded.load(path, &error))
        {
            qCritical() << "special-test: load failed" << error;
            ++failures;
        }
        else
        {
            int comic = 0;
            int radial = 0;
            int flow = 0;
            int halftone = 0;
            int wall = 0;
            for (const pvf::Entity& entity : loaded.entities())
            {
                if (entity.type == pvf::EntityType::ComicText)
                    ++comic;
                else if (entity.type == pvf::EntityType::RadialLines)
                    ++radial;
                else if (entity.type == pvf::EntityType::FlowLines)
                    ++flow;
                else if (entity.type == pvf::EntityType::HalftoneDots)
                    ++halftone;
                else if (entity.type == pvf::EntityType::BrandWall)
                    ++wall;
            }
            if (comic != 4 || radial != 1 || flow != 1 || halftone != 1
                || wall != 1)
            {
                qCritical() << "special-test: types lost on reload" << comic
                            << radial << flow << halftone << wall;
                ++failures;
            }
        }
        QFile::remove(path);
    }
}

// Performance monitor keeps the latest rolling timings.
void checkPerfMonitor(int& failures)
{
    auto& perf = pvf::PerfMonitor::instance();
    perf.setEnabled(true);
    perf.recordScene(7.5);
    perf.recordViewport(3.0);
    perf.recordTimeline(2.0);
    perf.recordVideo(1.0);
    perf.setEntityCount(53);
    const pvf::PerfSnapshot snapshot = perf.snapshot();
    if (!snapshot.enabled || std::abs(snapshot.sceneMs - 7.5) > 1e-6
        || std::abs(snapshot.viewportMs - 3.0) > 1e-6
        || std::abs(snapshot.timelineMs - 2.0) > 1e-6
        || std::abs(snapshot.videoMs - 1.0) > 1e-6
        || snapshot.entities != 53)
    {
        qCritical() << "special-test: performance monitor values wrong"
                    << snapshot.sceneMs << snapshot.viewportMs
                    << snapshot.timelineMs << snapshot.videoMs;
        ++failures;
    }
    perf.setEnabled(false);
    if (perf.snapshot().enabled)
    {
        qCritical() << "special-test: performance monitor did not disable";
        ++failures;
    }
}

// Workbench menu, comic spike drag, text layout dialog, perf label and the
// brand wall dialog, all driven from inside one event-loop turn.
void checkWorkbenchUi(QApplication& app, pvf::MainWindow& uiWindow,
                      pvf::Project* uiProject, int failures)
{
    int local = failures;
    auto* workbench = uiWindow.findChild<pvf::WorkbenchPanel*>();
    auto* types = workbench
        ? workbench->findChild<QListWidget*>(QStringLiteral("typeList"))
        : nullptr;
    auto* special = workbench
        ? workbench->findChild<QListWidget*>(QStringLiteral("specialTypes"))
        : nullptr;
    auto* create = workbench
        ? workbench->findChild<QPushButton*>(QStringLiteral("createButton"))
        : nullptr;
    if (!types || !special || !create)
    {
        qCritical() << "special-test: workbench widgets missing";
        ++local;
    }
    else
    {
        auto* textBoxes =
            workbench->findChild<QListWidget*>(QStringLiteral("textBoxTypes"));
        types->setCurrentRow(2);   // the "text box" page
        QApplication::processEvents();
        for (int row = 0; textBoxes && row < textBoxes->count(); ++row)
        {
            textBoxes->setCurrentRow(row);
            QApplication::processEvents();
            const int before = uiProject->entities().size();
            create->click();
            QApplication::processEvents();
            if (uiProject->entities().size() != before + 1)
            {
                qCritical() << "special-test: text box row did not create" << row;
                ++local;
            }
        }
        if (textBoxes)
            textBoxes->setCurrentRow(1);
        QApplication::processEvents();
        workbench->grab().toImage().save(
            QStringLiteral("/tmp/pvforge_special_workbench.png"));

        types->setCurrentRow(4);   // the "special" page
        QApplication::processEvents();
        for (int row = 0; row < special->count(); ++row)
        {
            special->setCurrentRow(row);
            QApplication::processEvents();
            const int before = uiProject->entities().size();
            create->click();
            QApplication::processEvents();
            if (uiProject->entities().size() != before + 1)
            {
                qCritical() << "special-test: workbench did not create row" << row;
                ++local;
            }
        }
        special->setCurrentRow(2);   // halftone, for the shot
        QApplication::processEvents();
        workbench->grab().toImage().save(
            QStringLiteral("/tmp/pvforge_halftone_workbench.png"));
        special->setCurrentRow(3);   // brand wall, for the shot
        QApplication::processEvents();
        workbench->grab().toImage().save(
            QStringLiteral("/tmp/pvforge_wall_workbench.png"));
    }

    // The comic spike can be dragged to a new tip position.
    auto* viewport = uiWindow.findChild<pvf::Viewport*>();
    pvf::EntityId comicId = pvf::kInvalidEntity;
    for (const pvf::Entity& entity : uiProject->entities())
    {
        if (entity.type == pvf::EntityType::ComicText)
        {
            comicId = entity.id;
            break;
        }
    }
    if (!viewport || comicId == pvf::kInvalidEntity)
    {
        qCritical() << "special-test: comic entity or viewport missing";
        ++local;
    }
    else
    {
        uiWindow.setSelectedEntityForTest(comicId);
        QApplication::processEvents();
        const QPointF tip = viewport->comicTipForTest(comicId);
        const pvf::Entity* comic = uiProject->entity(comicId);
        const double beforeX = comic->data.value(QStringLiteral("bubbleTipX")).toDouble();
        const auto send = [&](QEvent::Type type, const QPointF& pos) {
            QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)),
                              Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                 : Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        send(QEvent::MouseButtonPress, tip);
        send(QEvent::MouseMove, tip + QPointF(20, 0));
        send(QEvent::MouseMove, tip + QPointF(40, 0));
        send(QEvent::MouseMove, tip + QPointF(60, 0));
        send(QEvent::MouseButtonRelease, tip + QPointF(60, 0));
        QApplication::processEvents();
        const pvf::Entity* moved = uiProject->entity(comicId);
        const double afterX = moved->data.value(QStringLiteral("bubbleTipX")).toDouble();
        if (std::abs(afterX - beforeX) < 0.01)
        {
            qCritical() << "special-test: comic spike did not move" << beforeX
                        << afterX;
            ++local;
        }
        if (auto* undo = uiWindow.findChild<QAction*>(QStringLiteral("undoAction")))
        {
            undo->trigger();
            QApplication::processEvents();
            const pvf::Entity* undone = uiProject->entity(comicId);
            if (std::abs(undone->data.value(QStringLiteral("bubbleTipX")).toDouble()
                         - beforeX) > 0.01)
            {
                qCritical() << "special-test: comic spike undo failed";
                ++local;
            }
        }
    }

    // The layout dialog opens with the style it was given.
    pvf::TextStyle style;
    style.text = QStringLiteral("排版预览\n第二行");
    style.pixelSize = 48;
    style.color = QColor(30, 30, 36);
    style.letterSpacing = 8;
    style.boxWidth = 260;
    pvf::TextLayoutDialog dialog(style);
    dialog.show();
    QApplication::processEvents();
    if (!dialog.findChild<pvf::TextPreview*>(QStringLiteral("layoutPreview")))
    {
        qCritical() << "special-test: layout preview missing";
        ++local;
    }
    auto* letterSpacing =
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("layoutLetterSpacing"));
    if (!letterSpacing || std::abs(letterSpacing->value() - 8.0) > 1e-6)
    {
        qCritical() << "special-test: layout letter spacing missing";
        ++local;
    }
    if (dialog.style().text != style.text || dialog.style().boxWidth != style.boxWidth)
    {
        qCritical() << "special-test: layout dialog changed the style";
        ++local;
    }
    dialog.grab().toImage().save(QStringLiteral("/tmp/pvforge_text_layout.png"));

    // The status bar performance monitor toggles from the view menu.
    if (auto* perfAction =
            uiWindow.findChild<QAction*>(QStringLiteral("perfMonitorAction")))
    {
        auto* perfLabel =
            uiWindow.findChild<QLabel*>(QStringLiteral("perfInfo"));
        perfAction->setChecked(true);
        QApplication::processEvents();
        if (!perfLabel || !perfLabel->isVisible()
            || !perfLabel->text().contains(QStringLiteral("FPS")))
        {
            qCritical() << "special-test: performance monitor label missing";
            ++local;
        }
        perfAction->setChecked(false);
        QApplication::processEvents();
    }
    else
    {
        qCritical() << "special-test: performance monitor action missing";
        ++local;
    }

    // The wall editor opens with its slots and can add more logos.
    pvf::BrandWallParams wallParams;
    wallParams.width = 640;
    wallParams.height = 360;
    wallParams.columns = 8;
    wallParams.rows = 5;
    wallParams.gap = 4;
    wallParams.logos = QStringList{QString(), QString()};
    wallParams.tileColors = QStringList{QColor(210, 70, 160).name(),
                                        QColor(232, 232, 238).name()};
    pvf::BrandWallDialog wallDialog(wallParams,
                                    QVector<QImage>{QImage(), QImage()});
    wallDialog.show();
    QApplication::processEvents();
    if (!wallDialog.findChild<pvf::BrandWallPreview*>(
            QStringLiteral("wallPreview"))
        || wallDialog.params().logos.size() != 2
        || wallDialog.params().columns != 8
        || wallDialog.params().rows != 5)
    {
        qCritical() << "special-test: wall dialog state wrong";
        ++local;
    }
    wallDialog.grab().toImage().save(
        QStringLiteral("/tmp/pvforge_wall_dialog.png"));

    // A clean scene shot: only the selected comic balloon.
    for (pvf::Entity& entity : uiProject->mutableEntities())
        entity.hidden = entity.id != comicId;
    uiWindow.setSelectedEntityForTest(comicId);
    QApplication::processEvents();
    if (auto* props = uiWindow.findChild<pvf::PropertiesPanel*>())
        if (auto* scroll = qobject_cast<QScrollArea*>(
                props->parentWidget() ? props->parentWidget()->parentWidget()
                                      : nullptr))
            scroll->verticalScrollBar()->setValue(
                scroll->verticalScrollBar()->maximum());
    QApplication::processEvents();
    uiWindow.grab().toImage().save(QStringLiteral("/tmp/pvforge_special_scene.png"));
    qInfo() << "special-test failures" << local;
    app.exit(local == 0 ? 0 : 1);
}

} // namespace

namespace pvf::tests {

int runPropertiesSpecialTest(QApplication& app)
{
    auto* project = new pvf::Project;
    project->width = 800;
    project->height = 600;
    project->fps = 30;

    int failures = 0;
    SpecialHarness harness{*project, failures};

    checkComicShapes(harness);
    checkVerticalTextBalloon(failures);
    checkBalloonBounds(failures);
    checkGlyphOutline(failures);
    checkRadialLines(harness);
    checkFlowLines(harness);
    checkHalftone(harness);
    checkBrandWall(harness);
    checkVerticalColumns(failures);
    harness.render(0).save(QStringLiteral("/tmp/pvforge_special.png"));
    checkRoundTrip(*project, failures);
    checkPerfMonitor(failures);

    // The workbench offers the special tools and the layout dialog opens.
    auto* uiProject = new pvf::Project;
    uiProject->width = 1280;
    uiProject->height = 720;
    pvf::MainWindow uiWindow(uiProject);
    uiWindow.resize(1400, 900);
    uiWindow.show();
    QTimer::singleShot(500, &uiWindow, [&app, &uiWindow, uiProject, failures] {
        checkWorkbenchUi(app, uiWindow, uiProject, failures);
    });
    return app.exec();
}

} // namespace pvf::tests
