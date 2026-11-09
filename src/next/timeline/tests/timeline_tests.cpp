#include "next/core/Document.h"
#include "next/timeline/TimelineWidget.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
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
    FakeDocument()
        : Document(nullptr)
        , m_assets(QDir::tempPath())
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
    Uuid lastCommitUuid;
    QVector<Uuid> lastCommitUuids;
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

QImage renderWidget(TimelineWidget& widget, const QSize& size = QSize(800, 300))
{
    widget.resize(size);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    return image;
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
    void solverBandPaints();
    void privateTimelineShowsOnlyChildren();
    void doubleClickAndBreadcrumb();
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
    QCOMPARE(document.lastCommitUuid, uuid);
    QVERIFY(document.lastCommitMessage.startsWith(
        QStringLiteral("pvf: trim entity")));
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

    widget.setParentUuid(parent);
    QCOMPARE(widget.parentUuid(), parent);
    QVERIFY(widget.entityBarRect(child).isValid());
    QVERIFY(!widget.entityBarRect(parent).isValid());
    QVERIFY(!widget.entityBarRect(other).isValid());
    QVERIFY(widget.visibleEntities() == QVector<Uuid>{child});
}

void TimelineWidgetTest::doubleClickAndBreadcrumb()
{
    FakeDocument document;
    const Uuid parent = addEntity(document, QStringLiteral("parent"), 0, 90, 0);
    const Uuid child = addEntity(document, QStringLiteral("child"), 10, 20, 0,
                                 parent);

    TimelineWidget widget(&document);
    widget.resize(800, 300);
    const QRect bar = widget.entityBarRect(parent);
    QVERIFY(bar.isValid());

    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier, bar.center());
    QCOMPARE(widget.parentUuid(), parent);
    QVERIFY(widget.entityBarRect(child).isValid());

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      widget.breadcrumbRect().center());
    QVERIFY(widget.parentUuid().isEmpty());
    QVERIFY(widget.entityBarRect(parent).isValid());
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    TimelineWidgetTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "timeline_tests.moc"
