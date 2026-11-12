#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

int solverUiTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver UI");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-ui.pvproj"));
    auto session = pvf::EditorSession::createProject(projectDir, settings,
                                                     &error);
    if (!session)
    {
        qCritical() << "solver-ui-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("地面"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 600.0;
    project.entities[ground].content.vector.height = 60.0;
    project.entities[ground].content.vector.fillColor =
        QStringLiteral("#6a707a");
    project.entities[ground].physicsInteract = true;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("落球"), 1, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    project.entities[ball].content.vector.fillColor =
        QStringLiteral("#ffd166");
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    const pvf::Uuid spinner =
        makeEntity(QStringLiteral("自转物体"), 2, pvf::ContentKind::Vector);
    project.entities[spinner].transform.x = 150.0;
    project.entities[spinner].content.vector.width = 60.0;
    project.entities[spinner].content.vector.height = 60.0;
    pvf::model::Track spinRotation;
    spinRotation.keys = {{0, 0.0, {}, {}}};
    project.entities[spinner].tracks.insert(QStringLiteral("rotation"),
                                            spinRotation);

    const pvf::Uuid orbiter =
        makeEntity(QStringLiteral("公转物体"), 3, pvf::ContentKind::Vector);
    project.entities[orbiter].transform.x = -150.0;
    project.entities[orbiter].transform.y = 80.0;
    project.entities[orbiter].content.vector.width = 40.0;
    project.entities[orbiter].content.vector.height = 40.0;
    pvf::model::Track orbitX;
    orbitX.keys = {{0, -150.0, {}, {}}};
    project.entities[orbiter].tracks.insert(QStringLiteral("x"), orbitX);
    pvf::model::Track orbitY;
    orbitY.keys = {{0, 80.0, {}, {}}};
    project.entities[orbiter].tracks.insert(QStringLiteral("y"), orbitY);

    const QVector<pvf::Uuid> setup = {ground, ball, spinner, orbiter};
    if (!session->document()->commitEntities(setup,
                                             QStringLiteral("solver-ui setup"),
                                             &error))
    {
        qCritical() << "solver-ui-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* viewport = window->viewportForTest();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !panel || !viewport || !timeline)
    {
        qCritical() << "solver-ui-test: window parts missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "solver-ui-test: panel edit failed"
                                     << message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-ui-test:" << message;
        return condition;
    };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "solver-ui-test: flush failed" << flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };

    auto* solverSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("solverSection"));
    if (!check(solverSection != nullptr, QStringLiteral("solver section missing")))
        return 1;
    solverSection->setExpanded(true);
    QApplication::processEvents();

    auto* effectCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverEffectCombo"));
    auto* targetCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverTargetCombo"));
    auto* createEnd =
        panel->findChild<QSpinBox*>(QStringLiteral("solverCreateEnd"));
    auto* createManual =
        panel->findChild<QCheckBox*>(QStringLiteral("solverCreateManualEnd"));
    auto* createAuto =
        panel->findChild<QLabel*>(QStringLiteral("solverCreateEndAuto"));
    auto* createButton =
        panel->findChild<QPushButton*>(QStringLiteral("solverCreateButton"));
    if (!check(effectCombo && targetCombo && createEnd && createManual
                   && createAuto && createButton,
               QStringLiteral("solver create widgets missing")))
        return 1;

    // Default creation is automatic: no end-frame spin is shown, only the
    // read-only automatic explanation.
    if (!check(!createEnd->isVisible(),
               QStringLiteral("create end spin is visible by default"))
        || !check(!createManual->isChecked(),
                  QStringLiteral("manual end is checked by default"))
        || !check(createAuto->text().contains(QStringLiteral("自动")),
                  QStringLiteral("automatic end label missing")))
        return 1;

    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    effectCombo->setCurrentIndex(
        effectCombo->findData(int(pvf::SolverEffect::Gravity)));
    targetCombo->setCurrentIndex(targetCombo->findData(ground));
    const int beforeCreate = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();

    const auto& ballEntity = live->project().entities.value(ball);
    if (!check(ballEntity.solvers.size() == 1,
               QStringLiteral("gravity solver was not created"))
        || !check(ballEntity.solvers.first().endFrame == -1,
                  QStringLiteral("new solver is not automatic"))
        || !check(undoDepth() == beforeCreate + 1,
                  QStringLiteral("solver creation was not one commit")))
        return 1;
    const pvf::Uuid gravityId = ballEntity.solvers.first().id;

    // Tighten the collision parameters through the card widgets; every edit
    // goes through EditActions and keeps the automatic end at -1.
    auto* strength =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_strength"));
    auto* elasticity =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_elasticity"));
    auto* friction =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_friction"));
    auto* threshold =
        panel->findChild<QDoubleSpinBox*>(QStringLiteral("solverParam0_stopThreshold"));
    if (!check(strength && elasticity && friction && threshold,
               QStringLiteral("gravity parameter widgets missing")))
        return 1;
    const auto setParam = [](QDoubleSpinBox* spin, double value) {
        spin->setValue(value);
        QMetaObject::invokeMethod(spin, "editingFinished",
                                  Qt::DirectConnection);
        QApplication::processEvents();
    };
    setParam(strength, 500.0);
    setParam(elasticity, 0.0);
    setParam(friction, 0.5);
    setParam(threshold, 10.0);
    flush();

    const pvf::model::Entity tunedBall =
        live->project().entities.value(ball);
    const pvf::SolverPreviewEval expected = pvf::evaluateSolverPreview(
        live->project(), live->assets(), tunedBall,
        tunedBall.solvers.first());
    if (!check(expected.isValid(), QStringLiteral("preview failed: %1")
                                       .arg(expected.error))
        || !check(expected.result.stopFrame > 0
                      && expected.result.stopFrame < 59,
                  QStringLiteral("gravity did not stop inside the clip: "
                                 "stop=%1 reason=%2")
                      .arg(expected.result.stopFrame)
                      .arg(pvf::solver::stopReasonName(
                          expected.result.reason)))
        || !check(expected.result.reason
                      == pvf::solver::StopReason::Collision,
                  QStringLiteral("gravity stop reason is not collision")))
        return 1;
    const int stopFrame = expected.result.stopFrame;

    auto* endInfo =
        panel->findChild<QLabel*>(QStringLiteral("solverEndInfo0"));
    auto* manualEnd =
        panel->findChild<QCheckBox*>(QStringLiteral("solverManualEnd0"));
    auto* solverEnd =
        panel->findChild<QSpinBox*>(QStringLiteral("solverEnd0"));
    if (!check(endInfo && manualEnd && solverEnd,
               QStringLiteral("solver card end widgets missing"))
        || !check(endInfo->text().contains(QString::number(stopFrame))
                      && endInfo->text().contains(QStringLiteral("碰撞")),
                  QStringLiteral("card does not show stop frame/reason: %1")
                      .arg(endInfo ? endInfo->text() : QString()))
        || !check(!solverEnd->isVisible(),
                  QStringLiteral("card end spin is visible in automatic mode")))
        return 1;

    // Advanced override: toggling the checkbox compacts one explicit end,
    // editing the spin updates it, and unticking returns to automatic.
    manualEnd->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.first().endFrame
                   == stopFrame,
               QStringLiteral("manual end toggle did not write stop frame"))
        || !check(manualEnd->isChecked() && solverEnd->isVisible(),
                  QStringLiteral("manual end spin did not appear")))
        return 1;
    solverEnd->setValue(12);
    QMetaObject::invokeMethod(solverEnd, "editingFinished",
                              Qt::DirectConnection);
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.first().endFrame
                   == 12,
               QStringLiteral("explicit end edit did not reach the model"))
        || !check(endInfo->text().contains(QStringLiteral("显式覆盖")),
                  QStringLiteral("explicit end label missing")))
        return 1;
    manualEnd->click();
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.first().endFrame
                   == -1,
               QStringLiteral("manual end toggle did not restore automatic")))
        return 1;

    // The trajectory now lives in the properties panel's mini window; the
    // viewport no longer draws a solver overlay or a velocity handle.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    auto* mini = panel->findChild<pvf::next::SolverPreviewWidget*>(
        QStringLiteral("solverPreviewMini"));
    if (!check(!viewport->sessionSolverPreviewVisibleForTest(),
               QStringLiteral("viewport still draws the solver overlay"))
        || !check(mini != nullptr, QStringLiteral("mini solver preview missing")))
        return 1;
    if (!check(mini->entityUuid() == ball && mini->solverId() == gravityId,
               QStringLiteral("mini preview is not bound to the gravity solver"))
        || !check(mini->hasTarget() && mini->hasPath() && mini->hasArrow(),
                  QStringLiteral("mini preview misses target/path/arrow: %1")
                      .arg(mini->errorText()))
        || !check(mini->sampleCount() >= 2 && !mini->isTruncated(),
                  QStringLiteral("mini preview sample count wrong: %1")
                      .arg(mini->sampleCount())))
        return 1;

    // Playhead changes repaint the mini window without a commit.
    live->setPlayhead(5);
    QApplication::processEvents();
    if (!check(!mini->currentPoint().isNull(),
               QStringLiteral("mini preview current point missing")))
        return 1;
    live->setPlayhead(0);
    QApplication::processEvents();

    // Orbit / spin segments have no velocity handle.
    pvf::model::SolverSegment spinSolver;
    spinSolver.id = pvf::model::newUuid();
    spinSolver.effect = pvf::SolverEffect::Spin;
    spinSolver.startFrame = 0;
    spinSolver.endFrame = -1;
    spinSolver.params.insert(QStringLiteral("angularVelocity"), 6.0);
    spinSolver.params.insert(QStringLiteral("direction"), 1);
    live->mutableProject().entities[spinner].solvers.append(spinSolver);
    live->mutableProject().entities[spinner].tracks[
        QStringLiteral("rotation")].keys = {{0, 0.0, {}, {}}};
    if (!document->commitEntity(spinner, QStringLiteral("solver-ui spin"),
                                &error))
    {
        qCritical() << "solver-ui-test: spin commit failed" << error;
        return 1;
    }

    pvf::model::SolverSegment orbitSolver;
    orbitSolver.id = pvf::model::newUuid();
    orbitSolver.effect = pvf::SolverEffect::Orbit;
    orbitSolver.startFrame = 0;
    orbitSolver.endFrame = -1;
    orbitSolver.params.insert(QStringLiteral("targetUuid"), ground);
    orbitSolver.params.insert(QStringLiteral("aspect"), 1.0);
    orbitSolver.params.insert(QStringLiteral("angle"), 0.0);
    orbitSolver.params.insert(QStringLiteral("direction"), 1);
    orbitSolver.params.insert(QStringLiteral("periodFrames"), 60.0);
    live->mutableProject().entities[orbiter].solvers.append(orbitSolver);
    if (!document->commitEntity(orbiter, QStringLiteral("solver-ui orbit"),
                                &error))
    {
        qCritical() << "solver-ui-test: orbit commit failed" << error;
        return 1;
    }
    flush();
    for (const pvf::Uuid& uuid : {spinner, orbiter})
    {
        document->setSelection(uuid);
        live->setPlayhead(0);
        QApplication::processEvents();
        if (!check(!viewport->sessionSolverPreviewVisibleForTest(),
                   QStringLiteral("orbit/spin still draws a viewport overlay"))
            || !check(mini->entityUuid() == uuid && mini->hasPath(),
                      QStringLiteral("orbit/spin mini preview is not visible"))
            || !check(!mini->hasArrow(),
                      QStringLiteral("orbit/spin shows a velocity arrow")))
            return 1;
    }

    // Timeline band ends at Result::stopFrame, not the clip end.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    const QRect band =
        timeline->solverBandRectForTest(ball, gravityId);
    const int bandStop =
        timeline->solverBandStopFrameForTest(ball, gravityId);
    if (!check(!band.isEmpty(), QStringLiteral("timeline solver band missing"))
        || !check(bandStop == stopFrame,
                  QStringLiteral("timeline band stop frame wrong"))
        || !check(std::abs(band.right()
                           - (timeline->frameXForTest(stopFrame) - 1))
                  <= 2,
                  QStringLiteral("timeline band does not end at stopFrame")))
        return 1;

    // Screenshot with the solver card and the mini trajectory window visible.
    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
        objectTab->verticalScrollBar()->setValue(
            objectTab->verticalScrollBar()->maximum());
    live->setPlayhead(5);
    QApplication::processEvents();
    viewport->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-solver-preview.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "solver-ui-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "solver-ui-test OK: automatic end, stop reason/frame, explicit"
               " override, mini trajectory window, orbit/spin, timeline band,"
               " screenshot"
            << screenshot;
    return 0;
}

int solverMiniPreviewTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "solver-mini-preview-test: no temp dir";
        return 1;
    }

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver Mini");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-mini.pvproj"));
    auto session = pvf::EditorSession::createProject(projectDir, settings,
                                                     &error);
    if (!session)
    {
        qCritical() << "solver-mini-preview-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("黑洞"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 80.0;
    project.entities[ground].content.vector.height = 80.0;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("落球"), 1, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    const pvf::Uuid moon =
        makeEntity(QStringLiteral("卫星"), 2, pvf::ContentKind::Vector);
    project.entities[moon].transform.x = -150.0;
    project.entities[moon].transform.y = 60.0;
    project.entities[moon].content.vector.width = 40.0;
    project.entities[moon].content.vector.height = 40.0;

    // Long clip whose black hole target sits exactly on the launch point.
    // The capture happens long before the 2000-frame preview cap, so the
    // preview must not claim truncation and must still name the hole.
    const pvf::Uuid holeBall =
        makeEntity(QStringLiteral("黑洞球"), 3, pvf::ContentKind::Vector);
    project.entities[holeBall].end = 3000;
    project.entities[holeBall].transform.x = 0.0;
    project.entities[holeBall].transform.y = -100.0;
    project.entities[holeBall].content.vector.width = 20.0;
    project.entities[holeBall].content.vector.height = 20.0;

    pvf::model::SolverSegment gravity;
    gravity.id = pvf::model::newUuid();
    gravity.effect = pvf::SolverEffect::Gravity;
    gravity.startFrame = 0;
    gravity.endFrame = -1;
    gravity.initialVelocity = QPointF(60.0, 120.0);
    gravity.params.insert(QStringLiteral("targetUuid"), ground);
    gravity.params.insert(QStringLiteral("strength"), 500.0);
    gravity.params.insert(QStringLiteral("elasticity"), 0.0);
    gravity.params.insert(QStringLiteral("friction"), 0.5);
    gravity.params.insert(QStringLiteral("stopThreshold"), 10.0);
    gravity.params.insert(QStringLiteral("softening"), 60.0);
    gravity.params.insert(QStringLiteral("maxSpeed"), 4000.0);
    project.entities[ball].solvers.append(gravity);

    pvf::model::SolverSegment orbit;
    orbit.id = pvf::model::newUuid();
    orbit.effect = pvf::SolverEffect::Orbit;
    orbit.startFrame = 0;
    orbit.endFrame = -1;
    orbit.params.insert(QStringLiteral("targetUuid"), ground);
    orbit.params.insert(QStringLiteral("aspect"), 1.0);
    orbit.params.insert(QStringLiteral("angle"), 0.0);
    orbit.params.insert(QStringLiteral("direction"), 1);
    orbit.params.insert(QStringLiteral("periodFrames"), 60.0);
    project.entities[moon].solvers.append(orbit);

    pvf::model::SolverSegment hole;
    hole.id = pvf::model::newUuid();
    hole.effect = pvf::SolverEffect::BlackHole;
    hole.startFrame = 0;
    hole.endFrame = -1;
    hole.initialVelocity = QPointF(80.0, 220.0);
    hole.params.insert(QStringLiteral("targetUuid"), ground);
    hole.params.insert(QStringLiteral("mass"), 5000.0);
    hole.params.insert(QStringLiteral("eventHorizonRadius"), 30.0);
    hole.params.insert(QStringLiteral("objectRadius"), 5.0);
    hole.params.insert(QStringLiteral("softening"), 30.0);
    hole.params.insert(QStringLiteral("maxSpeed"), 4000.0);
    project.entities[holeBall].solvers.append(hole);

    if (!session->document()->commitEntities({ground, ball, moon, holeBall},
                                             QStringLiteral("solver-mini setup"),
                                             &error))
    {
        qCritical() << "solver-mini-preview-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* viewport = window->viewportForTest();
    auto* solverSection = panel
        ? panel->findChild<pvf::CollapsibleSection*>(
              QStringLiteral("solverSection"))
        : nullptr;
    if (!live || !document || !panel || !viewport || !solverSection)
    {
        qCritical() << "solver-mini-preview-test: window parts missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "solver-mini-preview-test: panel edit"
                                        " failed" << message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-mini-preview-test:" << message;
        return condition;
    };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "solver-mini-preview-test: flush failed" << flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };

    solverSection->setExpanded(true);
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();

    auto* mini = panel->findChild<pvf::next::SolverPreviewWidget*>(
        QStringLiteral("solverPreviewMini"));
    if (!check(mini != nullptr, QStringLiteral("mini widget missing"))
        || !check(solverSection->isAncestorOf(mini),
                  QStringLiteral("mini widget is not inside the solver section"))
        || !check(mini->isVisible(), QStringLiteral("mini widget hidden")))
        return 1;

    // Size window: about 260x160 .. 300x180 (width follows the panel).
    if (!check(mini->width() >= 260 && mini->width() <= 440,
               QStringLiteral("mini width out of range: %1").arg(mini->width()))
        || !check(mini->height() >= 160 && mini->height() <= 180,
                  QStringLiteral("mini height out of range: %1")
                      .arg(mini->height())))
        return 1;

    if (!check(mini->entityUuid() == ball && mini->solverId() == gravity.id,
               QStringLiteral("mini widget is not bound to the gravity solver"))
        || !check(mini->errorText().isEmpty(),
                  QStringLiteral("mini widget error: %1").arg(mini->errorText()))
        || !check(mini->hasTarget() && mini->hasPath() && mini->hasArrow(),
                  QStringLiteral("mini widget misses target/path/arrow"))
        || !check(mini->sampleCount() >= 2,
                  QStringLiteral("mini widget has no samples"))
        || !check(mini->targetName() == QStringLiteral("黑洞"),
                  QStringLiteral("target is not labelled with its entity name: %1")
                      .arg(mini->targetName()))
        || !check(mini->entityName() == QStringLiteral("落球"),
                  QStringLiteral("start is not labelled with its entity name: %1")
                      .arg(mini->entityName()))
        || !check(!mini->isTruncated(),
                  QStringLiteral("short segment reported as truncated")))
        return 1;

    // Only points/lines: the four public anchors sit inside the window.
    const QRect windowRect = mini->rect();
    if (!check(windowRect.contains(mini->startPoint().toPoint())
                   && windowRect.contains(mini->targetPoint().toPoint())
                   && windowRect.contains(mini->arrowTip().toPoint()),
               QStringLiteral("mini anchors fall outside the window: rect=%1,%2 "
                              "%3x%4 start=%5,%6 target=%7,%8 arrow=%9,%10")
                   .arg(windowRect.x())
                   .arg(windowRect.y())
                   .arg(windowRect.width())
                   .arg(windowRect.height())
                   .arg(mini->startPoint().x())
                   .arg(mini->startPoint().y())
                   .arg(mini->targetPoint().x())
                   .arg(mini->targetPoint().y())
                   .arg(mini->arrowTip().x())
                   .arg(mini->arrowTip().y()))
        || !check(mini->startPoint() != mini->targetPoint(),
                  QStringLiteral("start and target collapse onto one point")))
        return 1;

    // Solver space is Y-up, the widget is Y-down: the start (solver y=200)
    // must be above the target (solver y=-100) on screen, exactly like the
    // canvas.  A missing flip mirrored the preview vertically.
    if (!check(mini->startPoint().y() < mini->targetPoint().y(),
               QStringLiteral("preview is vertically mirrored: start y %1, "
                              "target y %2")
                   .arg(mini->startPoint().y())
                   .arg(mini->targetPoint().y())))
        return 1;

    // Background drag pans the view and wheel zooms around the cursor;
    // double-click returns to the automatic fit.
    const QPointF fittedStart = mini->startPoint();
    const QPointF fittedTarget = mini->targetPoint();
    const auto arrowPixels = [mini] {
        const QPointF delta = mini->arrowTip() - mini->startPoint();
        return std::hypot(delta.x(), delta.y());
    };
    if (!check(std::abs(arrowPixels() - 48.0) < 1.5,
               QStringLiteral("velocity arrow is not a fixed 48 px: %1")
                   .arg(arrowPixels())))
        return 1;
    const QPointF panFrom(10.0, mini->height() - 10.0);
    const QPointF panTo = panFrom + QPointF(24.0, 12.0);
    QMouseEvent panPress(QEvent::MouseButtonPress, panFrom,
                         mini->mapToGlobal(panFrom.toPoint()), Qt::LeftButton,
                         Qt::LeftButton, Qt::NoModifier);
    QMouseEvent panMove(QEvent::MouseMove, panTo,
                        mini->mapToGlobal(panTo.toPoint()), Qt::NoButton,
                        Qt::LeftButton, Qt::NoModifier);
    QMouseEvent panRelease(QEvent::MouseButtonRelease, panTo,
                           mini->mapToGlobal(panTo.toPoint()), Qt::LeftButton,
                           Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(mini, &panPress);
    QApplication::sendEvent(mini, &panMove);
    QApplication::sendEvent(mini, &panRelease);
    QApplication::processEvents();
    const QPointF pannedDelta = mini->startPoint() - fittedStart;
    if (!check(std::hypot(pannedDelta.x() - 24.0, pannedDelta.y() - 12.0) < 1.0,
               QStringLiteral("pan did not follow the pointer: %1,%2")
                   .arg(pannedDelta.x())
                   .arg(pannedDelta.y())))
        return 1;

    const double fittedSpan =
        std::hypot(fittedTarget.x() - fittedStart.x(),
                   fittedTarget.y() - fittedStart.y());
    const QPointF zoomAt = mini->rect().center();
    QWheelEvent zoomEvent(zoomAt, mini->mapToGlobal(zoomAt.toPoint()),
                          QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(mini, &zoomEvent);
    QApplication::processEvents();
    const QPointF zoomedStart = mini->startPoint();
    const QPointF zoomedTarget = mini->targetPoint();
    const double zoomedSpan =
        std::hypot(zoomedTarget.x() - zoomedStart.x(),
                   zoomedTarget.y() - zoomedStart.y());
    if (!check(fittedSpan > 1.0
                   && std::abs(zoomedSpan / fittedSpan - 1.15) < 0.02,
               QStringLiteral("wheel zoom factor: %1 -> %2")
                   .arg(fittedSpan)
                   .arg(zoomedSpan)))
        return 1;
    if (!check(std::abs(arrowPixels() - 48.0) < 1.5,
               QStringLiteral("zooming changed the velocity arrow length: %1")
                   .arg(arrowPixels())))
        return 1;

    QMouseEvent resetEvent(QEvent::MouseButtonDblClick, panFrom,
                           mini->mapToGlobal(panFrom.toPoint()), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(mini, &resetEvent);
    QApplication::processEvents();
    if (!check(std::hypot(mini->startPoint().x() - fittedStart.x(),
                          mini->startPoint().y() - fittedStart.y()) < 1.0,
               QStringLiteral("double-click did not restore the fit")))
        return 1;

    // Black hole target sitting exactly on the launch point, on a clip much
    // longer than the preview cap.  The physical capture happens early, so the
    // preview must still name both dots and must not report truncation.
    mini->setSolver(holeBall, hole.id);
    document->setSelection(holeBall);
    live->setPlayhead(0);
    QApplication::processEvents();
    mini->refreshPreview();
    QApplication::processEvents();
    if (!check(mini->errorText().isEmpty(),
               QStringLiteral("black hole preview error: %1")
                   .arg(mini->errorText()))
        || !check(mini->hasTarget() && mini->hasPath(),
                  QStringLiteral("black hole preview misses target/path"))
        || !check(mini->targetName() == QStringLiteral("黑洞"),
                  QStringLiteral("black hole target label: %1")
                      .arg(mini->targetName()))
        || !check(mini->entityName() == QStringLiteral("黑洞球"),
                  QStringLiteral("black hole entity label: %1")
                      .arg(mini->entityName()))
        || !check(mini->startPoint() == mini->targetPoint(),
                  QStringLiteral("black hole target should overlap the start"))
        || !check(!mini->isTruncated(),
                  QStringLiteral("early capture reported as truncated")))
        return 1;
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    mini->repaint();
    QApplication::processEvents();
    if (!mini->grab().save(
            QStringLiteral("/tmp/opencode/pvforge-solver-mini-overlap.png")))
    {
        qCritical() << "solver-mini-preview-test: overlap screenshot failed";
        return 1;
    }

    // Back to the gravity segment for the drag/undo assertions below.
    document->setSelection(ball);
    mini->setSolver(ball, gravity.id);
    live->setPlayhead(0);
    mini->refreshPreview();
    QApplication::processEvents();

    // Drag the arrow handle: live model write, exactly one commit.
    const auto sendMouseTo = [](QWidget* target, QEvent::Type type,
                                const QPointF& pos, Qt::MouseButton button,
                                Qt::MouseButtons buttons) {
        QMouseEvent event(type, pos, target->mapToGlobal(pos.toPoint()),
                          button, buttons, Qt::NoModifier);
        QApplication::sendEvent(target, &event);
    };
    const int beforeDrag = undoDepth();
    const QPointF tip = mini->arrowTip();
    const QPointF pathBeforeDrag = mini->pathPoint(10);
    const QPoint dragTo = (tip + QPointF(55.0, -35.0)).toPoint();
    sendMouseTo(mini, QEvent::MouseButtonPress, tip, Qt::LeftButton,
                Qt::LeftButton);
    sendMouseTo(mini, QEvent::MouseMove, dragTo, Qt::NoButton,
                Qt::LeftButton);
    // While the handle is held the tip tracks the pointer exactly (the fit is
    // frozen during the drag).
    const QPointF duringDragTip = mini->arrowTip();
    const QPointF pathDuringDrag = mini->pathPoint(10);
    sendMouseTo(mini, QEvent::MouseButtonRelease, dragTo, Qt::LeftButton,
                Qt::NoButton);
    QApplication::processEvents();
    flush();

    const QPointF dragged =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    if (!check(std::abs(dragged.x()) > 1.0 && std::abs(dragged.y()) > 1.0,
               QStringLiteral("arrow drag did not write initialVelocity"))
        || !check(undoDepth() == beforeDrag + 1,
                  QStringLiteral("arrow drag was not exactly one commit"))
        || !check(!mini->isDragging(),
                  QStringLiteral("arrow drag did not finish"))
        || !check(QLineF(duringDragTip, QPointF(dragTo)).length() < 1.5,
                  QStringLiteral("arrow tip did not follow the drag: %1,%2")
                      .arg(duringDragTip.x())
                      .arg(duringDragTip.y()))
        || !check(QLineF(mini->arrowTip(), mini->startPoint()).length() > 1.0,
                  QStringLiteral("arrow tip stayed on the start point"))
        || !check(QLineF(pathBeforeDrag, pathDuringDrag).length() > 1.0,
                  QStringLiteral("trajectory did not update during the "
                                 "velocity drag: %1,%2 -> %3,%4")
                      .arg(pathBeforeDrag.x())
                      .arg(pathBeforeDrag.y())
                      .arg(pathDuringDrag.x())
                      .arg(pathDuringDrag.y())))
        return 1;

    // Switching the selection rebinds the window; entities without solvers
    // clear it.
    document->setSelection(moon);
    live->setPlayhead(0);
    QApplication::processEvents();
    if (!check(mini->entityUuid() == moon && mini->solverId() == orbit.id,
               QStringLiteral("mini widget did not follow the selection"))
        || !check(mini->hasTarget() && !mini->hasArrow(),
                  QStringLiteral("orbit segment shows wrong mini content")))
        return 1;

    document->setSelection(ground);
    QApplication::processEvents();
    if (!check(!mini->isVisible() && mini->entityUuid().isEmpty(),
               QStringLiteral("mini widget did not clear on solverless entity")))
        return 1;

    // Esc cancels the drag: velocity and undo stack roll back.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    const QPointF beforeEscVelocity =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    const int beforeEsc = undoDepth();
    const QPointF escTip = mini->arrowTip();
    sendMouseTo(mini, QEvent::MouseButtonPress, escTip, Qt::LeftButton,
                Qt::LeftButton);
    sendMouseTo(mini, QEvent::MouseMove,
                (escTip + QPointF(50.0, 40.0)).toPoint(), Qt::NoButton,
                Qt::LeftButton);
    if (!check(mini->isDragging(), QStringLiteral("drag did not start")))
        return 1;
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(mini, &escape);
    QApplication::processEvents();
    const QPointF afterEscVelocity =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    if (!check(!mini->isDragging(), QStringLiteral("Esc did not end the drag"))
        || !check(QLineF(beforeEscVelocity, afterEscVelocity).length() < 1e-6,
                  QStringLiteral("Esc did not restore initialVelocity"))
        || !check(undoDepth() == beforeEsc,
                  QStringLiteral("Esc created an undo entry")))
        return 1;

    // The viewport no longer draws an overlay or grabs the velocity handle:
    // dragging the solver-driven ball writes nothing, and its velocity is
    // untouched.
    const QPointF pivot = viewport->projectToWidgetForTest(QPointF(0.0, 200.0));
    const auto sendMouse = [viewport](QEvent::Type type, const QPointF& pos,
                                      Qt::MouseButton button,
                                      Qt::MouseButtons buttons) {
        QMouseEvent event(type, pos, viewport->mapToGlobal(pos.toPoint()),
                          button, buttons, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    };
    const int beforeViewportDrag = undoDepth();
    sendMouse(QEvent::MouseButtonPress, pivot, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseMove, pivot + QPointF(60.0, -40.0), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, pivot + QPointF(60.0, -40.0),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    flush();
    const QPointF afterViewportDrag =
        live->project().entities.value(ball).solvers.first().initialVelocity;
    if (!check(!viewport->sessionSolverPreviewVisibleForTest(),
               QStringLiteral("viewport still exposes a solver overlay"))
        || !check(QLineF(afterEscVelocity, afterViewportDrag).length() < 1e-6,
                  QStringLiteral("viewport drag wrote the solver velocity"))
        || !check(undoDepth() == beforeViewportDrag,
                  QStringLiteral("viewport wrote a solver-driven transform")))
        return 1;

    // The entity gizmo still works on an entity the solver does not drive.
    document->setSelection(ground);
    live->setPlayhead(0);
    QApplication::processEvents();
    const QPointF groundCenter =
        viewport->projectToWidgetForTest(QPointF(0.0, -100.0));
    sendMouse(QEvent::MouseButtonPress, groundCenter, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseMove, groundCenter + QPointF(40.0, 25.0),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, groundCenter + QPointF(40.0, 25.0),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    flush();
    if (!check(undoDepth() == beforeViewportDrag + 1,
               QStringLiteral("viewport gizmo no longer commits moves")))
        return 1;

    // Screenshot: mini window inside the expanded solver section.
    // Screenshot state: a readable arc so the target, the solver point, the
    // path and the velocity arrow are all visible at once.
    {
        pvf::model::Project& shotProject = live->mutableProject();
        auto shotEntity = shotProject.entities.find(ball);
        if (shotEntity != shotProject.entities.end()
            && !shotEntity->solvers.isEmpty())
        {
            shotEntity->solvers.first().initialVelocity = QPointF(150.0, -200.0);
            shotEntity->solvers.first().params.insert(
                QStringLiteral("strength"), 160.0);
        }
        document->commitEntity(ball, QStringLiteral("solver-mini screenshot"),
                               &error);
    }
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
    {
        // Find a scroll offset that shows the whole mini window.
        if (auto* scroll = objectTab->verticalScrollBar())
        {
            const QWidget* view = objectTab->viewport();
            for (int value = 0; value <= scroll->maximum(); value += 8)
            {
                scroll->setValue(value);
                QApplication::processEvents();
                const QRect box(mini->mapTo(view, QPoint(0, 0)), mini->size());
                if (view->rect().contains(box))
                    break;
            }
        }
    }
    QApplication::processEvents();
    mini->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-solver-mini.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "solver-mini-preview-test: screenshot failed" << screenshot;
        return 1;
    }

    // WA_DeleteOnClose deletes the window and every child; keep the numbers
    // the success line reports before closing so nothing dereferences `mini`
    // after it is gone.
    const int miniWidth = mini->width();
    const int miniHeight = mini->height();

    window->close();
    QApplication::processEvents();
    qInfo() << "solver-mini-preview-test OK: solver-section widget"
            << miniWidth << "x" << miniHeight
            << "target/path/arrow only, arrow drag one commit, Esc cancel,"
               " selection rebind, no viewport overlay, screenshot"
            << screenshot;
    return 0;
}

int solverPreviewFreezeTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "solver-preview-freeze-test: no temp dir";
        return 1;
    }

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver Freeze");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-freeze.pvproj"));
    auto session = pvf::EditorSession::createProject(projectDir, settings,
                                                     &error);
    if (!session)
    {
        qCritical() << "solver-preview-freeze-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("地面"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 600.0;
    project.entities[ground].content.vector.height = 60.0;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("落球"), 1, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    pvf::model::SolverSegment gravity;
    gravity.id = pvf::model::newUuid();
    gravity.effect = pvf::SolverEffect::Gravity;
    gravity.startFrame = 0;
    gravity.endFrame = -1;
    gravity.params.insert(QStringLiteral("targetUuid"), ground);
    gravity.params.insert(QStringLiteral("strength"), 500.0);
    gravity.params.insert(QStringLiteral("elasticity"), 0.0);
    gravity.params.insert(QStringLiteral("friction"), 0.5);
    gravity.params.insert(QStringLiteral("stopThreshold"), 10.0);
    gravity.params.insert(QStringLiteral("softening"), 60.0);
    gravity.params.insert(QStringLiteral("maxSpeed"), 4000.0);
    project.entities[ball].solvers.append(gravity);

    if (!session->document()->commitEntities({ground, ball},
                                             QStringLiteral("solver-freeze setup"),
                                             &error))
    {
        qCritical() << "solver-preview-freeze-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* timeline = window->timelineForTest();
    auto* solverSection = panel
        ? panel->findChild<pvf::CollapsibleSection*>(
              QStringLiteral("solverSection"))
        : nullptr;
    if (!live || !document || !panel || !timeline || !solverSection)
    {
        qCritical() << "solver-preview-freeze-test: window parts missing";
        return 1;
    }

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-preview-freeze-test:" << message;
        return condition;
    };

    solverSection->setExpanded(true);
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    QApplication::processEvents();

    auto* mini = panel->findChild<pvf::next::SolverPreviewWidget*>(
        QStringLiteral("solverPreviewMini"));
    if (!check(mini != nullptr, QStringLiteral("mini widget missing"))
        || !check(mini->isVisible(), QStringLiteral("mini widget hidden"))
        || !check(mini->entityUuid() == ball && mini->solverId() == gravity.id,
                  QStringLiteral("mini widget is not bound to the solver"))
        || !check(mini->sampleCount() >= 2 && mini->errorText().isEmpty(),
                  QStringLiteral("mini preview did not compute: %1")
                      .arg(mini->errorText())))
        return 1;

    const QRect band = timeline->solverBandRectForTest(ball, gravity.id);
    if (!check(!band.isEmpty(),
               QStringLiteral("timeline solver band is missing from the cache")))
        return 1;

    // Painting the timeline reads the cached band; it must never re-solve.
    const int bandSolves = timeline->solverBandSolveCountForTest();
    if (!check(timeline->solverBandStopFrameForTest(ball, gravity.id) >= 0,
               QStringLiteral("solver band stop frame is missing")))
        return 1;
    timeline->grab();
    mini->grab();
    QApplication::processEvents();
    if (!check(timeline->solverBandSolveCountForTest() == bandSolves,
               QStringLiteral("timeline paint re-solved the band: %1 -> %2")
                   .arg(bandSolves)
                   .arg(timeline->solverBandSolveCountForTest())))
        return 1;

    // The real playback signal must freeze the panel and the mini widget.
    if (auto* playback = live->playback())
    {
        playback->play();
        if (!check(panel->playbackActiveForTest(),
                   QStringLiteral("playbackStateChanged(true) did not reach "
                                  "the properties panel"))
            || !check(mini->isPlaybackActive(),
                      QStringLiteral("playbackStateChanged(true) did not "
                                     "freeze the mini preview")))
            return 1;
        playback->pause();
        QApplication::processEvents();
        if (!check(!panel->playbackActiveForTest()
                       && !mini->isPlaybackActive(),
                   QStringLiteral("playbackStateChanged(false) did not "
                                  "unfreeze the previews")))
            return 1;
    }

    // Baseline after the setup and the play/pause round trip.
    const int miniBaseline = mini->computeCount();
    const int bandBaseline = timeline->solverBandSolveCountForTest();
    const QPointF frozenDot = mini->currentPoint();
    if (!check(miniBaseline > 0, QStringLiteral("mini compute hook never ran")))
        return 1;

    // Simulated playback: 30 playhead ticks, full repaints of both panels.
    panel->setPlaybackActive(true);
    if (!check(mini->isPlaybackActive(),
               QStringLiteral("setPlaybackActive(true) did not freeze the mini")))
        return 1;
    for (int frame = 0; frame < 30; ++frame)
    {
        live->setPlayhead(frame);
        QApplication::processEvents();
        timeline->grab();
        mini->grab();
        QApplication::processEvents();
    }
    if (!check(mini->computeCount() == miniBaseline,
               QStringLiteral("mini preview re-solved during playback: %1 "
                              "-> %2")
                   .arg(miniBaseline)
                   .arg(mini->computeCount())))
        return 1;
    if (!check(timeline->solverBandSolveCountForTest() == bandBaseline,
               QStringLiteral("timeline re-solved during playback: %1 -> %2")
                   .arg(bandBaseline)
                   .arg(timeline->solverBandSolveCountForTest())))
        return 1;
    if (!check(mini->currentPoint() == frozenDot,
               QStringLiteral("mini current dot moved while frozen")))
        return 1;

    // Pause refreshes at most once, and later seeks do not re-solve.
    panel->setPlaybackActive(false);
    QApplication::processEvents();
    const int afterPause = mini->computeCount();
    if (!check(afterPause - miniBaseline <= 1,
               QStringLiteral("pause refreshed more than once: +%1")
                   .arg(afterPause - miniBaseline)))
        return 1;
    live->setPlayhead(30);
    QApplication::processEvents();
    timeline->grab();
    mini->grab();
    QApplication::processEvents();
    if (!check(mini->computeCount() == afterPause,
               QStringLiteral("seek after pause re-solved the mini: %1 -> %2")
                   .arg(afterPause)
                   .arg(mini->computeCount())))
        return 1;
    if (!check(timeline->solverBandSolveCountForTest() == bandBaseline,
               QStringLiteral("seek after pause re-solved the band: %1 -> %2")
                   .arg(bandBaseline)
                   .arg(timeline->solverBandSolveCountForTest())))
        return 1;

    window->close();
    QApplication::processEvents();
    qInfo() << "solver-preview-freeze-test OK: mini frozen during playback,"
               " no re-solve on playhead/paint, timeline band cached,"
               " one refresh on pause";
    return 0;
}

// End-to-end playback acceptance for the shared solver cache.  Uses
// setPlayhead instead of the real 30 fps clock: the timer adds scheduling
// noise and cannot tell a cached frame from a re-solve.  The project is a
// 640x360@30 black-hole capture whose clip is longer than the measured range,
// so every frame in 0..119 is a real workbench cache fill on the first pass.
int solverPlaybackPerfTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "solver-playback-perf-test: no temp dir";
        return 1;
    }

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver Playback Perf");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-playback-perf.pvproj"));
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "solver-playback-perf-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 3000;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("地面"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 600.0;
    project.entities[ground].content.vector.height = 60.0;

    // A second, static target gives the render/meta path a normal entity that
    // must never be solved.
    const pvf::Uuid staticTarget =
        makeEntity(QStringLiteral("静态目标"), 1, pvf::ContentKind::Vector);
    project.entities[staticTarget].transform.x = 200.0;
    project.entities[staticTarget].transform.y = 60.0;
    project.entities[staticTarget].content.vector.width = 60.0;
    project.entities[staticTarget].content.vector.height = 60.0;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("黑洞球"), 2, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    pvf::model::SolverSegment hole;
    hole.id = pvf::model::newUuid();
    hole.effect = pvf::SolverEffect::BlackHole;
    hole.startFrame = 0;
    hole.endFrame = -1;
    // Moving toward the ground target; the 35 px capture radius is reached in
    // roughly 20-30 frames at 30 fps, well inside the measured 120 frames.
    hole.initialVelocity = QPointF(24.0, -320.0);
    hole.params.insert(QStringLiteral("targetUuid"), ground);
    hole.params.insert(QStringLiteral("mass"), 5000.0);
    hole.params.insert(QStringLiteral("eventHorizonRadius"), 30.0);
    hole.params.insert(QStringLiteral("objectRadius"), 5.0);
    hole.params.insert(QStringLiteral("softening"), 30.0);
    hole.params.insert(QStringLiteral("maxSpeed"), 4000.0);
    project.entities[ball].solvers.append(hole);

    if (!session->document()->commitEntities(
            {ground, staticTarget, ball},
            QStringLiteral("solver playback perf setup"), &error))
    {
        qCritical() << "solver-playback-perf-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* timeline = window->timelineForTest();
    auto* viewport = window->viewportForTest();
    auto* solverSection =
        panel ? panel->findChild<pvf::CollapsibleSection*>(
                    QStringLiteral("solverSection"))
              : nullptr;
    // The user's screenshot keeps the floating monitor open; leave it visible
    // so its render path participates in every measured frame.
    auto* monitor = window->findChild<pvf::CameraMonitor*>();
    if (!live || !document || !panel || !timeline || !viewport
        || !solverSection)
    {
        qCritical() << "solver-playback-perf-test: window parts missing";
        return 1;
    }

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-playback-perf-test:" << message;
        return condition;
    };

    solverSection->setExpanded(true);
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    QApplication::processEvents();

    auto* mini = panel->findChild<pvf::next::SolverPreviewWidget*>(
        QStringLiteral("solverPreviewMini"));
    if (!check(mini != nullptr, QStringLiteral("mini widget missing"))
        || !check(mini->isVisible(), QStringLiteral("mini widget hidden"))
        || !check(mini->entityUuid() == ball && mini->solverId() == hole.id,
                  QStringLiteral("mini widget is not bound to the solver"))
        || !check(mini->sampleCount() >= 2 && mini->errorText().isEmpty(),
                  QStringLiteral("mini preview did not compute: %1")
                      .arg(mini->errorText())))
        return 1;
    if (monitor && !monitor->isVisible())
        monitor->setVisible(true);

    // Let the deferred EditorSession precompute run before measuring.  The
    // session queues it with a 0 ms timer after construction/setup.
    {
        QElapsedTimer warmWait;
        warmWait.start();
        while (!live->solverResultFor(ball, hole.id)
               && warmWait.elapsed() < 3000)
        {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
    }
    if (!check(live->solverResultFor(ball, hole.id) != nullptr,
               QStringLiteral("queued solver precompute did not warm the "
                              "black-hole segment")))
        return 1;

    // Full per-frame paint path: viewport (workbench lookup/store), timeline
    // (band cache), mini preview, floating camera monitor and the shell.
    auto renderFrame = [&](int frame) {
        live->setPlayhead(frame);
        viewport->repaint();
        timeline->repaint();
        mini->repaint();
        if (monitor && monitor->isVisible())
            monitor->repaint();
        window->repaint();
        QApplication::processEvents();
    };

    // Warm frame 0 and everything the deferred precompute touched.
    renderFrame(0);
    window->grab();
    QApplication::processEvents();
    const int warmCount = live->solverEvaluationCount();

    auto runPass = [&](int frames, qint64* elapsedNs, int* growthFrames,
                       int* maxFrameDelta) {
        QElapsedTimer timer;
        timer.start();
        int previous = live->solverEvaluationCount();
        int grown = 0;
        int biggest = 0;
        for (int frame = 0; frame < frames; ++frame)
        {
            renderFrame(frame);
            const int now = live->solverEvaluationCount();
            if (now != previous)
            {
                ++grown;
                biggest = std::max(biggest, now - previous);
                previous = now;
            }
        }
        if (elapsedNs)
            *elapsedNs = timer.nsecsElapsed();
        if (growthFrames)
            *growthFrames = grown;
        if (maxFrameDelta)
            *maxFrameDelta = biggest;
    };

    // First pass fills the workbench image cache for 0..119; its duration is
    // reported but not asserted (cache misses encode WebP).
    qint64 firstPassNs = 0;
    runPass(120, &firstPassNs, nullptr, nullptr);

    // Baseline strictly after the cache is full.  Returning to frame 0 must
    // not solve anything either.
    live->setPlayhead(0);
    QApplication::processEvents();
    const int countBaseline = live->solverEvaluationCount();
    const int miniBaseline = mini->computeCount();
    const int bandBaseline = timeline->solverBandSolveCountForTest();

    qint64 secondPassNs = 0;
    int growthFrames = 0;
    int maxFrameDelta = 0;
    runPass(120, &secondPassNs, &growthFrames, &maxFrameDelta);

    const int countDelta = live->solverEvaluationCount() - countBaseline;
    const int miniDelta = mini->computeCount() - miniBaseline;
    const int bandDelta = timeline->solverBandSolveCountForTest() - bandBaseline;
    const double firstMsPerFrame =
        static_cast<double>(firstPassNs) / 1.0e6 / 120.0;
    const double secondMsPerFrame =
        static_cast<double>(secondPassNs) / 1.0e6 / 120.0;
    const double secondFps =
        secondMsPerFrame > 0.0 ? 1000.0 / secondMsPerFrame : 0.0;

    qInfo().nospace()
        << "solver-playback-perf-test: frames=120 countDelta=" << countDelta
        << " ms/frame=" << secondMsPerFrame << " fps=" << secondFps;
    qInfo() << "solver-playback-perf-test: first pass"
            << firstMsPerFrame << "ms/frame, warm count" << warmCount
            << ", mini delta" << miniDelta << ", band delta" << bandDelta;

    if (countDelta == 1)
        qInfo() << "solver-playback-perf-test: countDelta=1 accepted as the "
                   "single deferred precompute queued by document setup";
    if (countDelta > 1)
        qCritical() << "solver-playback-perf-test: solver re-ran during cached "
                       "playback:"
                    << countDelta << "solves over 120 frames across"
                    << growthFrames << " frames, worst frame gained"
                    << maxFrameDelta;
    if (!check(countDelta <= 1,
               QStringLiteral("solverEvaluationCount grew by %1 during the "
                              "second pass (%2 frames grew, worst +%3); the "
                              "solver is still on the paint path")
                   .arg(countDelta)
                   .arg(growthFrames)
                   .arg(maxFrameDelta)))
        return 1;

    if (!check(miniDelta == 0,
               QStringLiteral("mini preview recomputed during playback: %1")
                   .arg(miniDelta)))
        return 1;
    if (!check(bandDelta == 0,
               QStringLiteral("timeline band re-solved during playback: %1")
                   .arg(bandDelta)))
        return 1;
    if (!check(secondMsPerFrame < 33.0,
               QStringLiteral("second pass is %1 ms/frame (%2 fps), over the "
                              "33 ms 60 FPS budget")
                   .arg(secondMsPerFrame)
                   .arg(secondFps)))
        return 1;

    // Workbench hit test: 50 consecutive cached frames must build the meta
    // through the shared results and never call evaluateResult().
    auto* workbench = live->workbench();
    if (!check(workbench != nullptr, QStringLiteral("workbench missing")))
        return 1;
    const int workbenchBaseline = live->solverEvaluationCount();
    int workbenchHits = 0;
    for (int frame = 0; frame < 50; ++frame)
    {
        bool hit = false;
        const QImage cached =
            workbench->lookup(frame, workbench->cameraUuid(), &hit);
        if (hit && !cached.isNull())
            ++workbenchHits;
    }
    const int workbenchDelta =
        live->solverEvaluationCount() - workbenchBaseline;
    qInfo() << "solver-playback-perf-test: workbench lookup frames=50 hits="
            << workbenchHits << "countDelta=" << workbenchDelta;
    if (!check(workbenchHits == 50,
               QStringLiteral("workbench lookup hit %1/50 frames after the "
                              "first pass")
                   .arg(workbenchHits)))
        return 1;
    if (!check(workbenchDelta == 0,
               QStringLiteral("workbench meta lookup re-solved: +%1")
                   .arg(workbenchDelta)))
        return 1;

    // Edit invalidation: changing the segment must invalidate exactly once.
    const int beforeEdit = live->solverEvaluationCount();
    auto& editable = live->mutableProject();
    auto ballIt = editable.entities.find(ball);
    if (!check(ballIt != editable.entities.end()
                   && !ballIt->solvers.isEmpty(),
               QStringLiteral("solved entity disappeared before the edit")))
        return 1;
    ballIt->solvers.first().initialVelocity = QPointF(60.0, -280.0);
    if (!check(document->commitEntity(
                   ball, QStringLiteral("solver playback perf edit"), &error),
               QStringLiteral("edit commit failed: %1").arg(error)))
        return 1;

    // Document::changed clears the shared map and queues one precompute.
    for (int i = 0; i < 10; ++i)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    renderFrame(0);
    const int editDelta = live->solverEvaluationCount() - beforeEdit;
    const int afterEditRender = live->solverEvaluationCount();
    for (int frame = 1; frame <= 10; ++frame)
        renderFrame(frame);
    const int postEditDelta =
        live->solverEvaluationCount() - afterEditRender;
    qInfo() << "solver-playback-perf-test: editDelta=" << editDelta
            << "postEditDelta=" << postEditDelta;
    if (!check(editDelta >= 1 && editDelta <= 2,
               QStringLiteral("edit caused %1 solves, expected 1-2")
                   .arg(editDelta)))
        return 1;
    if (!check(postEditDelta == 0,
               QStringLiteral("edited segment kept re-solving: +%1 over 10 "
                              "frames")
                   .arg(postEditDelta)))
        return 1;

    const int finalCount = live->solverEvaluationCount();
    window->close();
    QApplication::processEvents();
    qInfo().nospace()
        << "solver-playback-perf-test OK: frames=120 countDelta=" << countDelta
        << " ms/frame=" << secondMsPerFrame << " fps=" << secondFps
        << " workbenchHits=" << workbenchHits << " editDelta=" << editDelta
        << " finalCount=" << finalCount;
    return 0;
}

int solverConvertTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Solver Convert");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("solver-convert.pvproj"));
    auto session = pvf::EditorSession::createProject(projectDir, settings,
                                                     &error);
    if (!session)
    {
        qCritical() << "solver-convert-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    auto makeEntity = [&project](const QString& name, int row,
                                 pvf::ContentKind kind) {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 60;
        entity.row = row;
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);
        return entity.uuid;
    };

    const pvf::Uuid ground =
        makeEntity(QStringLiteral("地面"), 0, pvf::ContentKind::Vector);
    project.entities[ground].transform.y = -100.0;
    project.entities[ground].content.vector.width = 600.0;
    project.entities[ground].content.vector.height = 60.0;
    project.entities[ground].physicsInteract = true;

    const pvf::Uuid ball =
        makeEntity(QStringLiteral("落球"), 1, pvf::ContentKind::Vector);
    project.entities[ball].transform.y = 200.0;
    project.entities[ball].content.vector.width = 40.0;
    project.entities[ball].content.vector.height = 40.0;
    pvf::model::Track ballX;
    ballX.keys = {{0, 0.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("x"), ballX);
    pvf::model::Track ballY;
    ballY.keys = {{0, 200.0, {}, {}}};
    project.entities[ball].tracks.insert(QStringLiteral("y"), ballY);

    // A last key on a non-driven track (opacity) exercises conversion without
    // requiring an x/y key at the solver start frame.
    const pvf::Uuid marker =
        makeEntity(QStringLiteral("淡出块"), 2, pvf::ContentKind::Vector);
    pvf::model::Track markerOpacity;
    markerOpacity.keys = {{10, 0.5, {}, {}}, {20, 1.0, {}, {}}};
    project.entities[marker].tracks.insert(QStringLiteral("opacity"),
                                           markerOpacity);

    const QVector<pvf::Uuid> setup = {ground, ball, marker};
    if (!session->document()->commitEntities(setup,
                                             QStringLiteral("solver-convert setup"),
                                             &error))
    {
        qCritical() << "solver-convert-test: setup failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    auto* timeline = window->findChild<pvf::TimelinePanel*>();
    if (!live || !document || !panel || !timeline)
    {
        qCritical() << "solver-convert-test: window parts missing";
        return 1;
    }
    QObject::connect(panel, &pvf::PropertiesPanel::sessionEditFailed,
                     [](const QString& message) {
                         qCritical() << "solver-convert-test: panel edit failed"
                                     << message;
                     });

    auto check = [](bool condition, const QString& message) {
        if (!condition)
            qCritical() << "solver-convert-test:" << message;
        return condition;
    };
    auto flush = [live] {
        QString flushError;
        live->flush(&flushError);
        if (!flushError.isEmpty())
            qCritical() << "solver-convert-test: flush failed" << flushError;
    };
    const auto undoDepth = [document] {
        return document->store()->undoDepth();
    };

    auto* solverSection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("solverSection"));
    if (solverSection)
        solverSection->setExpanded(true);
    auto* keySection =
        panel->findChild<pvf::CollapsibleSection*>(QStringLiteral("keySection"));
    if (keySection)
        keySection->setExpanded(true);
    QApplication::processEvents();

    auto* convertButton =
        panel->findChild<QPushButton*>(QStringLiteral("solverConvertButton"));
    auto* convertHint =
        panel->findChild<QLabel*>(QStringLiteral("solverConvertHint"));
    auto* effectCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverEffectCombo"));
    auto* targetCombo =
        panel->findChild<QComboBox*>(QStringLiteral("solverTargetCombo"));
    auto* createEnd =
        panel->findChild<QSpinBox*>(QStringLiteral("solverCreateEnd"));
    auto* createManual =
        panel->findChild<QCheckBox*>(QStringLiteral("solverCreateManualEnd"));
    auto* createAuto =
        panel->findChild<QLabel*>(QStringLiteral("solverCreateEndAuto"));
    auto* createButton =
        panel->findChild<QPushButton*>(QStringLiteral("solverCreateButton"));
    if (!check(convertButton && convertHint && effectCombo && targetCombo
                   && createEnd && createManual && createAuto && createButton,
               QStringLiteral("convert/create widgets missing")))
        return 1;

    // Property no longer requires an end frame.
    if (!check(!createEnd->isVisible(),
               QStringLiteral("create end spin is visible by default"))
        || !check(!createManual->isChecked(),
                  QStringLiteral("manual end is checked by default"))
        || !check(createAuto->text().contains(QStringLiteral("自动")),
                  QStringLiteral("automatic end explanation missing")))
        return 1;

    // Non-last key: conversion is disabled with the expected hint.
    document->setSelection(marker);
    live->setPlayhead(10);
    QApplication::processEvents();
    if (!check(!convertButton->isEnabled(),
               QStringLiteral("non-last key conversion is enabled"))
        || !check(convertHint->text().contains(QStringLiteral("后面还有关键帧")),
                  QStringLiteral("non-last hint missing: %1")
                      .arg(convertHint->text()))
        || !check(!createButton->isEnabled(),
                  QStringLiteral("solver create button ignores non-last key")))
        return 1;

    // The same entity's last key (opacity only) can be converted.
    live->setPlayhead(20);
    QApplication::processEvents();
    if (!check(convertButton->isEnabled(),
               QStringLiteral("last key conversion is disabled")))
        return 1;

    // Gravity conversion at frame 0: one commit, automatic end.
    document->setSelection(ball);
    live->setPlayhead(0);
    QApplication::processEvents();
    effectCombo->setCurrentIndex(
        effectCombo->findData(int(pvf::SolverEffect::Gravity)));
    targetCombo->setCurrentIndex(targetCombo->findData(ground));
    const int beforeCreate = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();

    const auto& ballAfterCreate = live->project().entities.value(ball);
    if (!check(ballAfterCreate.solvers.size() == 1,
               QStringLiteral("gravity solver was not created"))
        || !check(ballAfterCreate.solvers.first().effect
                      == pvf::SolverEffect::Gravity
                  && ballAfterCreate.solvers.first().startFrame == 0
                  && ballAfterCreate.solvers.first().endFrame == -1,
                  QStringLiteral("converted solver defaults wrong"))
        || !check(ballAfterCreate.solvers.first()
                          .params.value(QStringLiteral("targetUuid"))
                          .toString()
                      == ground,
                  QStringLiteral("converted solver target wrong"))
        || !check(undoDepth() == beforeCreate + 1,
                  QStringLiteral("conversion was not one commit")))
        return 1;
    const pvf::Uuid gravityId = ballAfterCreate.solvers.first().id;
    if (!check(pvf::model::solverStructuralEnd(
                   ballAfterCreate, ballAfterCreate.solvers.first())
                   == 59,
               QStringLiteral("automatic structural end is not the clip end")))
        return 1;

    auto* endInfo =
        panel->findChild<QLabel*>(QStringLiteral("solverEndInfo0"));
    if (!check(endInfo && endInfo->text().contains(QStringLiteral("自动")),
               QStringLiteral("solver card does not show automatic end: %1")
                   .arg(endInfo ? endInfo->text() : QString())))
        return 1;

    // One commit is also one undo step.
    if (!document->undo(&error))
    {
        qCritical() << "solver-convert-test: undo failed" << error;
        return 1;
    }
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.isEmpty(),
               QStringLiteral("undo did not remove the solver")))
        return 1;
    if (!document->redo(&error))
    {
        qCritical() << "solver-convert-test: redo failed" << error;
        return 1;
    }
    QApplication::processEvents();
    flush();
    if (!check(live->project().entities.value(ball).solvers.size() == 1,
               QStringLiteral("redo did not restore the solver")))
        return 1;

    // The first key created after the solver start is the end marker: the
    // structural end becomes N-1 and the key at N is ordinary animation.
    if (!check(live->editActions()->addKeyframe(ball, QStringLiteral("x"), 30,
                                                &error),
               QStringLiteral("could not add the end-marker key: %1").arg(error)))
        return 1;
    flush();
    {
        const auto& entity = live->project().entities.value(ball);
        if (!check(pvf::model::solverStructuralEnd(entity,
                                                   entity.solvers.first())
                       == 29,
                   QStringLiteral("end-marker key did not derive end N-1"))
            || !check(pvf::anim::solverAt(entity, 29) != nullptr,
                      QStringLiteral("solver is not active at N-1"))
            || !check(pvf::anim::solverAt(entity, 30) == nullptr,
                      QStringLiteral("solver still active at the end key"))
            || !check(pvf::model::validateEntity(entity, &error),
                      QStringLiteral("entity invalid after end key: %1")
                          .arg(error)))
            return 1;
    }

    // Deleting the end marker restores the automatic end.
    if (!check(live->editActions()->deleteKeyframes(
                   {pvf::KeyRef{ball, QStringLiteral("x"), 30}}, &error),
               QStringLiteral("could not delete the end-marker key: %1")
                   .arg(error)))
        return 1;
    flush();
    {
        const auto& entity = live->project().entities.value(ball);
        if (!check(pvf::model::solverStructuralEnd(entity,
                                                   entity.solvers.first())
                       == 59,
                   QStringLiteral("deleting the end key did not restore auto"))
            || !check(pvf::anim::solverAt(entity, 59) != nullptr,
                      QStringLiteral("restored auto solver does not reach clip end")))
            return 1;
    }

    // A later last key can become a second segment in the same clip.
    if (!check(live->editActions()->addKeyframe(ball, QStringLiteral("x"), 40,
                                                &error),
               QStringLiteral("could not add the second start key: %1")
                   .arg(error)))
        return 1;
    live->setPlayhead(40);
    QApplication::processEvents();
    if (!check(convertButton->isEnabled(),
               QStringLiteral("second segment conversion is disabled")))
        return 1;
    const int beforeSecond = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();
    {
        const auto& entity = live->project().entities.value(ball);
        if (!check(entity.solvers.size() == 2,
                   QStringLiteral("second solver was not created"))
            || !check(entity.solvers.at(1).startFrame == 40
                      && entity.solvers.at(1).endFrame == -1,
                      QStringLiteral("second solver range wrong"))
            || !check(pvf::model::solverStructuralEnd(entity,
                                                       entity.solvers.at(0))
                          == 39,
                      QStringLiteral("first segment did not end before second"))
            || !check(pvf::model::solverStructuralEnd(entity,
                                                       entity.solvers.at(1))
                          == 59,
                      QStringLiteral("second segment structural end wrong"))
            || !check(undoDepth() == beforeSecond + 1,
                      QStringLiteral("second conversion was not one commit")))
            return 1;
    }

    // Any track's last key works: opacity-only key converted to spin.
    document->setSelection(marker);
    live->setPlayhead(20);
    QApplication::processEvents();
    effectCombo->setCurrentIndex(
        effectCombo->findData(int(pvf::SolverEffect::Spin)));
    const int beforeMarker = undoDepth();
    createButton->click();
    QApplication::processEvents();
    flush();
    {
        const auto& entity = live->project().entities.value(marker);
        if (!check(entity.solvers.size() == 1,
                   QStringLiteral("non-driven-track conversion failed"))
            || !check(entity.solvers.first().effect == pvf::SolverEffect::Spin
                      && entity.solvers.first().startFrame == 20
                      && entity.solvers.first().endFrame == -1,
                      QStringLiteral("non-driven-track solver range wrong"))
            || !check(undoDepth() == beforeMarker + 1,
                      QStringLiteral("non-driven-track conversion was not one commit"))
            || !check(!createEnd->isVisible(),
                      QStringLiteral("conversion still exposes an end-frame spin")))
            return 1;
    }

    // Timeline band uses the physical stop clipped by the derived end.
    const int bandStop = timeline->solverBandStopFrameForTest(ball, gravityId);
    if (!check(bandStop >= 0 && bandStop <= 39,
               QStringLiteral("timeline band stop is not clipped: %1")
                   .arg(bandStop)))
        return 1;

    if (auto* objectTab =
            window->findChild<QScrollArea*>(QStringLiteral("objectTab")))
        objectTab->verticalScrollBar()->setValue(
            objectTab->verticalScrollBar()->maximum());
    document->setSelection(ball);
    live->setPlayhead(40);
    QApplication::processEvents();
    window->viewportForTest()->repaint();
    QApplication::processEvents();
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const QString screenshot =
        QStringLiteral("/tmp/opencode/pvforge-solver-convert.png");
    if (!window->grab().save(screenshot))
    {
        qCritical() << "solver-convert-test: screenshot failed" << screenshot;
        return 1;
    }

    window->close();
    QApplication::processEvents();
    qInfo() << "solver-convert-test OK: last/non-last key, any track,"
               "N-1 end marker, delete restores auto, multi-segment,"
               "one commit/undo, automatic end UI, screenshot"
            << screenshot;
    return 0;
}


} // namespace

namespace pvf::tests {

void registerSolverTests(Registry& registry)
{
    registry.add(QStringLiteral("--solver-ui-test"),
                 [](QApplication& app) { return solverUiTest(app); });
    registry.add(QStringLiteral("--solver-mini-preview-test"),
                 [](QApplication& app) { return solverMiniPreviewTest(app); });
    registry.add(QStringLiteral("--solver-preview-freeze-test"),
                 [](QApplication& app) { return solverPreviewFreezeTest(app); });
    registry.add(QStringLiteral("--solver-playback-perf-test"),
                 [](QApplication& app) { return solverPlaybackPerfTest(app); });
    registry.add(QStringLiteral("--solver-convert-test"),
                 [](QApplication& app) { return solverConvertTest(app); });
}

} // namespace pvf::tests
