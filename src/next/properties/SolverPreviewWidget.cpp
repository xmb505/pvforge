#include "SolverPreviewWidget.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QFontMetricsF>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pvf::next {
namespace {

// Arrow length: velocity is px/second.  0.25 s keeps the arrow proportional
// for the usual 0..600 px/s range, but a long trajectory shrinks the fit until
// the handle is a few pixels; the arrow time base is then stretched so the
// hand-readable length stays inside [kMinArrowPixels, kMaxArrowPixels].
constexpr double kVelocitySeconds = 0.25;
constexpr double kMinArrowPixels = 26.0;
constexpr double kMaxArrowPixels = 72.0;
// A very long segment is truncated for the preview only; export/render still
// evaluates the whole segment.
constexpr int kMaxPreviewFrames = 2000;
constexpr double kArrowHitRadius = 13.0;

const QColor kBackground(20, 22, 26);
const QColor kGrid(34, 38, 46);
const QColor kBorder(78, 82, 92);
const QColor kMutedText(130, 136, 148);
const QColor kTrajectory(116, 206, 255);
const QColor kStartColor(125, 255, 158);
const QColor kTargetColor(255, 159, 67);
const QColor kCurrentColor(255, 190, 60);
const QColor kVelocityColor(255, 107, 107);
const QColor kErrorColor(255, 138, 138);

QString effectLabel(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return QStringLiteral("重力");
    case SolverEffect::Orbit:
        return QStringLiteral("公转");
    case SolverEffect::Spin:
        return QStringLiteral("自转");
    case SolverEffect::BlackHole:
        return QStringLiteral("黑洞");
    }
    return QStringLiteral("求解");
}

double trackValue(const model::Entity& entity, const QString& name, int frame,
                  double fallback)
{
    const auto it = entity.tracks.constFind(name);
    if (it != entity.tracks.constEnd())
    {
        if (const auto value = model::evaluateTrack(it.value(), frame))
            return *value;
    }
    return fallback;
}

double transformRotation(const QTransform& transform)
{
    return qRadiansToDegrees(std::atan2(transform.m12(), transform.m11()));
}

// Solver-space pose of the driven entity without running its solver.  This
// mirrors anim::evaluateEntity()'s recursion fallback: gravity asks for the
// moving entity's own shape while solving it, so we must not recurse.
QTransform plainLocalTransform(const model::Entity& entity, int relativeFrame)
{
    const model::Transform& fallback = entity.transform;
    const double x = trackValue(entity, QStringLiteral("x"), relativeFrame, fallback.x);
    const double y = trackValue(entity, QStringLiteral("y"), relativeFrame, fallback.y);
    const double rotation = trackValue(entity, QStringLiteral("rotation"),
                                       relativeFrame, fallback.rotation);
    const double scaleX = trackValue(entity, QStringLiteral("scaleX"),
                                      relativeFrame, fallback.scaleX);
    const double scaleY = trackValue(entity, QStringLiteral("scaleY"),
                                      relativeFrame, fallback.scaleY);
    const double pivotX = trackValue(entity, QStringLiteral("pivotX"),
                                      relativeFrame, fallback.pivotX);
    const double pivotY = trackValue(entity, QStringLiteral("pivotY"),
                                      relativeFrame, fallback.pivotY);

    QTransform local;
    local.translate(x, y);
    local.translate(pivotX, pivotY);
    local.rotate(rotation);
    local.scale(scaleX, scaleY);
    local.translate(-pivotX, -pivotY);
    return local;
}

// Map relative solver frames to project frames and project canvas coordinates
// back into the driven entity's parent space.  This is the same conversion
// anim's internal SolverWorldAdapter performs, built on render2::SceneWorld.
class PreviewWorld final : public solver::World
{
public:
    PreviewWorld(const model::Project& project, const model::Entity& driven,
                 render2::SceneWorld& scene)
        : m_project(project)
        , m_driven(driven)
        , m_scene(scene)
    {
    }

    solver::Pose poseOf(const Uuid& uuid, int frame) const override
    {
        const int projectFrame = frame + m_driven.start;
        if (uuid == m_driven.uuid)
        {
            const QTransform local = plainLocalTransform(m_driven, frame);
            return solver::Pose{local.map(QPointF(0.0, 0.0)),
                                transformRotation(local)};
        }

        const solver::Pose raw = m_scene.poseOf(uuid, projectFrame);
        const QTransform parent = parentWorldOf(projectFrame);
        if (parent.isIdentity())
            return raw;

        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        if (!invertible)
            return raw;
        return solver::Pose{inverse.map(raw.position),
                            raw.rotation - transformRotation(parent)};
    }

    QPainterPath shapeOf(const Uuid& uuid, int frame) const override
    {
        const int projectFrame = frame + m_driven.start;
        if (uuid == m_driven.uuid)
        {
            // Use the real vector outline when possible; other content kinds
            // fall back to a point so the preview never recurses into itself.
            if (m_driven.content.kind != ContentKind::Vector)
                return {};
            const model::VectorContent& vector = m_driven.content.vector;
            const QRectF rect(-vector.width * 0.5, -vector.height * 0.5,
                              std::max(1.0, vector.width),
                              std::max(1.0, vector.height));
            QPainterPath path;
            path.addRect(rect);
            return plainLocalTransform(m_driven, frame).map(path);
        }

        const QPainterPath path = m_scene.shapeOf(uuid, projectFrame);
        if (path.isEmpty())
            return {};
        const QTransform parent = parentWorldOf(projectFrame);
        if (parent.isIdentity())
            return path;

        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        return invertible ? inverse.map(path) : path;
    }

    bool physicsInteract(const Uuid& uuid, int frame) const override
    {
        return m_scene.physicsInteract(uuid, frame + m_driven.start);
    }

    QVector<Uuid> physicsEntities(int frame) const override
    {
        return m_scene.physicsEntities(frame + m_driven.start);
    }

private:
    QTransform parentWorldOf(int projectFrame) const
    {
        if (m_driven.parentUuid.isEmpty())
            return QTransform();
        if (!m_project.entities.contains(m_driven.parentUuid))
            return QTransform();

        const auto cached = m_parentWorld.constFind(projectFrame);
        if (cached != m_parentWorld.constEnd())
            return cached.value();

        const QTransform world =
            anim::evaluateEntity(m_project, m_driven.parentUuid, projectFrame,
                                 m_scene)
                .world;
        m_parentWorld.insert(projectFrame, world);
        return world;
    }

    const model::Project& m_project;
    const model::Entity& m_driven;
    render2::SceneWorld& m_scene;
    mutable QHash<int, QTransform> m_parentWorld;
};

QRectF spinPlotRect(const QRect& widgetRect)
{
    return QRectF(widgetRect).adjusted(28.0, 28.0, -14.0, -22.0);
}

QPointF spinPoint(const QVector<solver::Sample>& samples, const QRectF& plot,
                  const solver::Sample& sample)
{
    if (samples.isEmpty())
        return {};
    double minimum = samples.first().pose.rotation;
    double maximum = minimum;
    for (const solver::Sample& item : samples)
    {
        minimum = std::min(minimum, item.pose.rotation);
        maximum = std::max(maximum, item.pose.rotation);
    }
    if (maximum - minimum < 1e-6)
    {
        minimum -= 1.0;
        maximum += 1.0;
    }

    const int firstFrame = samples.first().frame;
    const int lastFrame = samples.last().frame;
    const double span = std::max(1, lastFrame - firstFrame);
    const double x = plot.left()
        + (double(sample.frame - firstFrame) / span) * plot.width();
    const double y = plot.bottom()
        - ((sample.pose.rotation - minimum) / (maximum - minimum)) * plot.height();
    return QPointF(x, y);
}

void drawArrowHead(QPainter& painter, QPointF tip, QPointF direction,
                   double size, const QColor& color)
{
    const double length = std::hypot(direction.x(), direction.y());
    if (length < 1e-9)
        return;
    const QPointF unit = direction / length;
    const QPointF perpendicular(-unit.y(), unit.x());
    QPolygonF head;
    head << tip << tip - unit * size + perpendicular * (size * 0.55)
         << tip - unit * size - perpendicular * (size * 0.55);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
}

} // namespace

SolverPreviewWidget::SolverPreviewWidget(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("solverPreview"));
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setToolTip(QStringLiteral("拖动空白处平移 · 滚轮缩放 · 双击自适应"));
    // Needed for Esc-to-cancel: clicking the handle gives the widget keyboard
    // focus so the key press arrives here instead of the shell.
    setFocusPolicy(Qt::StrongFocus);

    if (m_document)
    {
        connect(m_document, &Document::changed, this, [this] {
            if (!m_dragging)
                refreshPreview();
        });
        connect(m_document, &Document::playheadChanged, this,
                qOverload<>(&QWidget::update));
    }
}

SolverPreviewWidget::SolverPreviewWidget(Document* document,
                                         const Uuid& entityUuid,
                                         const Uuid& solverId, QWidget* parent)
    : SolverPreviewWidget(document, parent)
{
    setSolver(entityUuid, solverId);
}

QSize SolverPreviewWidget::sizeHint() const
{
    return QSize(300, 172);
}

void SolverPreviewWidget::setSolver(const Uuid& entityUuid, const Uuid& solverId)
{
    if (entityUuid == m_entityUuid && solverId == m_solverId)
    {
        update();
        return;
    }

    // The card was rebound while the handle was held; drop the drag (and put
    // the old velocity back) rather than writing the pointer position into the
    // new segment.
    cancelDrag();

    m_entityUuid = entityUuid;
    m_solverId = solverId;
    computePreview(false);
}

void SolverPreviewWidget::clearSolver()
{
    cancelDrag();

    m_entityUuid.clear();
    m_solverId.clear();
    m_samples.clear();
    m_hasPath = false;
    m_hasTarget = false;
    m_entityName.clear();
    m_targetName.clear();
    m_fitValid = false;
    m_error.clear();
    update();
}

void SolverPreviewWidget::refreshPreview()
{
    if (!m_dragging)
        computePreview(false);
}

const model::SolverSegment* SolverPreviewWidget::findSolver(
    const model::Entity& entity) const
{
    for (const model::SolverSegment& segment : entity.solvers)
    {
        if (segment.id == m_solverId)
            return &segment;
    }
    return nullptr;
}

bool SolverPreviewWidget::usesVelocity() const
{
    return m_effect == SolverEffect::Gravity
        || m_effect == SolverEffect::BlackHole;
}

void SolverPreviewWidget::computePreview(bool duringDrag)
{
    if (m_dragging && !duringDrag)
        return;

    m_samples.clear();
    m_hasPath = false;
    m_hasTarget = false;
    m_entityName.clear();
    m_targetName.clear();
    // Freeze the fit while the handle is dragged: refitting mid-drag would
    // rescale the view under the pointer and the arrow tip would lag behind it.
    if (!duringDrag)
        m_fitValid = false;
    m_truncated = false;
    m_error.clear();

    if (!m_document)
    {
        m_error = QStringLiteral("无法解算");
        update();
        return;
    }

    const model::Project& project = m_document->project();
    const auto entityIt = project.entities.constFind(m_entityUuid);
    if (entityIt == project.entities.constEnd())
    {
        m_error = QStringLiteral("无法解算：实体缺失");
        update();
        return;
    }

    const model::Entity& entity = entityIt.value();
    const model::SolverSegment* solver = findSolver(entity);
    if (!solver)
    {
        m_error = QStringLiteral("无法解算：求解段缺失");
        update();
        return;
    }

    m_entityName = entity.name;
    m_entityStart = entity.start;
    m_segmentStart = solver->startFrame;
    m_effect = solver->effect;
    m_initialVelocity = solver->initialVelocity;

    const int duration = entity.duration();
    const int effectiveEnd = solver->endFrame < 0
        ? duration - 1
        : std::min(solver->endFrame, duration - 1);
    if (duration <= 0 || solver->startFrame < 0
        || solver->startFrame >= duration || effectiveEnd < solver->startFrame)
    {
        m_error = QStringLiteral("无法解算");
        update();
        return;
    }

    const int startFrame = solver->startFrame;
    const model::Transform& fallback = entity.transform;
    double startRotation = fallback.rotation;
    m_startPose = QPointF(
        trackValue(entity, QStringLiteral("x"), startFrame, fallback.x),
        trackValue(entity, QStringLiteral("y"), startFrame, fallback.y));
    startRotation = trackValue(entity, QStringLiteral("rotation"), startFrame,
                               fallback.rotation);

    Uuid targetUuid;
    if (solver->effect != SolverEffect::Spin)
    {
        targetUuid = solver->params.value(QStringLiteral("targetUuid")).toString();
        const auto targetIt = project.entities.constFind(targetUuid);
        if (targetUuid.isEmpty() || targetIt == project.entities.constEnd())
        {
            m_error = QStringLiteral("目标缺失");
            update();
            return;
        }
        m_targetName = targetIt.value().name;
        if (m_targetName.isEmpty())
            m_targetName = QStringLiteral("目标");
    }

    int evaluationEnd = effectiveEnd;
    if (effectiveEnd - startFrame + 1 > kMaxPreviewFrames)
    {
        evaluationEnd = startFrame + kMaxPreviewFrames - 1;
        m_truncated = true;
    }

    solver::Segment request;
    request.effect = solver->effect;
    request.entityUuid = entity.uuid;
    request.targetUuid = targetUuid;
    request.startFrame = startFrame;
    request.endFrame = evaluationEnd;
    request.startPose = solver::Pose{m_startPose, startRotation};
    request.initialVelocity = solver->initialVelocity;
    request.params = solver->params;

    render2::SceneWorld scene(project, m_document->assets());
    PreviewWorld world(project, entity, scene);
    m_samples = solver::evaluate(request, world, duration,
                                 std::max(1, project.settings.fps));

    if (m_samples.isEmpty())
    {
        m_error = QStringLiteral("无法解算");
        update();
        return;
    }

    // The frame cap only hides part of the trajectory when the motion is
    // still running at the cap.  If physics stopped earlier (black hole
    // capture, stop threshold), the whole path to the stop is already here,
    // even when the clip itself is much longer than the cap.
    if (m_truncated)
    {
        const bool physicallyStopped =
            std::any_of(m_samples.cbegin(), m_samples.cend(),
                        [](const solver::Sample& sample) {
                            return sample.stopped;
                        });
        if (physicallyStopped)
            m_truncated = false;
    }

    if (solver->effect == SolverEffect::Spin)
    {
        m_hasPath = m_samples.size() >= 2;
        update();
        return;
    }

    m_targetSolver = world.poseOf(targetUuid, m_samples.first().frame).position;
    m_hasTarget = true;
    m_hasPath = m_samples.size() >= 2;
    m_arrowTipSolver =
        m_startPose + m_initialVelocity * kVelocitySeconds;

    QRectF bounds;
    bool hasPoint = false;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    const auto addPoint = [&](const QPointF& point) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
            return;
        if (!hasPoint)
        {
            minX = maxX = point.x();
            minY = maxY = point.y();
            hasPoint = true;
            return;
        }
        minX = std::min(minX, point.x());
        minY = std::min(minY, point.y());
        maxX = std::max(maxX, point.x());
        maxY = std::max(maxY, point.y());
    };
    addPoint(m_startPose);
    addPoint(m_arrowTipSolver);
    addPoint(m_targetSolver);
    for (const solver::Sample& sample : m_samples)
        addPoint(sample.pose.position);
    if (hasPoint)
    {
        bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
        // A purely vertical/horizontal trajectory has a zero-width (or
        // zero-height) box; QRectF::isValid() would then reject it and the fit
        // would collapse onto the start point.
        if (bounds.width() < 1.0)
            bounds.setWidth(1.0);
        if (bounds.height() < 1.0)
            bounds.setHeight(1.0);
    }
    m_solverBounds = bounds;
    if (!duringDrag)
    {
        computePositionFit();
        // Re-time the arrow for the new scale; the fit above used the 0.25 s
        // arrow, which is the conservative bound.
        updateArrowLength();
    }
    update();
}

void SolverPreviewWidget::updateArrowLength()
{
    m_arrowSeconds = kVelocitySeconds;
    const double speed = std::hypot(m_initialVelocity.x(), m_initialVelocity.y());
    if (m_fitValid && m_scale > 0.0 && speed > 1e-6)
    {
        const double pixels = speed * m_scale * kVelocitySeconds;
        if (pixels < kMinArrowPixels)
            m_arrowSeconds = kMinArrowPixels / (speed * m_scale);
        else if (pixels > kMaxArrowPixels)
            m_arrowSeconds = kMaxArrowPixels / (speed * m_scale);
    }
    m_arrowTipSolver = m_startPose + m_initialVelocity * m_arrowSeconds;
}

void SolverPreviewWidget::computePositionFit()
{
    m_fitValid = false;
    QSize canvas = size();
    if (canvas.width() < 64 || canvas.height() < 64)
        canvas = sizeHint();
    m_plotRect = QRectF(0.0, 0.0, canvas.width(), canvas.height())
                     .adjusted(16.0, 26.0, -16.0, -14.0);
    if (m_plotRect.width() < 8.0 || m_plotRect.height() < 8.0)
        return;

    QRectF bounds = m_solverBounds;
    if (!bounds.isValid())
        bounds = QRectF(m_startPose, QSizeF(1.0, 1.0));

    // Never zoom in past a sane minimum footprint, otherwise a static preview
    // turns into a single huge point.
    if (bounds.width() < 120.0)
    {
        const double centerX = bounds.center().x();
        bounds.setLeft(centerX - 60.0);
        bounds.setRight(centerX + 60.0);
    }
    if (bounds.height() < 120.0)
    {
        const double centerY = bounds.center().y();
        bounds.setTop(centerY - 60.0);
        bounds.setBottom(centerY + 60.0);
    }

    const double scaleX = m_plotRect.width() / bounds.width();
    const double scaleY = m_plotRect.height() / bounds.height();
    const double scale = std::min(scaleX, scaleY);
    if (!std::isfinite(scale) || scale <= 0.0)
        return;

    m_solverCenter = bounds.center();
    m_scale = scale;
    m_fitValid = true;
}

int SolverPreviewWidget::currentRelativeFrame() const
{
    if (!m_document)
        return -1;
    return m_document->playhead() - m_entityStart;
}

int SolverPreviewWidget::currentSampleIndex() const
{
    if (m_samples.isEmpty())
        return -1;
    const int index = currentRelativeFrame() - m_samples.first().frame;
    if (index < 0 || index >= m_samples.size())
        return -1;
    if (m_samples.at(index).frame != currentRelativeFrame())
        return -1;
    return index;
}

QPointF SolverPreviewWidget::mapSolverToWidget(const QPointF& point) const
{
    if (!m_fitValid)
        return {};
    const QPointF center = m_plotRect.center();
    // Solver space is Y-up (the project convention); widget space is Y-down,
    // so a positive solver Y must move the point up on screen.  Without this
    // flip the preview is a vertical mirror of the canvas.
    return QPointF(center.x() + (point.x() - m_solverCenter.x()) * m_scale,
                   center.y() - (point.y() - m_solverCenter.y()) * m_scale);
}

QPointF SolverPreviewWidget::mapWidgetToSolver(const QPointF& point) const
{
    if (!m_fitValid || m_scale <= 0.0)
        return {};
    const QPointF center = m_plotRect.center();
    return QPointF(m_solverCenter.x() + (point.x() - center.x()) / m_scale,
                   m_solverCenter.y() - (point.y() - center.y()) / m_scale);
}

QPointF SolverPreviewWidget::startPoint() const
{
    if (m_samples.isEmpty())
        return {};
    if (m_effect == SolverEffect::Spin)
        return spinPoint(m_samples, spinPlotRect(rect()), m_samples.first());
    return mapSolverToWidget(m_startPose);
}

QPointF SolverPreviewWidget::arrowTip() const
{
    if (m_effect == SolverEffect::Spin || m_samples.isEmpty())
        return {};
    return mapSolverToWidget(m_arrowTipSolver);
}

QPointF SolverPreviewWidget::targetPoint() const
{
    if (!m_hasTarget || m_samples.isEmpty())
        return {};
    if (m_effect == SolverEffect::Spin)
        return {};
    return mapSolverToWidget(m_targetSolver);
}

QPointF SolverPreviewWidget::currentPoint() const
{
    const int index = currentSampleIndex();
    if (index < 0)
        return {};
    if (m_effect == SolverEffect::Spin)
        return spinPoint(m_samples, spinPlotRect(rect()), m_samples.at(index));
    return mapSolverToWidget(m_samples.at(index).pose.position);
}

bool SolverPreviewWidget::hasArrow() const
{
    // The handle exists even at zero velocity: its tip then sits on the start
    // point and is drawn as a small ring.
    return usesVelocity() && !m_samples.isEmpty() && m_fitValid;
}

void SolverPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), kBackground);

    // Background grid.
    painter.setPen(QPen(kGrid, 1.0));
    constexpr int gridStep = 24;
    for (int x = gridStep; x < width(); x += gridStep)
        painter.drawLine(x, 0, x, height());
    for (int y = gridStep; y < height(); y += gridStep)
        painter.drawLine(0, y, width(), y);

    painter.setPen(QPen(kBorder, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));

    if (!m_error.isEmpty())
    {
        painter.setPen(kErrorColor);
        painter.drawText(rect().adjusted(12, 12, -12, -12),
                         Qt::AlignCenter | Qt::TextWordWrap, m_error);
        return;
    }

    QFont headerFont = painter.font();
    headerFont.setPointSizeF(std::max(7.0, headerFont.pointSizeF() - 1.0));
    painter.setFont(headerFont);

    // Effect name only: the mini preview shows the trajectory, not a stop
    // time report, so the frame range stays out of the header.
    QString header = effectLabel(m_effect);
    if (m_truncated)
        header += QStringLiteral(" · 预览截断");
    painter.setPen(kMutedText);
    painter.drawText(QRectF(8.0, 4.0, width() - 16.0, 18.0),
                     Qt::AlignLeft | Qt::AlignVCenter, header);

    if (m_effect == SolverEffect::Spin)
    {
        const QRectF plot = spinPlotRect(rect());
        if (plot.width() < 8.0 || plot.height() < 8.0 || m_samples.isEmpty())
            return;

        double minimum = m_samples.first().pose.rotation;
        double maximum = minimum;
        for (const solver::Sample& sample : m_samples)
        {
            minimum = std::min(minimum, sample.pose.rotation);
            maximum = std::max(maximum, sample.pose.rotation);
        }
        if (maximum - minimum < 1e-6)
        {
            minimum -= 1.0;
            maximum += 1.0;
        }

        painter.setPen(QPen(kBorder, 1.0));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.drawLine(plot.bottomLeft(), plot.topLeft());

        if (minimum <= 0.0 && maximum >= 0.0)
        {
            const double zeroY = plot.bottom()
                - ((0.0 - minimum) / (maximum - minimum)) * plot.height();
            painter.setPen(QPen(kGrid, 1.0, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), zeroY),
                             QPointF(plot.right(), zeroY));
        }

        QPolygonF polyline;
        for (const solver::Sample& sample : m_samples)
            polyline.append(spinPoint(m_samples, plot, sample));
        painter.setPen(QPen(kTrajectory, 1.8));
        painter.drawPolyline(polyline);

        painter.setPen(QPen(QColor(20, 22, 26), 1.0));
        painter.setBrush(kStartColor);
        painter.drawEllipse(spinPoint(m_samples, plot, m_samples.first()), 4.0, 4.0);

        const int index = currentSampleIndex();
        if (index >= 0)
        {
            painter.setPen(QPen(QColor(20, 22, 26), 1.0));
            painter.setBrush(kCurrentColor);
            painter.drawEllipse(spinPoint(m_samples, plot, m_samples.at(index)),
                                5.5, 5.5);
        }

        painter.setPen(kMutedText);
        painter.drawText(QRectF(plot.left(), plot.bottom() + 2.0,
                                plot.width(), 16.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1° … %2°")
                             .arg(minimum, 0, 'f', 1)
                             .arg(maximum, 0, 'f', 1));
        return;
    }

    if (!m_fitValid)
    {
        painter.setPen(kErrorColor);
        painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter,
                         QStringLiteral("无法解算"));
        return;
    }

    if (m_hasPath)
    {
        QPolygonF polyline;
        for (const solver::Sample& sample : m_samples)
            polyline.append(mapSolverToWidget(sample.pose.position));
        painter.setPen(QPen(kTrajectory, 1.8, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(polyline);
    }

    const QPointF start = mapSolverToWidget(m_startPose);
    const QPointF tip = mapSolverToWidget(m_arrowTipSolver);
    const double arrowLength =
        std::hypot(tip.x() - start.x(), tip.y() - start.y());
    if (usesVelocity() && arrowLength > 1.5)
    {
        painter.setPen(QPen(kVelocityColor, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(start, tip);
        drawArrowHead(painter, tip, tip - start, 8.0, kVelocityColor);
    }
    else if (usesVelocity())
    {
        painter.setPen(QPen(kVelocityColor, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(tip, 5.0, 5.0);
    }

    painter.setPen(QPen(QColor(20, 22, 26), 1.0));
    painter.setBrush(kStartColor);
    painter.drawEllipse(start, 4.5, 4.5);

    const int index = currentSampleIndex();
    if (index >= 0)
    {
        const QPointF current =
            mapSolverToWidget(m_samples.at(index).pose.position);
        painter.setPen(QPen(QColor(20, 22, 26), 1.5));
        painter.setBrush(kCurrentColor);
        painter.drawEllipse(current, 5.0, 5.0);
    }

    // The black-hole target is drawn last so it stays visible when it sits
    // exactly on the solved entity's start (a common setup: launch from the
    // hole).  Its ring is deliberately wider than the start/current dots.
    if (m_hasTarget)
    {
        const QPointF target = mapSolverToWidget(m_targetSolver);
        painter.setPen(QPen(kTargetColor, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(target, 8.5, 8.5);
        painter.drawLine(target + QPointF(-6.0, 0.0),
                         target + QPointF(6.0, 0.0));
        painter.drawLine(target + QPointF(0.0, -6.0),
                         target + QPointF(0.0, 6.0));
    }

    // Name the two dots.  Without labels an overlapping target and start look
    // like a single anonymous point ("which one is the black hole?").
    QFont labelFont = painter.font();
    labelFont.setPointSizeF(std::max(7.0, labelFont.pointSizeF() - 1.0));
    painter.setFont(labelFont);
    const QFontMetricsF metrics(labelFont, this);
    const auto drawDotLabel = [&](const QString& text, const QPointF& anchor,
                                  const QColor& color, bool above) {
        if (text.isEmpty())
            return;
        const QString shown = metrics.elidedText(text, Qt::ElideRight, 104.0);
        const double w = metrics.horizontalAdvance(shown) + 8.0;
        const double h = metrics.height() + 2.0;
        QRectF box(anchor.x() + 9.0,
                   above ? anchor.y() - h - 7.0 : anchor.y() + 7.0, w, h);
        if (box.right() > width() - 4.0)
            box.moveLeft(anchor.x() - 9.0 - w);
        if (box.left() < 4.0)
            box.moveLeft(4.0);
        if (box.top() < 25.0)
            box.moveTop(25.0);
        if (box.bottom() > height() - 4.0)
            box.moveTop(height() - 4.0 - h);
        painter.fillRect(box, QColor(18, 21, 26, 215));
        painter.setPen(color);
        painter.drawText(box, Qt::AlignCenter, shown);
    };
    if (m_hasTarget)
    {
        drawDotLabel(
            m_targetName.isEmpty() ? QStringLiteral("目标") : m_targetName,
            mapSolverToWidget(m_targetSolver), kTargetColor, true);
    }
    drawDotLabel(m_entityName.isEmpty() ? QStringLiteral("求解点") : m_entityName,
                 start, kStartColor, false);
}

bool SolverPreviewWidget::nearArrowTip(const QPointF& widgetPos) const
{
    if (!usesVelocity() || m_samples.isEmpty() || !m_fitValid)
        return false;
    const QPointF tip = arrowTip();
    return std::hypot(tip.x() - widgetPos.x(), tip.y() - widgetPos.y())
        <= kArrowHitRadius;
}

void SolverPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    // Background drag pans the view; the velocity handle keeps priority so a
    // click near the arrow tip never turns into a pan.
    if (event->button() == Qt::LeftButton && m_fitValid
        && m_effect != SolverEffect::Spin
        && !nearArrowTip(event->position()))
    {
        m_panning = true;
        m_panLast = event->position();
        m_panCenterStart = m_solverCenter;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || !m_document
        || !m_error.isEmpty() || !nearArrowTip(event->position()))
    {
        QWidget::mousePressEvent(event);
        return;
    }

    const model::Project& project = m_document->project();
    const auto entityIt = project.entities.constFind(m_entityUuid);
    if (entityIt == project.entities.constEnd() || entityIt->locked)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    const model::SolverSegment* solver = findSolver(entityIt.value());
    if (!solver)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    m_dragging = true;
    m_dragMoved = false;
    m_dragOriginalVelocity = solver->initialVelocity;
    setCursor(Qt::ClosedHandCursor);
    setFocus(Qt::MouseFocusReason);
    event->accept();
}

void SolverPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning)
    {
        if (!m_fitValid || m_scale <= 0.0)
        {
            event->accept();
            return;
        }
        const QPointF delta = event->position() - m_panLast;
        m_panLast = event->position();
        // Screen Y is down while solver Y is up: dragging down must increase
        // the solver-space centre Y to make the content follow the pointer.
        m_solverCenter = QPointF(m_panCenterStart.x() - delta.x() / m_scale,
                                 m_panCenterStart.y() + delta.y() / m_scale);
        m_panCenterStart = m_solverCenter;
        update();
        event->accept();
        return;
    }

    if (!m_dragging)
    {
        setCursor(nearArrowTip(event->position()) ? Qt::SizeAllCursor
                                                  : Qt::ArrowCursor);
        event->accept();
        return;
    }

    if (!m_document || !m_fitValid)
        return;

    const QPointF solverPos = mapWidgetToSolver(event->position());
    QPointF velocity = (solverPos - m_startPose) / m_arrowSeconds;
    if (!std::isfinite(velocity.x()) || !std::isfinite(velocity.y()))
        return;

    velocity.setX(std::clamp(velocity.x(), -1000000.0, 1000000.0));
    velocity.setY(std::clamp(velocity.y(), -1000000.0, 1000000.0));

    model::Project& project = m_document->mutableProject();
    auto entityIt = project.entities.find(m_entityUuid);
    if (entityIt == project.entities.end())
        return;

    bool found = false;
    for (model::SolverSegment& segment : entityIt.value().solvers)
    {
        if (segment.id == m_solverId)
        {
            segment.initialVelocity = velocity;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    m_dragMoved = true;
    computePreview(true);
    event->accept();
}

void SolverPreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_panning)
    {
        m_panning = false;
        setCursor(nearArrowTip(event->position()) ? Qt::SizeAllCursor
                                                  : Qt::ArrowCursor);
        update();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || !m_dragging)
    {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_dragging = false;
    setCursor(Qt::ArrowCursor);

    if (!m_document || !m_dragMoved)
    {
        update();
        return;
    }

    QPointF currentVelocity;
    bool found = false;
    {
        const model::Project& project = m_document->project();
        const auto entityIt = project.entities.constFind(m_entityUuid);
        if (entityIt != project.entities.constEnd())
        {
            for (const model::SolverSegment& segment : entityIt.value().solvers)
            {
                if (segment.id == m_solverId)
                {
                    currentVelocity = segment.initialVelocity;
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found
        || std::hypot(currentVelocity.x() - m_dragOriginalVelocity.x(),
                      currentVelocity.y() - m_dragOriginalVelocity.y())
               < 1e-6)
    {
        refreshPreview();
        return;
    }

    QString error;
    const bool ok = m_document->commitEntity(
        m_entityUuid, QStringLiteral("pvf: set solver initial velocity"),
        &error);
    if (!ok)
    {
        // Put the pre-drag value back so the preview and the spin boxes never
        // show a velocity that was not saved.
        model::Project& project = m_document->mutableProject();
        auto entityIt = project.entities.find(m_entityUuid);
        if (entityIt != project.entities.end())
        {
            for (model::SolverSegment& segment : entityIt.value().solvers)
            {
                if (segment.id == m_solverId)
                {
                    segment.initialVelocity = m_dragOriginalVelocity;
                    break;
                }
            }
        }
    }

    refreshPreview();
    event->accept();
}

void SolverPreviewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_document
        && !nearArrowTip(event->position()))
    {
        // Discard the manual pan/zoom and fit the current trajectory again.
        m_fitValid = false;
        computePreview(false);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SolverPreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_fitValid || m_scale <= 0.0 || m_effect == SolverEffect::Spin)
    {
        QWidget::wheelEvent(event);
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    if (std::abs(steps) < 1e-9)
    {
        event->ignore();
        return;
    }

    const QPointF cursor = event->position();
    const QPointF anchorSolver = mapWidgetToSolver(cursor);
    m_scale = std::clamp(m_scale * std::pow(1.15, steps), 1e-4, 1e6);
    const QPointF center = m_plotRect.center();
    // Keep the solver point under the cursor pinned while zooming.
    m_solverCenter = QPointF(
        anchorSolver.x() - (cursor.x() - center.x()) / m_scale,
        anchorSolver.y() + (cursor.y() - center.y()) / m_scale);
    update();
    event->accept();
}

bool SolverPreviewWidget::restoreOriginalVelocity()
{
    if (!m_document || !m_dragMoved)
        return false;

    model::Project& project = m_document->mutableProject();
    auto entityIt = project.entities.find(m_entityUuid);
    if (entityIt == project.entities.end())
        return false;

    for (model::SolverSegment& segment : entityIt.value().solvers)
    {
        if (segment.id != m_solverId)
            continue;
        segment.initialVelocity = m_dragOriginalVelocity;
        return true;
    }
    return false;
}

bool SolverPreviewWidget::cancelDrag()
{
    if (!m_dragging)
        return false;

    const bool moved = m_dragMoved;
    // restoreOriginalVelocity() keys off m_dragMoved, so restore before the
    // flags are cleared.
    restoreOriginalVelocity();
    m_dragging = false;
    m_dragMoved = false;
    setCursor(Qt::ArrowCursor);
    computePreview(false);
    update();
    return moved;
}

void SolverPreviewWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_dragging)
    {
        cancelDrag();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SolverPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_effect != SolverEffect::Spin)
    {
        computePositionFit();
        updateArrowLength();
    }
    update();
}

void SolverPreviewWidget::leaveEvent(QEvent* event)
{
    if (!m_dragging && !m_panning)
        setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

} // namespace pvf::next
