#pragma once

#include "next/core/Document.h"
#include "solver/Solver.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

namespace pvf::next {

// Read-only 2D preview of one solver segment for the properties panel.
//
// The trajectory is evaluated with solver::evaluate() against a small
// solver::World adapter that forwards to render2::SceneWorld.  The adapter maps
// the segment's entity-relative frames to project frames and converts the
// project-space target shapes/poses back into the driven entity's parent space,
// which is the solver's coordinate space.
//
// gravity/blackhole segments also get a draggable initial-velocity handle:
// the arrow tip is startPose.position + initialVelocity * kVelocitySeconds.
// Dragging updates the in-memory model live and commits exactly once on
// release with the message "pvf: set solver initial velocity".
class SolverPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SolverPreviewWidget(Document* document, QWidget* parent = nullptr);
    SolverPreviewWidget(Document* document, const Uuid& entityUuid,
                        const Uuid& solverId, QWidget* parent = nullptr);

    // Binds the preview to a solver segment.  Calling it again with the same
    // ids only repaints; call refreshPreview() after the model changed.
    void setSolver(const Uuid& entityUuid, const Uuid& solverId);
    void clearSolver();

    Uuid entityUuid() const { return m_entityUuid; }
    Uuid solverId() const { return m_solverId; }

    QString errorText() const { return m_error; }
    int sampleCount() const { return m_samples.size(); }
    bool hasPath() const { return m_hasPath; }
    bool hasTarget() const { return m_hasTarget; }
    bool hasArrow() const;
    bool isTruncated() const { return m_truncated; }

    // Widget-space helpers used by tests and by hit testing.
    QPointF startPoint() const;
    QPointF arrowTip() const;
    QPointF targetPoint() const;
    QPointF currentPoint() const;
    QPointF mapSolverToWidget(const QPointF& point) const;
    QPointF mapWidgetToSolver(const QPointF& point) const;

    // Solver-space values (parent space, px/s for velocities).
    QPointF startPose() const { return m_startPose; }
    QPointF arrowTipSolver() const { return m_arrowTipSolver; }
    QPointF initialVelocity() const { return m_initialVelocity; }

    QSize sizeHint() const override;

public slots:
    void refreshPreview();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool usesVelocity() const;
    const model::SolverSegment* findSolver(const model::Entity& entity) const;
    void computePreview(bool duringDrag);
    void computePositionFit();
    int currentRelativeFrame() const;
    int currentSampleIndex() const;
    bool nearArrowTip(const QPointF& widgetPos) const;

    Document* m_document = nullptr;
    Uuid m_entityUuid;
    Uuid m_solverId;

    int m_entityStart = 0;
    int m_segmentStart = 0;
    SolverEffect m_effect = SolverEffect::Gravity;

    QVector<solver::Sample> m_samples;
    QPointF m_startPose;
    QPointF m_initialVelocity;
    QPointF m_arrowTipSolver;
    QPointF m_targetSolver;
    bool m_hasTarget = false;
    bool m_hasPath = false;
    bool m_truncated = false;
    QString m_error;

    // Position-mode fit.  Spin mode uses its own frame/rotation mapping.
    QRectF m_plotRect;
    QRectF m_solverBounds;
    QPointF m_solverCenter;
    double m_scale = 1.0;
    bool m_fitValid = false;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPointF m_dragOriginalVelocity;
};

} // namespace pvf::next
