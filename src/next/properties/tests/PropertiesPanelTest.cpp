#include "next/properties/PropertiesPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

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
        Q_UNUSED(message);
        if (m_failCommit)
        {
            if (error)
                *error = QStringLiteral("fake commit failure");
            return false;
        }
        ++m_entityCommits;
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

    Uuid addTextEntity(const QString& name, const QString& text)
    {
        const Uuid uuid = addEntity(name, 0, 60);
        model::Entity& entity = m_project.entities[uuid];
        entity.content.kind = ContentKind::Text;
        entity.content.text.text = text;
        return uuid;
    }

    model::Entity& entity(const Uuid& uuid) { return m_project.entities[uuid]; }

    void setFailCommit(bool fail) { m_failCommit = fail; }
    int entityCommits() const { return m_entityCommits; }
    int projectCommits() const { return m_projectCommits; }

private:
    QTemporaryDir m_tempDir;
    assets::AssetStore m_assets;
    model::Project m_project;
    int m_playhead = 0;
    Uuid m_selection;
    int m_entityCommits = 0;
    int m_projectCommits = 0;
    bool m_failCommit = false;
};

template <typename T>
T* findWidget(QWidget& panel, const char* name)
{
    return panel.findChild<T*>(QString::fromLatin1(name));
}

bool fireEditingFinished(QWidget* widget)
{
    return QMetaObject::invokeMethod(widget, "editingFinished");
}

} // namespace

class PropertiesPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void showsBasicFields();
    void renameCommitsOnce();
    void transformEditUpdatesModel();
    void checkboxesWriteBack();
    void textEntityShowsTextControls();
    void solverSectionShowsEffectAndParams();
    void switchingSelectionDoesNotLeak();
    void invalidRangeIsRejected();
};

void PropertiesPanelTest::showsBasicFields()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("标题"), 10, 50);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* name = findWidget<QLineEdit>(panel, "nameEdit");
    auto* start = findWidget<QSpinBox>(panel, "startSpin");
    auto* end = findWidget<QSpinBox>(panel, "endSpin");
    QVERIFY(name);
    QVERIFY(start);
    QVERIFY(end);
    QCOMPARE(name->text(), QStringLiteral("标题"));
    QCOMPARE(start->value(), 10);
    QCOMPARE(end->value(), 50);
}

void PropertiesPanelTest::renameCommitsOnce()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("标题"), 0, 60);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* name = findWidget<QLineEdit>(panel, "nameEdit");
    QVERIFY(name);
    name->setText(QStringLiteral("新名字"));
    QVERIFY(fireEditingFinished(name));

    QCOMPARE(document.entity(uuid).name, QStringLiteral("新名字"));
    QCOMPARE(document.entityCommits(), 1);

    // Emitting editingFinished again without a change must not commit again.
    QVERIFY(fireEditingFinished(name));
    QCOMPARE(document.entityCommits(), 1);
}

void PropertiesPanelTest::transformEditUpdatesModel()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 0, 60);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* x = findWidget<QDoubleSpinBox>(panel, "transformXSpin");
    QVERIFY(x);
    x->setValue(42.5);
    QVERIFY(fireEditingFinished(x));

    QCOMPARE(document.entity(uuid).transform.x, 42.5);
    QCOMPARE(document.entityCommits(), 1);
}

void PropertiesPanelTest::checkboxesWriteBack()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 0, 60);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* hidden = findWidget<QCheckBox>(panel, "hiddenCheck");
    auto* locked = findWidget<QCheckBox>(panel, "lockedCheck");
    auto* name = findWidget<QLineEdit>(panel, "nameEdit");
    QVERIFY(hidden);
    QVERIFY(locked);
    QVERIFY(name);

    hidden->setChecked(true);
    QCOMPARE(document.entity(uuid).hidden, true);
    QCOMPARE(document.entityCommits(), 1);

    locked->setChecked(true);
    QCOMPARE(document.entity(uuid).locked, true);
    QCOMPARE(document.entityCommits(), 2);
    QVERIFY(!name->isEnabled());
    QVERIFY(locked->isEnabled());

    locked->setChecked(false);
    QCOMPARE(document.entity(uuid).locked, false);
    QCOMPARE(document.entityCommits(), 3);
    QVERIFY(name->isEnabled());
}

void PropertiesPanelTest::textEntityShowsTextControls()
{
    FakeDocument document;
    const Uuid uuid =
        document.addTextEntity(QStringLiteral("文字实体"), QStringLiteral("你好"));

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* textPage = findWidget<QWidget>(panel, "textPage");
    auto* vectorPage = findWidget<QWidget>(panel, "vectorPage");
    auto* text = findWidget<QLineEdit>(panel, "textContentEdit");
    QVERIFY(textPage);
    QVERIFY(vectorPage);
    QVERIFY(text);
    QVERIFY(!textPage->isHidden());
    QVERIFY(vectorPage->isHidden());
    QCOMPARE(text->text(), QStringLiteral("你好"));

    text->setText(QStringLiteral("世界"));
    QVERIFY(fireEditingFinished(text));
    QCOMPARE(document.entity(uuid).content.text.text, QStringLiteral("世界"));
    QCOMPARE(document.entityCommits(), 1);
}

void PropertiesPanelTest::solverSectionShowsEffectAndParams()
{
    FakeDocument document;
    const Uuid ground = document.addEntity(QStringLiteral("地面"), 0, 120);
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 120);

    model::SolverSegment solver;
    solver.id = model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 0;
    solver.endFrame = 59;
    solver.initialVelocity = QPointF(120.0, -240.0);
    solver.params.insert(QStringLiteral("targetUuid"), ground);
    solver.params.insert(QStringLiteral("strength"), 980.0);
    solver.params.insert(QStringLiteral("elasticity"), 0.35);
    document.entity(ball).solvers.append(solver);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* effect = findWidget<QLabel>(panel, "solverEffect0");
    auto* start = findWidget<QSpinBox>(panel, "solverStart0");
    auto* end = findWidget<QSpinBox>(panel, "solverEnd0");
    auto* target = findWidget<QLineEdit>(panel, "solverParam0_targetUuid");
    auto* strength = findWidget<QDoubleSpinBox>(panel, "solverParam0_strength");
    QVERIFY(effect);
    QVERIFY(start);
    QVERIFY(end);
    QVERIFY(target);
    QVERIFY(strength);

    QCOMPARE(effect->text(), solverEffectName(SolverEffect::Gravity));
    QCOMPARE(start->value(), 0);
    QCOMPARE(end->value(), 59);
    QCOMPARE(target->text(), ground);
    QCOMPARE(strength->value(), 980.0);
    QVERIFY(!strength->toolTip().isEmpty());

    strength->setValue(1234.5);
    QVERIFY(fireEditingFinished(strength));
    QCOMPARE(document.entity(ball).solvers.at(0).params.value(QStringLiteral("strength")).toDouble(),
             1234.5);
    QCOMPARE(document.entityCommits(), 1);

    auto* velocityX = findWidget<QDoubleSpinBox>(panel, "solverVelocityX0");
    QVERIFY(velocityX);
    velocityX->setValue(300.0);
    QVERIFY(fireEditingFinished(velocityX));
    QCOMPARE(document.entity(ball).solvers.at(0).initialVelocity.x(), 300.0);
    QCOMPARE(document.entityCommits(), 2);

    start->setValue(10);
    QVERIFY(fireEditingFinished(start));
    QCOMPARE(document.entity(ball).solvers.at(0).startFrame, 10);
    QCOMPARE(document.entityCommits(), 3);
}

void PropertiesPanelTest::switchingSelectionDoesNotLeak()
{
    FakeDocument document;
    const Uuid first = document.addEntity(QStringLiteral("甲"), 0, 10);
    const Uuid second = document.addEntity(QStringLiteral("乙"), 0, 20);
    document.entity(first).transform.x = 5.0;
    document.entity(second).transform.x = 7.0;

    PropertiesPanel panel(&document);
    document.setSelection(first);

    auto* name = findWidget<QLineEdit>(panel, "nameEdit");
    auto* x = findWidget<QDoubleSpinBox>(panel, "transformXSpin");
    QVERIFY(name);
    QVERIFY(x);
    QCOMPARE(name->text(), QStringLiteral("甲"));
    QCOMPARE(x->value(), 5.0);

    // Leave an uncommitted edit in the name field, then switch the selection.
    name->setText(QStringLiteral("甲改"));
    document.setSelection(second);

    QCOMPARE(name->text(), QStringLiteral("乙"));
    QCOMPARE(x->value(), 7.0);

    // A late editingFinished must not write the stale text into the new entity.
    QVERIFY(fireEditingFinished(name));
    QCOMPARE(document.entity(second).name, QStringLiteral("乙"));
    QCOMPARE(document.entity(first).name, QStringLiteral("甲"));
    QCOMPARE(document.entityCommits(), 0);
}

void PropertiesPanelTest::invalidRangeIsRejected()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 10, 50);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* end = findWidget<QSpinBox>(panel, "endSpin");
    QVERIFY(end);
    end->setValue(5);
    QVERIFY(fireEditingFinished(end));

    QCOMPARE(document.entity(uuid).end, 50);
    QCOMPARE(document.entityCommits(), 0);
    QCOMPARE(end->value(), 50);
}

QTEST_MAIN(PropertiesPanelTest)
#include "PropertiesPanelTest.moc"
