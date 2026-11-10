#include "next/core/Document.h"
#include "next/timeline/MarkerDialog.h"
#include "next/timeline/TimelineWidget.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

using pvf::AssetType;
using pvf::ContentKind;
using pvf::SolverEffect;
using pvf::Uuid;
using pvf::model::Project;
using pvf::next::Document;
using pvf::next::TimelineWidget;

class FakeDocument : public Document
{
public:
    explicit FakeDocument(const QString& projectRoot = QDir::tempPath())
        : Document(nullptr)
        , m_assets(projectRoot)
    {
    }

    const Project& project() const override { return m_project; }
    Project& mutableProject() override { return m_project; }
    pvf::assets::AssetStore& assets() override { return m_assets; }

    int playhead() const override { return m_playhead; }
    void setPlayhead(int frame) override
    {
        m_playhead = frame;
        emit playheadChanged();
    }

    Uuid selection() const override { return m_selection; }
    void setSelection(const Uuid& uuid) override
    {
        m_selection = uuid;
        emit selectionChanged();
    }

    bool commitEntity(const Uuid& uuid, const QString& message,
                      QString* error) override
    {
        Q_UNUSED(error);
        ++commitCount;
        lastCommitUuid = uuid;
        lastCommitUuids = {uuid};
        lastCommitMessage = message;
        return true;
    }

    bool commitEntities(const QVector<Uuid>& uuids, const QString& message,
                        QString* error) override
    {
        Q_UNUSED(error);
        ++commitCount;
        ++commitEntitiesCount;
        lastCommitUuids = uuids;
        lastCommitUuid = uuids.isEmpty() ? Uuid() : uuids.first();
        lastCommitMessage = message;
        return true;
    }

    bool commitChanges(const QVector<Uuid>& upserts,
                       const QVector<Uuid>& deletes, const QString& message,
                       QString* error) override
    {
        ++commitCount;
        ++commitChangesCount;
        lastUpserts = upserts;
        lastDeletes = deletes;
        lastCommitUuids = upserts;
        lastCommitUuid = upserts.isEmpty() ? Uuid() : upserts.first();
        lastCommitMessage = message;
        if (failCommitChanges)
        {
            if (error)
                *error = QStringLiteral("forced failure");
            return false;
        }
        for (const Uuid& uuid : deletes)
            m_project.entities.remove(uuid);
        return true;
    }

    bool commitProject(const QString& message, QString* error) override
    {
        Q_UNUSED(error);
        ++commitCount;
        lastCommitUuid.clear();
        lastCommitMessage = message;
        return true;
    }

    bool undo(QString* error) override
    {
        Q_UNUSED(error);
        return true;
    }

    bool redo(QString* error) override
    {
        Q_UNUSED(error);
        return true;
    }

    int commitCount = 0;
    int commitEntitiesCount = 0;
    int commitChangesCount = 0;
    bool failCommitChanges = false;
    Uuid lastCommitUuid;
    QVector<Uuid> lastCommitUuids;
    QVector<Uuid> lastUpserts;
    QVector<Uuid> lastDeletes;
    QString lastCommitMessage;

private:
    Project m_project;
    pvf::assets::AssetStore m_assets;
    int m_playhead = 0;
    Uuid m_selection;
};

Uuid addEntity(FakeDocument& document, const QString& name, int start, int end,
               int row, const Uuid& parent = Uuid(),
               ContentKind kind = ContentKind::Empty)
{
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = name;
    entity.parentUuid = parent;
    entity.start = start;
    entity.end = end;
    entity.row = row;
    entity.content.kind = kind;
    document.mutableProject().entities.insert(entity.uuid, entity);
    return entity.uuid;
}

void addKey(FakeDocument& document, const Uuid& uuid, const QString& trackName,
            int frame, double value)
{
    pvf::model::Keyframe key;
    key.frame = frame;
    key.value = value;
    document.mutableProject().entities[uuid].tracks[trackName].keys.append(key);
}

QPoint diamondPoint(TimelineWidget& widget, const Uuid& uuid, int absoluteFrame)
{
    const QRect bar = widget.entityBarRect(uuid);
    return QPoint(widget.xFromFrame(absoluteFrame), bar.center().y());
}

void trimLeftEdge(TimelineWidget& widget, const Uuid& uuid, int deltaFrames)
{
    const QRect bar = widget.entityBarRect(uuid);
    const QPoint from(bar.left() + 2, bar.center().y());
    const QPoint to =
        from + QPoint(int(deltaFrames * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);
}

void trimRightEdge(TimelineWidget& widget, const Uuid& uuid, int deltaFrames)
{
    const QRect bar = widget.entityBarRect(uuid);
    const QPoint from(bar.right() - 2, bar.center().y());
    const QPoint to =
        from + QPoint(int(deltaFrames * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);
}

QImage renderWidget(TimelineWidget& widget, const QSize& size = QSize(800, 300))
{
    widget.resize(size);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    return image;
}

int rowCenterY(const TimelineWidget& widget, int row)
{
    // Mirrors the widget layout: 14 px scrollbar, 30 px rows, row 0 at bottom.
    return widget.height() - 14 - 15 - row * 30;
}

bool writePng(const QString& path, const QColor& color)
{
    QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image.save(path, "PNG");
}

// One second of 440 Hz, quiet first half and loud second half so the peak
// envelope has a visible shape. Returns an empty string when ffmpeg is missing.
QString writeToneWav(const QString& path)
{
    QProcess process;
    process.start(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-y"), QStringLiteral("-hide_banner"),
         QStringLiteral("-loglevel"), QStringLiteral("error"),
         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("sine=frequency=440:duration=1"), QStringLiteral("-af"),
         QStringLiteral("volume='if(lt(t,0.5),0.2,6.0)':eval=frame"),
         QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"),
         QStringLiteral("1"), path});
    if (!process.waitForStarted(5000))
        return {};
    if (!process.waitForFinished(30000))
    {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0)
        return {};
    return QFile::exists(path) ? path : QString();
}

bool writeFakeAsset(const QString& projectRoot, const QString& hash,
                    const QString& type, const QString& name,
                    int durationFrames = 0,
                    const QString& audioHash = QString())
{
    const QString directory =
        QDir(projectRoot).filePath(QStringLiteral("assets/") + hash);
    if (!QDir().mkpath(directory))
        return false;

    QFile content(directory + QStringLiteral("/content.bin"));
    if (!content.open(QIODevice::WriteOnly))
        return false;
    content.write("x");
    content.close();

    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"), type);
    metadata.insert(QStringLiteral("name"), name);
    if (durationFrames > 0)
        metadata.insert(QStringLiteral("durationFrames"), durationFrames);
    if (!audioHash.isEmpty())
        metadata.insert(QStringLiteral("audioHash"), audioHash);

    QFile json(directory + QStringLiteral("/asset.json"));
    if (!json.open(QIODevice::WriteOnly))
        return false;
    json.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    json.close();
    return true;
}

bool sendAssetDrop(TimelineWidget& widget, const QString& hash,
                   const QPoint& position)
{
    constexpr char mimeType[] = "application/x-pvforge-asset";
    QMimeData mime;
    mime.setData(QString::fromLatin1(mimeType), hash.toUtf8());
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(&widget, &enter);
    QDropEvent event(QPointF(position), Qt::CopyAction, &mime,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
    return event.isAccepted();
}

} // namespace

class TimelineWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void rowsAreBottomUp();
    void clickEntityBarSelects();
    void rulerClickSetsPlayhead();
    void dragMiddleMovesBoth();
    void dragParentMovesDescendants();
    void dragParentClampsAtZero();
    void dragLeftEdgeTrimsStart();
    void trimStartShiftsKeys();
    void trimEndDropsKeys();
    void trimParentClipsChild();
    void trimParentDeletesChild();
    void trimExpandShiftsKeysAndSourceIn();
    void trimSolverStartCutsSegment();
    void trimSolverEndClamps();
    void trimFailureRestoresSnapshot();
    void solverBandPaints();
    void privateTimelineShowsOnlyChildren();
    void plusButtonRequestsPrivateTimeline();
    void doubleClickInsertsGenericKeys();
    void doubleClickKeepsExistingTracks();
    void keyframeDiamondDragMovesAllTracks();
    void keyframeShiftMultiSelectDrag();
    void keyframeDeleteRemovesSelection();
    void keyframeCopyPasteSameEntity();
    void lockedEntityRejectsKeyframeEdits();
    void solverSegmentRejectsKeyframeMove();
    void dropAssetCreatesEntity();
    void dropOntoOccupiedRowUsesNextRow();
    void dropVideoCreatesAudioEntity();
    void dragEnterRejectsUnknownMime();
    void markersAddEditDelete();
    void markerDragChangesFrame();
    void markerValidationRejects();
    void markerShortcutAdds();
    void waveformPaintsForAudioEntity();
    void waveformMissingAssetPaintsFlat();
};

void TimelineWidgetTest::rowsAreBottomUp()
{
    FakeDocument document;
    const Uuid low = addEntity(document, QStringLiteral("low"), 0, 30, 0);
    const Uuid high = addEntity(document, QStringLiteral("high"), 0, 30, 1);

    TimelineWidget widget(&document);
    const QImage image = renderWidget(widget);
    QVERIFY(!image.isNull());

    const QRect lowBar = widget.entityBarRect(low);
    const QRect highBar = widget.entityBarRect(high);
    QVERIFY(lowBar.isValid());
    QVERIFY(highBar.isValid());
    QVERIFY(lowBar.top() > highBar.top());
    QVERIFY(lowBar.bottom() > highBar.bottom());
}

void TimelineWidgetTest::clickEntityBarSelects()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("clip"), 10, 30, 0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QRect bar = widget.entityBarRect(uuid);
    QVERIFY(bar.isValid());
    QVERIFY(bar.contains(bar.center()));

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, bar.center());
    QCOMPARE(document.selection(), uuid);
    QCOMPARE(document.commitCount, 0);
}

void TimelineWidgetTest::rulerClickSetsPlayhead()
{
    FakeDocument document;
    addEntity(document, QStringLiteral("clip"), 0, 30, 0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    widget.setPixelsPerFrame(4.0);

    const QRect ruler = widget.rulerRect();
    const QPoint click(widget.xFromFrame(37), ruler.center().y());
    QVERIFY(click.x() > 0);

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, click);
    QCOMPARE(widget.playhead(), 37);
    QCOMPARE(document.playhead(), 37);
}

void TimelineWidgetTest::dragMiddleMovesBoth()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("move me"), 10, 30, 0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QRect bar = widget.entityBarRect(uuid);
    QVERIFY(bar.isValid());

    const QPoint from = bar.center();
    const QPoint to = from + QPoint(int(8 * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    const pvf::model::Entity entity = document.project().entities.value(uuid);
    QCOMPARE(entity.start, 18);
    QCOMPARE(entity.end, 38);
    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.commitEntitiesCount, 1);
    QCOMPARE(document.lastCommitUuid, uuid);
    QVERIFY(document.lastCommitUuids.contains(uuid));
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: move entity")));
}

void TimelineWidgetTest::dragParentMovesDescendants()
{
    FakeDocument document;
    const Uuid parent =
        addEntity(document, QStringLiteral("parent"), 10, 20, 0);
    const Uuid child =
        addEntity(document, QStringLiteral("child"), 12, 18, 0, parent);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QRect bar = widget.entityBarRect(parent);
    QVERIFY(bar.isValid());

    const QPoint from = bar.center();
    const QPoint to = from + QPoint(int(5 * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    const pvf::model::Entity movedParent =
        document.project().entities.value(parent);
    const pvf::model::Entity movedChild =
        document.project().entities.value(child);
    QCOMPARE(movedParent.start, 15);
    QCOMPARE(movedParent.end, 25);
    QCOMPARE(movedChild.start, 17);
    QCOMPARE(movedChild.end, 23);

    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.commitEntitiesCount, 1);
    QCOMPARE(document.lastCommitUuids.size(), 2);
    QVERIFY(document.lastCommitUuids.contains(parent));
    QVERIFY(document.lastCommitUuids.contains(child));
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: move entity")));
}

void TimelineWidgetTest::dragParentClampsAtZero()
{
    FakeDocument document;
    const Uuid parent =
        addEntity(document, QStringLiteral("parent"), 10, 20, 0);
    const Uuid child =
        addEntity(document, QStringLiteral("child"), 12, 18, 0, parent);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QRect bar = widget.entityBarRect(parent);
    QVERIFY(bar.isValid());

    const QPoint from = bar.center();
    const QPoint to(from.x() - int(15 * widget.pixelsPerFrame()),
                    from.y());
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    const pvf::model::Entity movedParent =
        document.project().entities.value(parent);
    const pvf::model::Entity movedChild =
        document.project().entities.value(child);
    QCOMPARE(movedParent.start, 0);
    QCOMPARE(movedParent.end, 10);
    QCOMPARE(movedChild.start, 2);
    QCOMPARE(movedChild.end, 8);
    QVERIFY(movedParent.start >= 0);
    QVERIFY(movedChild.start >= 0);
    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.commitEntitiesCount, 1);
}

void TimelineWidgetTest::dragLeftEdgeTrimsStart()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("trim me"), 10, 30, 0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QRect bar = widget.entityBarRect(uuid);
    QVERIFY(bar.isValid());

    const QPoint from(bar.left() + 2, bar.center().y());
    const QPoint to = from + QPoint(int(8 * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    const pvf::model::Entity entity = document.project().entities.value(uuid);
    QCOMPARE(entity.start, 18);
    QCOMPARE(entity.end, 30);
    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.commitChangesCount, 1);
    QCOMPARE(document.lastCommitUuid, uuid);
    QVERIFY(document.lastUpserts.contains(uuid));
    QVERIFY(document.lastDeletes.isEmpty());
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: trim entity")));
}

void TimelineWidgetTest::trimStartShiftsKeys()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("trim"), 0, 30, 0);
    addKey(document, uuid, QStringLiteral("x"), 0, 0.0);
    addKey(document, uuid, QStringLiteral("x"), 5, 5.0);
    addKey(document, uuid, QStringLiteral("x"), 10, 10.0);
    addKey(document, uuid, QStringLiteral("x"), 20, 20.0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    trimLeftEdge(widget, uuid, 10);

    const pvf::model::Entity entity = document.project().entities.value(uuid);
    QCOMPARE(entity.start, 10);
    QCOMPARE(entity.end, 30);
    QCOMPARE(entity.duration(), 20);

    const auto keys = entity.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(keys.size(), 2);
    QCOMPARE(keys.at(0).frame, 0);
    QCOMPARE(keys.at(0).value, 10.0);
    QCOMPARE(keys.at(1).frame, 10);
    QCOMPARE(keys.at(1).value, 20.0);
    QCOMPARE(document.commitChangesCount, 1);
    QVERIFY(document.lastUpserts.contains(uuid));
    QVERIFY(document.lastDeletes.isEmpty());
}

void TimelineWidgetTest::trimEndDropsKeys()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("trim"), 0, 30, 0);
    addKey(document, uuid, QStringLiteral("x"), 0, 0.0);
    addKey(document, uuid, QStringLiteral("x"), 5, 5.0);
    addKey(document, uuid, QStringLiteral("x"), 10, 10.0);
    addKey(document, uuid, QStringLiteral("x"), 20, 20.0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    trimRightEdge(widget, uuid, -10);

    const pvf::model::Entity entity = document.project().entities.value(uuid);
    QCOMPARE(entity.start, 0);
    QCOMPARE(entity.end, 20);
    QCOMPARE(entity.duration(), 20);

    const auto keys = entity.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(keys.size(), 3);
    QCOMPARE(keys.at(0).frame, 0);
    QCOMPARE(keys.at(1).frame, 5);
    QCOMPARE(keys.at(2).frame, 10);
    QCOMPARE(document.commitChangesCount, 1);
}

void TimelineWidgetTest::trimParentClipsChild()
{
    FakeDocument document;
    const Uuid parent =
        addEntity(document, QStringLiteral("parent"), 10, 30, 0);
    const Uuid child = addEntity(document, QStringLiteral("child"), 12, 28, 0,
                                 parent);
    addKey(document, child, QStringLiteral("x"), 0, 0.0);
    addKey(document, child, QStringLiteral("x"), 5, 5.0);
    addKey(document, child, QStringLiteral("x"), 10, 10.0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    trimLeftEdge(widget, parent, 10);

    const pvf::model::Entity movedParent =
        document.project().entities.value(parent);
    const pvf::model::Entity movedChild =
        document.project().entities.value(child);
    QCOMPARE(movedParent.start, 20);
    QCOMPARE(movedParent.end, 30);
    QCOMPARE(movedChild.start, 20);
    QCOMPARE(movedChild.end, 28);

    const auto keys = movedChild.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.at(0).frame, 2);
    QCOMPARE(keys.at(0).value, 10.0);

    QCOMPARE(document.commitChangesCount, 1);
    QCOMPARE(document.lastUpserts.size(), 2);
    QVERIFY(document.lastUpserts.contains(parent));
    QVERIFY(document.lastUpserts.contains(child));
    QVERIFY(document.lastDeletes.isEmpty());
}

void TimelineWidgetTest::trimParentDeletesChild()
{
    FakeDocument document;
    const Uuid parent =
        addEntity(document, QStringLiteral("parent"), 10, 30, 0);
    const Uuid child = addEntity(document, QStringLiteral("child"), 12, 18, 0,
                                 parent);
    const Uuid grandchild = addEntity(document, QStringLiteral("grandchild"),
                                      13, 17, 0, child);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    trimLeftEdge(widget, parent, 10);

    const pvf::model::Entity movedParent =
        document.project().entities.value(parent);
    QCOMPARE(movedParent.start, 20);
    QCOMPARE(movedParent.end, 30);
    QCOMPARE(document.commitChangesCount, 1);
    QCOMPARE(document.lastUpserts.size(), 1);
    QVERIFY(document.lastUpserts.contains(parent));
    QCOMPARE(document.lastDeletes.size(), 2);
    QVERIFY(document.lastDeletes.contains(child));
    QVERIFY(document.lastDeletes.contains(grandchild));
    QVERIFY(!document.project().entities.contains(child));
    QVERIFY(!document.project().entities.contains(grandchild));
}

void TimelineWidgetTest::trimExpandShiftsKeysAndSourceIn()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("expand"), 10, 20, 0);
    pvf::model::Entity& entity = document.mutableProject().entities[uuid];
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Video;
    entity.content.asset.sourceIn = 3;
    addKey(document, uuid, QStringLiteral("x"), 0, 0.0);
    addKey(document, uuid, QStringLiteral("x"), 2, 2.0);
    addKey(document, uuid, QStringLiteral("x"), 5, 5.0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    trimLeftEdge(widget, uuid, -5);

    const pvf::model::Entity expanded =
        document.project().entities.value(uuid);
    QCOMPARE(expanded.start, 5);
    QCOMPARE(expanded.end, 20);
    QCOMPARE(expanded.duration(), 15);
    QCOMPARE(expanded.content.asset.sourceIn, 0);

    const auto keys = expanded.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(keys.size(), 3);
    QCOMPARE(keys.at(0).frame, 5);
    QCOMPARE(keys.at(1).frame, 7);
    QCOMPARE(keys.at(2).frame, 10);
    QCOMPARE(document.commitChangesCount, 1);
}

void TimelineWidgetTest::trimSolverStartCutsSegment()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("solver"), 0, 30, 0);
    pvf::model::SolverSegment solver;
    solver.id = pvf::model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 5;
    solver.endFrame = 20;
    document.mutableProject().entities[uuid].solvers.append(solver);
    addKey(document, uuid, QStringLiteral("x"), 5, 42.0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    trimLeftEdge(widget, uuid, 10);

    const pvf::model::Entity entity = document.project().entities.value(uuid);
    QCOMPARE(entity.start, 10);
    QCOMPARE(entity.end, 30);
    QVERIFY(entity.solvers.isEmpty());

    const auto keys = entity.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.at(0).frame, 0);
    QCOMPARE(keys.at(0).value, 42.0);
    QCOMPARE(document.commitChangesCount, 1);
}

void TimelineWidgetTest::trimSolverEndClamps()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("solver"), 0, 30, 0);
    pvf::model::SolverSegment solver;
    solver.id = pvf::model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 5;
    solver.endFrame = 25;
    document.mutableProject().entities[uuid].solvers.append(solver);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    trimRightEdge(widget, uuid, -10);

    const pvf::model::Entity entity = document.project().entities.value(uuid);
    QCOMPARE(entity.start, 0);
    QCOMPARE(entity.end, 20);
    QCOMPARE(entity.solvers.size(), 1);
    QCOMPARE(entity.solvers.at(0).startFrame, 5);
    QCOMPARE(entity.solvers.at(0).endFrame, 19);
    QCOMPARE(document.commitChangesCount, 1);
}

void TimelineWidgetTest::trimFailureRestoresSnapshot()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("trim"), 0, 30, 0);
    addKey(document, uuid, QStringLiteral("x"), 0, 0.0);
    addKey(document, uuid, QStringLiteral("x"), 5, 5.0);
    addKey(document, uuid, QStringLiteral("x"), 10, 10.0);
    addKey(document, uuid, QStringLiteral("x"), 20, 20.0);
    document.failCommitChanges = true;

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    QSignalSpy spy(&widget, &TimelineWidget::statusMessage);
    trimLeftEdge(widget, uuid, 10);

    const pvf::model::Entity entity = document.project().entities.value(uuid);
    QCOMPARE(entity.start, 0);
    QCOMPARE(entity.end, 30);
    const auto keys = entity.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(keys.size(), 4);
    QCOMPARE(keys.at(0).frame, 0);
    QCOMPARE(keys.at(1).frame, 5);
    QCOMPARE(keys.at(2).frame, 10);
    QCOMPARE(keys.at(3).frame, 20);
    QCOMPARE(document.commitChangesCount, 1);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(
        QStringLiteral("forced failure")));
}

void TimelineWidgetTest::solverBandPaints()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("A"), 0, 60, 0);

    pvf::model::SolverSegment solver;
    solver.id = pvf::model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 10;
    solver.endFrame = 30;
    document.mutableProject().entities[uuid].solvers.append(solver);

    TimelineWidget widget(&document);
    const QImage image = renderWidget(widget);
    QVERIFY(!image.isNull());

    const QRect bar = widget.entityBarRect(uuid);
    QVERIFY(bar.isValid());
    QVERIFY(bar.width() >= 100);

    const int x = widget.xFromFrame(20);
    const QColor upper = image.pixelColor(x, bar.top() + 4);
    const QColor lower = image.pixelColor(x, bar.bottom() - 2);
    QVERIFY(upper.isValid());
    QVERIFY(lower.isValid());
    QVERIFY(upper != lower);
}

void TimelineWidgetTest::privateTimelineShowsOnlyChildren()
{
    FakeDocument document;
    const Uuid parent = addEntity(document, QStringLiteral("parent"), 0, 90, 0);
    const Uuid child = addEntity(document, QStringLiteral("child"), 10, 20, 0,
                                 parent);
    const Uuid other = addEntity(document, QStringLiteral("other"), 0, 30, 0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    QVERIFY(widget.entityBarRect(parent).isValid());
    QVERIFY(widget.entityBarRect(other).isValid());
    QVERIFY(!widget.entityBarRect(child).isValid());
    QVERIFY(widget.breadcrumbRect().isEmpty());

    widget.setParentUuid(parent);
    QCOMPARE(widget.parentUuid(), parent);
    QVERIFY(widget.entityBarRect(child).isValid());
    QVERIFY(!widget.entityBarRect(parent).isValid());
    QVERIFY(!widget.entityBarRect(other).isValid());
    QVERIFY(widget.visibleEntities() == QVector<Uuid>{child});
    QVERIFY(!widget.breadcrumbRect().isEmpty());

    // The overlay's widget can still return to the root timeline.
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      widget.breadcrumbRect().center());
    QVERIFY(widget.parentUuid().isEmpty());
    QVERIFY(widget.entityBarRect(parent).isValid());
    QVERIFY(!widget.entityBarRect(child).isValid());
}

void TimelineWidgetTest::plusButtonRequestsPrivateTimeline()
{
    FakeDocument document;
    const Uuid parent = addEntity(document, QStringLiteral("parent"), 0, 90, 0);
    const Uuid child = addEntity(document, QStringLiteral("child"), 10, 20, 0,
                                 parent);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QRect bar = widget.entityBarRect(parent);
    QVERIFY(bar.isValid());
    QVERIFY(widget.breadcrumbRect().isEmpty());

    const QRect addRect = widget.addButtonRect(parent);
    QVERIFY(addRect.isValid());
    QVERIFY(bar.contains(addRect.center()));

    QSignalSpy spy(&widget, &TimelineWidget::privateTimelineRequested);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      addRect.center());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), parent);
    QCOMPARE(document.selection(), parent);
    QVERIFY(widget.parentUuid().isEmpty());
    QCOMPARE(document.commitCount, 0);

    // Double-clicking the bar body writes keys instead of opening the overlay.
    spy.clear();
    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier, bar.center());
    QCOMPARE(spy.count(), 0);
    QVERIFY(widget.parentUuid().isEmpty());
    QVERIFY(widget.entityBarRect(child).isEmpty());
    QVERIFY(widget.breadcrumbRect().isEmpty());
}

void TimelineWidgetTest::doubleClickInsertsGenericKeys()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("clip"), 10, 40, 0);
    pvf::model::Entity& entity = document.mutableProject().entities[uuid];
    entity.transform.x = 5.0;
    entity.transform.y = 7.0;
    entity.transform.rotation = 30.0;
    entity.transform.scaleX = 2.0;
    entity.transform.scaleY = 0.5;
    entity.transform.opacity = 0.8;

    TimelineWidget widget(&document);
    widget.resize(800, 300);

    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier,
                       diamondPoint(widget, uuid, 20));

    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.lastCommitUuid, uuid);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: set keyframes")));

    const pvf::model::Entity written =
        document.project().entities.value(uuid);
    QCOMPARE(written.tracks.size(), 6);
    const QStringList expected = {QStringLiteral("x"), QStringLiteral("y"),
                                  QStringLiteral("rotation"),
                                  QStringLiteral("scaleX"),
                                  QStringLiteral("scaleY"),
                                  QStringLiteral("opacity")};
    for (const QString& trackName : expected)
    {
        const auto keys = written.tracks.value(trackName).keys;
        QCOMPARE(keys.size(), 1);
        QCOMPARE(keys.at(0).frame, 10);
    }
    QCOMPARE(written.tracks.value(QStringLiteral("x")).keys.at(0).value, 5.0);
    QCOMPARE(written.tracks.value(QStringLiteral("y")).keys.at(0).value, 7.0);
    QCOMPARE(written.tracks.value(QStringLiteral("rotation")).keys.at(0).value,
             30.0);
    QCOMPARE(written.tracks.value(QStringLiteral("scaleX")).keys.at(0).value,
             2.0);
    QCOMPARE(written.tracks.value(QStringLiteral("scaleY")).keys.at(0).value,
             0.5);
    QCOMPARE(written.tracks.value(QStringLiteral("opacity")).keys.at(0).value,
             0.8);
}

void TimelineWidgetTest::doubleClickKeepsExistingTracks()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("clip"), 0, 30, 0);
    addKey(document, uuid, QStringLiteral("x"), 0, 0.0);
    addKey(document, uuid, QStringLiteral("x"), 10, 100.0);
    addKey(document, uuid, QStringLiteral("y"), 0, 5.0);

    TimelineWidget widget(&document);
    widget.resize(800, 300);

    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier,
                       diamondPoint(widget, uuid, 5));

    QCOMPARE(document.commitCount, 1);
    const pvf::model::Entity written =
        document.project().entities.value(uuid);
    QCOMPARE(written.tracks.size(), 2);
    QVERIFY(!written.tracks.contains(QStringLiteral("rotation")));
    QVERIFY(!written.tracks.contains(QStringLiteral("scaleX")));

    const auto xKeys = written.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(xKeys.size(), 3);
    QCOMPARE(xKeys.at(1).frame, 5);
    QCOMPARE(xKeys.at(1).value, 50.0);

    const auto yKeys = written.tracks.value(QStringLiteral("y")).keys;
    QCOMPARE(yKeys.size(), 2);
    QCOMPARE(yKeys.at(1).frame, 5);
    QCOMPARE(yKeys.at(1).value, 5.0);
}

void TimelineWidgetTest::keyframeDiamondDragMovesAllTracks()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("clip"), 0, 30, 0);
    addKey(document, uuid, QStringLiteral("x"), 5, 10.0);
    addKey(document, uuid, QStringLiteral("x"), 10, 20.0);
    addKey(document, uuid, QStringLiteral("y"), 5, 30.0);
    addKey(document, uuid, QStringLiteral("opacity"), 10, 0.5);
    document.setSelection(uuid);

    TimelineWidget widget(&document);
    widget.resize(800, 300);

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      diamondPoint(widget, uuid, 5));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::ControlModifier,
                      diamondPoint(widget, uuid, 10));

    const QPoint from = diamondPoint(widget, uuid, 5);
    const QPoint to = from + QPoint(int(3 * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    QCOMPARE(document.commitCount, 1);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: move keyframes")));

    const pvf::model::Entity moved = document.project().entities.value(uuid);
    QSet<int> xFrames;
    for (const auto& key : moved.tracks.value(QStringLiteral("x")).keys)
        xFrames.insert(key.frame);
    QCOMPARE(xFrames, (QSet<int>{8, 13}));

    const auto yKeys = moved.tracks.value(QStringLiteral("y")).keys;
    QCOMPARE(yKeys.size(), 1);
    QCOMPARE(yKeys.at(0).frame, 8);
    QCOMPARE(yKeys.at(0).value, 30.0);

    const auto opacityKeys = moved.tracks.value(QStringLiteral("opacity")).keys;
    QCOMPARE(opacityKeys.size(), 1);
    QCOMPARE(opacityKeys.at(0).frame, 13);
    QCOMPARE(opacityKeys.at(0).value, 0.5);
}

void TimelineWidgetTest::keyframeShiftMultiSelectDrag()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("clip"), 0, 40, 0);
    addKey(document, uuid, QStringLiteral("x"), 4, 1.0);
    addKey(document, uuid, QStringLiteral("y"), 12, 2.0);
    document.setSelection(uuid);

    TimelineWidget widget(&document);
    widget.resize(800, 300);

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      diamondPoint(widget, uuid, 4));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::ShiftModifier,
                      diamondPoint(widget, uuid, 12));

    const QPoint from = diamondPoint(widget, uuid, 4);
    const QPoint to = from + QPoint(int(-2 * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    QCOMPARE(document.commitCount, 1);
    const pvf::model::Entity moved = document.project().entities.value(uuid);
    QCOMPARE(moved.tracks.value(QStringLiteral("x")).keys.at(0).frame, 2);
    QCOMPARE(moved.tracks.value(QStringLiteral("y")).keys.at(0).frame, 10);
}

void TimelineWidgetTest::keyframeDeleteRemovesSelection()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("clip"), 0, 30, 0);
    addKey(document, uuid, QStringLiteral("x"), 5, 1.0);
    addKey(document, uuid, QStringLiteral("x"), 10, 2.0);
    addKey(document, uuid, QStringLiteral("x"), 20, 3.0);
    addKey(document, uuid, QStringLiteral("y"), 5, 4.0);
    addKey(document, uuid, QStringLiteral("opacity"), 10, 0.5);
    document.setSelection(uuid);

    TimelineWidget widget(&document);
    widget.resize(800, 300);

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      diamondPoint(widget, uuid, 5));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::ControlModifier,
                      diamondPoint(widget, uuid, 10));
    QTest::keyClick(&widget, Qt::Key_Delete);

    QCOMPARE(document.commitCount, 1);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: delete keyframes")));

    const pvf::model::Entity deleted = document.project().entities.value(uuid);
    const auto xKeys = deleted.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(xKeys.size(), 1);
    QCOMPARE(xKeys.at(0).frame, 20);
    QVERIFY(!deleted.tracks.contains(QStringLiteral("y")));
    QVERIFY(!deleted.tracks.contains(QStringLiteral("opacity")));
}

void TimelineWidgetTest::keyframeCopyPasteSameEntity()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("clip"), 0, 60, 0);
    addKey(document, uuid, QStringLiteral("x"), 5, 42.0);
    addKey(document, uuid, QStringLiteral("y"), 5, 7.0);
    addKey(document, uuid, QStringLiteral("x"), 10, 99.0);
    document.setSelection(uuid);

    TimelineWidget widget(&document);
    widget.resize(800, 300);

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      diamondPoint(widget, uuid, 5));
    QTest::keyClick(&widget, Qt::Key_C, Qt::ControlModifier);
    widget.setPlayhead(20);
    QTest::keyClick(&widget, Qt::Key_V, Qt::ControlModifier);

    QCOMPARE(document.commitCount, 1);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: paste keyframes")));

    const pvf::model::Entity pasted = document.project().entities.value(uuid);
    QCOMPARE(pasted.tracks.value(QStringLiteral("x")).keys.size(), 3);
    QCOMPARE(pasted.tracks.value(QStringLiteral("y")).keys.size(), 2);

    double pastedX = 0.0;
    for (const auto& key : pasted.tracks.value(QStringLiteral("x")).keys)
    {
        if (key.frame == 20)
            pastedX = key.value;
    }
    QCOMPARE(pastedX, 42.0);

    double pastedY = 0.0;
    for (const auto& key : pasted.tracks.value(QStringLiteral("y")).keys)
    {
        if (key.frame == 20)
            pastedY = key.value;
    }
    QCOMPARE(pastedY, 7.0);
}

void TimelineWidgetTest::lockedEntityRejectsKeyframeEdits()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("locked"), 0, 30, 0);
    document.mutableProject().entities[uuid].locked = true;
    addKey(document, uuid, QStringLiteral("x"), 5, 1.0);
    document.setSelection(uuid);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    QSignalSpy spy(&widget, &TimelineWidget::statusMessage);

    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier,
                       diamondPoint(widget, uuid, 10));
    QCOMPARE(document.commitCount, 0);
    QCOMPARE(document.project()
                 .entities.value(uuid)
                 .tracks.value(QStringLiteral("x"))
                 .keys.size(),
             1);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("锁定")));

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      diamondPoint(widget, uuid, 5));
    QCOMPARE(document.commitCount, 0);
    QCOMPARE(spy.count(), 2);
}

void TimelineWidgetTest::solverSegmentRejectsKeyframeMove()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("solver"), 0, 60, 0);
    pvf::model::SolverSegment solver;
    solver.id = pvf::model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 10;
    solver.endFrame = 30;
    document.mutableProject().entities[uuid].solvers.append(solver);
    addKey(document, uuid, QStringLiteral("x"), 5, 1.0);
    document.setSelection(uuid);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    QSignalSpy spy(&widget, &TimelineWidget::statusMessage);

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      diamondPoint(widget, uuid, 5));

    const QPoint from = diamondPoint(widget, uuid, 5);
    const QPoint to = from + QPoint(int(15 * widget.pixelsPerFrame()), 0);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    QCOMPARE(document.commitCount, 0);
    const pvf::model::Entity kept = document.project().entities.value(uuid);
    QCOMPARE(kept.tracks.value(QStringLiteral("x")).keys.size(), 1);
    QCOMPARE(kept.tracks.value(QStringLiteral("x")).keys.at(0).frame, 5);
    QCOMPARE(kept.tracks.value(QStringLiteral("x")).keys.at(0).value, 1.0);
    QVERIFY(spy.count() >= 1);
    QVERIFY(spy.at(spy.count() - 1).at(0).toString().contains(
        QStringLiteral("求解段")));
}

void TimelineWidgetTest::dropAssetCreatesEntity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());

    const QString path = directory.path() + QStringLiteral("/green.png");
    QVERIFY(writePng(path, Qt::green));
    QString error;
    const auto stored = document.assets().importFile(path, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QPoint dropPosition(widget.xFromFrame(30), rowCenterY(widget, 0));
    QVERIFY(sendAssetDrop(widget, stored->hash, dropPosition));

    QCOMPARE(document.commitEntitiesCount, 1);
    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.lastCommitUuids.size(), 1);
    QCOMPARE(int(document.project().entities.size()), 1);

    const Uuid uuid = document.lastCommitUuid;
    const pvf::model::Entity entity =
        document.project().entities.value(uuid);
    QCOMPARE(entity.name, QStringLiteral("green.png"));
    QCOMPARE(entity.parentUuid, Uuid());
    QCOMPARE(entity.start, 30);
    QCOMPARE(entity.end, 60);
    QCOMPARE(entity.row, 0);
    QCOMPARE(entity.content.kind, ContentKind::Asset);
    QCOMPARE(entity.content.asset.hash, stored->hash);
    QCOMPARE(entity.content.asset.type, AssetType::Image);
    QCOMPARE(entity.content.asset.sourceIn, 0);
    QCOMPARE(entity.content.asset.volume, 1.0);
    QCOMPARE(document.selection(), uuid);
}

void TimelineWidgetTest::dropOntoOccupiedRowUsesNextRow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());
    const Uuid existing =
        addEntity(document, QStringLiteral("existing"), 20, 40, 0);

    const QString path = directory.path() + QStringLiteral("/green.png");
    QVERIFY(writePng(path, Qt::green));
    QString error;
    const auto stored = document.assets().importFile(path, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QPoint dropPosition(widget.xFromFrame(30), rowCenterY(widget, 0));
    QVERIFY(sendAssetDrop(widget, stored->hash, dropPosition));

    QCOMPARE(document.commitEntitiesCount, 1);
    QCOMPARE(int(document.project().entities.size()), 2);
    const Uuid created = document.lastCommitUuid;
    QCOMPARE(document.project().entities.value(created).row, 1);
    QCOMPARE(document.project().entities.value(existing).row, 0);
}

void TimelineWidgetTest::dropVideoCreatesAudioEntity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeDocument document(directory.path());

    const QString videoHash(64, QLatin1Char('a'));
    const QString audioHash(64, QLatin1Char('b'));
    QVERIFY(writeFakeAsset(directory.path(), videoHash,
                           QStringLiteral("video"),
                           QStringLiteral("clip.mp4"), 45, audioHash));

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QPoint dropPosition(widget.xFromFrame(10), rowCenterY(widget, 0));
    QVERIFY(sendAssetDrop(widget, videoHash, dropPosition));

    QCOMPARE(document.commitEntitiesCount, 1);
    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.lastCommitUuids.size(), 2);

    const Uuid videoUuid = document.lastCommitUuids.at(0);
    const Uuid audioUuid = document.lastCommitUuids.at(1);
    const pvf::model::Entity video =
        document.project().entities.value(videoUuid);
    const pvf::model::Entity audio =
        document.project().entities.value(audioUuid);

    QCOMPARE(video.name, QStringLiteral("clip.mp4"));
    QCOMPARE(video.start, 10);
    QCOMPARE(video.end, 55);
    QCOMPARE(video.row, 0);
    QCOMPARE(video.content.asset.hash, videoHash);
    QCOMPARE(video.content.asset.type, AssetType::Video);

    QCOMPARE(audio.name, QStringLiteral("clip.mp4 音频"));
    QCOMPARE(audio.start, 10);
    QCOMPARE(audio.end, 55);
    QCOMPARE(audio.row, 1);
    QCOMPARE(audio.content.asset.hash, audioHash);
    QCOMPARE(audio.content.asset.type, AssetType::Audio);
    QCOMPARE(document.selection(), videoUuid);
}

void TimelineWidgetTest::dragEnterRejectsUnknownMime()
{
    FakeDocument document;
    TimelineWidget widget(&document);
    widget.resize(800, 300);

    QMimeData unknown;
    unknown.setText(QStringLiteral("plain text"));
    QDragEnterEvent unknownEvent(QPoint(10, 10), Qt::CopyAction, &unknown,
                                 Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &unknownEvent);
    QVERIFY(!unknownEvent.isAccepted());

    constexpr char mimeType[] = "application/x-pvforge-asset";
    QMimeData asset;
    asset.setData(QString::fromLatin1(mimeType), QByteArray("hash"));
    QDragEnterEvent assetEvent(QPoint(10, 10), Qt::CopyAction, &asset,
                               Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &assetEvent);
    QVERIFY(assetEvent.isAccepted());
}

void TimelineWidgetTest::markersAddEditDelete()
{
    FakeDocument document;
    TimelineWidget widget(&document);
    widget.resize(800, 300);
    widget.setPlayhead(42);

    widget.addMarkerAtPlayhead();
    QCOMPARE(document.commitCount, 1);
    QCOMPARE(document.project().markers.size(), 1);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: add marker")));

    const pvf::model::Marker added = document.project().markers.at(0);
    QCOMPARE(added.frame, 42);
    QVERIFY(!added.id.isEmpty());
    QVERIFY(!added.title.isEmpty());

    pvf::model::Marker edited = added;
    edited.title = QStringLiteral("副歌");
    edited.color = QStringLiteral("#00ff00");
    edited.note = QStringLiteral("段落备注");
    edited.tags = {QStringLiteral("music"), QStringLiteral("chorus")};

    QString error;
    QVERIFY2(widget.updateMarker(added.id, edited, &error),
             qPrintable(error));
    QCOMPARE(document.commitCount, 2);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: edit marker")));

    const pvf::model::Marker stored = document.project().markers.at(0);
    QCOMPARE(stored.id, added.id);
    QCOMPARE(stored.title, QStringLiteral("副歌"));
    QCOMPARE(stored.color, QStringLiteral("#00ff00"));
    QCOMPARE(stored.note, QStringLiteral("段落备注"));
    QCOMPARE(stored.tags.size(), 2);
    QCOMPARE(stored.frame, 42);

    QVERIFY2(widget.deleteMarker(added.id, &error), qPrintable(error));
    QCOMPARE(document.commitCount, 3);
    QVERIFY(document.project().markers.isEmpty());
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: delete marker")));
}

void TimelineWidgetTest::markerDragChangesFrame()
{
    FakeDocument document;
    pvf::model::Marker marker;
    marker.id = pvf::model::newUuid();
    marker.frame = 10;
    marker.title = QStringLiteral("intro");
    marker.color = QStringLiteral("#ffbe3c");
    document.mutableProject().markers.append(marker);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    widget.setPixelsPerFrame(4.0);

    const QRect lane = widget.markerLaneRect();
    const QPoint from(widget.xFromFrame(10), lane.center().y());
    const QPoint to(widget.xFromFrame(30), lane.center().y());
    QCOMPARE(widget.markerIdAt(from), marker.id);

    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&widget, to, 10);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, to);

    QCOMPARE(document.project().markers.size(), 1);
    QCOMPARE(document.project().markers.at(0).frame, 30);
    QCOMPARE(document.commitCount, 1);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: move marker")));
}

void TimelineWidgetTest::markerValidationRejects()
{
    QString error;
    pvf::model::Marker marker;
    marker.id = pvf::model::newUuid();
    marker.title = QStringLiteral("   ");
    QVERIFY(!pvf::next::normalizeMarker(marker, &error));
    QVERIFY(!error.isEmpty());

    marker.title = QStringLiteral("ok");
    marker.note = QString(4097, QLatin1Char('x'));
    QVERIFY(!pvf::next::normalizeMarker(marker, &error));

    marker.note.clear();
    for (int i = 0; i < 33; ++i)
        marker.tags.append(QStringLiteral("tag%1").arg(i));
    QVERIFY(!pvf::next::normalizeMarker(marker, &error));

    marker.tags = {QString(65, QLatin1Char('a'))};
    QVERIFY(!pvf::next::normalizeMarker(marker, &error));

    FakeDocument document;
    TimelineWidget widget(&document);
    pvf::model::Marker invalid;
    invalid.title.clear();
    QVERIFY(!widget.insertMarker(invalid, &error));
    QCOMPARE(document.commitCount, 0);
    QVERIFY(document.project().markers.isEmpty());
}

void TimelineWidgetTest::markerShortcutAdds()
{
    FakeDocument document;
    document.setPlayhead(12);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    widget.setFocus();

    QTest::keyClick(&widget, Qt::Key_M);
    QCOMPARE(document.project().markers.size(), 1);
    QCOMPARE(document.project().markers.at(0).frame, 12);
    QCOMPARE(document.commitCount, 1);
}

void TimelineWidgetTest::waveformPaintsForAudioEntity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString wav = writeToneWav(directory.filePath(QStringLiteral("tone.wav")));
    if (wav.isEmpty())
        QSKIP("ffmpeg 不可用");

    FakeDocument document(directory.path());
    QString error;
    const auto asset = document.assets().importFile(wav, &error);
    QVERIFY2(asset.has_value(), qPrintable(error));

    const Uuid uuid = addEntity(document, QStringLiteral("tone"), 0, 30, 0);
    pvf::model::Entity& entity = document.mutableProject().entities[uuid];
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Audio;
    entity.content.asset.hash = asset->hash;
    entity.content.asset.sourceIn = 0;

    TimelineWidget widget(&document);
    const QImage image = renderWidget(widget);
    QVERIFY(!image.isNull());
    QCOMPARE(widget.waveformCacheSize(), 1);

    const QRect bar = widget.entityBarRect(uuid);
    QVERIFY(bar.isValid());
    QVERIFY(bar.width() > 40);

    // Quiet first half: y = center - 5 stays on the bar fill. Loud second
    // half: the waveform reaches that row, so the pixels differ.
    const QColor quiet = image.pixelColor(bar.left() + bar.width() / 4,
                                          bar.center().y() - 5);
    const QColor loud = image.pixelColor(bar.right() - bar.width() / 4,
                                         bar.center().y() - 5);
    QVERIFY(quiet.isValid());
    QVERIFY(loud.isValid());
    QVERIFY(quiet != loud);
}

void TimelineWidgetTest::waveformMissingAssetPaintsFlat()
{
    FakeDocument document;
    const Uuid uuid = addEntity(document, QStringLiteral("missing"), 0, 30, 0);
    pvf::model::Entity& entity = document.mutableProject().entities[uuid];
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Audio;
    entity.content.asset.hash = QStringLiteral("deadbeef");

    TimelineWidget widget(&document);
    const QImage image = renderWidget(widget);
    QVERIFY(!image.isNull());
    QCOMPARE(widget.waveformCacheSize(), 0);

    const QRect bar = widget.entityBarRect(uuid);
    QVERIFY(bar.isValid());
    QVERIFY(image.pixelColor(bar.center()).isValid());
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    TimelineWidgetTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "timeline_tests.moc"
