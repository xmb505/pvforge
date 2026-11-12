#include "store/Store.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;
using namespace pvf::store;

namespace {

struct CommandResult
{
    int exitCode = -1;
    QString out;
    QString err;
};

CommandResult runGit(const QString& root, const QStringList& arguments)
{
    CommandResult result;
    QProcess process;
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), root} + arguments);
    if (!process.waitForStarted(10000))
        return result;
    if (!process.waitForFinished(60000))
    {
        process.kill();
        process.waitForFinished(5000);
        return result;
    }
    result.exitCode = process.exitStatus() == QProcess::NormalExit
                          ? process.exitCode()
                          : -1;
    result.out = QString::fromUtf8(process.readAllStandardOutput());
    result.err = QString::fromUtf8(process.readAllStandardError());
    return result;
}

bool gitAvailable()
{
    static const bool available = [] {
        QProcess process;
        process.start(QStringLiteral("git"), {QStringLiteral("--version")});
        if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
            return false;
        return process.exitStatus() == QProcess::NormalExit
               && process.exitCode() == 0;
    }();
    return available;
}

bool writeText(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = text.toUtf8();
    return file.write(bytes) == bytes.size();
}

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString projectPath(QTemporaryDir& temp)
{
    return temp.filePath(QStringLiteral("Demo.pvproj"));
}

model::Entity makeEntity(const QString& name = QStringLiteral("实体"),
                         int start = 0, int end = 30)
{
    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = start;
    entity.end = end;
    return entity;
}

struct PathGuard
{
    PathGuard() : oldPath(qgetenv("PATH")) {}
    ~PathGuard() { qputenv("PATH", oldPath); }
    QByteArray oldPath;
};

#define SKIP_WITHOUT_GIT()                                                    \
    do                                                                        \
    {                                                                         \
        if (!gitAvailable())                                                  \
            QSKIP("git 不可用，跳过依赖 git 的测试");                         \
    } while (false)

} // namespace

class StoreTests : public QObject
{
    Q_OBJECT

private slots:
    void createOpenRoundTrip();
    void saveEntityReopen();
    void saveProjectRoundTrip();
    void deleteEntityFile();
    void commitAndHistory();
    void assetImportCommit();
    void commitFailureReportsStderr();
    void undoRedoRestoresText();
    void undoProjectSettings();
    void undoNewAndDeletedFiles();
    void undoStackLimits();
    void assetsSurviveUndo();
    void corruptProjectJson();
    void corruptEntityJson();
    void nonGitDirectoryRejected();
    void missingGitCreateFallsBack();
    void createRejectsInvalidInput();
    void cleanProjectHasNoDirtyText();
    void dirtyTextDetectedOnOpen();
    void commitRecoveredTextCreatesCommit();
    void discardDirtyTextRestores();
    void restoreCommitRewindsText();
    void restoreCommitDeletesNewEntities();
    void restoreCommitRejectsDirtyText();
    void restoreCommitRejectsInvalidHash();
    void restoreCommitClearsStacksAndKeepsAssets();
    void restoreCommitRollsBackOnCommitFailure();
    void createWithoutGit();
    void openWithoutGitRepoAndGitRequested();
    void enableGitLater();
    void disableGitThenCommitStillUndoable();
    void setGitEnabledReportsMissingGit();
    void openRejectsFutureVersion();
    void openRejectsUnmigratableVersion();
    void openMigratesV1ProjectAndEntity();
    void openRejectsFutureEntityVersion();
    void openRejectsStaleEntityVersion();
    void openStrictRequiredFields();
    void unknownFieldsSurviveReopen();
};

void StoreTests::createOpenRoundTrip()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    model::ProjectSettings settings;
    settings.name = QStringLiteral("测试工程");
    settings.width = 1280;
    settings.height = 720;
    settings.fps = 24;
    settings.background = QStringLiteral("#202030");

    QString error;
    auto created = ProjectStore::create(path, settings, &error);
    QVERIFY2(created.has_value(), qPrintable(error));

    QCOMPARE(created->root(), QFileInfo(path).absoluteFilePath());
    QCOMPARE(created->project().settings.name, settings.name);
    QCOMPARE(created->project().settings.width, 1280);
    QCOMPARE(created->project().settings.fps, 24);
    QVERIFY(created->project().entities.isEmpty());
    QVERIFY(created->project().markers.isEmpty());
    QCOMPARE(created->assets().projectRoot(), created->root());

    QVERIFY(QFileInfo::exists(path + QStringLiteral("/project.json")));
    QVERIFY(QFileInfo(path + QStringLiteral("/entities")).isDir());
    QVERIFY(QFileInfo(path + QStringLiteral("/assets")).isDir());
    QVERIFY(!QFileInfo::exists(path + QStringLiteral("/.gitattributes")));
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/.gitignore")));
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/.git")));

    const QString ignore = readText(path + QStringLiteral("/.gitignore"));
    QVERIFY(ignore.contains(QStringLiteral("assets/")));
    QVERIFY(ignore.contains(QStringLiteral(".cache/")));
    QVERIFY(ignore.contains(QStringLiteral(".pvforge/")));
    QVERIFY(ignore.contains(QStringLiteral("*.tmp")));

    const CommandResult initialShow =
        runGit(created->root(),
               {QStringLiteral("show"), QStringLiteral("--name-only"),
                QStringLiteral("--format="), QStringLiteral("HEAD")});
    QCOMPARE(initialShow.exitCode, 0);
    QVERIFY2(initialShow.out.contains(QStringLiteral("project.json")),
             qPrintable(initialShow.out));
    QVERIFY2(!initialShow.out.contains(QStringLiteral("assets/")),
             qPrintable(initialShow.out));
    QVERIFY2(!initialShow.out.contains(QStringLiteral(".gitattributes")),
             qPrintable(initialShow.out));

    const QVector<GitCommit> history = created->history(50, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().message, QStringLiteral("pvf: init project"));
    QCOMPARE(history.first().hash.size(), 40);
    QVERIFY(history.first().time.isValid());

    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QCOMPARE(opened->project().settings.name, settings.name);
    QCOMPARE(opened->project().settings.background, settings.background);
    QVERIFY(opened->project().entities.isEmpty());
}

void StoreTests::saveEntityReopen()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("标题"), 5, 25);
    entity.row = 2;
    entity.transform.x = 12.5;
    entity.transform.rotation = -30;
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("Hello");
    entity.content.text.pixelSize = 120;

    model::Track track;
    model::Keyframe key;
    key.frame = 0;
    key.value = 3.5;
    track.keys.append(key);
    entity.tracks.insert(QStringLiteral("x"), track);

    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/entities/") + entity.uuid
                              + QStringLiteral(".json")));

    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    const auto it = opened->project().entities.constFind(entity.uuid);
    QVERIFY(it != opened->project().entities.constEnd());
    QCOMPARE(it->name, entity.name);
    QCOMPARE(it->start, 5);
    QCOMPARE(it->end, 25);
    QCOMPARE(it->row, 2);
    QCOMPARE(it->transform.x, 12.5);
    QCOMPARE(it->transform.rotation, -30.0);
    QCOMPARE(it->content.kind, ContentKind::Text);
    QCOMPARE(it->content.text.text, QStringLiteral("Hello"));
    QCOMPARE(it->content.text.pixelSize, 120);
    QCOMPARE(it->tracks.value(QStringLiteral("x")).keys.size(), 1);
    QCOMPARE(it->tracks.value(QStringLiteral("x")).keys.first().value, 3.5);

    model::Entity invalid = makeEntity(QStringLiteral("坏实体"), 10, 10);
    store->mutableProject().entities.insert(invalid.uuid, invalid);
    error.clear();
    QVERIFY(!store->saveEntity(invalid.uuid, &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!store->saveEntity(model::newUuid(), &error));
    QVERIFY(!error.isEmpty());
}

void StoreTests::saveProjectRoundTrip()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Marker marker;
    marker.id = model::newUuid();
    marker.frame = 42;
    marker.title = QStringLiteral("前奏");
    marker.note = QStringLiteral("Markdown 备注");
    marker.tags = {QStringLiteral("music")};
    marker.color = QStringLiteral("#ff0000");

    store->mutableProject().settings.name = QStringLiteral("改名后的工程");
    store->mutableProject().markers.append(marker);
    QVERIFY2(store->saveProject(&error), qPrintable(error));

    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QCOMPARE(opened->project().settings.name, QStringLiteral("改名后的工程"));
    QCOMPARE(opened->project().markers.size(), 1);
    QCOMPARE(opened->project().markers.first().id, marker.id);
    QCOMPARE(opened->project().markers.first().frame, 42);
    QCOMPARE(opened->project().markers.first().title, marker.title);
    QCOMPARE(opened->project().markers.first().tags, marker.tags);

    model::Marker invalid;
    invalid.title = QStringLiteral("没有 id");
    opened->mutableProject().markers.append(invalid);
    error.clear();
    QVERIFY(!opened->saveProject(&error));
    QVERIFY(!error.isEmpty());
}

void StoreTests::deleteEntityFile()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity parent = makeEntity(QStringLiteral("父实体"), 0, 100);
    model::Entity child = makeEntity(QStringLiteral("子实体"), 10, 20);
    child.parentUuid = parent.uuid;

    store->mutableProject().entities.insert(parent.uuid, parent);
    store->mutableProject().entities.insert(child.uuid, child);
    QVERIFY2(store->saveEntity(parent.uuid, &error), qPrintable(error));
    QVERIFY2(store->saveEntity(child.uuid, &error), qPrintable(error));

    const QString parentPath =
        path + QStringLiteral("/entities/") + parent.uuid + QStringLiteral(".json");
    const QString childPath =
        path + QStringLiteral("/entities/") + child.uuid + QStringLiteral(".json");
    QVERIFY(QFileInfo::exists(parentPath));
    QVERIFY(QFileInfo::exists(childPath));
    QVERIFY2(store->commit(QStringLiteral("pvf: add entities"), &error),
             qPrintable(error));

    QVERIFY2(store->deleteEntity(parent.uuid, &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(parentPath));
    QVERIFY(QFileInfo::exists(childPath));
    QVERIFY(!store->project().entities.contains(parent.uuid));
    QVERIFY(store->project().entities.contains(child.uuid));

    error.clear();
    QVERIFY(!store->deleteEntity(parent.uuid, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY2(store->commit(QStringLiteral("pvf: delete parent"), &error),
             qPrintable(error));
    const CommandResult show =
        runGit(store->root(),
               {QStringLiteral("show"), QStringLiteral("--diff-filter=D"),
                QStringLiteral("--name-only"), QStringLiteral("--format="),
                QStringLiteral("HEAD")});
    QCOMPARE(show.exitCode, 0);
    QVERIFY2(show.out.contains(QStringLiteral("entities/") + parent.uuid
                               + QStringLiteral(".json")),
             qPrintable(show.out));

    error.clear();
    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(parentPath));
    QVERIFY(store->project().entities.contains(parent.uuid));
    QVERIFY(store->project().entities.contains(child.uuid));
}

void StoreTests::commitAndHistory()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity();
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));

    // A user-created file, staged by hand, must stay out of the app commit.
    const QString notesPath = path + QStringLiteral("/notes.txt");
    QVERIFY(writeText(notesPath, QStringLiteral("user notes\n")));
    const CommandResult userAdd =
        runGit(store->root(),
               {QStringLiteral("add"), QStringLiteral("notes.txt")});
    QCOMPARE(userAdd.exitCode, 0);

    QVERIFY2(store->commit(QStringLiteral("pvf: add entity"), &error),
             qPrintable(error));

    QVector<GitCommit> history = store->history(1, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().message, QStringLiteral("pvf: add entity"));
    QVERIFY(history.first().time.isValid());

    const CommandResult show =
        runGit(store->root(),
               {QStringLiteral("show"), QStringLiteral("--name-only"),
                QStringLiteral("--format="), QStringLiteral("HEAD")});
    QCOMPARE(show.exitCode, 0);
    QVERIFY2(show.out.contains(QStringLiteral("entities/") + entity.uuid
                               + QStringLiteral(".json")),
             qPrintable(show.out));
    QVERIFY2(!show.out.contains(QStringLiteral("notes.txt")),
             qPrintable(show.out));

    const CommandResult status =
        runGit(store->root(),
               {QStringLiteral("status"), QStringLiteral("--porcelain")});
    QVERIFY2(status.out.contains(QStringLiteral("notes.txt")),
             qPrintable(status.out));

    const int countBefore = store->history(50, &error).size();
    error.clear();
    QVERIFY2(store->commit(QStringLiteral("pvf: noop"), &error),
             qPrintable(error));
    QCOMPARE(store->history(50, &error).size(), countBefore);

    entity.name = QStringLiteral("改名");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("save project"), &error),
             qPrintable(error));

    history = store->history(1, &error);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().message, QStringLiteral("pvf: save project"));
    QCOMPARE(store->history(50, &error).size(), countBefore + 1);
}

void StoreTests::assetImportCommit()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QTemporaryDir source;
    QVERIFY(source.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    const QString pngPath = source.filePath(QStringLiteral("dot.png"));
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(pngPath));

    const auto asset = store->assets().importFile(pngPath, &error);
    QVERIFY2(asset.has_value(), qPrintable(error));
    const QString assetDirectory =
        QStringLiteral("assets/") + asset->hash;
    QVERIFY(QFileInfo(asset->directory).isDir());
    QVERIFY(QFileInfo(asset->directory + QStringLiteral("/asset.json")).isFile());

    QVERIFY2(store->commit(QStringLiteral("pvf: import asset"), &error),
             qPrintable(error));

    const CommandResult tracked =
        runGit(store->root(),
               {QStringLiteral("ls-files"), QStringLiteral("--"),
                QStringLiteral("assets")});
    QCOMPARE(tracked.exitCode, 0);
    QVERIFY2(tracked.out.trimmed().isEmpty(), qPrintable(tracked.out));

    const CommandResult status =
        runGit(store->root(),
               {QStringLiteral("status"), QStringLiteral("--porcelain"),
                QStringLiteral("--untracked-files=all")});
    QCOMPARE(status.exitCode, 0);
    QVERIFY2(!status.out.contains(QStringLiteral("assets/")),
             qPrintable(status.out));

    const CommandResult ignored =
        runGit(store->root(),
               {QStringLiteral("check-ignore"), QStringLiteral("-q"),
                assetDirectory + QStringLiteral("/asset.json")});
    QCOMPARE(ignored.exitCode, 0);

    const CommandResult show =
        runGit(store->root(),
               {QStringLiteral("show"), QStringLiteral("--name-only"),
                QStringLiteral("--format="), QStringLiteral("HEAD")});
    QCOMPARE(show.exitCode, 0);
    QVERIFY2(!show.out.contains(QStringLiteral("assets/")),
             qPrintable(show.out));
}

void StoreTests::commitFailureReportsStderr()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity();
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));

    const QString hookPath =
        store->root() + QStringLiteral("/.git/hooks/pre-commit");
    QVERIFY(writeText(hookPath,
                      QStringLiteral("#!/bin/sh\n"
                                     "echo hook-boom >&2\n"
                                     "exit 1\n")));
    QVERIFY(QFile::setPermissions(
        hookPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
            | QFileDevice::ReadGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::ExeOther));

    error.clear();
    QVERIFY(!store->commit(QStringLiteral("pvf: should fail"), &error));
    QVERIFY2(error.contains(QStringLiteral("hook-boom")), qPrintable(error));

    QVERIFY(QFile::remove(hookPath));
    error.clear();
    QVERIFY2(store->commit(QStringLiteral("pvf: retry"), &error),
             qPrintable(error));
    QCOMPARE(store->history(1, &error).first().message,
             QStringLiteral("pvf: retry"));
}

void StoreTests::undoRedoRestoresText()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("原标题"));
    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: create entity"), &error),
             qPrintable(error));

    const int commits = store->history(50, &error).size();
    QCOMPARE(commits, 2);

    // An unrelated file staged by the user must survive undo/redo unchanged.
    const QString userPath = path + QStringLiteral("/user-notes.txt");
    QVERIFY(writeText(userPath, QStringLiteral("keep me\n")));
    QCOMPARE(runGit(store->root(),
                    {QStringLiteral("add"), QStringLiteral("user-notes.txt")})
                 .exitCode,
             0);

    model::Entity renamed = store->project().entities.value(entity.uuid);
    renamed.name = QStringLiteral("新标题");
    store->mutableProject().entities.insert(entity.uuid, renamed);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: rename"), &error),
             qPrintable(error));
    QCOMPARE(store->history(50, &error).size(), commits + 1);

    // Undo only touches the memory stack: git log must not grow.
    QVERIFY2(store->undo(&error), qPrintable(error));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("原标题"));
    QVERIFY2(readText(entityPath).contains(QStringLiteral("原标题")),
             qPrintable(readText(entityPath)));
    QCOMPARE(store->history(50, &error).size(), commits + 1);
    QVERIFY(runGit(store->root(),
                   {QStringLiteral("status"), QStringLiteral("--porcelain")})
                .out.contains(QStringLiteral("user-notes.txt")));
    QCOMPARE(store->history(1, &error).first().message,
             QStringLiteral("pvf: rename"));

    QVERIFY2(store->redo(&error), qPrintable(error));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("新标题"));
    QVERIFY2(readText(entityPath).contains(QStringLiteral("新标题")),
             qPrintable(readText(entityPath)));
    QCOMPARE(store->history(50, &error).size(), commits + 1);

    // Memory history is not persisted across sessions.
    auto reopened = ProjectStore::open(path, &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    error.clear();
    QVERIFY(!reopened->undo(&error));
    QVERIFY(!error.isEmpty());
}

void StoreTests::undoProjectSettings()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    store->mutableProject().settings.name = QStringLiteral("改过的工程名");
    QVERIFY2(store->saveProject(&error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: rename project"), &error),
             qPrintable(error));
    const int commits = store->history(50, &error).size();

    QVERIFY2(store->undo(&error), qPrintable(error));
    QCOMPARE(store->project().settings.name, QStringLiteral("未命名项目"));
    QCOMPARE(store->history(50, &error).size(), commits);

    QVERIFY2(store->redo(&error), qPrintable(error));
    QCOMPARE(store->project().settings.name, QStringLiteral("改过的工程名"));
    QCOMPARE(store->history(50, &error).size(), commits);
}

void StoreTests::undoNewAndDeletedFiles()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("甲乙"));
    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: create entity"), &error),
             qPrintable(error));
    const int commits = store->history(50, &error).size();

    // Undoing a new file deletes it; redo brings it back.
    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(entityPath));
    QVERIFY(!store->project().entities.contains(entity.uuid));
    QCOMPARE(store->history(50, &error).size(), commits);

    QVERIFY2(store->redo(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(entityPath));
    QVERIFY(store->project().entities.contains(entity.uuid));
    QCOMPARE(store->history(50, &error).size(), commits);

    // Undoing a deletion restores the file and the model entry.
    QVERIFY2(store->deleteEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(entityPath));
    QVERIFY2(store->commit(QStringLiteral("pvf: delete entity"), &error),
             qPrintable(error));
    const int afterDelete = store->history(50, &error).size();

    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(entityPath));
    QVERIFY(store->project().entities.contains(entity.uuid));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("甲乙"));
    QCOMPARE(store->history(50, &error).size(), afterDelete);

    QVERIFY2(store->redo(&error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(entityPath));
    QVERIFY(!store->project().entities.contains(entity.uuid));
    QCOMPARE(store->history(50, &error).size(), afterDelete);
}

void StoreTests::undoStackLimits()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    // Empty stack reports a clear error instead of silently doing nothing.
    error.clear();
    QVERIFY(!store->undo(&error));
    QVERIFY2(error.contains(QStringLiteral("撤销")), qPrintable(error));
    error.clear();
    QVERIFY(!store->redo(&error));
    QVERIFY2(error.contains(QStringLiteral("重做")), qPrintable(error));

    // Entry-count cap: keep only the newest two operations.
    store->setUndoLimitsForTest(2, 512LL * 1024 * 1024);
    model::Entity entity = makeEntity(QStringLiteral("v1"));
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: v1"), &error), qPrintable(error));

    entity.name = QStringLiteral("v2");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: v2"), &error), qPrintable(error));

    entity.name = QStringLiteral("v3");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: v3"), &error), qPrintable(error));

    QCOMPARE(store->undoDepth(), 2);
    QVERIFY2(store->undo(&error), qPrintable(error));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("v2"));
    QVERIFY2(store->undo(&error), qPrintable(error));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("v1"));
    error.clear();
    QVERIFY(!store->undo(&error));

    // Byte cap: an entry larger than the cap is evicted immediately.
    store->setUndoLimitsForTest(200, 1);
    QCOMPARE(store->undoDepth(), 0);
    entity.name = QStringLiteral("v4");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: v4"), &error), qPrintable(error));
    QCOMPARE(store->undoDepth(), 0);
    error.clear();
    QVERIFY(!store->undo(&error));
}

void StoreTests::assetsSurviveUndo()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QTemporaryDir source;
    QVERIFY(source.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    const QString pngPath = source.filePath(QStringLiteral("dot.png"));
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(pngPath));

    const auto asset = store->assets().importFile(pngPath, &error);
    QVERIFY2(asset.has_value(), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: import asset"), &error),
             qPrintable(error));
    // Assets never produce undo entries.
    QCOMPARE(store->undoDepth(), 0);

    model::Entity entity = makeEntity(QStringLiteral("素材实体"));
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = asset->hash;
    entity.content.asset.type = AssetType::Image;
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: add asset entity"), &error),
             qPrintable(error));

    const QString assetJson = asset->directory + QStringLiteral("/asset.json");
    QVERIFY(QFileInfo::exists(assetJson));
    QVERIFY(store->assets().get(asset->hash).has_value());

    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY(!store->project().entities.contains(entity.uuid));
    // The asset was neither recorded nor rolled back.
    QVERIFY(store->assets().get(asset->hash).has_value());
    QVERIFY(QFileInfo::exists(assetJson));
    bool hasContent = false;
    const QFileInfoList files =
        QDir(asset->directory).entryInfoList(QStringList{QStringLiteral("content.*")},
                                             QDir::Files);
    hasContent = !files.isEmpty();
    QVERIFY2(hasContent, qPrintable(asset->directory));
}

void StoreTests::corruptProjectJson()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);
    const QString projectFile = path + QStringLiteral("/project.json");

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    QVERIFY(writeText(projectFile, QStringLiteral("{ this is not json")));
    error.clear();
    QVERIFY(!ProjectStore::open(path, &error));
    QVERIFY2(error.contains(QStringLiteral("JSON")), qPrintable(error));

    QVERIFY(writeText(projectFile,
                      QStringLiteral("{\"format\":\"other\",\"version\":1}")));
    error.clear();
    QVERIFY(!ProjectStore::open(path, &error));
    QVERIFY2(error.contains(QStringLiteral("PVForge")), qPrintable(error));

    QVERIFY(writeText(projectFile,
                      QStringLiteral("{\"format\":\"pvforge-project\","
                                     "\"version\":99}")));
    error.clear();
    QVERIFY(!ProjectStore::open(path, &error));
    QVERIFY2(error.contains(QStringLiteral("版本")), qPrintable(error));

    QVERIFY(QFile::remove(projectFile));
    error.clear();
    QVERIFY(!ProjectStore::open(path, &error));
    QVERIFY2(error.contains(QStringLiteral("project.json")), qPrintable(error));
}

void StoreTests::corruptEntityJson()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    const QString uuid = model::newUuid();
    const QString entityFile =
        path + QStringLiteral("/entities/") + uuid + QStringLiteral(".json");

    QVERIFY(writeText(entityFile, QStringLiteral("{ broken")));
    error.clear();
    QVERIFY(!ProjectStore::open(path, &error));
    QVERIFY2(error.contains(uuid), qPrintable(error));

    model::Entity other = makeEntity();
    QVERIFY(writeText(entityFile,
                      QString::fromUtf8(
                          QJsonDocument(model::toJson(other)).toJson())));
    error.clear();
    QVERIFY(!ProjectStore::open(path, &error));
    QVERIFY2(error.contains(QStringLiteral("不一致")), qPrintable(error));

    QVERIFY(writeText(entityFile, QStringLiteral("{}")));
    error.clear();
    QVERIFY(!ProjectStore::open(path, &error));
    QVERIFY(!error.isEmpty());
}

void StoreTests::nonGitDirectoryRejected()
{
    // Opening a non-git project with the default useGit=true now succeeds with
    // git disabled instead of rejecting the directory.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Project project;
    const QByteArray json =
        QJsonDocument(model::toJson(project)).toJson(QJsonDocument::Indented);
    QVERIFY(writeText(temp.filePath(QStringLiteral("project.json")),
                      QString::fromUtf8(json)));

    QString error;
    auto opened = ProjectStore::open(temp.path(), &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(!opened->gitEnabled());
    QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral(".git"))));

    error.clear();
    QVERIFY(!ProjectStore::open(temp.filePath(QStringLiteral("missing")), &error));
    QVERIFY2(error.contains(QStringLiteral("不存在")), qPrintable(error));
}

void StoreTests::missingGitCreateFallsBack()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    const QString path = temp.filePath(QStringLiteral("Demo.pvproj"));
    auto created = ProjectStore::create(path, model::ProjectSettings(), &error,
                                        false);
    QVERIFY2(created.has_value(), qPrintable(error));

    PathGuard guard;
    QVERIFY(qputenv("PATH", QByteArray("/nonexistent")));

    // Creating with an explicit git request still creates a plain store.
    const QString withGit = temp.filePath(QStringLiteral("WithGit.pvproj"));
    error.clear();
    auto fallback =
        ProjectStore::create(withGit, model::ProjectSettings(), &error);
    QVERIFY2(fallback.has_value(), qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!fallback->gitEnabled());
    QVERIFY(!QFileInfo::exists(withGit + QStringLiteral("/.git")));
    QVERIFY(QFileInfo::exists(withGit + QStringLiteral("/project.json")));

    // The fallback store still commits to its in-memory undo stack.
    model::Entity entity = makeEntity();
    fallback->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(fallback->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(fallback->commit(QStringLiteral("pvf: plain"), &error),
             qPrintable(error));
    QCOMPARE(fallback->undoDepth(), 1);

    // Opening falls back to a plain text store.
    error.clear();
    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(!opened->gitEnabled());
}

void StoreTests::createRejectsInvalidInput()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    model::ProjectSettings badSettings;
    badSettings.fps = 0;
    QVERIFY(!ProjectStore::create(temp.filePath(QStringLiteral("bad.pvproj")),
                                 badSettings, &error));
    QVERIFY2(error.contains(QStringLiteral("fps")), qPrintable(error));

    error.clear();
    QVERIFY(!ProjectStore::create(QString(), model::ProjectSettings(), &error));
    QVERIFY(!error.isEmpty());

    SKIP_WITHOUT_GIT();

    const QString nonEmpty = temp.filePath(QStringLiteral("full.pvproj"));
    QVERIFY(QDir().mkpath(nonEmpty));
    QVERIFY(writeText(nonEmpty + QStringLiteral("/keep.txt"),
                      QStringLiteral("keep")));
    error.clear();
    QVERIFY(!ProjectStore::create(nonEmpty, model::ProjectSettings(), &error));
    QVERIFY2(error.contains(QStringLiteral("不为空")), qPrintable(error));
}

void StoreTests::cleanProjectHasNoDirtyText()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));
    QVERIFY(store->dirtyTextFiles(&error).isEmpty());
    QVERIFY(store->dirtyText().isEmpty());

    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY2(opened->dirtyTextFiles(&error).isEmpty(), qPrintable(error));
    QVERIFY(opened->dirtyText().isEmpty());
}

void StoreTests::dirtyTextDetectedOnOpen()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("标题"));
    const QString relativePath =
        QStringLiteral("entities/") + entity.uuid + QStringLiteral(".json");
    const QString entityPath = path + QLatin1Char('/') + relativePath;
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: create entity"), &error),
             qPrintable(error));

    // Changed on disk without a commit, as after a crash.
    QVERIFY(writeText(entityPath, readText(entityPath) + QStringLiteral("\n")));

    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QCOMPARE(opened->dirtyText().size(), 1);
    QCOMPARE(opened->dirtyText().first().path, relativePath);
    QCOMPARE(opened->dirtyText().first().status, QStringLiteral("modified"));

    const QVector<DirtyText> dirty = opened->dirtyTextFiles(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(dirty.size(), 1);
    QCOMPARE(dirty.first().path, relativePath);
    QCOMPARE(dirty.first().status, QStringLiteral("modified"));

    // A deleted tracked file is reported as deleted.
    QVERIFY(QFile::remove(entityPath));
    auto reopened = ProjectStore::open(path, &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    const QVector<DirtyText> deleted = reopened->dirtyTextFiles(&error);
    QCOMPARE(deleted.size(), 1);
    QCOMPARE(deleted.first().path, relativePath);
    QCOMPARE(deleted.first().status, QStringLiteral("deleted"));
}

void StoreTests::commitRecoveredTextCreatesCommit()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity tracked = makeEntity(QStringLiteral("已提交"));
    const QString trackedRelative =
        QStringLiteral("entities/") + tracked.uuid + QStringLiteral(".json");
    const QString trackedPath = path + QLatin1Char('/') + trackedRelative;
    store->mutableProject().entities.insert(tracked.uuid, tracked);
    QVERIFY2(store->saveEntity(tracked.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: create entity"), &error),
             qPrintable(error));

    QVERIFY(writeText(trackedPath,
                      readText(trackedPath) + QStringLiteral("\n")));

    model::Entity added = makeEntity(QStringLiteral("崩溃时新增"));
    const QString addedRelative =
        QStringLiteral("entities/") + added.uuid + QStringLiteral(".json");
    const QString addedPath = path + QLatin1Char('/') + addedRelative;
    QVERIFY(writeText(addedPath,
                      QString::fromUtf8(
                          QJsonDocument(model::toJson(added)).toJson())));

    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QCOMPARE(opened->dirtyText().size(), 2);

    const int before = opened->history(50, &error).size();
    error.clear();
    QVERIFY2(opened->commitRecoveredText(&error), qPrintable(error));

    const QVector<GitCommit> history = opened->history(50, &error);
    QCOMPARE(history.size(), before + 1);
    QCOMPARE(history.first().message, QStringLiteral("pvf: recover"));
    QVERIFY(opened->dirtyText().isEmpty());
    error.clear();
    QVERIFY2(opened->dirtyTextFiles(&error).isEmpty(), qPrintable(error));

    const CommandResult show =
        runGit(opened->root(),
               {QStringLiteral("show"), QStringLiteral("--name-only"),
                QStringLiteral("--format="), QStringLiteral("HEAD")});
    QCOMPARE(show.exitCode, 0);
    QVERIFY2(show.out.contains(trackedRelative), qPrintable(show.out));
    QVERIFY2(show.out.contains(addedRelative), qPrintable(show.out));

    const CommandResult status =
        runGit(opened->root(),
               {QStringLiteral("status"), QStringLiteral("--porcelain")});
    QVERIFY2(!status.out.contains(QStringLiteral("entities/")),
             qPrintable(status.out));
}

void StoreTests::discardDirtyTextRestores()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity kept = makeEntity(QStringLiteral("保留甲"));
    model::Entity removed = makeEntity(QStringLiteral("删除乙"));
    model::Entity staged = makeEntity(QStringLiteral("暂存丙"));
    store->mutableProject().entities.insert(kept.uuid, kept);
    store->mutableProject().entities.insert(removed.uuid, removed);
    store->mutableProject().entities.insert(staged.uuid, staged);
    QVERIFY2(store->saveEntity(kept.uuid, &error), qPrintable(error));
    QVERIFY2(store->saveEntity(removed.uuid, &error), qPrintable(error));
    QVERIFY2(store->saveEntity(staged.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: add entities"), &error),
             qPrintable(error));

    const auto entityPath = [&path](const model::Entity& entity) {
        return path + QStringLiteral("/entities/") + entity.uuid
               + QStringLiteral(".json");
    };
    const QString keptPath = entityPath(kept);
    const QString removedPath = entityPath(removed);
    const QString stagedPath = entityPath(staged);
    const QString keptBytes = readText(keptPath);
    const QString removedBytes = readText(removedPath);
    const QString stagedBytes = readText(stagedPath);

    // Unstaged edit, unstaged deletion, and a change staged by an incomplete
    // commit (crash between `git add` and `git commit`).
    QVERIFY(writeText(keptPath, keptBytes + QStringLiteral("\n")));
    QVERIFY(QFile::remove(removedPath));
    QVERIFY(writeText(stagedPath, stagedBytes + QStringLiteral("\n")));
    QCOMPARE(runGit(path, {QStringLiteral("add"),
                           QStringLiteral("entities/") + staged.uuid
                               + QStringLiteral(".json")})
                 .exitCode,
             0);

    // An untracked entity written just before the crash.
    model::Entity untracked = makeEntity(QStringLiteral("新增丁"));
    const QString untrackedPath = entityPath(untracked);
    QVERIFY(writeText(untrackedPath,
                      QString::fromUtf8(
                          QJsonDocument(model::toJson(untracked)).toJson())));

    // Ignored media/cache and an unrelated user file must survive discard.
    const QString assetPath =
        path + QStringLiteral("/assets/deadbeef/asset.json");
    const QString cachePath = path + QStringLiteral("/.cache/frame.webp");
    const QString notesPath = path + QStringLiteral("/notes.txt");
    QVERIFY(writeText(assetPath, QStringLiteral("local media\n")));
    QVERIFY(writeText(cachePath, QStringLiteral("cache\n")));
    QVERIFY(writeText(notesPath, QStringLiteral("user notes\n")));

    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QCOMPARE(opened->dirtyText().size(), 4);

    const QVector<DirtyText> dirty = opened->dirtyTextFiles(&error);
    QCOMPARE(dirty.size(), 4);
    QStringList paths;
    for (const DirtyText& item : dirty)
        paths << item.path;
    QVERIFY(paths.contains(QStringLiteral("entities/") + kept.uuid
                           + QStringLiteral(".json")));
    QVERIFY(paths.contains(QStringLiteral("entities/") + removed.uuid
                           + QStringLiteral(".json")));
    QVERIFY(paths.contains(QStringLiteral("entities/") + staged.uuid
                           + QStringLiteral(".json")));
    QVERIFY(paths.contains(QStringLiteral("entities/") + untracked.uuid
                           + QStringLiteral(".json")));
    for (const DirtyText& item : dirty)
    {
        QVERIFY(!item.path.startsWith(QStringLiteral("assets/")));
        QVERIFY(!item.path.startsWith(QStringLiteral(".cache/")));
    }

    error.clear();
    QVERIFY2(opened->discardDirtyText(&error), qPrintable(error));

    QCOMPARE(readText(keptPath), keptBytes);
    QCOMPARE(readText(removedPath), removedBytes);
    QCOMPARE(readText(stagedPath), stagedBytes);
    QVERIFY(!QFileInfo::exists(untrackedPath));
    QVERIFY(QFileInfo::exists(assetPath));
    QVERIFY(QFileInfo::exists(cachePath));
    QVERIFY(QFileInfo::exists(notesPath));

    QVERIFY(opened->dirtyText().isEmpty());
    error.clear();
    QVERIFY2(opened->dirtyTextFiles(&error).isEmpty(), qPrintable(error));

    QVERIFY2(opened->reloadAfterDiscard(&error), qPrintable(error));
    QVERIFY(opened->project().entities.contains(kept.uuid));
    QVERIFY(opened->project().entities.contains(removed.uuid));
    QVERIFY(opened->project().entities.contains(staged.uuid));
    QVERIFY(!opened->project().entities.contains(untracked.uuid));
    QCOMPARE(opened->project().entities.value(kept.uuid).name,
             QStringLiteral("保留甲"));
    QCOMPARE(opened->project().entities.value(removed.uuid).name,
             QStringLiteral("删除乙"));
    QCOMPARE(opened->project().entities.value(staged.uuid).name,
             QStringLiteral("暂存丙"));
}

void StoreTests::restoreCommitRewindsText()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("提交 A 的实体"));
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit A"), &error),
             qPrintable(error));

    QVector<GitCommit> history = store->history(1, &error);
    QCOMPARE(history.size(), 1);
    const QString commitA = history.first().hash;
    const QString entityRelative =
        QStringLiteral("entities/") + entity.uuid + QStringLiteral(".json");
    const QString entityPath = path + QLatin1Char('/') + entityRelative;
    const QString bytesA = readText(entityPath);

    entity.name = QStringLiteral("提交 B 的实体");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit B"), &error),
             qPrintable(error));

    history = store->history(1, &error);
    QCOMPARE(history.size(), 1);
    const QString headB = history.first().hash;
    QVERIFY(headB != commitA);
    const int commits = store->history(50, &error).size();

    error.clear();
    QVERIFY2(store->restoreCommit(commitA, &error), qPrintable(error));

    QCOMPARE(readText(entityPath), bytesA);
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("提交 A 的实体"));

    history = store->history(50, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(history.size(), commits + 1);
    QCOMPARE(history.first().message,
             QStringLiteral("pvf: restore ") + commitA.left(7));
    QVERIFY(history.first().hash != headB);

    const CommandResult head =
        runGit(store->root(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QCOMPARE(head.exitCode, 0);
    QCOMPARE(head.out.trimmed(), history.first().hash);

    const CommandResult status =
        runGit(store->root(),
               {QStringLiteral("status"), QStringLiteral("--porcelain")});
    QCOMPARE(status.exitCode, 0);
    QVERIFY2(status.out.trimmed().isEmpty(), qPrintable(status.out));
}

void StoreTests::restoreCommitDeletesNewEntities()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity kept = makeEntity(QStringLiteral("旧实体"));
    store->mutableProject().entities.insert(kept.uuid, kept);
    QVERIFY2(store->saveEntity(kept.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit A"), &error),
             qPrintable(error));
    const QString commitA = store->history(1, &error).first().hash;

    model::Entity added = makeEntity(QStringLiteral("后来新增"));
    store->mutableProject().entities.insert(added.uuid, added);
    QVERIFY2(store->saveEntity(added.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: add entity"), &error),
             qPrintable(error));

    const QString keptPath = path + QStringLiteral("/entities/") + kept.uuid
                             + QStringLiteral(".json");
    const QString addedPath = path + QStringLiteral("/entities/") + added.uuid
                              + QStringLiteral(".json");
    QVERIFY(QFileInfo::exists(keptPath));
    QVERIFY(QFileInfo::exists(addedPath));

    error.clear();
    QVERIFY2(store->restoreCommit(commitA, &error), qPrintable(error));

    QVERIFY(!QFileInfo::exists(addedPath));
    QVERIFY(!store->project().entities.contains(added.uuid));
    QVERIFY(QFileInfo::exists(keptPath));
    QVERIFY(store->project().entities.contains(kept.uuid));

    const CommandResult lsTree =
        runGit(store->root(),
               {QStringLiteral("ls-tree"), QStringLiteral("-r"),
                QStringLiteral("--name-only"), QStringLiteral("HEAD"),
                QStringLiteral("--"), QStringLiteral("entities")});
    QCOMPARE(lsTree.exitCode, 0);
    QVERIFY2(!lsTree.out.contains(added.uuid), qPrintable(lsTree.out));
}

void StoreTests::restoreCommitRejectsDirtyText()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("实体"));
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit A"), &error),
             qPrintable(error));
    const QString commitA = store->history(1, &error).first().hash;

    const QString relativePath =
        QStringLiteral("entities/") + entity.uuid + QStringLiteral(".json");
    const QString entityPath = path + QLatin1Char('/') + relativePath;
    const QString original = readText(entityPath);
    QVERIFY(writeText(entityPath, original + QStringLiteral("\n")));
    const QString dirtyBytes = readText(entityPath);

    const QString headBefore =
        runGit(store->root(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")})
            .out.trimmed();
    const int commits = store->history(50, &error).size();

    error.clear();
    QVERIFY(!store->restoreCommit(commitA, &error));
    QVERIFY2(error.contains(relativePath), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("未提交")), qPrintable(error));

    // Nothing was discarded or committed.
    QCOMPARE(readText(entityPath), dirtyBytes);
    QCOMPARE(store->history(50, &error).size(), commits);
    QCOMPARE(runGit(store->root(),
                    {QStringLiteral("rev-parse"), QStringLiteral("HEAD")})
                 .out.trimmed(),
             headBefore);
}

void StoreTests::restoreCommitRejectsInvalidHash()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    const QString headBefore =
        runGit(store->root(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")})
            .out.trimmed();
    const int commits = store->history(50, &error).size();
    const QString missing =
        QStringLiteral("0123456789abcdef0123456789abcdef01234567");

    error.clear();
    QVERIFY(!store->restoreCommit(missing, &error));
    QVERIFY2(error.contains(QStringLiteral("无效")), qPrintable(error));

    error.clear();
    QVERIFY(!store->restoreCommit(QString(), &error));
    QVERIFY(!error.isEmpty());

    // An existing object that is not a commit must be rejected too.
    QVERIFY(writeText(path + QStringLiteral("/blob.txt"),
                      QStringLiteral("not a commit\n")));
    const CommandResult blob =
        runGit(store->root(),
               {QStringLiteral("hash-object"), QStringLiteral("-w"),
                QStringLiteral("blob.txt")});
    QCOMPARE(blob.exitCode, 0);
    error.clear();
    QVERIFY(!store->restoreCommit(blob.out.trimmed(), &error));
    QVERIFY2(error.contains(QStringLiteral("不是 commit")), qPrintable(error));

    QCOMPARE(store->history(50, &error).size(), commits);
    QCOMPARE(runGit(store->root(),
                    {QStringLiteral("rev-parse"), QStringLiteral("HEAD")})
                 .out.trimmed(),
             headBefore);
}

void StoreTests::restoreCommitClearsStacksAndKeepsAssets()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    // Ignored media and cache files must survive a restore.
    const QString assetPath = path + QStringLiteral("/assets/deadbeef/asset.json");
    const QString cachePath = path + QStringLiteral("/.cache/frame.webp");
    QVERIFY(writeText(assetPath, QStringLiteral("local media\n")));
    QVERIFY(writeText(cachePath, QStringLiteral("cache\n")));

    model::Entity entity = makeEntity(QStringLiteral("v1"));
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit A"), &error),
             qPrintable(error));
    const QString commitA = store->history(1, &error).first().hash;
    const QString entityPath = path + QStringLiteral("/entities/") + entity.uuid
                               + QStringLiteral(".json");
    const QString bytesA = readText(entityPath);

    entity.name = QStringLiteral("v2");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit B"), &error),
             qPrintable(error));

    entity.name = QStringLiteral("v3");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit C"), &error),
             qPrintable(error));

    // Undo leaves an in-memory redo entry and a dirty text tree; discard the
    // dirty bytes so the restore may proceed, then check both stacks are
    // cleared by it.
    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY(store->undoDepth() > 0);
    QVERIFY(store->redoDepth() > 0);
    QVERIFY2(store->discardDirtyText(&error), qPrintable(error));
    QVERIFY2(store->reloadAfterDiscard(&error), qPrintable(error));
    QVERIFY(store->undoDepth() > 0);
    QVERIFY(store->redoDepth() > 0);

    error.clear();
    QVERIFY2(store->restoreCommit(commitA, &error), qPrintable(error));

    QCOMPARE(store->undoDepth(), 0);
    QCOMPARE(store->redoDepth(), 0);
    error.clear();
    QVERIFY(!store->undo(&error));
    error.clear();
    QVERIFY(!store->redo(&error));

    QCOMPARE(readText(entityPath), bytesA);
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("v1"));
    QVERIFY(QFileInfo::exists(assetPath));
    QVERIFY(QFileInfo::exists(cachePath));
}

void StoreTests::restoreCommitRollsBackOnCommitFailure()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("v1"));
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit A"), &error),
             qPrintable(error));
    const QString commitA = store->history(1, &error).first().hash;

    entity.name = QStringLiteral("v2");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: commit B"), &error),
             qPrintable(error));

    const QString headB = store->history(1, &error).first().hash;
    const int commits = store->history(50, &error).size();
    const QString entityPath = path + QStringLiteral("/entities/") + entity.uuid
                               + QStringLiteral(".json");
    const QString bytesB = readText(entityPath);

    const QString hookPath =
        store->root() + QStringLiteral("/.git/hooks/pre-commit");
    QVERIFY(writeText(hookPath,
                      QStringLiteral("#!/bin/sh\n"
                                     "echo hook-boom >&2\n"
                                     "exit 1\n")));
    QVERIFY(QFile::setPermissions(
        hookPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
            | QFileDevice::ReadGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::ExeOther));

    error.clear();
    QVERIFY(!store->restoreCommit(commitA, &error));
    QVERIFY2(error.contains(QStringLiteral("hook-boom")), qPrintable(error));

    // The failed restore rolled the text tree, index and HEAD back.
    QCOMPARE(readText(entityPath), bytesB);
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("v2"));
    QCOMPARE(store->history(50, &error).size(), commits);
    QCOMPARE(store->history(1, &error).first().hash, headB);

    const CommandResult status =
        runGit(store->root(),
               {QStringLiteral("status"), QStringLiteral("--porcelain")});
    QVERIFY2(status.out.trimmed().isEmpty(), qPrintable(status.out));
    const CommandResult staged =
        runGit(store->root(),
               {QStringLiteral("diff"), QStringLiteral("--cached"),
                QStringLiteral("--name-only")});
    QVERIFY2(staged.out.trimmed().isEmpty(), qPrintable(staged.out));
}

void StoreTests::createWithoutGit()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto created = ProjectStore::create(path, model::ProjectSettings(), &error,
                                        false);
    QVERIFY2(created.has_value(), qPrintable(error));
    QVERIFY(!created->gitEnabled());
    QVERIFY(!QFileInfo::exists(path + QStringLiteral("/.git")));
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/project.json")));
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/.gitignore")));
    QVERIFY(QFileInfo(path + QStringLiteral("/entities")).isDir());
    QVERIFY(QFileInfo(path + QStringLiteral("/assets")).isDir());

    model::Entity entity = makeEntity(QStringLiteral("原标题"));
    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    created->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(created->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(created->commit(QStringLiteral("pvf: create"), &error),
             qPrintable(error));
    QCOMPARE(created->undoDepth(), 1);

    entity.name = QStringLiteral("新标题");
    created->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(created->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(created->commit(QStringLiteral("pvf: rename"), &error),
             qPrintable(error));
    QCOMPARE(created->undoDepth(), 2);

    QVERIFY2(created->undo(&error), qPrintable(error));
    QCOMPARE(created->project().entities.value(entity.uuid).name,
             QStringLiteral("原标题"));
    QVERIFY2(readText(entityPath).contains(QStringLiteral("原标题")),
             qPrintable(readText(entityPath)));

    error.clear();
    QVERIFY(created->history(50, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("Git 未启用")), qPrintable(error));
    error.clear();
    QVERIFY(!created->restoreCommit(QStringLiteral("deadbeef"), &error));
    QVERIFY2(error.contains(QStringLiteral("Git 未启用")), qPrintable(error));
    error.clear();
    QVERIFY(!created->commitRecoveredText(&error));
    QVERIFY2(error.contains(QStringLiteral("Git 未启用")), qPrintable(error));
    error.clear();
    QVERIFY(!created->discardDirtyText(&error));
    QVERIFY2(error.contains(QStringLiteral("Git 未启用")), qPrintable(error));
    QVERIFY(created->dirtyText().isEmpty());

    error.clear();
    QVERIFY(created->dirtyTextFiles(&error).isEmpty());
    QVERIFY2(error.isEmpty(), qPrintable(error));

    error.clear();
    auto opened = ProjectStore::open(path, &error, false);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(!opened->gitEnabled());
    QCOMPARE(opened->project().entities.value(entity.uuid).name,
             QStringLiteral("原标题"));
    QCOMPARE(opened->undoDepth(), 0);
    error.clear();
    QVERIFY(!opened->undo(&error));
}

void StoreTests::openWithoutGitRepoAndGitRequested()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Project project;
    const QByteArray json =
        QJsonDocument(model::toJson(project)).toJson(QJsonDocument::Indented);
    QVERIFY(writeText(temp.filePath(QStringLiteral("project.json")),
                      QString::fromUtf8(json)));

    QString error;
    auto opened = ProjectStore::open(temp.path(), &error, true);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(!opened->gitEnabled());
    QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral(".git"))));
    QVERIFY(opened->dirtyText().isEmpty());

    error.clear();
    auto plain = ProjectStore::open(temp.path(), &error, false);
    QVERIFY2(plain.has_value(), qPrintable(error));
    QVERIFY(!plain->gitEnabled());
}

void StoreTests::enableGitLater()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));
    QVERIFY(!store->gitEnabled());

    const QString assetPath = path + QStringLiteral("/assets/local/asset.json");
    const QString cachePath = path + QStringLiteral("/.cache/frame.webp");
    QVERIFY(writeText(assetPath, QStringLiteral("local media\n")));
    QVERIFY(writeText(cachePath, QStringLiteral("cache\n")));

    model::Entity entity = makeEntity(QStringLiteral("启用前"));
    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: before git"), &error),
             qPrintable(error));

    error.clear();
    QVERIFY2(store->setGitEnabled(true, &error), qPrintable(error));
    QVERIFY(store->gitEnabled());
    QVERIFY(QFileInfo(path + QStringLiteral("/.git")).isDir());
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/.gitignore")));

    QVector<GitCommit> history = store->history(50, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(history.size() >= 1);
    QCOMPARE(history.first().message, QStringLiteral("pvf: init"));
    const QString initHash = history.first().hash;

    const CommandResult tracked =
        runGit(store->root(),
               {QStringLiteral("ls-files"), QStringLiteral("--")});
    QCOMPARE(tracked.exitCode, 0);
    QVERIFY2(tracked.out.contains(QStringLiteral("project.json")),
             qPrintable(tracked.out));
    QVERIFY2(tracked.out.contains(QStringLiteral("entities/") + entity.uuid
                                  + QStringLiteral(".json")),
             qPrintable(tracked.out));
    QVERIFY2(!tracked.out.contains(QStringLiteral("assets/")),
             qPrintable(tracked.out));
    QVERIFY2(!tracked.out.contains(QStringLiteral(".cache/")),
             qPrintable(tracked.out));

    const CommandResult status =
        runGit(store->root(),
               {QStringLiteral("status"), QStringLiteral("--porcelain"),
                QStringLiteral("--untracked-files=all")});
    QCOMPARE(status.exitCode, 0);
    QVERIFY2(!status.out.contains(QStringLiteral("assets/")),
             qPrintable(status.out));
    QVERIFY2(!status.out.contains(QStringLiteral(".cache/")),
             qPrintable(status.out));

    const int before = history.size();
    entity.name = QStringLiteral("启用后");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: after git"), &error),
             qPrintable(error));
    history = store->history(50, &error);
    QCOMPARE(history.size(), before + 1);
    QCOMPARE(history.first().message, QStringLiteral("pvf: after git"));

    error.clear();
    QVERIFY2(store->restoreCommit(initHash, &error), qPrintable(error));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("启用前"));
    QVERIFY2(readText(entityPath).contains(QStringLiteral("启用前")),
             qPrintable(readText(entityPath)));
    QVERIFY(QFileInfo::exists(assetPath));
    QVERIFY(QFileInfo::exists(cachePath));
}

void StoreTests::disableGitThenCommitStillUndoable()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));
    QVERIFY(store->gitEnabled());

    model::Entity entity = makeEntity(QStringLiteral("v1"));
    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: v1"), &error), qPrintable(error));

    const QString countBefore =
        runGit(store->root(),
               {QStringLiteral("rev-list"), QStringLiteral("--count"),
                QStringLiteral("HEAD")})
            .out.trimmed();

    QVERIFY2(store->setGitEnabled(false, &error), qPrintable(error));
    QVERIFY(!store->gitEnabled());
    QVERIFY(store->dirtyText().isEmpty());

    entity.name = QStringLiteral("v2");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: v2"), &error),
             qPrintable(error));

    const CommandResult countAfter =
        runGit(store->root(),
               {QStringLiteral("rev-list"), QStringLiteral("--count"),
                QStringLiteral("HEAD")});
    QCOMPARE(countAfter.exitCode, 0);
    QCOMPARE(countAfter.out.trimmed(), countBefore);

    QVERIFY2(store->undo(&error), qPrintable(error));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("v1"));
    QVERIFY2(readText(entityPath).contains(QStringLiteral("v1")),
             qPrintable(readText(entityPath)));

    QVERIFY2(store->redo(&error), qPrintable(error));
    QCOMPARE(store->project().entities.value(entity.uuid).name,
             QStringLiteral("v2"));
}

void StoreTests::setGitEnabledReportsMissingGit()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));

    PathGuard guard;
    QVERIFY(qputenv("PATH", QByteArray("/nonexistent")));

    QVERIFY(!ProjectStore::gitBinaryAvailable());
    error.clear();
    QVERIFY(!store->setGitEnabled(true, &error));
    QVERIFY2(error.contains(QStringLiteral("git")), qPrintable(error));
    QVERIFY(!store->gitEnabled());
    QVERIFY(!QFileInfo::exists(path + QStringLiteral("/.git")));
}

void StoreTests::openRejectsFutureVersion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QJsonObject object = model::toJson(model::Project{});
    object.insert(QStringLiteral("version"), model::kProjectFormatVersion + 1);
    const QString text =
        QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    QVERIFY(writeText(projectFile, text));

    QString error;
    QVERIFY(!ProjectStore::open(temp.path(), &error, false));
    QVERIFY2(error.contains(QStringLiteral("工程版本")), qPrintable(error));
    QVERIFY2(error.contains(QString::number(model::kProjectFormatVersion + 1)),
             qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("升级")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("不会被修改")), qPrintable(error));
    // Opening never writes, so the refused file must be byte-identical.
    QCOMPARE(readText(projectFile), text);
}

void StoreTests::openRejectsUnmigratableVersion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    // The production chain starts at v1, so a v0 document is below the oldest
    // supported version: opening must fail readably and leave it alone instead
    // of half-parsing it.
    QJsonObject object = model::toJson(model::Project{});
    object.insert(QStringLiteral("version"), 0);
    object.insert(QStringLiteral("legacyScene"),
                  QJsonObject{{QStringLiteral("name"), QStringLiteral("旧")}});
    const QString text =
        QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    QVERIFY(writeText(projectFile, text));

    QString error;
    QVERIFY(!ProjectStore::open(temp.path(), &error, false));
    QVERIFY2(error.contains(QStringLiteral("过旧")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("最低支持版本")), qPrintable(error));
    QVERIFY2(error.contains(QString::number(model::migrations::minimumSupportedVersion())),
             qPrintable(error));
    QCOMPARE(readText(projectFile), text);
}

void StoreTests::openMigratesV1ProjectAndEntity()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    // The exact shape PVForge wrote before v2: project.json at version 1 and an
    // entity file with neither its own format marker nor its own version.
    model::Project project;
    project.settings.name = QStringLiteral("v1 工程");

    model::Entity entity = makeEntity(QStringLiteral("v1 实体"), 0, 30);
    entity.transform.x = 12.5;
    model::Keyframe key;
    key.frame = 7;
    key.value = 3.5;
    entity.tracks.insert(QStringLiteral("x"), model::Track{{key}});

    QJsonObject projectObject = model::toJson(project);
    projectObject.insert(QStringLiteral("version"), 1);

    QJsonObject entityObject = model::toJson(entity);
    entityObject.remove(QStringLiteral("version"));
    entityObject.remove(QStringLiteral("format"));

    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    const QString entityFile = temp.filePath(QStringLiteral("entities/")
                                             + entity.uuid
                                             + QStringLiteral(".json"));
    const QString projectText =
        QString::fromUtf8(QJsonDocument(projectObject).toJson(QJsonDocument::Indented));
    const QString entityText =
        QString::fromUtf8(QJsonDocument(entityObject).toJson(QJsonDocument::Indented));
    QVERIFY(writeText(projectFile, projectText));
    QVERIFY(writeText(entityFile, entityText));

    QString error;
    auto opened = ProjectStore::open(temp.path(), &error, false);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QCOMPARE(opened->project().settings.name, QStringLiteral("v1 工程"));

    const auto it = opened->project().entities.constFind(entity.uuid);
    QVERIFY(it != opened->project().entities.constEnd());
    QCOMPARE(it->name, QStringLiteral("v1 实体"));
    QCOMPARE(it->transform.x, 12.5);
    QCOMPARE(it->tracks.value(QStringLiteral("x")).keys.size(), 1);
    QCOMPARE(it->tracks.value(QStringLiteral("x")).keys.first().frame, 7);

    // Opening never writes, so both v1 files stay byte-identical until a save.
    QCOMPARE(readText(projectFile), projectText);
    QCOMPARE(readText(entityFile), entityText);

    // Saving writes the current version and the entity format marker.
    QVERIFY2(opened->saveProject(&error), qPrintable(error));
    QVERIFY2(opened->saveEntity(entity.uuid, &error), qPrintable(error));

    const QJsonObject savedProject =
        QJsonDocument::fromJson(readText(projectFile).toUtf8()).object();
    QCOMPARE(savedProject.value(QStringLiteral("version")).toInt(),
             model::kProjectFormatVersion);
    const QJsonObject savedEntity =
        QJsonDocument::fromJson(readText(entityFile).toUtf8()).object();
    QCOMPARE(savedEntity.value(QStringLiteral("version")).toInt(),
             model::kProjectFormatVersion);
    QCOMPARE(savedEntity.value(QStringLiteral("format")).toString(),
             QString::fromLatin1(model::kEntityFormat));

    // The upgraded files reopen with the same data.
    error.clear();
    auto reopened = ProjectStore::open(temp.path(), &error, false);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    const auto reopenedIt = reopened->project().entities.constFind(entity.uuid);
    QVERIFY(reopenedIt != reopened->project().entities.constEnd());
    QCOMPARE(reopenedIt->name, QStringLiteral("v1 实体"));
    QCOMPARE(reopenedIt->transform.x, 12.5);
    QCOMPARE(reopenedIt->tracks.value(QStringLiteral("x")).keys.first().value, 3.5);
}

void StoreTests::openRejectsFutureEntityVersion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Entity entity = makeEntity(QStringLiteral("未来实体"));
    QJsonObject entityObject = model::toJson(entity);
    entityObject.insert(QStringLiteral("version"),
                        model::kProjectFormatVersion + 1);
    const QString entityText =
        QString::fromUtf8(QJsonDocument(entityObject).toJson(QJsonDocument::Indented));

    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    const QString entityFile = temp.filePath(QStringLiteral("entities/")
                                             + entity.uuid
                                             + QStringLiteral(".json"));
    QVERIFY(writeText(
        projectFile,
        QString::fromUtf8(QJsonDocument(model::toJson(model::Project{}))
                              .toJson(QJsonDocument::Indented))));
    QVERIFY(writeText(entityFile, entityText));

    QString error;
    QVERIFY(!ProjectStore::open(temp.path(), &error, false));
    // The error names the file and its uuid, not just "entity invalid".
    QVERIFY2(error.contains(entity.uuid), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("实体版本")), qPrintable(error));
    QVERIFY2(error.contains(QString::number(model::kProjectFormatVersion + 1)),
             qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("不会被修改")), qPrintable(error));
    QCOMPARE(readText(entityFile), entityText);
}

void StoreTests::openRejectsStaleEntityVersion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Entity entity = makeEntity(QStringLiteral("过旧实体"));
    QJsonObject entityObject = model::toJson(entity);
    entityObject.insert(QStringLiteral("version"), 0);
    const QString entityText =
        QString::fromUtf8(QJsonDocument(entityObject).toJson(QJsonDocument::Indented));

    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    const QString entityFile = temp.filePath(QStringLiteral("entities/")
                                             + entity.uuid
                                             + QStringLiteral(".json"));
    QVERIFY(writeText(
        projectFile,
        QString::fromUtf8(QJsonDocument(model::toJson(model::Project{}))
                              .toJson(QJsonDocument::Indented))));
    QVERIFY(writeText(entityFile, entityText));

    QString error;
    QVERIFY(!ProjectStore::open(temp.path(), &error, false));
    QVERIFY2(error.contains(entity.uuid), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("过旧")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("最低支持版本")), qPrintable(error));
    QCOMPARE(readText(entityFile), entityText);
}

void StoreTests::openStrictRequiredFields()
{
    // project.json: a required settings field is gone.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QJsonObject object = model::toJson(model::Project{});
        object.remove(QStringLiteral("fps"));
        QVERIFY(writeText(
            temp.filePath(QStringLiteral("project.json")),
            QString::fromUtf8(
                QJsonDocument(object).toJson(QJsonDocument::Indented))));

        QString error;
        QVERIFY(!ProjectStore::open(temp.path(), &error, false));
        QVERIFY2(error.contains(QStringLiteral("fps")), qPrintable(error));
    }

    // Entity file: the error names the missing field.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(writeText(
            temp.filePath(QStringLiteral("project.json")),
            QString::fromUtf8(QJsonDocument(model::toJson(model::Project{}))
                                  .toJson(QJsonDocument::Indented))));

        model::Entity entity = makeEntity();
        QJsonObject entityObject = model::toJson(entity);
        entityObject.remove(QStringLiteral("start"));
        QVERIFY(writeText(
            temp.filePath(QStringLiteral("entities/") + entity.uuid
                          + QStringLiteral(".json")),
            QString::fromUtf8(
                QJsonDocument(entityObject).toJson(QJsonDocument::Indented))));

        QString error;
        QVERIFY(!ProjectStore::open(temp.path(), &error, false));
        QVERIFY2(error.contains(QStringLiteral("start")), qPrintable(error));
        QVERIFY2(error.contains(entity.uuid), qPrintable(error));
    }
}

void StoreTests::unknownFieldsSurviveReopen()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Project project;
    project.settings.name = QStringLiteral("未知字段");
    project.extras.insert(QStringLiteral("futureProjectField"),
                          QJsonObject{{QStringLiteral("a"), 1}});

    model::Entity entity = makeEntity(QStringLiteral("带未知字段"));
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("x");
    entity.extras.insert(QStringLiteral("futureEntityField"), 3.25);
    entity.content.extras.insert(QStringLiteral("futureContentField"), true);
    project.entities.insert(entity.uuid, entity);

    QVERIFY(writeText(
        temp.filePath(QStringLiteral("project.json")),
        QString::fromUtf8(QJsonDocument(model::toJson(project))
                              .toJson(QJsonDocument::Indented))));
    const QString entityFile = temp.filePath(QStringLiteral("entities/")
                                             + entity.uuid
                                             + QStringLiteral(".json"));
    QVERIFY(writeText(entityFile,
                      QString::fromUtf8(QJsonDocument(model::toJson(entity))
                                            .toJson(QJsonDocument::Indented))));

    QString error;
    auto opened = ProjectStore::open(temp.path(), &error, false);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(opened->project().extras.contains(
        QStringLiteral("futureProjectField")));
    QCOMPARE(opened->project()
                 .entities.value(entity.uuid)
                 .extras.value(QStringLiteral("futureEntityField"))
                 .toDouble(),
             3.25);
    QVERIFY(opened->project()
                .entities.value(entity.uuid)
                .content.extras.contains(QStringLiteral("futureContentField")));

    // Saving writes the unknown keys back instead of dropping them.
    QVERIFY2(opened->saveProject(&error), qPrintable(error));
    QVERIFY2(opened->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY(readText(temp.filePath(QStringLiteral("project.json")))
                .contains(QStringLiteral("futureProjectField")));
    const QString entityText = readText(entityFile);
    QVERIFY2(entityText.contains(QStringLiteral("futureEntityField")),
             qPrintable(entityText));
    QVERIFY2(entityText.contains(QStringLiteral("futureContentField")),
             qPrintable(entityText));

    // A second open sees the same document.
    error.clear();
    auto reopened = ProjectStore::open(temp.path(), &error, false);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QVERIFY(reopened->project().extras.contains(
        QStringLiteral("futureProjectField")));
    QVERIFY(reopened->project()
                .entities.value(entity.uuid)
                .extras.contains(QStringLiteral("futureEntityField")));
}

QTEST_GUILESS_MAIN(StoreTests)

#include "store_tests.moc"
