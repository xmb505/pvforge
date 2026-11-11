#pragma once

#include "next/core/Document.h"
#include "solver/Solver.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

namespace pvf::anim {
struct Cache;
}

namespace pvf::next {

// Read-only 2D preview of one solver segment for the properties panel.
//
// The trajectory is evaluated with solver::evaluate() against a small
// solver::World adapter that forwards to render2::SceneWorld.  The adapter maps
// the segment's entity-relative frames to project frames and converts the
// project-space target shapes/poses back into the driven entity's parent space,
// which is the solver's coordinate space.
//
// gravity/blackhole segments also get a draggable initial-velocity handle.
// The arrow is a screen-space gizmo with a fixed 48 px length (direction maps
// to the velocity, magnitude is in the X/Y fields), so panning/zooming cannot
// turn it into a huge world vector.  Dragging updates the in-memory model live
// and commits exactly once on release with the message "pvf: set solver
// initial velocity".  Esc (or cancelDrag()) puts the pre-drag velocity back
// without a commit.
//
// During playback the preview freezes: playhead ticks do not repaint the
// current dot and never re-evaluate the trajectory.  Pausing refreshes once.
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

    // Playback freeze: while the project is playing the mini preview keeps
    // its last evaluated trajectory/current point and stops repainting per
    // playhead tick.  Pausing refreshes it once, on the current frame.
    void setPlaybackActive(bool active);
    bool isPlaybackActive() const { return m_playbackActive; }
    // Test hook: number of real computePreview() evaluations.  Playhead ticks
    // during playback must not grow it.
    int computeCount() const { return m_computeCount; }

    // Borrowed session cache.  When it already holds the bound segment, the
    // preview draws those samples instead of solving the segment again.
    void setAnimCache(anim::Cache* cache) { m_animCache = cache; }
    anim::Cache* animCache() const { return m_animCache; }

    // Drops an in-progress handle drag and restores the pre-drag velocity.
    // Returns true when a drag was actually cancelled.
    bool cancelDrag();
    bool isDragging() const { return m_dragging; }

    QString errorText() const { return m_error; }
    // Names shown next to the two dots, so the black hole target is never
    // confused with the solved entity (they can overlap exactly).
    QString entityName() const { return m_entityName; }
    QString targetName() const { return m_targetName; }
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
    // Widget-space trajectory point at one sample index; useful for verifying
    // that a live velocity drag updates the path, not just the arrow.
    QPointF pathPoint(int index) const;

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
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool usesVelocity() const;
    const model::SolverSegment* findSolver(const model::Entity& entity) const;
    void computePreview(bool duringDrag);
    void computePositionFit();
    void updateArrowLength();
    bool restoreOriginalVelocity();
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
    // Solver-space time base of the arrow (0.25 s = kVelocitySeconds in the
    // .cpp); stretched so the handle stays 26..72 px long after the fit.
    double m_arrowSeconds = 0.25;
    QPointF m_targetSolver;
    QString m_entityName;
    QString m_targetName;
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

    // Playback freeze.  m_frozenRelativeFrame is the entity-relative frame of
    // the dot that was current when playback started; paintEvent keeps using
    // it until pause refreshes the preview.
    bool m_playbackActive = false;
    int m_frozenRelativeFrame = -1;
    int m_computeCount = 0;
    anim::Cache* m_animCache = nullptr;

    // Background pan: keeps the auto-fit until the user drags, then the view
    // stays where it was put.  Double-click refits.
    bool m_panning = false;
    QPointF m_panLast;
    QPointF m_panCenterStart;
};

} // namespace pvf::next
