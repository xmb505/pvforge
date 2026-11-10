#include "next/viewport/ViewportWidget.h"

#include "gizmo/Gizmo.h"
#include "model/Model.h"
#include "next/core/Document.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QMouseEvent>
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
        m_project.settings.width = 320;
        m_project.settings.height = 180;
        m_project.settings.background = QStringLiteral("#101014");
    }

    const model::Project& project() const override { return m_project; }
    model::Project& mutableProject() override { return m_project; }
    assets::AssetStore& assets() override { return m_assets; }

    int playhead() const override { return m_playhead; }
    void setPlayhead(int frame) override
    {
        if (m_playhead == frame)
            return;
        m_playhead = frame;
        emit playheadChanged();
    }

    Uuid selection() const override { return m_selection; }
    void setSelection(const Uuid& uuid) override
    {
        if (m_selection == uuid)
            return;
        m_selection = uuid;
        emit selectionChanged();
    }

    bool commitEntity(const Uuid& uuid, const QString& message,
                      QString* error = nullptr) override
    {
        Q_UNUSED(uuid);
        Q_UNUSED(error);
        ++m_entityCommits;
        m_lastMessage = message;
        emit changed();
        return true;
    }

    bool commitProject(const QString& message, QString* error = nullptr) override
    {
        Q_UNUSED(error);
        ++m_projectCommits;
        m_lastMessage = message;
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

    Uuid addVector(const QString& name, double x, double y, double width,
                   double height)
    {
        model::Entity entity;
        entity.uuid = model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 30;
        entity.transform.x = x;
        entity.transform.y = y;
        entity.content.kind = ContentKind::Vector;
        entity.content.vector.shape = 0;
        entity.content.vector.width = width;
        entity.content.vector.height = height;
        entity.content.vector.fillColor = QStringLiteral("#ff0000");
        const Uuid uuid = entity.uuid;
        m_project.entities.insert(uuid, entity);
        return uuid;
    }

    Uuid addCamera(double x, double y, double scaleX, double scaleY)
    {
        model::Entity entity;
        entity.uuid = model::newUuid();
        entity.name = QStringLiteral("Camera");
        entity.start = 0;
        entity.end = 30;
        entity.transform.x = x;
        entity.transform.y = y;
        entity.transform.scaleX = scaleX;
        entity.transform.scaleY = scaleY;
        entity.content.kind = ContentKind::Camera;
        const Uuid uuid = entity.uuid;
        m_project.entities.insert(uuid, entity);
        return uuid;
    }

    // Asset entity centered on the project canvas.
    Uuid addAsset(const QString& name, AssetType type, const QString& hash)
    {
        model::Entity entity;
        entity.uuid = model::newUuid();
        entity.name = name;
        entity.start = 0;
        entity.end = 30;
        entity.transform.x = 160.0;
        entity.transform.y = 90.0;
        entity.content.kind = ContentKind::Asset;
        entity.content.asset.type = type;
        entity.content.asset.hash = hash;
        const Uuid uuid = entity.uuid;
        m_project.entities.insert(uuid, entity);
        return uuid;
    }

    // Writes a minimal but real asset into the store so render2 sees it as
    // present, and returns its content hash.
    AssetHash registerAsset(AssetType type, const QString& name, int width,
                            int height)
    {
        const AssetHash hash = QString::fromLatin1(
            QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256)
                .toHex());
        const QString directory =
            m_assets.assetsDirectory() + QLatin1Char('/') + hash;
        QDir().mkpath(directory);

        QJsonObject metadata;
        metadata.insert(QStringLiteral("hash"), hash);
        metadata.insert(QStringLiteral("type"), assetTypeName(type));
        metadata.insert(QStringLiteral("name"), name);
        if (width > 0)
            metadata.insert(QStringLiteral("width"), width);
        if (height > 0)
            metadata.insert(QStringLiteral("height"), height);

        QFile json(directory + QStringLiteral("/asset.json"));
        if (!json.open(QIODevice::WriteOnly))
            return {};
        json.write(QJsonDocument(metadata).toJson());
        json.close();

        if (type == AssetType::Html)
        {
            QDir().mkpath(directory + QStringLiteral("/bundle"));
            QFile index(directory + QStringLiteral("/bundle/index.html"));
            if (!index.open(QIODevice::WriteOnly))
                return {};
            index.write("<html></html>");
        }
        else
        {
            QFile content(directory + QStringLiteral("/content.bin"));
            if (!content.open(QIODevice::WriteOnly))
                return {};
            content.write("x");
        }
        return hash;
    }

    int entityCommits() const { return m_entityCommits; }
    int projectCommits() const { return m_projectCommits; }
    QString lastMessage() const { return m_lastMessage; }

private:
    QTemporaryDir m_tempDir;
    assets::AssetStore m_assets;
    model::Project m_project;
    int m_playhead = 0;
    Uuid m_selection;
    int m_entityCommits = 0;
    int m_projectCommits = 0;
    QString m_lastMessage;
};

void showWidget(ViewportWidget& view)
{
    view.resize(640, 360);
    view.show();
    QTest::qWait(10);
}

QImage redFrame()
{
    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    return image;
}

bool isRed(const QColor& color)
{
    return color.red() > 200 && color.green() < 60 && color.blue() < 60;
}

bool isGreen(const QColor& color)
{
    return color.green() > 200 && color.red() < 60 && color.blue() < 60;
}

gizmo::Transform gizmoTransformOf(const model::Entity& entity)
{
    gizmo::Transform transform;
    transform.x = entity.transform.x;
    transform.y = entity.transform.y;
    transform.rotation = entity.transform.rotation;
    transform.scaleX = entity.transform.scaleX;
    transform.scaleY = entity.transform.scaleY;
    transform.pivotX = entity.transform.pivotX;
    transform.pivotY = entity.transform.pivotY;
    return transform;
}

gizmo::Bounds vectorBoundsOf(const model::Entity& entity)
{
    gizmo::Bounds bounds;
    bounds.left = -entity.content.vector.width / 2.0;
    bounds.right = entity.content.vector.width / 2.0;
    bounds.top = -entity.content.vector.height / 2.0;
    bounds.bottom = entity.content.vector.height / 2.0;
    return bounds;
}

double projectScale(const ViewportWidget& view)
{
    return QLineF(view.projectToWidget(QPointF(0.0, 0.0)),
                  view.projectToWidget(QPointF(1.0, 0.0)))
        .length();
}

QPointF handleWidgetPosition(const ViewportWidget& view,
                             const model::Entity& entity, gizmo::Handle handle)
{
    const QPointF parentPosition = gizmo::handlePosition(
        handle, gizmoTransformOf(entity), vectorBoundsOf(entity),
        projectScale(view));
    return view.projectToWidget(parentPosition);
}

QPointF rotateVector(QPointF vector, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    return QPointF(vector.x() * std::cos(radians) - vector.y() * std::sin(radians),
                   vector.x() * std::sin(radians) + vector.y() * std::cos(radians));
}

void sendMouseMove(QWidget& widget, QPointF position,
                   Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseMove, position,
                      widget.mapToGlobal(position.toPoint()), Qt::NoButton,
                      Qt::LeftButton, modifiers);
    QApplication::sendEvent(&widget, &event);
}

void dragHandle(ViewportWidget& view, const model::Entity& entity,
                gizmo::Handle handle, QPointF target,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPointF pressPosition = handleWidgetPosition(view, entity, handle);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                      pressPosition.toPoint());
    sendMouseMove(view, target, modifiers);
    QTest::mouseRelease(&view, Qt::LeftButton, modifiers, target.toPoint());
}

} // namespace

class ViewportWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void projectCanvasRenders();
    void framePresentedOnlyForNewScenes();
    void framePresentedSkipsMediaPlaceholder();
    void clickSelectsAndClears();
    void dragMoveXCommitsOnce();
    void gizmoFollowsRotationTrack();
    void gizmoFollowsStaticRotation();
    void gizmoFollowsSpinSolver();
    void gizmoWritesRotationKeyAtPlayhead();
    void gizmoAutoKeyWritesMoveKey();
    void childGizmoUsesParentTransform();
    void cornerScaleKeepsOppositeCorner();
    void centerScaleFlipsBothAxes();
    void rotateDragAndShiftSnap();
    void moveSnapsToCanvasCenter();
    void moveSnapsToEntityEdge();
    void moveSnapsToEdgeMidpointWhenEnabled();
    void ctrlDisablesSnapping();
    void altDoesNotDisableSnapping();
    void rotationSnapsToRightAngles();
    void rotationSnapsTo30WhenEnabled();
    void snapThresholdComesFromSetting();
    void scaleSnapsToPixelGrid();
    void lockedEntityIgnoresGizmo();
    void hiddenEntityStillSelectable();
    void negativeCameraScaleRenders();
    void htmlProviderRendersFrame();
    void videoProviderRendersFrame();
    void providerReceivesProjectFrame();
    void noProviderKeepsPlaceholder();
    void sceneCacheLookupSkipsRender();
    void sceneCacheMissStoresRenderedFrame();
    void sceneCacheBypassedDuringGizmoDrag();
    void sceneExpandShowsOutsideEntity();
    void sceneExpandZeroHidesOutsideEntity();
    void cameraViewIgnoresSceneExpand();
};

void ViewportWidgetTest::projectCanvasRenders()
{
    FakeDocument document;
    document.mutableProject().settings.background = QStringLiteral("#112233");

    ViewportWidget view(&document);
    showWidget(view);

    const QImage frame = view.grabFrame();
    QCOMPARE(frame.size(), QSize(320, 180));
    QCOMPARE(frame.pixelColor(0, 0), QColor(QStringLiteral("#112233")));
    QCOMPARE(frame.pixelColor(319, 179), QColor(QStringLiteral("#112233")));

    const QPointF projectPoint(123.0, 45.0);
    const QPointF roundTrip =
        view.widgetToProject(view.projectToWidget(projectPoint));
    QVERIFY(QLineF(roundTrip, projectPoint).length() < 1e-6);
    QVERIFY(view.zoom() > 0.0);
}

void ViewportWidgetTest::framePresentedOnlyForNewScenes()
{
    FakeDocument document;
    document.addVector(QStringLiteral("box"), 120.0, 60.0, 80.0, 60.0);

    ViewportWidget view(&document);
    QSignalSpy presented(&view, &ViewportWidget::framePresented);
    showWidget(view);

    QTRY_VERIFY(presented.count() >= 1);
    QCOMPARE(presented.last().at(0).toInt(), 0);

    // Repainting the same cached scene is an exposure event, not a new frame.
    const int before = presented.count();
    view.grab();
    QTest::qWait(20);
    QCOMPARE(presented.count(), before);

    document.setPlayhead(1);
    view.refresh();
    view.grab();
    QTRY_COMPARE(presented.count(), before + 1);
    QCOMPARE(presented.last().at(0).toInt(), 1);
}

void ViewportWidgetTest::framePresentedSkipsMediaPlaceholder()
{
    FakeDocument document;
    const AssetHash hash = document.registerAsset(
        AssetType::Video, QStringLiteral("Video"), 320, 180);
    document.addAsset(QStringLiteral("video"), AssetType::Video, hash);

    ViewportWidget view(&document);
    view.setVideoFrameProvider(
        [](const Uuid&, double) { return QImage(); });
    QSignalSpy presented(&view, &ViewportWidget::framePresented);
    showWidget(view);
    view.grab();
    QTest::qWait(20);

    // render2 drew its placeholder: nothing reached the screen.
    QCOMPARE(presented.count(), 0);

    view.setVideoFrameProvider(
        [](const Uuid&, double) { return redFrame(); });
    view.refresh();
    view.grab();
    QTRY_COMPARE(presented.count(), 1);
    QCOMPARE(presented.last().at(0).toInt(), 0);
}

void ViewportWidgetTest::clickSelectsAndClears()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);

    ViewportWidget view(&document);
    showWidget(view);

    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                      view.projectToWidget(QPointF(160.0, 90.0)).toPoint());
    QCOMPARE(document.selection(), uuid);

    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                      view.projectToWidget(QPointF(10.0, 10.0)).toPoint());
    QVERIFY(document.selection().isEmpty());
}

void ViewportWidgetTest::dragMoveXCommitsOnce()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target = handle + QPointF(40.0, 0.0);
    const double startX = entity.transform.x;
    const double scale = projectScale(view);

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    QTest::mouseMove(&view, target.toPoint(), 5);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, target.toPoint());

    const model::Entity moved = document.project().entities.value(uuid);
    QCOMPARE(document.entityCommits(), 1);
    QVERIFY(moved.transform.x > startX + 10.0);
    QVERIFY(std::abs(moved.transform.x - (startX + 40.0 / scale)) < 1.0);
    QVERIFY(qFuzzyCompare(moved.transform.y, entity.transform.y));
    QVERIFY(document.lastMessage().startsWith(QStringLiteral("pvf: move")));
}

void ViewportWidgetTest::gizmoFollowsRotationTrack()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);

    model::Track rotation;
    model::Keyframe first;
    first.frame = 0;
    first.value = 0.0;
    model::Keyframe last;
    last.frame = 30;
    last.value = 90.0;
    rotation.keys << first << last;
    document.mutableProject().entities[uuid].tracks.insert(
        QStringLiteral("rotation"), rotation);
    document.setSelection(uuid);
    // Half way through the 0 -> 90 ramp.
    document.setPlayhead(15);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const gizmo::Bounds bounds = vectorBoundsOf(entity);
    const double scale = projectScale(view);

    gizmo::Transform at45 = gizmoTransformOf(entity);
    at45.rotation = 45.0;
    const QPointF expectedHandle = view.projectToWidget(
        gizmo::handlePosition(gizmo::Handle::MoveX, at45, bounds, scale));
    const QPointF unrotatedHandle = view.projectToWidget(
        gizmo::handlePosition(gizmo::Handle::MoveX, gizmoTransformOf(entity),
                              bounds, scale));
    QVERIFY(QLineF(expectedHandle, unrotatedHandle).length() > 10.0);

    // Pressing the arrow the gizmo draws at 45 degrees must start a MoveX
    // drag, and the drag must project onto the rotated local X axis.
    const QPointF target =
        expectedHandle + rotateVector(QPointF(1.0, 0.0), 45.0) * 40.0;
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                      expectedHandle.toPoint());
    sendMouseMove(view, target, Qt::ControlModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target.toPoint());

    const model::Entity moved = document.project().entities.value(uuid);
    QCOMPARE(document.entityCommits(), 1);
    QVERIFY(std::abs(moved.transform.x - (160.0 + 40.0 / scale)) < 1.5);
    QVERIFY(std::abs(moved.transform.y - 90.0) < 1e-6);
}

void ViewportWidgetTest::gizmoFollowsStaticRotation()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.mutableProject().entities[uuid].transform.rotation = 90.0;
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF expectedHandle = view.projectToWidget(
        gizmo::handlePosition(gizmo::Handle::MoveX, gizmoTransformOf(entity),
                              vectorBoundsOf(entity), projectScale(view)));
    // At 90 degrees local +X points down the screen.
    const QPointF axisTip = view.projectToWidget(
        QPointF(160.0, 90.0 + 48.0 / projectScale(view)));
    QVERIFY(QLineF(expectedHandle, axisTip).length() < 0.5);

    const double scale = projectScale(view);
    const QPointF target = expectedHandle + QPointF(0.0, 40.0);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                      expectedHandle.toPoint());
    sendMouseMove(view, target, Qt::ControlModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target.toPoint());

    const model::Entity moved = document.project().entities.value(uuid);
    QCOMPARE(document.entityCommits(), 1);
    QVERIFY(std::abs(moved.transform.x - (160.0 + 40.0 / scale)) < 1.5);
    QVERIFY(std::abs(moved.transform.y - 90.0) < 1e-6);
}

void ViewportWidgetTest::gizmoFollowsSpinSolver()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);

    model::SolverSegment spin;
    spin.id = QStringLiteral("spin");
    spin.effect = SolverEffect::Spin;
    spin.startFrame = 0;
    spin.endFrame = -1;
    document.mutableProject().entities[uuid].solvers.append(spin);
    document.setSelection(uuid);
    // The default spin is 6 degrees per frame, so frame 15 shows 90 degrees.
    document.setPlayhead(15);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const double scale = projectScale(view);
    const QPointF expectedHandle = view.projectToWidget(
        QPointF(160.0, 90.0 + 48.0 / scale));

    const QPointF target = expectedHandle + QPointF(0.0, 40.0);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                      expectedHandle.toPoint());
    sendMouseMove(view, target, Qt::ControlModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target.toPoint());

    const model::Entity moved = document.project().entities.value(uuid);
    QCOMPARE(document.entityCommits(), 1);
    QVERIFY(std::abs(moved.transform.x - (160.0 + 40.0 / scale)) < 1.5);
    QVERIFY(std::abs(moved.transform.y - 90.0) < 1e-6);
}

void ViewportWidgetTest::gizmoWritesRotationKeyAtPlayhead()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);

    model::Track rotation;
    model::Keyframe first;
    first.frame = 0;
    first.value = 0.0;
    model::Keyframe last;
    last.frame = 30;
    last.value = 0.0;
    rotation.keys << first << last;
    document.mutableProject().entities[uuid].tracks.insert(
        QStringLiteral("rotation"), rotation);
    document.setSelection(uuid);
    document.setPlayhead(15);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF pivot(entity.transform.x + entity.transform.pivotX,
                        entity.transform.y + entity.transform.pivotY);
    const QPointF handle = handleWidgetPosition(
        view, entity, gizmo::Handle::RotateBottomRight);
    const QPointF handleProject = view.widgetToProject(handle);
    const QPointF target =
        view.projectToWidget(pivot + rotateVector(handleProject - pivot, 30.0));

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    sendMouseMove(view, target, Qt::ControlModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target.toPoint());

    const model::Entity rotated = document.project().entities.value(uuid);
    QCOMPARE(document.entityCommits(), 1);
    const auto track = rotated.tracks.constFind(QStringLiteral("rotation"));
    QVERIFY(track != rotated.tracks.constEnd());
    double keyValue = 0.0;
    bool found = false;
    for (const model::Keyframe& key : track.value().keys)
    {
        if (key.frame == 15)
        {
            found = true;
            keyValue = key.value;
        }
    }
    QVERIFY(found);
    QVERIFY(std::abs(keyValue - 30.0) < 0.5);
    QVERIFY(std::abs(model::evaluateTrack(track.value(), 15).value_or(0.0)
                     - 30.0)
            < 0.5);
}

void ViewportWidgetTest::gizmoAutoKeyWritesMoveKey()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);
    document.setPlayhead(7);

    ViewportWidget view(&document);
    showWidget(view);
    QVERIFY(!view.autoKeyEnabled());

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target = handle + QPointF(40.0, 0.0);

    // Auto-key is off and the property is static: only the static transform
    // may change, no track appears.
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    sendMouseMove(view, target, Qt::ControlModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target.toPoint());
    QVERIFY(document.project().entities.value(uuid).tracks.isEmpty());

    view.setAutoKeyEnabled(true);
    QVERIFY(view.autoKeyEnabled());
    const model::Entity afterStatic =
        document.project().entities.value(uuid);
    const QPointF autoHandle =
        handleWidgetPosition(view, afterStatic, gizmo::Handle::MoveX);
    const QPointF autoTarget = autoHandle + QPointF(20.0, 0.0);
    const double startX = afterStatic.transform.x;
    const double scale = projectScale(view);

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                      autoHandle.toPoint());
    sendMouseMove(view, autoTarget, Qt::ControlModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        autoTarget.toPoint());

    const model::Entity keyed = document.project().entities.value(uuid);
    const auto track = keyed.tracks.constFind(QStringLiteral("x"));
    QVERIFY(track != keyed.tracks.constEnd());
    const auto value = model::evaluateTrack(track.value(), 7);
    QVERIFY(value.has_value());
    QVERIFY(std::abs(*value - (startX + 20.0 / scale)) < 1.5);
    QVERIFY(keyed.tracks.constFind(QStringLiteral("y"))
            == keyed.tracks.constEnd());
}

void ViewportWidgetTest::childGizmoUsesParentTransform()
{
    FakeDocument document;

    model::Entity parent;
    parent.uuid = model::newUuid();
    parent.name = QStringLiteral("Parent");
    parent.start = 0;
    parent.end = 30;
    parent.transform.x = 160.0;
    parent.transform.y = 90.0;
    parent.transform.rotation = 90.0;
    parent.content.kind = ContentKind::Vector;
    parent.content.vector.shape = 0;
    parent.content.vector.width = 40.0;
    parent.content.vector.height = 20.0;
    parent.content.vector.fillColor = QStringLiteral("#00ff00");

    model::Entity child;
    child.uuid = model::newUuid();
    child.name = QStringLiteral("Child");
    child.parentUuid = parent.uuid;
    child.start = 0;
    child.end = 30;
    child.transform.x = 20.0;
    child.transform.y = 0.0;
    child.content.kind = ContentKind::Vector;
    child.content.vector.shape = 0;
    child.content.vector.width = 40.0;
    child.content.vector.height = 20.0;
    child.content.vector.fillColor = QStringLiteral("#ff0000");

    document.mutableProject().entities.insert(parent.uuid, parent);
    document.mutableProject().entities.insert(child.uuid, child);
    document.setSelection(child.uuid);

    ViewportWidget view(&document);
    showWidget(view);

    // Parent rotates 90°: child local +X points along project +Y.
    QTransform parentWorld;
    parentWorld.translate(parent.transform.x, parent.transform.y);
    parentWorld.rotate(parent.transform.rotation);
    const QPointF localHandle = gizmo::handlePosition(
        gizmo::Handle::MoveX, gizmoTransformOf(child), vectorBoundsOf(child),
        projectScale(view));
    const QPointF screenHandle =
        view.projectToWidget(parentWorld.map(localHandle));

    const double before = document.project().entities.value(child.uuid).transform.x;
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                      screenHandle.toPoint());
    const QPointF target = screenHandle + QPointF(0.0, 40.0);
    sendMouseMove(view, target, Qt::ControlModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target.toPoint());

    const double after = document.project().entities.value(child.uuid).transform.x;
    QVERIFY2(std::abs(after - before) > 1.0,
             "the gizmo handle must be hit in parent-transformed space");
}

void ViewportWidgetTest::cornerScaleKeepsOppositeCorner()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const gizmo::Transform startTransform = gizmoTransformOf(entity);
    const gizmo::Bounds bounds = vectorBoundsOf(entity);
    const QPointF oppositeLocal(bounds.left, bounds.top);
    const QPointF oppositeBefore =
        gizmo::localToWorld(startTransform, oppositeLocal);

    const QPointF handle = handleWidgetPosition(
        view, entity, gizmo::Handle::ScaleBottomRight);
    const QPointF target = handle + QPointF(-50.0, -30.0);

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    QTest::mouseMove(&view, target.toPoint(), 5);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, target.toPoint());

    const model::Entity scaled = document.project().entities.value(uuid);
    const QPointF oppositeAfter =
        gizmo::localToWorld(gizmoTransformOf(scaled), oppositeLocal);
    QVERIFY(QLineF(oppositeBefore, oppositeAfter).length() < 0.01);
    QVERIFY(std::abs(scaled.transform.scaleX) < 0.99);
    QCOMPARE(document.entityCommits(), 1);
    QVERIFY(document.lastMessage().startsWith(QStringLiteral("pvf: scale")));
}

void ViewportWidgetTest::centerScaleFlipsBothAxes()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::CenterScale);
    const QPointF target = handle + QPointF(-60.0, -60.0);

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    QTest::mouseMove(&view, target.toPoint(), 5);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, target.toPoint());

    const model::Entity scaled = document.project().entities.value(uuid);
    QVERIFY(scaled.transform.scaleX < 0.0);
    QVERIFY(scaled.transform.scaleY < 0.0);
    QCOMPARE(document.entityCommits(), 1);
}

void ViewportWidgetTest::rotateDragAndShiftSnap()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF pivot(entity.transform.x + entity.transform.pivotX,
                        entity.transform.y + entity.transform.pivotY);
    const QPointF handle = handleWidgetPosition(
        view, entity, gizmo::Handle::RotateBottomRight);
    const QPointF handleProject = view.widgetToProject(handle);

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    const QPointF target20 =
        view.projectToWidget(pivot + rotateVector(handleProject - pivot, 20.0));
    QTest::mouseMove(&view, target20.toPoint(), 5);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target20.toPoint());

    QVERIFY(std::abs(document.project().entities.value(uuid).transform.rotation
                     - 20.0)
            < 0.5);

    model::Entity& reset = document.mutableProject().entities[uuid];
    reset.transform.rotation = 0.0;

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    const QPointF target22 =
        view.projectToWidget(pivot + rotateVector(handleProject - pivot, 22.0));
    sendMouseMove(view, target22, Qt::ShiftModifier);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        target22.toPoint());

    const double snapped =
        document.project().entities.value(uuid).transform.rotation;
    QVERIFY(std::abs(snapped) < 1e-6);
}

void ViewportWidgetTest::moveSnapsToCanvasCenter()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 100.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);
    QVERIFY(view.snapEnabled());

    const model::Entity entity = document.project().entities.value(uuid);
    // 1.4 project pixels away from the canvas centre line, inside the 6 px
    // screen threshold.
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target =
        handle + QPointF((158.6 - entity.transform.x) * projectScale(view), 0.0);
    dragHandle(view, entity, gizmo::Handle::MoveX, target);

    QCOMPARE(document.entityCommits(), 1);
    QCOMPARE(document.project().entities.value(uuid).transform.x, 160.0);

    // Disabling canvas centre snapping leaves only the pixel grid.
    document.mutableProject().entities[uuid] = entity;
    view.setSnapCanvasCenter(false);
    dragHandle(view, entity, gizmo::Handle::MoveX, target);
    QCOMPARE(document.project().entities.value(uuid).transform.x, 159.0);
    view.setSnapCanvasCenter(true);
}

void ViewportWidgetTest::moveSnapsToEntityEdge()
{
    FakeDocument document;
    // Anchor box occupies x 110..210; the mover's right edge starts at 90.
    document.addVector(QStringLiteral("Anchor"), 160.0, 90.0, 100.0, 60.0);
    const Uuid mover = document.addVector(QStringLiteral("Mover"), 40.0, 30.0,
                                          100.0, 40.0);
    document.setSelection(mover);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(mover);
    // Raw right edge 109.4 snaps to the anchor's left edge at 110.
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target =
        handle + QPointF((59.4 - entity.transform.x) * projectScale(view), 0.0);
    dragHandle(view, entity, gizmo::Handle::MoveX, target);

    QCOMPARE(document.entityCommits(), 1);
    // The pixel grid alone would leave x at 59; the edge snap wins.
    QCOMPARE(document.project().entities.value(mover).transform.x, 60.0);
}

void ViewportWidgetTest::moveSnapsToEdgeMidpointWhenEnabled()
{
    FakeDocument document;
    // Anchor centre x = 100; the mover's right edge targets it.  No entity
    // edge or canvas centre line is within reach, and the pixel grid is off.
    document.addVector(QStringLiteral("Anchor"), 100.0, 90.0, 100.0, 60.0);
    const Uuid mover = document.addVector(QStringLiteral("Mover"), 40.0, 30.0,
                                          20.0, 20.0);
    document.setSelection(mover);

    ViewportWidget view(&document);
    showWidget(view);
    QVERIFY(!view.snapEdgeMidpoints());
    view.setSnapPixels(0);

    const model::Entity entity = document.project().entities.value(mover);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target =
        handle + QPointF((90.4 - entity.transform.x) * projectScale(view), 0.0);

    dragHandle(view, entity, gizmo::Handle::MoveX, target);
    // Midpoints are off by default: the raw 90.4 is kept.
    QVERIFY(std::abs(document.project().entities.value(mover).transform.x
                     - 90.4)
            < 0.5);

    document.mutableProject().entities[mover] = entity;
    view.setSnapEdgeMidpoints(true);
    dragHandle(view, entity, gizmo::Handle::MoveX, target);
    QVERIFY(std::abs(document.project().entities.value(mover).transform.x
                     - 90.0)
            < 1e-6);
}

void ViewportWidgetTest::ctrlDisablesSnapping()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 100.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target =
        handle + QPointF((158.6 - entity.transform.x) * projectScale(view), 0.0);
    dragHandle(view, entity, gizmo::Handle::MoveX, target, Qt::ControlModifier);

    // Ctrl turns every snap off: neither the centre line nor the pixel grid
    // applies, so the raw drag result is kept.
    const double x = document.project().entities.value(uuid).transform.x;
    QVERIFY(std::abs(x - 158.6) < 1.0);
    QVERIFY(std::abs(x - 160.0) > 1.0);
    QVERIFY(std::abs(x - std::round(x)) > 1e-6);
}

void ViewportWidgetTest::altDoesNotDisableSnapping()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 100.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target =
        handle + QPointF((158.6 - entity.transform.x) * projectScale(view), 0.0);
    dragHandle(view, entity, gizmo::Handle::MoveX, target, Qt::AltModifier);

    // Alt is only the around-centre rotation modifier; it no longer disables
    // snapping, so the centre line wins.
    QCOMPARE(document.project().entities.value(uuid).transform.x, 160.0);
}

void ViewportWidgetTest::rotationSnapsToRightAngles()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF pivot(entity.transform.x + entity.transform.pivotX,
                        entity.transform.y + entity.transform.pivotY);
    const QPointF handle = handleWidgetPosition(
        view, entity, gizmo::Handle::RotateBottomRight);
    const QPointF handleProject = view.widgetToProject(handle);

    const auto rotateTo = [&](double degrees) {
        document.mutableProject().entities[uuid] = entity;
        const QPointF target =
            view.projectToWidget(pivot
                                 + rotateVector(handleProject - pivot, degrees));
        dragHandle(view, document.project().entities.value(uuid),
                   gizmo::Handle::RotateBottomRight, target);
        return document.project().entities.value(uuid).transform.rotation;
    };

    QVERIFY(std::abs(rotateTo(88.0) - 90.0) < 1e-6);
    QVERIFY(std::abs(rotateTo(178.0) - 180.0) < 1e-6);
    QVERIFY(std::abs(rotateTo(267.0) - 270.0) < 1e-6);
}

void ViewportWidgetTest::rotationSnapsTo30WhenEnabled()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);
    QVERIFY(view.snapAngles()
            == QVector<double>({0.0, 90.0, 180.0, 270.0}));

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF pivot(entity.transform.x + entity.transform.pivotX,
                        entity.transform.y + entity.transform.pivotY);
    const QPointF handle = handleWidgetPosition(
        view, entity, gizmo::Handle::RotateBottomRight);
    const QPointF handleProject = view.widgetToProject(handle);

    const auto rotateTo = [&](double degrees) {
        document.mutableProject().entities[uuid] = entity;
        const QPointF target =
            view.projectToWidget(pivot
                                 + rotateVector(handleProject - pivot, degrees));
        dragHandle(view, document.project().entities.value(uuid),
                   gizmo::Handle::RotateBottomRight, target);
        return document.project().entities.value(uuid).transform.rotation;
    };

    // 32 degrees is 2 degrees from 30, but 30 is off by default.
    QVERIFY(std::abs(rotateTo(32.0) - 32.0) < 0.5);

    view.setSnapAngles({0.0, 30.0, 45.0, 90.0, 180.0, 270.0});
    QVERIFY(std::abs(rotateTo(32.0) - 30.0) < 1e-6);
}

void ViewportWidgetTest::snapThresholdComesFromSetting()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 40.0, 90.0,
                                         20.0, 20.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    // Raw x = 155 is 5 project pixels away from the canvas centre line.
    const QPointF target =
        handle + QPointF((155.0 - entity.transform.x) * projectScale(view), 0.0);

    view.setSnapThresholdPixels(1);
    QCOMPARE(view.snapThresholdPixels(), 1);
    dragHandle(view, entity, gizmo::Handle::MoveX, target);
    QCOMPARE(document.project().entities.value(uuid).transform.x, 155.0);

    document.mutableProject().entities[uuid] = entity;
    view.setSnapThresholdPixels(32);
    QCOMPARE(view.snapThresholdPixels(), 32);
    dragHandle(view, entity, gizmo::Handle::MoveX, target);
    QCOMPARE(document.project().entities.value(uuid).transform.x, 160.0);

    view.setSnapThresholdPixels(0);
    QCOMPARE(view.snapThresholdPixels(), 1);
    view.setSnapThresholdPixels(99);
    QCOMPARE(view.snapThresholdPixels(), 32);
}

void ViewportWidgetTest::scaleSnapsToPixelGrid()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const gizmo::Bounds bounds = vectorBoundsOf(entity);
    const QPointF handle = handleWidgetPosition(
        view, entity, gizmo::Handle::ScaleBottomRight);
    const QPointF target = handle + QPointF(-37.0, -21.0);
    dragHandle(view, entity, gizmo::Handle::ScaleBottomRight, target);

    const model::Entity scaled = document.project().entities.value(uuid);
    const QPointF corner = gizmo::localToWorld(
        gizmoTransformOf(scaled),
        QPointF(bounds.right, bounds.bottom));
    QVERIFY(std::abs(corner.x() - std::round(corner.x())) < 1e-6);
    QVERIFY(std::abs(corner.y() - std::round(corner.y())) < 1e-6);
    QCOMPARE(document.entityCommits(), 1);

    // Dragging past the opposite corner flips both axes; snapping must keep
    // the negative scale and still land the dragged corner on the grid.
    document.mutableProject().entities[uuid] = entity;
    const QPointF flipTarget = handle + QPointF(-260.0, -170.0);
    dragHandle(view, entity, gizmo::Handle::ScaleBottomRight, flipTarget);

    const model::Entity flipped = document.project().entities.value(uuid);
    QVERIFY(flipped.transform.scaleX < 0.0);
    QVERIFY(flipped.transform.scaleY < 0.0);
    const QPointF flippedCorner = gizmo::localToWorld(
        gizmoTransformOf(flipped),
        QPointF(bounds.right, bounds.bottom));
    QVERIFY(std::abs(flippedCorner.x() - std::round(flippedCorner.x())) < 1e-6);
    QVERIFY(std::abs(flippedCorner.y() - std::round(flippedCorner.y())) < 1e-6);
    QCOMPARE(document.entityCommits(), 2);
}

void ViewportWidgetTest::lockedEntityIgnoresGizmo()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         100.0, 60.0);
    document.mutableProject().entities[uuid].locked = true;
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);
    const QPointF target = handle + QPointF(40.0, 0.0);

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, handle.toPoint());
    QTest::mouseMove(&view, target.toPoint(), 5);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, target.toPoint());

    const model::Entity unchanged = document.project().entities.value(uuid);
    QVERIFY(qFuzzyCompare(unchanged.transform.x, entity.transform.x));
    QCOMPARE(document.entityCommits(), 0);
}

void ViewportWidgetTest::hiddenEntityStillSelectable()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Hidden"), 160.0, 90.0,
                                         100.0, 60.0);
    document.mutableProject().entities[uuid].hidden = true;

    ViewportWidget view(&document);
    showWidget(view);

    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                      view.projectToWidget(QPointF(160.0, 90.0)).toPoint());
    QCOMPARE(document.selection(), uuid);
}

void ViewportWidgetTest::negativeCameraScaleRenders()
{
    FakeDocument document;
    document.addVector(QStringLiteral("Box"), 160.0, 90.0, 100.0, 60.0);
    const Uuid camera = document.addCamera(160.0, 90.0, -0.5, -0.5);

    ViewportWidget view(&document);
    showWidget(view);
    view.setCameraUuid(camera);
    QCOMPARE(view.cameraUuid(), camera);

    const QImage frame = view.grabFrame();
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.size(), QSize(320, 180));
    QCOMPARE(frame.pixelColor(0, 0), QColor(QStringLiteral("#101014")));
    QVERIFY(!view.grab().isNull());

    const QPointF center(160.0, 90.0);
    const QPointF roundTrip =
        view.widgetToProject(view.projectToWidget(center));
    QVERIFY(std::isfinite(roundTrip.x()));
    QVERIFY(std::isfinite(roundTrip.y()));
    QVERIFY(QLineF(roundTrip, center).length() < 0.01);
}

void ViewportWidgetTest::htmlProviderRendersFrame()
{
    FakeDocument document;
    const AssetHash hash = document.registerAsset(AssetType::Html,
                                                  QStringLiteral("Html"), 800, 120);
    document.addAsset(QStringLiteral("Html"), AssetType::Html, hash);

    ViewportWidget view(&document);
    showWidget(view);

    const QImage placeholder = view.grabFrame();
    QCOMPARE(placeholder.size(), QSize(320, 180));
    QVERIFY(!isRed(placeholder.pixelColor(160, 90)));

    view.setHtmlFrameProvider([](const Uuid&, double) { return redFrame(); });

    const QImage frame = view.grabFrame();
    QCOMPARE(frame.size(), QSize(320, 180));
    QVERIFY(isRed(frame.pixelColor(160, 90)));

    // The widget paint path goes through ensureSceneImage and must also see
    // the provider installed after the first placeholder render.
    const QImage painted = view.grab().toImage();
    QVERIFY(isRed(painted.pixelColor(painted.width() / 2,
                                     painted.height() / 2)));
}

void ViewportWidgetTest::videoProviderRendersFrame()
{
    FakeDocument document;
    const AssetHash hash = document.registerAsset(
        AssetType::Video, QStringLiteral("Video"), 320, 180);
    document.addAsset(QStringLiteral("Video"), AssetType::Video, hash);

    ViewportWidget view(&document);
    showWidget(view);

    QVERIFY(!isRed(view.grabFrame().pixelColor(160, 90)));

    view.setVideoFrameProvider([](const Uuid&, double) { return redFrame(); });

    const QImage frame = view.grabFrame();
    QVERIFY(isRed(frame.pixelColor(160, 90)));
}

void ViewportWidgetTest::providerReceivesProjectFrame()
{
    FakeDocument document;
    const AssetHash hash = document.registerAsset(
        AssetType::Video, QStringLiteral("Video"), 320, 180);
    const Uuid uuid = document.addAsset(QStringLiteral("Video"),
                                        AssetType::Video, hash);
    document.setPlayhead(7);

    ViewportWidget view(&document);
    showWidget(view);

    Uuid receivedUuid;
    double receivedFrame = -1.0;
    view.setVideoFrameProvider([&](const Uuid& entityUuid, double projectFrame) {
        receivedUuid = entityUuid;
        receivedFrame = projectFrame;
        return redFrame();
    });

    QVERIFY(isRed(view.grabFrame().pixelColor(160, 90)));
    QCOMPARE(receivedUuid, uuid);
    QCOMPARE(receivedFrame, 7.0);
}

void ViewportWidgetTest::noProviderKeepsPlaceholder()
{
    FakeDocument document;
    const AssetHash hash = document.registerAsset(AssetType::Html,
                                                  QStringLiteral("Html"), 800, 120);
    document.addAsset(QStringLiteral("Html"), AssetType::Html, hash);

    ViewportWidget view(&document);
    showWidget(view);

    const QImage frame = view.grabFrame();
    QCOMPARE(frame.size(), QSize(320, 180));
    QVERIFY(!isRed(frame.pixelColor(160, 90)));
    QVERIFY(!view.grab().isNull());

    // Clearing an installed provider falls back to the placeholder.
    view.setHtmlFrameProvider([](const Uuid&, double) { return redFrame(); });
    QVERIFY(isRed(view.grabFrame().pixelColor(160, 90)));
    view.setHtmlFrameProvider({});
    QVERIFY(!isRed(view.grabFrame().pixelColor(160, 90)));
}

void ViewportWidgetTest::sceneCacheLookupSkipsRender()
{
    FakeDocument document;
    const AssetHash hash = document.registerAsset(
        AssetType::Html, QStringLiteral("Html"), 800, 120);
    document.addAsset(QStringLiteral("Html"), AssetType::Html, hash);

    ViewportWidget view(&document);
    showWidget(view);

    int providerCalls = 0;
    view.setHtmlFrameProvider([&](const Uuid&, double) {
        ++providerCalls;
        return redFrame();
    });

    QImage cached(320, 180, QImage::Format_ARGB32_Premultiplied);
    cached.fill(Qt::green);
    int lookups = 0;
    int stores = 0;
    int lookupFrame = -1;
    view.setSceneCacheHooks(
        [&](int frame, const Uuid&, bool* hit) {
            ++lookups;
            lookupFrame = frame;
            *hit = true;
            return cached;
        },
        [&](int, const Uuid&, const QImage&) { ++stores; });

    // MCP grabFrame always renders the project canvas live and never reads the
    // workbench cache (which may be the expanded image).
    const QImage frame = view.grabFrame();
    QCOMPARE(frame.size(), QSize(320, 180));
    QVERIFY(isRed(frame.pixelColor(160, 90)));
    QVERIFY(providerCalls > 0);
    QCOMPARE(lookups, 0);
    QCOMPARE(stores, 0);

    // The widget paint path uses ensureSceneImage and hits the cache.
    const int providerCallsAfterGrab = providerCalls;
    const QImage painted = view.grab().toImage();
    const QColor center = painted.pixelColor(painted.width() / 2,
                                             painted.height() / 2);
    QVERIFY(center.green() > 200 && center.red() < 60);
    QCOMPARE(providerCalls, providerCallsAfterGrab);
    QCOMPARE(lookupFrame, 0);
    QVERIFY(lookups >= 1);
}

void ViewportWidgetTest::sceneCacheMissStoresRenderedFrame()
{
    FakeDocument document;
    document.addVector(QStringLiteral("Red"), 160.0, 90.0, 320.0, 180.0);

    ViewportWidget view(&document);
    showWidget(view);

    int lookups = 0;
    int stores = 0;
    QImage stored;
    view.setSceneCacheHooks(
        [&](int, const Uuid&, bool* hit) {
            ++lookups;
            *hit = false;
            return QImage();
        },
        [&](int, const Uuid&, const QImage& image) {
            ++stores;
            stored = image;
        });

    // grabFrame bypasses the cache entirely.
    const QImage frame = view.grabFrame();
    QCOMPARE(lookups, 0);
    QCOMPARE(stores, 0);
    QCOMPARE(frame.size(), QSize(320, 180));
    QVERIFY(isRed(frame.pixelColor(160, 90)));

    // The paint path renders at the widget view size and stores that frame.
    const QImage painted = view.grab().toImage();
    QVERIFY(lookups >= 1);
    QCOMPARE(stores, 1);
    QVERIFY(!stored.isNull());
    QVERIFY(stored.width() > 0 && stored.height() > 0);
    QVERIFY(isRed(stored.pixelColor(stored.width() / 2,
                                    stored.height() / 2)));
    QVERIFY(isRed(painted.pixelColor(painted.width() / 2,
                                     painted.height() / 2)));
}

void ViewportWidgetTest::sceneCacheBypassedDuringGizmoDrag()
{
    FakeDocument document;
    const Uuid uuid = document.addVector(QStringLiteral("Box"), 160.0, 90.0,
                                         320.0, 180.0);
    document.setSelection(uuid);

    ViewportWidget view(&document);
    showWidget(view);

    QImage cached(320, 180, QImage::Format_ARGB32_Premultiplied);
    cached.fill(Qt::green);
    int lookups = 0;
    int stores = 0;
    view.setSceneCacheHooks(
        [&](int, const Uuid&, bool* hit) {
            ++lookups;
            *hit = true;
            return cached;
        },
        [&](int, const Uuid&, const QImage&) { ++stores; });

    const auto centerIsGreen = [&](const QImage& image) {
        const QColor color =
            image.pixelColor(view.projectToWidget(QPointF(100.0, 60.0)).toPoint());
        return color.green() > 200 && color.red() < 60;
    };
    const auto centerIsRed = [&](const QImage& image) {
        return isRed(image.pixelColor(
            view.projectToWidget(QPointF(100.0, 60.0)).toPoint()));
    };

    QVERIFY(centerIsGreen(view.grab().toImage()));

    const model::Entity entity = document.project().entities.value(uuid);
    const QPointF handle =
        handleWidgetPosition(view, entity, gizmo::Handle::MoveX);

    // A click that only shows the gizmo keeps using the cached frame.
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                      handle.toPoint());
    QVERIFY(centerIsGreen(view.grab().toImage()));

    // Actual movement edits mutableProject() before the commit that clears the
    // meta cache; the scene must come from render2 until release.
    QTest::mouseMove(&view, (handle + QPointF(24.0, 0.0)).toPoint());
    const int lookupsBeforeDrag = lookups;
    const QImage live = view.grab().toImage();
    QCOMPARE(lookups, lookupsBeforeDrag);
    QVERIFY(centerIsRed(live));

    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        (handle + QPointF(24.0, 0.0)).toPoint());
    QCOMPARE(stores, 0);
}

void ViewportWidgetTest::sceneExpandShowsOutsideEntity()
{
    FakeDocument document;
    document.mutableProject().settings.width = 100;
    document.mutableProject().settings.height = 100;
    document.addVector(QStringLiteral("Outside"), -30.0, 50.0, 20.0, 20.0);

    ViewportWidget view(&document);
    view.setSceneExpandPercent(50);
    showWidget(view);

    const QPointF point = view.projectToWidget(QPointF(-30.0, 50.0));
    QVERIFY(view.rect().contains(point.toPoint()));

    const QImage painted = view.grab().toImage();
    QVERIFY2(isRed(painted.pixelColor(point.toPoint())),
             qPrintable(QStringLiteral("pixel %1,%2 = %3")
                            .arg(point.x())
                            .arg(point.y())
                            .arg(painted.pixelColor(point.toPoint()).name())));
}

void ViewportWidgetTest::sceneExpandZeroHidesOutsideEntity()
{
    FakeDocument document;
    document.mutableProject().settings.width = 100;
    document.mutableProject().settings.height = 100;
    document.addVector(QStringLiteral("Outside"), -30.0, 50.0, 20.0, 20.0);

    ViewportWidget view(&document);
    view.setSceneExpandPercent(0);
    showWidget(view);

    const QPointF point = view.projectToWidget(QPointF(-30.0, 50.0));
    QVERIFY(view.rect().contains(point.toPoint()));

    const QImage painted = view.grab().toImage();
    const QColor color = painted.pixelColor(point.toPoint());
    QVERIFY(!isRed(color));
    QCOMPARE(color, QColor(20, 22, 26));
}

void ViewportWidgetTest::cameraViewIgnoresSceneExpand()
{
    FakeDocument document;
    document.mutableProject().settings.width = 100;
    document.mutableProject().settings.height = 100;
    const Uuid camera = document.addCamera(50.0, 50.0, 1.0, 1.0);

    ViewportWidget view(&document);
    view.setSceneExpandPercent(0);
    view.setCameraUuid(camera);
    showWidget(view);

    const QPointF beforeOrigin = view.projectToWidget(QPointF(0.0, 0.0));
    const QPointF beforeCorner = view.projectToWidget(QPointF(100.0, 100.0));

    view.setSceneExpandPercent(50);
    const QPointF afterOrigin = view.projectToWidget(QPointF(0.0, 0.0));
    const QPointF afterCorner = view.projectToWidget(QPointF(100.0, 100.0));

    QCOMPARE(afterOrigin, beforeOrigin);
    QCOMPARE(afterCorner, beforeCorner);
    QCOMPARE(view.grabFrame().size(), QSize(100, 100));

    // Painting the camera view still starts from the opaque background even
    // though the editor canvas expansion is enabled.
    const QImage painted = view.grab().toImage();
    QVERIFY(!painted.isNull());
}

QTEST_MAIN(ViewportWidgetTest)
#include "ViewportWidgetTest.moc"
