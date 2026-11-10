#include "next/app/CameraMonitorWidget.h"

#include "next/app/ProjectDocument.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace pvf;
using namespace pvf::next;
using namespace pvf::model;

namespace {

std::unique_ptr<ProjectDocument> makeDocument(const QString& projectDir,
                                              QString* error)
{
    ProjectSettings settings;
    settings.name = QStringLiteral("Monitor");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    settings.background = QStringLiteral("#101014");
    return ProjectDocument::create(projectDir, settings, error);
}

Entity makeEntity(ContentKind kind, const QString& name, double x = 0.0)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = 0;
    entity.end = 30;
    entity.content.kind = kind;
    entity.transform.x = x;
    return entity;
}

Entity makeWhiteVector()
{
    Entity entity = makeEntity(ContentKind::Vector, QStringLiteral("白块"));
    entity.transform.x = 320.0;
    entity.transform.y = 180.0;
    entity.content.vector.shape = 0;
    entity.content.vector.width = 200.0;
    entity.content.vector.height = 120.0;
    entity.content.vector.fillColor = QStringLiteral("#ffffff");
    entity.content.vector.strokeWidth = 0.0;
    return entity;
}

// One second of solid red at project fps, so the overlay can be told apart
// from the project background.
QString makeRedVideo(const QString& output)
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        return {};
    QProcess process;
    process.start(QStringLiteral("ffmpeg"),
                  {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                   QStringLiteral("error"), QStringLiteral("-y"),
                   QStringLiteral("-f"), QStringLiteral("lavfi"),
                   QStringLiteral("-i"),
                   QStringLiteral("color=c=red:s=64x36:d=1:r=30"),
                   QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                   QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                   output});
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0)
        return {};
    return QFile::exists(output) ? output : QString();
}

} // namespace

class CameraMonitorTests : public QObject
{
    Q_OBJECT

private slots:
    void rendersCameraFrame();
    void cameraChangeReframesScene();
    void emptyContextAndNoCameraDoNotCrash();
    void referenceVideoOverlayFollowsOpacity();
};

void CameraMonitorTests::rendersCameraFrame()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/monitor.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const Entity vector = makeWhiteVector();
    document->mutableProject().entities.insert(vector.uuid, vector);

    CameraMonitorWidget monitor(document.get(), nullptr);
    monitor.resize(320, 180);

    const QImage frame = monitor.grabForTest();
    QVERIFY(!frame.isNull());
    QVERIFY(frame.width() > 0);
    QVERIFY(frame.height() > 0);
    QVERIFY(frame.width() <= 640);
    QVERIFY(frame.height() <= 360);

    // The white vector sits at the canvas centre, so the scene is not just
    // the project background.
    const QColor centre =
        frame.pixelColor(frame.width() / 2, frame.height() / 2);
    QVERIFY(centre.red() > 200);
    QVERIFY(centre.green() > 200);
    QVERIFY(centre.blue() > 200);
}

void CameraMonitorTests::cameraChangeReframesScene()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/monitor.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const Entity vector = makeWhiteVector();
    document->mutableProject().entities.insert(vector.uuid, vector);
    const Entity cameraA = makeEntity(ContentKind::Camera, QStringLiteral("A"));
    const Entity cameraB =
        makeEntity(ContentKind::Camera, QStringLiteral("B"), 160.0);
    document->mutableProject().entities.insert(cameraA.uuid, cameraA);
    document->mutableProject().entities.insert(cameraB.uuid, cameraB);

    CameraMonitorWidget monitor(document.get(), nullptr);
    monitor.resize(320, 180);

    monitor.setCameraUuid(cameraA.uuid);
    QCOMPARE(monitor.cameraUuid(), cameraA.uuid);
    const QImage first = monitor.grabForTest();
    QVERIFY(!first.isNull());

    monitor.setCameraUuid(cameraB.uuid);
    const QImage second = monitor.grabForTest();
    QVERIFY(!second.isNull());
    QVERIFY(first != second);
}

void CameraMonitorTests::emptyContextAndNoCameraDoNotCrash()
{
    CameraMonitorWidget empty(nullptr, nullptr);
    empty.resize(320, 180);
    QVERIFY(empty.grabForTest().isNull());
    empty.show();
    QVERIFY(!empty.grab().isNull());

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/monitor.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    CameraMonitorWidget monitor(document.get(), nullptr);
    monitor.resize(320, 180);
    // An empty camera UUID means the whole canvas; it must not crash.
    monitor.setCameraUuid(Uuid());
    QVERIFY(!monitor.grabForTest().isNull());

    // A stale camera UUID falls back to the canvas view inside render2.
    monitor.setCameraUuid(model::newUuid());
    QVERIFY(!monitor.grabForTest().isNull());
}

void CameraMonitorTests::referenceVideoOverlayFollowsOpacity()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/monitor.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    CameraMonitorWidget monitor(document.get(), nullptr);
    monitor.resize(320, 180);

    // A missing file is rejected and leaves the previous reference untouched.
    QVERIFY(!monitor.setReferenceVideo(
        temp.path() + QStringLiteral("/missing.mp4"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(monitor.referencePath().isEmpty());

    const QString video = makeRedVideo(temp.path() + QStringLiteral("/red.mp4"));
    if (video.isEmpty())
        QSKIP("ffmpeg is not available");

    error.clear();
    QVERIFY2(monitor.setReferenceVideo(video, &error), qPrintable(error));
    QCOMPARE(monitor.referencePath(), QFileInfo(video).absoluteFilePath());

    QSignalSpy changed(&monitor, &CameraMonitorWidget::referenceChanged);
    QVERIFY(changed.isValid());

    monitor.setReferenceOpacity(0.0);
    const QImage plain = monitor.grabForTest();
    QVERIFY(!plain.isNull());

    monitor.setReferenceOpacity(0.5);
    const QImage blended = monitor.grabForTest();
    QVERIFY(!blended.isNull());

    monitor.setReferenceOpacity(1.0);
    const QImage full = monitor.grabForTest();
    QVERIFY(!full.isNull());

    QVERIFY(changed.count() >= 2);
    QVERIFY(plain != blended);
    QVERIFY(blended != full);

    const QColor covered =
        full.pixelColor(full.width() / 2, full.height() / 2);
    QVERIFY(covered.red() > 150);
    QVERIFY(covered.green() < 120);
    QVERIFY(covered.blue() < 120);

    // A reference file that disappears after loading leaves the camera
    // picture in place and surfaces a hint instead of crashing.
    QVERIFY(QFile::remove(video));
    document->setPlayhead(10);
    const QImage disappeared = monitor.grabForTest();
    QVERIFY(!disappeared.isNull());
    QVERIFY(!monitor.referenceHint().isEmpty());

    monitor.clearReference();
    QVERIFY(monitor.referencePath().isEmpty());
    QVERIFY(monitor.referenceHint().isEmpty());
    QVERIFY(!monitor.grabForTest().isNull());
}

QTEST_MAIN(CameraMonitorTests)

#include "CameraMonitorTests.moc"
