#include "next/properties/PropertiesPanel.h"

#include "model/Easing.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGroupBox>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
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

    Uuid addVectorEntity(const QString& name, int start = 0, int end = 60)
    {
        const Uuid uuid = addEntity(name, start, end);
        model::Entity& entity = m_project.entities[uuid];
        entity.content.kind = ContentKind::Vector;
        return uuid;
    }

    Uuid addAudioEntity(const QString& name, int start = 0, int end = 60)
    {
        const Uuid uuid = addEntity(name, start, end);
        model::Entity& entity = m_project.entities[uuid];
        entity.content.kind = ContentKind::Asset;
        entity.content.asset.type = AssetType::Audio;
        entity.content.asset.hash = QStringLiteral("test-audio-hash");
        return uuid;
    }

    Uuid addAssetEntity(const QString& name, const AssetHash& hash, AssetType type,
                        int start = 0, int end = 60)
    {
        const Uuid uuid = addEntity(name, start, end);
        model::Entity& entity = m_project.entities[uuid];
        entity.content.kind = ContentKind::Asset;
        entity.content.asset.hash = hash;
        entity.content.asset.type = type;
        return uuid;
    }

    QString projectRoot() const { return m_tempDir.path(); }

    Uuid addCameraEntity(const QString& name, int start = 0, int end = 60)
    {
        const Uuid uuid = addEntity(name, start, end);
        m_project.entities[uuid].content.kind = ContentKind::Camera;
        return uuid;
    }

    model::Entity& entity(const Uuid& uuid) { return m_project.entities[uuid]; }

    void addKey(const Uuid& uuid, const QString& trackName, int frame, double value)
    {
        model::Keyframe keyframe;
        keyframe.frame = frame;
        keyframe.value = value;
        m_project.entities[uuid].tracks[trackName].keys.append(keyframe);
    }

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

QString hexHash(char digit)
{
    return QString(64, QChar::fromLatin1(digit));
}

QString writeAsset(const QString& root, const QString& hash, const QString& type,
                   const QString& name, const QJsonObject& extra = QJsonObject())
{
    const QString directory = QDir(root).filePath(QStringLiteral("assets/") + hash);
    if (!QDir().mkpath(directory))
        return {};
    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"), type);
    metadata.insert(QStringLiteral("name"), name);
    metadata.insert(QStringLiteral("mime"), QStringLiteral("application/octet-stream"));
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        metadata.insert(it.key(), it.value());

    QFile file(directory + QStringLiteral("/asset.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(QJsonDocument(metadata).toJson());
    file.close();
    return directory;
}

AssetHash makeImageAsset(const QString& root, char digit, const QString& name)
{
    const AssetHash hash = hexHash(digit);
    const QString directory = writeAsset(root, hash, QStringLiteral("image"), name);
    if (directory.isEmpty())
        return {};
    QImage image(64, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(directory + QStringLiteral("/content.png"), "PNG"))
        return {};
    return hash;
}

AssetHash makeHtmlAsset(const QString& root, char digit, const QString& name,
                        const QJsonObject& variables, bool timeDependent)
{
    const AssetHash hash = hexHash(digit);
    QJsonObject extra;
    extra.insert(QStringLiteral("width"), 800);
    extra.insert(QStringLiteral("height"), 120);
    extra.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
    extra.insert(QStringLiteral("variables"), variables);
    extra.insert(QStringLiteral("timeDependent"), timeDependent);
    const QString directory =
        writeAsset(root, hash, QStringLiteral("html"), name, extra);
    if (directory.isEmpty())
        return {};
    if (!QDir(directory).mkpath(QStringLiteral("bundle")))
        return {};
    QFile index(directory + QStringLiteral("/bundle/index.html"));
    if (!index.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    index.write("<!doctype html><html><body>hud</body></html>");
    index.close();
    return hash;
}

AssetHash makeVideoAsset(const QString& root, char digit, const QString& name,
                         int frames)
{
    const AssetHash hash = hexHash(digit);
    QJsonObject extra;
    extra.insert(QStringLiteral("width"), 160);
    extra.insert(QStringLiteral("height"), 90);
    extra.insert(QStringLiteral("durationFrames"), frames);
    const QString directory =
        writeAsset(root, hash, QStringLiteral("video"), name, extra);
    if (directory.isEmpty())
        return {};
    QFile content(directory + QStringLiteral("/content.mp4"));
    if (!content.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    content.write("not a real video");
    content.close();
    return hash;
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
    void createGravitySolverFromPlayhead();
    void rejectSolverWithoutXYKeys();
    void createSpinSolverFromRotationKey();
    void createSolverFailureRollsBack();
    void rejectOverlappingSolver();
    void rejectEmptyOrSelfTarget();
    void deleteSolverKeepsStartKey();
    void deleteSolverFailureRestoresSegment();
    void lockedEntityDisablesSolverControls();
    void switchingSelectionDoesNotLeak();
    void invalidRangeIsRejected();
    void diamondTogglesKeyframe();
    void diamondOutsideRangeIsRejected();
    void diamondFailureRestoresEntity();
    void autoKeyWritesKeyOrStatic();
    void typedContentEditAutoKeys();
    void keyframeValueEditUpdatesExistingKeyWhenAutoKeyOff();
    void keyframeRowsFollowContentKind();
    void keyframeEasingPresetAndBezier();
    void keyframeRowsDoNotLeakBetweenEntities();
    void lockedEntityDisablesKeyframeControls();
    void htmlVariablesUseManifestDefaults();
    void htmlVariableEditAutoKeysTrack();
    void replaceImageAssetKeepsDuration();
    void replaceWithShorterVideoTrimsEnd();
    void missingAssetDisablesReplaceButton();
    void assetPickerFiltersAndShowsPreview();
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

void PropertiesPanelTest::createGravitySolverFromPlayhead()
{
    FakeDocument document;
    const Uuid ground = document.addEntity(QStringLiteral("地面"), 0, 120);
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 10, 80);
    document.addKey(ball, QStringLiteral("x"), 15, 0.0);
    document.addKey(ball, QStringLiteral("y"), 15, 0.0);
    document.setPlayhead(25);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* effect = findWidget<QComboBox>(panel, "solverEffectCombo");
    auto* target = findWidget<QComboBox>(panel, "solverTargetCombo");
    auto* end = findWidget<QSpinBox>(panel, "solverCreateEnd");
    auto* create = findWidget<QPushButton>(panel, "solverCreateButton");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(effect);
    QVERIFY(target);
    QVERIFY(end);
    QVERIFY(create);
    QVERIFY(status);

    QCOMPARE(effect->currentData().toInt(), int(SolverEffect::Gravity));
    QCOMPARE(end->value(), -1);
    QVERIFY(target->isEnabled());
    const int groundIndex = target->findData(ground);
    QVERIFY(groundIndex >= 0);
    QVERIFY(target->itemText(groundIndex).contains(QStringLiteral("地面")));
    QVERIFY(target->itemText(groundIndex).contains(ground.left(8)));
    QCOMPARE(target->findData(ball), -1);

    target->setCurrentIndex(groundIndex);
    create->click();

    QCOMPARE(document.entity(ball).solvers.size(), 1);
    QCOMPARE(document.entityCommits(), 1);
    const model::SolverSegment& solver = document.entity(ball).solvers.at(0);
    QVERIFY(model::isValidUuid(solver.id));
    QCOMPARE(solver.effect, SolverEffect::Gravity);
    QCOMPARE(solver.startFrame, 15);
    QCOMPARE(solver.endFrame, -1);
    QCOMPARE(solver.version, 1);
    QCOMPARE(solver.initialVelocity, QPointF(0.0, 0.0));
    QCOMPARE(solver.params.value(QStringLiteral("targetUuid")).toString(), ground);
    QVERIFY(status->text().contains(QStringLiteral("已创建求解段")));

    auto* card = findWidget<QGroupBox>(panel, "solverCard0");
    QVERIFY(card);
    QVERIFY(card->styleSheet().contains(QStringLiteral("border")));
}

void PropertiesPanelTest::rejectSolverWithoutXYKeys()
{
    for (const QString& present : {QStringLiteral("x"), QStringLiteral("y")})
    {
        FakeDocument document;
        const Uuid ground = document.addEntity(QStringLiteral("地面"), 0, 60);
        const Uuid ball = document.addEntity(QStringLiteral("小球"), 10, 70);
        document.addKey(ball, present, 5, 0.0);
        document.setPlayhead(15);

        PropertiesPanel panel(&document);
        document.setSelection(ball);

        auto* target = findWidget<QComboBox>(panel, "solverTargetCombo");
        auto* create = findWidget<QPushButton>(panel, "solverCreateButton");
        auto* status = findWidget<QLabel>(panel, "statusLabel");
        QVERIFY(target);
        QVERIFY(create);
        QVERIFY(status);
        target->setCurrentIndex(target->findData(ground));
        create->click();

        QVERIFY(document.entity(ball).solvers.isEmpty());
        QCOMPARE(document.entityCommits(), 0);
        QVERIFY(status->text().contains(
            QStringLiteral("请先在播放头处给 X/Y（或旋转）打普通关键帧")));
    }
}

void PropertiesPanelTest::createSpinSolverFromRotationKey()
{
    FakeDocument document;
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 60);
    document.addKey(ball, QStringLiteral("rotation"), 20, 45.0);
    document.setPlayhead(20);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* effect = findWidget<QComboBox>(panel, "solverEffectCombo");
    auto* target = findWidget<QComboBox>(panel, "solverTargetCombo");
    auto* create = findWidget<QPushButton>(panel, "solverCreateButton");
    QVERIFY(effect);
    QVERIFY(target);
    QVERIFY(create);

    effect->setCurrentIndex(effect->findData(int(SolverEffect::Spin)));
    QVERIFY(!target->isEnabled());
    create->click();

    QCOMPARE(document.entity(ball).solvers.size(), 1);
    QCOMPARE(document.entityCommits(), 1);
    const model::SolverSegment& solver = document.entity(ball).solvers.at(0);
    QCOMPARE(solver.effect, SolverEffect::Spin);
    QCOMPARE(solver.startFrame, 20);
    QCOMPARE(solver.endFrame, -1);
    QCOMPARE(solver.params.value(QStringLiteral("angularVelocity")).toDouble(), 6.0);
    QCOMPARE(solver.params.value(QStringLiteral("direction")).toInt(), 1);
    QVERIFY(!solver.params.contains(QStringLiteral("targetUuid")));
}

void PropertiesPanelTest::createSolverFailureRollsBack()
{
    FakeDocument document;
    const Uuid ground = document.addEntity(QStringLiteral("地面"), 0, 60);
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 60);
    document.addKey(ball, QStringLiteral("x"), 0, 0.0);
    document.addKey(ball, QStringLiteral("y"), 0, 0.0);
    document.setFailCommit(true);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* target = findWidget<QComboBox>(panel, "solverTargetCombo");
    auto* create = findWidget<QPushButton>(panel, "solverCreateButton");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(target);
    QVERIFY(create);
    QVERIFY(status);
    target->setCurrentIndex(target->findData(ground));
    create->click();

    QVERIFY(document.entity(ball).solvers.isEmpty());
    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(status->text().contains(QStringLiteral("创建求解段失败")));
}

void PropertiesPanelTest::rejectOverlappingSolver()
{
    FakeDocument document;
    const Uuid ground = document.addEntity(QStringLiteral("地面"), 0, 60);
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 60);
    document.addKey(ball, QStringLiteral("x"), 15, 0.0);
    document.addKey(ball, QStringLiteral("y"), 15, 0.0);

    model::SolverSegment existing;
    existing.id = model::newUuid();
    existing.effect = SolverEffect::Gravity;
    existing.startFrame = 10;
    existing.endFrame = 20;
    document.entity(ball).solvers.append(existing);
    document.setPlayhead(15);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* target = findWidget<QComboBox>(panel, "solverTargetCombo");
    auto* create = findWidget<QPushButton>(panel, "solverCreateButton");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(target);
    QVERIFY(create);
    QVERIFY(status);
    target->setCurrentIndex(target->findData(ground));
    create->click();

    QCOMPARE(document.entity(ball).solvers.size(), 1);
    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(status->text().contains(QStringLiteral("重叠")));
    QVERIFY(status->text().contains(QStringLiteral("10–20")));
}

void PropertiesPanelTest::rejectEmptyOrSelfTarget()
{
    FakeDocument document;
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 60);
    document.addKey(ball, QStringLiteral("x"), 10, 0.0);
    document.addKey(ball, QStringLiteral("y"), 10, 0.0);
    document.setPlayhead(10);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* target = findWidget<QComboBox>(panel, "solverTargetCombo");
    auto* create = findWidget<QPushButton>(panel, "solverCreateButton");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(target);
    QVERIFY(create);
    QVERIFY(status);

    QCOMPARE(target->currentIndex(), 0);
    QCOMPARE(target->findData(ball), -1);
    create->click();
    QVERIFY(document.entity(ball).solvers.isEmpty());
    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(status->text().contains(QStringLiteral("目标实体")));

    // The combo normally lists only other entities; force the self value to
    // check the guard as well.
    target->addItem(QStringLiteral("自己"), ball);
    target->setCurrentIndex(target->count() - 1);
    create->click();
    QVERIFY(document.entity(ball).solvers.isEmpty());
    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(status->text().contains(QStringLiteral("不能是自己")));
}

void PropertiesPanelTest::deleteSolverKeepsStartKey()
{
    FakeDocument document;
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 60);
    document.addKey(ball, QStringLiteral("x"), 0, 0.0);
    document.addKey(ball, QStringLiteral("y"), 0, 0.0);

    model::SolverSegment solver;
    solver.id = model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 0;
    solver.endFrame = 30;
    document.entity(ball).solvers.append(solver);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* remove = findWidget<QPushButton>(panel, "solverDelete0");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(remove);
    QVERIFY(status);
    remove->click();

    QVERIFY(document.entity(ball).solvers.isEmpty());
    QCOMPARE(document.entityCommits(), 1);
    QCOMPARE(document.entity(ball).tracks.value(QStringLiteral("x")).keys.size(), 1);
    QCOMPARE(document.entity(ball).tracks.value(QStringLiteral("y")).keys.size(), 1);
    QCOMPARE(document.entity(ball).tracks.value(QStringLiteral("x")).keys.at(0).frame, 0);
    QVERIFY(status->text().contains(QStringLiteral("已删除求解段")));
}

void PropertiesPanelTest::deleteSolverFailureRestoresSegment()
{
    FakeDocument document;
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 60);
    document.addKey(ball, QStringLiteral("x"), 0, 0.0);
    document.addKey(ball, QStringLiteral("y"), 0, 0.0);

    model::SolverSegment solver;
    solver.id = model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 0;
    solver.endFrame = 30;
    document.entity(ball).solvers.append(solver);
    document.setFailCommit(true);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* remove = findWidget<QPushButton>(panel, "solverDelete0");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(remove);
    QVERIFY(status);
    remove->click();

    QCOMPARE(document.entity(ball).solvers.size(), 1);
    QCOMPARE(document.entity(ball).solvers.at(0).id, solver.id);
    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(status->text().contains(QStringLiteral("删除求解段失败")));
}

void PropertiesPanelTest::lockedEntityDisablesSolverControls()
{
    FakeDocument document;
    const Uuid ball = document.addEntity(QStringLiteral("小球"), 0, 60);
    document.entity(ball).locked = true;
    document.addKey(ball, QStringLiteral("x"), 0, 0.0);
    document.addKey(ball, QStringLiteral("y"), 0, 0.0);

    model::SolverSegment solver;
    solver.id = model::newUuid();
    solver.effect = SolverEffect::Gravity;
    solver.startFrame = 0;
    solver.endFrame = 30;
    document.entity(ball).solvers.append(solver);

    PropertiesPanel panel(&document);
    document.setSelection(ball);

    auto* effect = findWidget<QComboBox>(panel, "solverEffectCombo");
    auto* target = findWidget<QComboBox>(panel, "solverTargetCombo");
    auto* end = findWidget<QSpinBox>(panel, "solverCreateEnd");
    auto* create = findWidget<QPushButton>(panel, "solverCreateButton");
    auto* remove = findWidget<QPushButton>(panel, "solverDelete0");
    QVERIFY(effect);
    QVERIFY(target);
    QVERIFY(end);
    QVERIFY(create);
    QVERIFY(remove);
    QVERIFY(!effect->isEnabled());
    QVERIFY(!target->isEnabled());
    QVERIFY(!end->isEnabled());
    QVERIFY(!create->isEnabled());
    QVERIFY(!remove->isEnabled());
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

void PropertiesPanelTest::diamondTogglesKeyframe()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 10, 70);
    document.entity(uuid).transform.x = 5.0;
    document.setPlayhead(25);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* diamond = findWidget<QPushButton>(panel, "keyDiamond_x");
    QVERIFY(diamond);
    QCOMPARE(diamond->text(), QStringLiteral("◇"));

    diamond->click();
    QCOMPARE(document.entityCommits(), 1);
    QCOMPARE(diamond->text(), QStringLiteral("◆"));
    const model::Track track = document.entity(uuid).tracks.value(QStringLiteral("x"));
    QCOMPARE(track.keys.size(), 1);
    QCOMPARE(track.keys.at(0).frame, 15);
    QCOMPARE(track.keys.at(0).value, 5.0);

    diamond->click();
    QCOMPARE(document.entityCommits(), 2);
    QCOMPARE(diamond->text(), QStringLiteral("◇"));
    QVERIFY(!document.entity(uuid).tracks.contains(QStringLiteral("x")));
}

void PropertiesPanelTest::diamondOutsideRangeIsRejected()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 10, 70);
    document.setPlayhead(5);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* diamond = findWidget<QPushButton>(panel, "keyDiamond_x");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(diamond);
    QVERIFY(status);
    diamond->click();

    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(document.entity(uuid).tracks.isEmpty());
    QVERIFY(status->text().contains(QStringLiteral("播放头不在实体范围内")));
}

void PropertiesPanelTest::diamondFailureRestoresEntity()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 0, 60);
    document.setFailCommit(true);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* diamond = findWidget<QPushButton>(panel, "keyDiamond_x");
    auto* status = findWidget<QLabel>(panel, "statusLabel");
    QVERIFY(diamond);
    QVERIFY(status);
    diamond->click();

    QCOMPARE(document.entityCommits(), 0);
    QVERIFY(document.entity(uuid).tracks.isEmpty());
    QCOMPARE(diamond->text(), QStringLiteral("◇"));
    QVERIFY(status->text().contains(QStringLiteral("保存失败")));
}

void PropertiesPanelTest::autoKeyWritesKeyOrStatic()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 10, 70);
    document.setPlayhead(25);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);
    QVERIFY(panel.autoKey());

    auto* x = findWidget<QDoubleSpinBox>(panel, "transformXSpin");
    QVERIFY(x);
    x->setValue(42.5);
    QVERIFY(fireEditingFinished(x));

    QCOMPARE(document.entity(uuid).transform.x, 42.5);
    QCOMPARE(document.entityCommits(), 1);
    const model::Track track = document.entity(uuid).tracks.value(QStringLiteral("x"));
    QCOMPARE(track.keys.size(), 1);
    QCOMPARE(track.keys.at(0).frame, 15);
    QCOMPARE(track.keys.at(0).value, 42.5);

    panel.setAutoKey(false);
    QVERIFY(!panel.autoKey());
    x->setValue(50.0);
    QVERIFY(fireEditingFinished(x));

    QCOMPARE(document.entity(uuid).transform.x, 50.0);
    QCOMPARE(document.entityCommits(), 2);
    const model::Track unchanged =
        document.entity(uuid).tracks.value(QStringLiteral("x"));
    QCOMPARE(unchanged.keys.size(), 1);
    QCOMPARE(unchanged.keys.at(0).value, 42.5);
}

void PropertiesPanelTest::typedContentEditAutoKeys()
{
    {
        FakeDocument document;
        const Uuid uuid =
            document.addTextEntity(QStringLiteral("文字实体"), QStringLiteral("你好"));
        document.setPlayhead(12);

        PropertiesPanel panel(&document);
        document.setSelection(uuid);

        auto* fontSize = findWidget<QSpinBox>(panel, "textPixelSizeSpin");
        QVERIFY(fontSize);
        fontSize->setValue(77);
        QVERIFY(fireEditingFinished(fontSize));

        QCOMPARE(document.entityCommits(), 1);
        QCOMPARE(document.entity(uuid).content.text.pixelSize, 77);
        const model::Track track =
            document.entity(uuid).tracks.value(QStringLiteral("fontSize"));
        QCOMPARE(track.keys.size(), 1);
        QCOMPARE(track.keys.at(0).frame, 12);
        QCOMPARE(track.keys.at(0).value, 77.0);
    }
    {
        FakeDocument document;
        const Uuid uuid = document.addVectorEntity(QStringLiteral("矢量实体"));
        document.setPlayhead(20);

        PropertiesPanel panel(&document);
        document.setSelection(uuid);

        auto* width = findWidget<QDoubleSpinBox>(panel, "vectorWidthSpin");
        QVERIFY(width);
        width->setValue(640.0);
        QVERIFY(fireEditingFinished(width));

        QCOMPARE(document.entityCommits(), 1);
        QCOMPARE(document.entity(uuid).content.vector.width, 640.0);
        const model::Track track =
            document.entity(uuid).tracks.value(QStringLiteral("width"));
        QCOMPARE(track.keys.size(), 1);
        QCOMPARE(track.keys.at(0).frame, 20);
        QCOMPARE(track.keys.at(0).value, 640.0);
    }
}

void PropertiesPanelTest::keyframeValueEditUpdatesExistingKeyWhenAutoKeyOff()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 0, 60);
    document.addKey(uuid, QStringLiteral("x"), 10, 1.0);
    document.setPlayhead(10);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);
    panel.setAutoKey(false);

    auto* value = findWidget<QDoubleSpinBox>(panel, "keyValue_x");
    QVERIFY(value);
    value->setValue(9.0);
    QVERIFY(fireEditingFinished(value));

    QCOMPARE(document.entityCommits(), 1);
    const model::Track track = document.entity(uuid).tracks.value(QStringLiteral("x"));
    QCOMPARE(track.keys.size(), 1);
    QCOMPARE(track.keys.at(0).frame, 10);
    QCOMPARE(track.keys.at(0).value, 9.0);
}

void PropertiesPanelTest::keyframeRowsFollowContentKind()
{
    {
        FakeDocument document;
        const Uuid uuid =
            document.addTextEntity(QStringLiteral("文字实体"), QStringLiteral("你好"));
        PropertiesPanel panel(&document);
        document.setSelection(uuid);

        QVERIFY(findWidget<QPushButton>(panel, "keyDiamond_fontSize"));
        QVERIFY(findWidget<QDoubleSpinBox>(panel, "keyValue_lineHeight"));
        QVERIFY(findWidget<QComboBox>(panel, "keyEasing_letterSpacing"));
        QVERIFY(findWidget<QDoubleSpinBox>(panel, "keyValue_padding"));
        QVERIFY(findWidget<QDoubleSpinBox>(panel, "keyValue_boxWidth"));
        QVERIFY(!findWidget<QPushButton>(panel, "keyDiamond_width"));
    }
    {
        FakeDocument document;
        const Uuid uuid = document.addVectorEntity(QStringLiteral("矢量实体"));
        PropertiesPanel panel(&document);
        document.setSelection(uuid);

        QVERIFY(findWidget<QPushButton>(panel, "keyDiamond_width"));
        QVERIFY(findWidget<QPushButton>(panel, "keyDiamond_height"));
        QVERIFY(findWidget<QPushButton>(panel, "keyDiamond_cornerRadius"));
        QVERIFY(!findWidget<QPushButton>(panel, "keyDiamond_fontSize"));
    }
    {
        FakeDocument document;
        const Uuid uuid = document.addAudioEntity(QStringLiteral("音频实体"));
        PropertiesPanel panel(&document);
        document.setSelection(uuid);

        QVERIFY(findWidget<QPushButton>(panel, "keyDiamond_volume"));
        QVERIFY(findWidget<QDoubleSpinBox>(panel, "keyValue_volume"));
    }
    {
        FakeDocument document;
        const Uuid uuid = document.addCameraEntity(QStringLiteral("摄像机"));
        PropertiesPanel panel(&document);
        document.setSelection(uuid);

        QVERIFY(findWidget<QPushButton>(panel, "keyDiamond_x"));
        QVERIFY(findWidget<QPushButton>(panel, "keyDiamond_scaleX"));
        QVERIFY(!findWidget<QPushButton>(panel, "keyDiamond_opacity"));
    }
}

void PropertiesPanelTest::keyframeEasingPresetAndBezier()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 0, 60);
    document.addKey(uuid, QStringLiteral("x"), 20, 3.0);
    document.setPlayhead(20);

    PropertiesPanel panel(&document);
    panel.show();
    document.setSelection(uuid);

    auto* easing = findWidget<QComboBox>(panel, "keyEasing_x");
    QVERIFY(easing);

    // linear + instant + the 30 presets + bezier, presets in
    // model::easingPresetNames() order.
    const QStringList& presets = model::easingPresetNames();
    QCOMPARE(presets.size(), 30);
    QCOMPARE(easing->count(), presets.size() + 3);
    QCOMPARE(easing->itemData(0).toString(), QStringLiteral("linear"));
    QCOMPARE(easing->itemData(1).toString(), QStringLiteral("instant"));
    for (int i = 0; i < presets.size(); ++i)
        QCOMPARE(easing->itemData(i + 2).toString(), presets.at(i));
    QCOMPARE(easing->itemData(easing->count() - 1).toString(), QStringLiteral("bezier"));
    QVERIFY(!easing->toolTip().isEmpty());
    QVERIFY(!easing->itemData(0, Qt::ToolTipRole).toString().isEmpty());

    const int outCubic = easing->findData(QStringLiteral("easeoutcubic"));
    QVERIFY(outCubic >= 0);
    easing->setCurrentIndex(outCubic);
    QCOMPARE(document.entityCommits(), 1);
    QCOMPARE(document.entity(uuid).tracks.value(QStringLiteral("x")).keys.at(0).easing.type,
             QStringLiteral("easeoutcubic"));

    // Legacy easeIn/easeOut/easeInOut/step keyframes resolve to their canonical
    // items instead of falling back to linear or growing a bogus entry.
    auto reloadAs = [&](const char* alias) {
        document.entity(uuid).tracks[QStringLiteral("x")].keys[0].easing.type =
            QString::fromLatin1(alias);
        panel.refresh();
        return easing->currentData().toString();
    };
    QCOMPARE(reloadAs("easeIn"), QStringLiteral("easeinquad"));
    QCOMPARE(reloadAs("easeOut"), QStringLiteral("easeoutquad"));
    QCOMPARE(reloadAs("easeInOut"), QStringLiteral("easeinoutquad"));
    QCOMPARE(reloadAs("step"), QStringLiteral("instant"));
    QCOMPARE(easing->currentText(), QStringLiteral("instant"));

    // Picking bezier opens the existing custom-handle editor.
    bool dialogChecked = false;
    QTimer::singleShot(0, &panel, [&dialogChecked] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        const QList<QDoubleSpinBox*> spins = dialog->findChildren<QDoubleSpinBox*>();
        if (spins.size() != 4)
        {
            dialog->reject();
            return;
        }
        spins.at(0)->setValue(0.2);
        spins.at(1)->setValue(0.1);
        spins.at(2)->setValue(0.8);
        spins.at(3)->setValue(0.9);
        dialogChecked = true;
        dialog->accept();
    });
    easing->setCurrentIndex(easing->findData(QStringLiteral("bezier")));
    QVERIFY(dialogChecked);

    QCOMPARE(document.entityCommits(), 2);
    const model::EasingSpec saved =
        document.entity(uuid).tracks.value(QStringLiteral("x")).keys.at(0).easing;
    QCOMPARE(saved.type, QStringLiteral("bezier"));
    QCOMPARE(saved.inX, 0.2);
    QCOMPARE(saved.inY, 0.1);
    QCOMPARE(saved.outX, 0.8);
    QCOMPARE(saved.outY, 0.9);
    QCOMPARE(easing->currentData().toString(), QStringLiteral("bezier"));
}

void PropertiesPanelTest::keyframeRowsDoNotLeakBetweenEntities()
{
    FakeDocument document;
    const Uuid first = document.addEntity(QStringLiteral("甲"), 0, 60);
    const Uuid second = document.addEntity(QStringLiteral("乙"), 0, 60);
    document.entity(first).transform.x = 4.0;
    document.addKey(first, QStringLiteral("x"), 0, 4.0);

    PropertiesPanel panel(&document);
    document.setSelection(first);

    auto* diamond = findWidget<QPushButton>(panel, "keyDiamond_x");
    auto* value = findWidget<QDoubleSpinBox>(panel, "keyValue_x");
    auto* easing = findWidget<QComboBox>(panel, "keyEasing_x");
    QVERIFY(diamond);
    QVERIFY(value);
    QVERIFY(easing);
    QCOMPARE(diamond->text(), QStringLiteral("◆"));
    QCOMPARE(value->value(), 4.0);
    QCOMPARE(easing->currentData().toString(), QStringLiteral("linear"));

    document.setSelection(second);

    diamond = findWidget<QPushButton>(panel, "keyDiamond_x");
    value = findWidget<QDoubleSpinBox>(panel, "keyValue_x");
    easing = findWidget<QComboBox>(panel, "keyEasing_x");
    QVERIFY(diamond);
    QVERIFY(value);
    QVERIFY(easing);
    QCOMPARE(diamond->text(), QStringLiteral("◇"));
    QCOMPARE(value->value(), 0.0);
    QCOMPARE(easing->currentIndex(), -1);
    QVERIFY(document.entity(second).tracks.isEmpty());
    QCOMPARE(document.entityCommits(), 0);
}

void PropertiesPanelTest::lockedEntityDisablesKeyframeControls()
{
    FakeDocument document;
    const Uuid uuid = document.addEntity(QStringLiteral("实体"), 0, 60);
    document.entity(uuid).locked = true;
    document.addKey(uuid, QStringLiteral("x"), 0, 1.0);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* diamond = findWidget<QPushButton>(panel, "keyDiamond_x");
    auto* value = findWidget<QDoubleSpinBox>(panel, "keyValue_x");
    auto* easing = findWidget<QComboBox>(panel, "keyEasing_x");
    QVERIFY(diamond);
    QVERIFY(value);
    QVERIFY(easing);
    QVERIFY(!diamond->isEnabled());
    QVERIFY(!value->isEnabled());
    QVERIFY(!easing->isEnabled());
}

void PropertiesPanelTest::htmlVariablesUseManifestDefaults()
{
    FakeDocument document;
    QJsonObject variables;
    variables.insert(QStringLiteral("speed"), 2.5);
    variables.insert(QStringLiteral("show"), true);
    variables.insert(QStringLiteral("label"), QStringLiteral("hello"));
    const AssetHash hash =
        makeHtmlAsset(document.projectRoot(), 'a', QStringLiteral("hud"), variables, false);
    QVERIFY(!hash.isEmpty());
    const Uuid uuid =
        document.addAssetEntity(QStringLiteral("HUD"), hash, AssetType::Html, 0, 60);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* status = findWidget<QLabel>(panel, "htmlTimeDependentLabel");
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("否")));

    auto* value = findWidget<QDoubleSpinBox>(panel, "htmlVarValue_var.speed");
    QVERIFY(value);
    QCOMPARE(value->value(), 2.5);

    auto* readOnlyBool = findWidget<QLabel>(panel, "htmlVarReadOnly_var.show");
    QVERIFY(readOnlyBool);
    QCOMPARE(readOnlyBool->text(), QStringLiteral("true"));
    QVERIFY(!findWidget<QPushButton>(panel, "htmlVarDiamond_var.show"));

    auto* readOnlyString = findWidget<QLabel>(panel, "htmlVarReadOnly_var.label");
    QVERIFY(readOnlyString);
    QCOMPARE(readOnlyString->text(), QStringLiteral("hello"));

    auto* diamond = findWidget<QPushButton>(panel, "htmlVarDiamond_var.speed");
    QVERIFY(diamond);
    QVERIFY(diamond->isEnabled());
    diamond->click();

    QVERIFY(document.entity(uuid).tracks.contains(QStringLiteral("var.speed")));
    const QVector<model::Keyframe> keys =
        document.entity(uuid).tracks.value(QStringLiteral("var.speed")).keys;
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.first().frame, 0);
    QCOMPARE(keys.first().value, 2.5);
    QCOMPARE(document.entityCommits(), 1);
    QCOMPARE(diamond->text(), QStringLiteral("◆"));
}

void PropertiesPanelTest::htmlVariableEditAutoKeysTrack()
{
    FakeDocument document;
    QJsonObject variables;
    variables.insert(QStringLiteral("speed"), 1.0);
    const AssetHash hash =
        makeHtmlAsset(document.projectRoot(), 'b', QStringLiteral("hud"), variables, true);
    QVERIFY(!hash.isEmpty());
    const Uuid uuid =
        document.addAssetEntity(QStringLiteral("HUD"), hash, AssetType::Html, 0, 60);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);
    document.setPlayhead(10);

    auto* value = findWidget<QDoubleSpinBox>(panel, "htmlVarValue_var.speed");
    QVERIFY(value);
    QCOMPARE(value->value(), 1.0);
    value->setValue(7.5);
    QVERIFY(fireEditingFinished(value));

    const QVector<model::Keyframe> keys =
        document.entity(uuid).tracks.value(QStringLiteral("var.speed")).keys;
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.first().frame, 10);
    QCOMPARE(keys.first().value, 7.5);
}

void PropertiesPanelTest::replaceImageAssetKeepsDuration()
{
    FakeDocument document;
    const AssetHash first =
        makeImageAsset(document.projectRoot(), 'c', QStringLiteral("one.png"));
    const AssetHash second =
        makeImageAsset(document.projectRoot(), 'd', QStringLiteral("two.png"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    const Uuid uuid =
        document.addAssetEntity(QStringLiteral("图片"), first, AssetType::Image, 10, 50);
    document.addKey(uuid, QStringLiteral("x"), 30, 1.0);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* button = findWidget<QPushButton>(panel, "assetReplaceButton");
    QVERIFY(button);
    QVERIFY(button->isEnabled());

    QString error;
    QVERIFY2(panel.replaceAsset(second, &error), qPrintable(error));
    QCOMPARE(document.entity(uuid).content.asset.hash, second);
    QCOMPARE(document.entity(uuid).content.asset.type, AssetType::Image);
    QCOMPARE(document.entity(uuid).content.asset.sourceIn, 0);
    QCOMPARE(document.entity(uuid).start, 10);
    QCOMPARE(document.entity(uuid).end, 50);
    QVERIFY(document.entity(uuid).tracks.contains(QStringLiteral("x")));
}

void PropertiesPanelTest::replaceWithShorterVideoTrimsEnd()
{
    FakeDocument document;
    const AssetHash longVideo =
        makeVideoAsset(document.projectRoot(), 'e', QStringLiteral("long.mp4"), 300);
    const AssetHash shortVideo =
        makeVideoAsset(document.projectRoot(), 'f', QStringLiteral("short.mp4"), 20);
    QVERIFY(!longVideo.isEmpty());
    QVERIFY(!shortVideo.isEmpty());
    const Uuid uuid =
        document.addAssetEntity(QStringLiteral("视频"), longVideo, AssetType::Video, 0, 60);
    document.addKey(uuid, QStringLiteral("x"), 10, 1.0);
    document.addKey(uuid, QStringLiteral("x"), 30, 2.0);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    QString error;
    QVERIFY2(panel.replaceAsset(shortVideo, &error), qPrintable(error));
    QCOMPARE(document.entity(uuid).content.asset.hash, shortVideo);
    QCOMPARE(document.entity(uuid).content.asset.type, AssetType::Video);
    QCOMPARE(document.entity(uuid).end, 20);
    const QVector<model::Keyframe> keys =
        document.entity(uuid).tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.first().frame, 10);

    // sourceIn stays and is clamped into the new material length: 20 - 15 = 5.
    FakeDocument clamped;
    const AssetHash longAgain =
        makeVideoAsset(clamped.projectRoot(), '1', QStringLiteral("long.mp4"), 300);
    const AssetHash shortAgain =
        makeVideoAsset(clamped.projectRoot(), '2', QStringLiteral("short.mp4"), 20);
    const Uuid clampedUuid = clamped.addAssetEntity(
        QStringLiteral("视频"), longAgain, AssetType::Video, 0, 60);
    clamped.entity(clampedUuid).content.asset.sourceIn = 15;

    PropertiesPanel clampedPanel(&clamped);
    clamped.setSelection(clampedUuid);
    QVERIFY2(clampedPanel.replaceAsset(shortAgain, &error), qPrintable(error));
    QCOMPARE(clamped.entity(clampedUuid).content.asset.sourceIn, 15);
    QCOMPARE(clamped.entity(clampedUuid).end, 5);
}

void PropertiesPanelTest::missingAssetDisablesReplaceButton()
{
    FakeDocument document;
    const Uuid uuid = document.addAssetEntity(
        QStringLiteral("缺素材"), hexHash('9'), AssetType::Image, 0, 60);

    PropertiesPanel panel(&document);
    document.setSelection(uuid);

    auto* button = findWidget<QPushButton>(panel, "assetReplaceButton");
    QVERIFY(button);
    QVERIFY(!button->isEnabled());
}

void PropertiesPanelTest::assetPickerFiltersAndShowsPreview()
{
    FakeDocument document;
    const AssetHash first =
        makeImageAsset(document.projectRoot(), '7', QStringLiteral("alpha.png"));
    const AssetHash second =
        makeImageAsset(document.projectRoot(), '8', QStringLiteral("beta.png"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    const Uuid uuid =
        document.addAssetEntity(QStringLiteral("图片"), first, AssetType::Image, 0, 60);

    PropertiesPanel panel(&document);
    panel.show();
    document.setSelection(uuid);

    auto* button = findWidget<QPushButton>(panel, "assetReplaceButton");
    QVERIFY(button);
    QVERIFY(button->isEnabled());

    bool dialogChecked = false;
    QTimer::singleShot(0, &panel, [&dialogChecked] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("assetPickSearch"));
        auto* list = dialog->findChild<QListWidget*>(QStringLiteral("assetPickList"));
        auto* preview = dialog->findChild<QLabel*>(QStringLiteral("assetPickPreview"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!search || !list || !preview || !buttons)
        {
            dialog->reject();
            return;
        }

        search->setText(QStringLiteral("beta"));
        int visibleRow = -1;
        int visible = 0;
        for (int i = 0; i < list->count(); ++i)
        {
            if (list->item(i)->isHidden())
                continue;
            visibleRow = i;
            ++visible;
        }
        if (visible != 1 || visibleRow < 0)
        {
            dialog->reject();
            return;
        }
        list->setCurrentRow(visibleRow);
        const QPixmap pixmap = preview->pixmap();
        if (pixmap.isNull() || pixmap.width() > 320 || pixmap.height() > 320)
        {
            dialog->reject();
            return;
        }
        dialogChecked = true;
        buttons->button(QDialogButtonBox::Ok)->click();
        if (dialog->isVisible())
            dialog->reject();
    });

    button->click();
    QVERIFY(dialogChecked);
    QCOMPARE(document.entity(uuid).content.asset.hash, second);
}

QTEST_MAIN(PropertiesPanelTest)
#include "PropertiesPanelTest.moc"
