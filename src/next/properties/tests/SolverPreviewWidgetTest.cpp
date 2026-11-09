#include "next/properties/PropertiesPanel.h"
#include "next/properties/SolverPreviewWidget.h"

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace pvf;
using namespace pvf::next;

namespace {

class FakeDocument : public Document
{
    Q_OBJECT

public:
    FakeDocument()
        : m_assets(m_tempDir.path())
    {
        m_project.settings.width = 1920;
        m_project.settings.height = 1080;
        m_project.settings.fps = 30;
    }

    const model::Project& project() const override { return m_project; }
    model::Project& mutableProject() override { return m_project; }
    assets::AssetStore& assets() override { return m_assets; }

    int playhead() const override { return m_playhead; }
    void setPlayhead(int frame) override { m_playhead = frame; }

    Uuid selection() const override { return m_selection; }
    void setSelection(const Uuid& uuid) override
    {
        m_selection = uuid;
        emit selectionChanged();
    }

    bool commitEntity(const Uuid& uuid, const QString& message,
                      QString* error = nullptr) override
    {
        Q_UNUSED(uuid);
        if (m_failCommit)
        {
            if (error)
                *error = QStringLiteral("fake commit failure");
            return false;
        }
        ++m_entityCommits;
        m_lastMessage = message;
        emit changed();
        return true;
    }

    bool commitProject(const QString& message, QString* error = nullptr) override
    {
        Q_UNUSED(message);
        Q_UNUSED(error);
        ++m_projectCommits;
        emit changed();
        return true;
    }

    bool undo(QString* error = nullptr) override
    {
        Q_UNUSED(error);
        return false;
    }

    bool redo(QString* error = nullptr) override
    {
        Q_UNUSED(error);
        return false;
    }

    Uuid addEntity(const QString& name, int start, int end)
    {
        model::Entity entity;
        entity.uuid = model::newUuid();
        entity.name = name;
        entity.start = start;
        entity.end = end;
        const Uuid uuid = entity.uuid;
        m_project.entities.insert(uuid, entity);
        return uuid;
    }

    Uuid addVector(const QString& name, double x, double y, double w, double h,
                   int start = 0, int end = 120)
    {
        const Uuid uuid = addEntity(name, start, end);
        model::Entity& entity = m_project.entities[uuid];
        entity.content.kind = ContentKind::Vector;
        entity.content.vector.shape = 0;
        entity.content.vector.width = w;
        entity.content.vector.height = h;
        entity.transform.x = x;
        entity.transform.y = y;
        return uuid;
    }

    void addKey(const Uuid& uuid, const QString& trackName, int frame, double value)
    {
        model::Keyframe keyframe;
        keyframe.frame = frame;
        keyframe.value = value;
        m_project.entities[uuid].tracks[trackName].keys.append(keyframe);
    }

    void movePlayhead(int frame)
    {
        m_playhead = frame;
        emit playheadChanged();
    }

    model::Entity& entity(const Uuid& uuid) { return m_project.entities[uuid]; }
    int entityCommits() const { return m_entityCommits; }
    QString lastMessage() const { return m_lastMessage; }
    void setFailCommit(bool fail) { m_failCommit = fail; }

private:
    QTemporaryDir m_tempDir;
    assets::AssetStore m_assets;
    model::Project m_project;
    int m_playhead = 0;
    Uuid m_selection;
    int m_entityCommits = 0;
    int m_projectCommits = 0;
    QString m_lastMessage;
    bool m_failCommit = false;
};

struct GravityScene
{
    Uuid ground;
    Uuid ball;
    Uuid solverId;
};

GravityScene makeGravityScene(FakeDocument& document, QPointF velocity = QPointF(300.0, 0.0))
{
    GravityScene scene;
    scene.ground = document.addVector(QStringLiteral("地面"), 0.0, 300.0, 900.0, 40.0);
    document.entity(scene.ground).physicsInteract = true;

    scene.ball = document.addVector(QStringLiteral("小球"), -350.0, -220.0, 40.0, 40.0);
    document.addKey(scene.ball, QStringLiteral("x"), 0, -350.0);
    document.addKey(scene.ball, QStringLiteral("y"), 0, -220.0);

    model::SolverSegment solver;
    solver.id = model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 0;
    solver.endFrame = 59;
    solver.initialVelocity = velocity;
    solver.params.insert(QStringLiteral("targetUuid"), scene.ground);
    solver.params.insert(QStringLiteral("strength"), 980.0);
    solver.params.insert(QStringLiteral("elasticity"), 0.35);
    solver.params.insert(QStringLiteral("friction"), 0.2);
    solver.params.insert(QStringLiteral("stopThreshold"), 2.0);
    scene.solverId = solver.id;
    document.entity(scene.ball).solvers.append(solver);
    return scene;
}

bool insideRect(const QPointF& point, const QRect& rect)
{
    return rect.adjusted(-1, -1, 1, 1).contains(point.toPoint());
}

} // namespace

class SolverPreviewWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void gravityPreviewShowsPathTargetStartAndArrow();
    void zeroVelocityHandleIsDraggable();
    void arrowDragChangesVelocityAndCommitsOnce();
    void dragCommitFailureRestoresVelocity();
    void playheadChangesCurrentPoint();
    void missingTargetShowsErrorWithoutCrash();
    void orbitAndBlackHolePreviewSmoke();
    void spinPreviewDoesNotCrash();
    void propertiesPanelEmbedsPreview();
};

void SolverPreviewWidgetTest::gravityPreviewShowsPathTargetStartAndArrow()
{
    FakeDocument document;
    const GravityScene scene = makeGravityScene(document);

    SolverPreviewWidget preview(&document);
    preview.resize(400, 200);
    preview.setSolver(scene.ball, scene.solverId);

    QCOMPARE(preview.errorText(), QString());
    QCOMPARE(preview.sampleCount(), 60);
    QVERIFY(preview.hasPath());
    QVERIFY(preview.hasTarget());
    QVERIFY(preview.hasArrow());

    const QPointF start = preview.startPoint();
    const QPointF arrow = preview.arrowTip();
    const QPointF target = preview.targetPoint();
    QVERIFY(insideRect(start, preview.rect()));
    QVERIFY(insideRect(arrow, preview.rect()));
    QVERIFY(insideRect(target, preview.rect()));
    QVERIFY(start != arrow);
    QVERIFY(start != target);

    const QImage image = preview.grab().toImage();
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), preview.size());
}

void SolverPreviewWidgetTest::zeroVelocityHandleIsDraggable()
{
    FakeDocument document;
    const GravityScene scene = makeGravityScene(document, QPointF(0.0, 0.0));

    SolverPreviewWidget preview(&document);
    preview.resize(400, 200);
    preview.show();
    preview.setSolver(scene.ball, scene.solverId);

    QVERIFY(preview.hasArrow());
    QCOMPARE(preview.arrowTip().toPoint(), preview.startPoint().toPoint());

    const QPoint tip = preview.arrowTip().toPoint();
    QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, tip);
    QTest::mouseMove(&preview, tip + QPoint(40, -20));
    QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier,
                        tip + QPoint(40, -20));

    const QPointF updated = document.entity(scene.ball).solvers.at(0).initialVelocity;
    QVERIFY(std::hypot(updated.x(), updated.y()) > 0.0);
    QCOMPARE(document.entityCommits(), 1);
}

void SolverPreviewWidgetTest::arrowDragChangesVelocityAndCommitsOnce()
{
    FakeDocument document;
    const GravityScene scene = makeGravityScene(document, QPointF(120.0, -60.0));

    SolverPreviewWidget preview(&document);
    preview.resize(400, 200);
    preview.show();
    preview.setSolver(scene.ball, scene.solverId);
    QVERIFY(preview.hasArrow());

    const QPointF original = document.entity(scene.ball).solvers.at(0).initialVelocity;
    const QPoint tip = preview.arrowTip().toPoint();
    QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, tip);
    QTest::mouseMove(&preview, tip + QPoint(45, -25));
    QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier,
                        tip + QPoint(45, -25));

    const QPointF updated = document.entity(scene.ball).solvers.at(0).initialVelocity;
    QVERIFY(updated != original);
    QCOMPARE(document.entityCommits(), 1);
    QCOMPARE(document.lastMessage(),
             QStringLiteral("pvf: set solver initial velocity"));
    QVERIFY(preview.hasArrow());
}

void SolverPreviewWidgetTest::dragCommitFailureRestoresVelocity()
{
    FakeDocument document;
    const GravityScene scene = makeGravityScene(document, QPointF(120.0, -60.0));
    document.setFailCommit(true);

    SolverPreviewWidget preview(&document);
    preview.resize(400, 200);
    preview.show();
    preview.setSolver(scene.ball, scene.solverId);

    const QPointF original = document.entity(scene.ball).solvers.at(0).initialVelocity;
    const QPoint tip = preview.arrowTip().toPoint();
    QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, tip);
    QTest::mouseMove(&preview, tip + QPoint(45, -25));
    QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier,
                        tip + QPoint(45, -25));

    QCOMPARE(document.entity(scene.ball).solvers.at(0).initialVelocity, original);
    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(preview.hasArrow());
}

void SolverPreviewWidgetTest::playheadChangesCurrentPoint()
{
    FakeDocument document;
    const GravityScene scene = makeGravityScene(document);

    document.movePlayhead(0);
    SolverPreviewWidget preview(&document);
    preview.resize(400, 200);
    preview.setSolver(scene.ball, scene.solverId);

    const QPointF first = preview.currentPoint();
    QVERIFY(!first.isNull());

    document.movePlayhead(20);
    const QPointF later = preview.currentPoint();
    QVERIFY(!later.isNull());
    QVERIFY(later != first);

    document.movePlayhead(200); // past the segment end
    QVERIFY(preview.currentPoint().isNull());
}

void SolverPreviewWidgetTest::missingTargetShowsErrorWithoutCrash()
{
    FakeDocument document;
    const Uuid ball = document.addVector(QStringLiteral("小球"), -100.0, -100.0, 40.0, 40.0);
    document.addKey(ball, QStringLiteral("x"), 0, -100.0);
    document.addKey(ball, QStringLiteral("y"), 0, -100.0);

    model::SolverSegment solver;
    solver.id = model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 0;
    solver.endFrame = 30;
    solver.params.insert(QStringLiteral("targetUuid"),
                         QStringLiteral("0190f2c4-0000-7000-8000-000000000000"));
    document.entity(ball).solvers.append(solver);

    SolverPreviewWidget preview(&document);
    preview.resize(400, 200);
    preview.setSolver(ball, solver.id);

    QVERIFY(preview.errorText().contains(QStringLiteral("目标缺失")));
    QCOMPARE(preview.sampleCount(), 0);
    QVERIFY(!preview.hasPath());
    QVERIFY(!preview.hasTarget());
    QVERIFY(!preview.grab().toImage().isNull());
}

void SolverPreviewWidgetTest::orbitAndBlackHolePreviewSmoke()
{
    for (const SolverEffect effect : {SolverEffect::Orbit, SolverEffect::BlackHole})
    {
        FakeDocument document;
        const Uuid target =
            document.addVector(QStringLiteral("中心"), 0.0, 0.0, 120.0, 120.0);
        const Uuid ball =
            document.addVector(QStringLiteral("物体"), -220.0, 0.0, 40.0, 40.0);
        document.addKey(ball, QStringLiteral("x"), 0, -220.0);
        document.addKey(ball, QStringLiteral("y"), 0, 0.0);

        model::SolverSegment solver;
        solver.id = model::newUuid();
        solver.effect = effect;
        solver.startFrame = 0;
        solver.endFrame = 30;
        solver.params.insert(QStringLiteral("targetUuid"), target);
        if (effect == SolverEffect::Orbit)
        {
            solver.params.insert(QStringLiteral("aspect"), 1.0);
            solver.params.insert(QStringLiteral("angle"), 0.0);
            solver.params.insert(QStringLiteral("direction"), 1);
            solver.params.insert(QStringLiteral("periodFrames"), 30);
        }
        else
        {
            solver.initialVelocity = QPointF(80.0, 120.0);
            solver.params.insert(QStringLiteral("simulationSpeed"), 1.0);
            solver.params.insert(QStringLiteral("mass"), 1000.0);
            solver.params.insert(QStringLiteral("angularMomentum"), 0.0);
            solver.params.insert(QStringLiteral("eventHorizonRadius"), 80.0);
            solver.params.insert(QStringLiteral("objectRadius"), 40.0);
            solver.params.insert(QStringLiteral("softening"), 60.0);
            solver.params.insert(QStringLiteral("maxSpeed"), 4000.0);
        }
        document.entity(ball).solvers.append(solver);

        SolverPreviewWidget preview(&document);
        preview.resize(400, 200);
        preview.setSolver(ball, solver.id);

        QCOMPARE(preview.errorText(), QString());
        QCOMPARE(preview.sampleCount(), 31);
        QVERIFY(preview.hasPath());
        QVERIFY(preview.hasTarget());
        QVERIFY(!preview.grab().toImage().isNull());
    }
}

void SolverPreviewWidgetTest::spinPreviewDoesNotCrash()
{
    FakeDocument document;
    const Uuid ball = document.addVector(QStringLiteral("旋转体"), 0.0, 0.0, 120.0, 80.0);
    document.addKey(ball, QStringLiteral("rotation"), 0, 15.0);

    model::SolverSegment solver;
    solver.id = model::newUuid();
    solver.effect = SolverEffect::Spin;
    solver.startFrame = 0;
    solver.endFrame = 29;
    solver.params.insert(QStringLiteral("angularVelocity"), 6.0);
    solver.params.insert(QStringLiteral("direction"), 1);
    document.entity(ball).solvers.append(solver);

    document.movePlayhead(0);
    SolverPreviewWidget preview(&document);
    preview.resize(400, 200);
    preview.setSolver(ball, solver.id);

    QCOMPARE(preview.errorText(), QString());
    QCOMPARE(preview.sampleCount(), 30);
    QVERIFY(preview.hasPath());
    QVERIFY(!preview.hasTarget());
    QVERIFY(!preview.hasArrow());

    const QPointF first = preview.currentPoint();
    QVERIFY(!first.isNull());
    document.movePlayhead(15);
    const QPointF later = preview.currentPoint();
    QVERIFY(!later.isNull());
    QVERIFY(later != first);
    QVERIFY(!preview.grab().toImage().isNull());
}

void SolverPreviewWidgetTest::propertiesPanelEmbedsPreview()
{
    FakeDocument document;
    const GravityScene scene = makeGravityScene(document);

    PropertiesPanel panel(&document);
    document.setSelection(scene.ball);

    auto* card = panel.findChild<QGroupBox*>(QStringLiteral("solverCard0"));
    auto* preview =
        panel.findChild<SolverPreviewWidget*>(QStringLiteral("solverPreview0"));
    QVERIFY(card);
    QVERIFY(preview);
    QVERIFY(preview->height() >= 180);
    QCOMPARE(preview->entityUuid(), scene.ball);
    QCOMPARE(preview->solverId(), scene.solverId);
    QCOMPARE(preview->errorText(), QString());
    QVERIFY(preview->sampleCount() > 0);

    const QPointF arrowBefore = preview->arrowTip();
    auto* velocityX =
        panel.findChild<QDoubleSpinBox*>(QStringLiteral("solverVelocityX0"));
    QVERIFY(velocityX);
    velocityX->setValue(620.0);
    QVERIFY(QMetaObject::invokeMethod(velocityX, "editingFinished"));
    QVERIFY(preview->arrowTip() != arrowBefore);
}

QTEST_MAIN(SolverPreviewWidgetTest)
#include "SolverPreviewWidgetTest.moc"
