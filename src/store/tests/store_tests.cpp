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
    void undoRedo();
    void corruptProjectJson();
    void corruptEntityJson();
    void nonGitDirectoryRejected();
    void missingGitToolReportsError();
    void createRejectsInvalidInput();
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

void StoreTests::undoRedo()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity();
    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: add entity"), &error),
             qPrintable(error));
    QVERIFY(QFileInfo::exists(entityPath));
    QVERIFY(store->project().entities.contains(entity.uuid));
    const QString operationHash = store->history(1, &error).first().hash;
    QCOMPARE(operationHash.size(), 40);

    // An unrelated file staged by the user must survive undo/redo unchanged.
    const QString userPath = path + QStringLiteral("/user-notes.txt");
    QVERIFY(writeText(userPath, QStringLiteral("keep me\n")));
    QCOMPARE(runGit(store->root(),
                    {QStringLiteral("add"), QStringLiteral("user-notes.txt")})
                 .exitCode,
             0);

    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(entityPath));
    QVERIFY(!store->project().entities.contains(entity.uuid));
    QCOMPARE(store->history(1, &error).first().message,
             QStringLiteral("pvf: undo ") + operationHash);

    const CommandResult undoShow =
        runGit(store->root(),
               {QStringLiteral("show"), QStringLiteral("--name-only"),
                QStringLiteral("--format="), QStringLiteral("HEAD")});
    QVERIFY2(!undoShow.out.contains(QStringLiteral("user-notes.txt")),
             qPrintable(undoShow.out));
    QVERIFY(runGit(store->root(),
                   {QStringLiteral("status"), QStringLiteral("--porcelain")})
                .out.contains(QStringLiteral("user-notes.txt")));

    error.clear();
    QVERIFY(!store->undo(&error));
    QVERIFY(!error.isEmpty());

    QVERIFY2(store->redo(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(entityPath));
    QVERIFY(store->project().entities.contains(entity.uuid));
    QVERIFY(store->history(1, &error).first().message.startsWith(
        QStringLiteral("pvf: redo ")));

    // Undo again, then make sure the stacks survive a reopen.
    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(entityPath));

    auto reopened = ProjectStore::open(path, &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QVERIFY(!reopened->project().entities.contains(entity.uuid));

    error.clear();
    QVERIFY(!reopened->undo(&error));
    QVERIFY(!error.isEmpty());

    QVERIFY2(reopened->redo(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(entityPath));
    QVERIFY(reopened->project().entities.contains(entity.uuid));

    auto reopenedAgain = ProjectStore::open(path, &error);
    QVERIFY2(reopenedAgain.has_value(), qPrintable(error));
    QVERIFY(reopenedAgain->project().entities.contains(entity.uuid));
    QVERIFY2(reopenedAgain->undo(&error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(entityPath));
    QVERIFY(!reopenedAgain->project().entities.contains(entity.uuid));
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
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Project project;
    const QByteArray json =
        QJsonDocument(model::toJson(project)).toJson(QJsonDocument::Indented);
    QVERIFY(writeText(temp.filePath(QStringLiteral("project.json")),
                      QString::fromUtf8(json)));

    QString error;
    QVERIFY(!ProjectStore::open(temp.path(), &error));
    QVERIFY2(error.contains(QStringLiteral("git")), qPrintable(error));

    error.clear();
    QVERIFY(!ProjectStore::open(temp.filePath(QStringLiteral("missing")), &error));
    QVERIFY2(error.contains(QStringLiteral("不存在")), qPrintable(error));
}

void StoreTests::missingGitToolReportsError()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    PathGuard guard;
    QVERIFY(qputenv("PATH", QByteArray("/nonexistent")));

    QString error;
    auto created = ProjectStore::create(temp.filePath(QStringLiteral("Demo.pvproj")),
                                        model::ProjectSettings(), &error);
    QVERIFY(!created.has_value());
    QVERIFY2(error.contains(QStringLiteral("git")), qPrintable(error));

    error.clear();
    QVERIFY(!ProjectStore::open(temp.path(), &error));
    QVERIFY2(error.contains(QStringLiteral("git")), qPrintable(error));
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

QTEST_GUILESS_MAIN(StoreTests)

#include "store_tests.moc"
