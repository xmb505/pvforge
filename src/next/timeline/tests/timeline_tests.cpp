#include "next/core/Document.h"
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
    void doubleClickRequestsPrivateTimeline();
    void dropAssetCreatesEntity();
    void dropOntoOccupiedRowUsesNextRow();
    void dropVideoCreatesAudioEntity();
    void dragEnterRejectsUnknownMime();
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

void TimelineWidgetTest::doubleClickRequestsPrivateTimeline()
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

    QSignalSpy spy(&widget, &TimelineWidget::privateTimelineRequested);
    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier, bar.center());

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), parent);
    QVERIFY(widget.parentUuid().isEmpty());
    QCOMPARE(document.selection(), parent);
    QVERIFY(widget.entityBarRect(parent).isValid());
    QVERIFY(!widget.entityBarRect(child).isValid());
    QVERIFY(widget.breadcrumbRect().isEmpty());
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

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    TimelineWidgetTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "timeline_tests.moc"
