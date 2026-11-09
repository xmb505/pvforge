#include "next/viewport/ViewportWidget.h"

#include "gizmo/Gizmo.h"
#include "model/Model.h"
#include "next/core/Document.h"

#include <QApplication>
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

} // namespace

class ViewportWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void projectCanvasRenders();
    void clickSelectsAndClears();
    void dragMoveXCommitsOnce();
    void cornerScaleKeepsOppositeCorner();
    void centerScaleFlipsBothAxes();
    void rotateDragAndShiftSnap();
    void lockedEntityIgnoresGizmo();
    void hiddenEntityStillSelectable();
    void negativeCameraScaleRenders();
    void htmlProviderRendersFrame();
    void videoProviderRendersFrame();
    void providerReceivesProjectFrame();
    void noProviderKeepsPlaceholder();
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
    QVERIFY(std::abs(snapped - 15.0) < 1e-6);
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
    document.addAsset(QStringLiteral("Html"), AssetType::Html,
                      QStringLiteral("html-hash"));

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
    document.addAsset(QStringLiteral("Video"), AssetType::Video,
                      QStringLiteral("video-hash"));

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
    const Uuid uuid = document.addAsset(QStringLiteral("Video"),
                                        AssetType::Video,
                                        QStringLiteral("video-hash"));
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
    document.addAsset(QStringLiteral("Html"), AssetType::Html,
                      QStringLiteral("html-hash"));

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

QTEST_MAIN(ViewportWidgetTest)
#include "ViewportWidgetTest.moc"
