#include "SolverInternal.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace pvf::solver::detail {
namespace {

QPointF gravitationalAcceleration(const QPointF &position, const QPointF &center,
                                  double mu, double softening)
{
    const QPointF delta = position - center;
    const double distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
    const double softened = distanceSquared + softening * softening;
    const double denominator = std::pow(softened, 1.5);
    if (denominator <= 1e-12)
        return QPointF(0.0, 0.0);
    // Pull toward the center: acceleration = -mu * delta / (r^2 + eps^2)^1.5.
    return delta * (-mu / denominator);
}

} // namespace

QVector<Sample> evaluateBlackHole(const Segment &segment, const World &world,
                                  const FrameRange &range, int fps)
{
    if (segment.targetUuid.isEmpty())
        return {};

    const double simulationSpeed = std::max(
            0.0, paramNumber(segment.params, "simulationSpeed", 1.0));
    const double mass = std::max(
            0.0, paramNumber(segment.params, "mass", 1000.0));
    const double angularMomentum = paramNumber(
            segment.params, "angularMomentum", 0.0);
    const double eventHorizonRadius = std::max(
            0.0, paramNumber(segment.params, "eventHorizonRadius", 80.0));
    const double objectRadius = std::max(
            0.0, paramNumber(segment.params, "objectRadius", 40.0));
    const double softening = std::max(
            1e-6, paramNumber(segment.params, "softening", 60.0));
    const double maxSpeed = std::max(
            0.0, paramNumber(segment.params, "maxSpeed", 4000.0));
    // The horizon is deliberately not a hard boundary: objects may cross it and
    // either escape or keep falling.  It is accepted here for UI/preview parity.
    (void)eventHorizonRadius;

    // `mass` is a relative authoring unit; scale it to a usable gravitational
    // parameter at typical canvas distances.
    const double mu = mass * 1000.0;

    // The object's own radius keeps the softened core finite even when the UI
    // leaves `softening` at zero.
    const double effectiveSoftening = std::max(softening, objectRadius);

    const Uuid &centerEntity = segment.targetUuid;

    const double dt = simulationSpeed / double(fps);

    Pose pose = segment.startPose;
    QPointF velocity = segment.initialVelocity; // solver space
    if (maxSpeed > 0.0)
    {
        const double initialSpeed = std::hypot(velocity.x(), velocity.y());
        if (initialSpeed > maxSpeed)
            velocity *= maxSpeed / initialSpeed;
    }
    bool stopped = false;

    QVector<Sample> samples;
    samples.reserve(range.count);

    for (int i = 0; i < range.count; ++i)
    {
        const int frame = range.startFrame + i;

        if (i > 0 && !stopped && simulationSpeed > 0.0)
        {
            const QPointF center = world.poseOf(centerEntity, frame).position;

            // Velocity Verlet: a single fixed step per frame, second order for
            // the inverse-square field.
            const QPointF accelerationStart = gravitationalAcceleration(
                    pose.position, center, mu, effectiveSoftening);
            const QPointF newPosition = pose.position + velocity * dt
                    + accelerationStart * (0.5 * dt * dt);
            const QPointF accelerationEnd = gravitationalAcceleration(
                    newPosition, center, mu, effectiveSoftening);
            QPointF newVelocity = velocity
                    + (accelerationStart + accelerationEnd) * (0.5 * dt);

            if (angularMomentum != 0.0)
            {
                const QPointF delta = newPosition - center;
                const double distanceSquared = delta.x() * delta.x()
                        + delta.y() * delta.y();
                const double omega = angularMomentum
                        / (distanceSquared + effectiveSoftening * effectiveSoftening);
                newVelocity = rotatePoint(newVelocity,
                                          qRadiansToDegrees(omega * dt));
            }

            if (maxSpeed > 0.0)
            {
                const double speed = std::hypot(newVelocity.x(), newVelocity.y());
                if (speed > maxSpeed)
                    newVelocity *= maxSpeed / speed;
            }

            pose.position = newPosition;
            velocity = newVelocity;
        }

        const bool last = (i == range.count - 1);
        if (last)
        {
            velocity = QPointF(0.0, 0.0);
            stopped = true;
        }

        Sample sample;
        sample.frame = frame;
        sample.pose = pose;
        sample.velocity = velocity;
        sample.collided = false;
        sample.stopped = stopped;
        samples.append(sample);
    }

    return samples;
}

} // namespace pvf::solver::detail
