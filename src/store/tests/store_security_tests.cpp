// Security tests for opening untrusted project repositories (issue #121).
// They live in their own translation unit so store_tests.cpp stays below the
// code-size ratchet; the methods still belong to StoreTests.

#include "store/Store.h"

#include "store_tests.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;
using namespace pvf::store;

namespace {

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

QString projectPath(const QTemporaryDir& temp)
{
    return temp.filePath(QStringLiteral("Demo.pvproj"));
}

model::Entity makeEntity()
{
    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("实体");
    entity.start = 0;
    entity.end = 30;
    return entity;
}

} // namespace

void StoreTests::commitIgnoresProjectHooks()
{
    if (!gitAvailable())
        QSKIP("git 不可用，跳过依赖 git 的测试");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity();
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));

    // A downloaded project can ship any executable hook. PVForge must run git
    // with core.hooksPath=/dev/null, so this script never executes.
    const QString marker = path + QStringLiteral("/hook-ran.txt");
    const QString hookPath =
        store->root() + QStringLiteral("/.git/hooks/pre-commit");
    QVERIFY(writeText(hookPath,
                      QStringLiteral("#!/bin/sh\n"
                                     "touch '%1'\n"
                                     "echo hook-boom >&2\n"
                                     "exit 1\n")
                          .arg(marker)));
    QVERIFY(QFile::setPermissions(
        hookPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
            | QFileDevice::ReadGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::ExeOther));

    error.clear();
    QVERIFY2(store->commit(QStringLiteral("pvf: hooks must not run"), &error),
             qPrintable(error));
    QCOMPARE(store->history(1, &error).first().message,
             QStringLiteral("pvf: hooks must not run"));
    QVERIFY2(!QFileInfo::exists(marker), "project pre-commit hook was executed");

    // restoreCommit and the recovery commit use the same hardened runner.
    const QString commit = store->history(1, &error).first().hash;
    entity.name = QStringLiteral("v2");
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: second"), &error),
             qPrintable(error));
    error.clear();
    QVERIFY2(store->restoreCommit(commit, &error), qPrintable(error));
    QVERIFY2(!QFileInfo::exists(marker), "hook ran during restoreCommit");
}

void StoreTests::commitRejectsCleanFilter()
{
    if (!gitAvailable())
        QSKIP("git 不可用，跳过依赖 git 的测试");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity();
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));

    // The filter is declared in the repository config and bound by project
    // .gitattributes, so `git add` would run it while staging entities/*.json.
    const QString marker = path + QStringLiteral("/filter-ran.txt");
    QFile config(path + QStringLiteral("/.git/config"));
    QVERIFY(config.open(QIODevice::Append));
    config.write(QStringLiteral("[filter \"evil\"]\n"
                                "\tclean = touch '%1'\n"
                                "\tsmudge = cat\n")
                     .arg(marker)
                     .toUtf8());
    config.close();
    QVERIFY(writeText(path + QStringLiteral("/.gitattributes"),
                      QStringLiteral("*.json filter=evil\n")));

    error.clear();
    QVERIFY(!store->commit(QStringLiteral("pvf: must be refused"), &error));
    QVERIFY2(error.contains(QStringLiteral("不可信")), qPrintable(error));
    QVERIFY2(!QFileInfo::exists(marker), "clean filter was executed");
}

void StoreTests::openRefusesForeignRepository()
{
    if (!gitAvailable())
        QSKIP("git 不可用，跳过依赖 git 的测试");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));
    QVERIFY(store->gitEnabled());

    // A repository without the PVForge marker may have any hooks/config.
    QVERIFY(QFile::remove(path + QStringLiteral("/.git/pvforge-repo")));
    error.clear();
    auto foreign = ProjectStore::open(path, &error);
    QVERIFY2(foreign.has_value(), qPrintable(error));
    QVERIFY(!foreign->gitEnabled());
    QVERIFY2(foreign->gitWarning().contains(QStringLiteral("PVForge")),
             qPrintable(foreign->gitWarning()));

    // Marking it as ours makes it usable again.
    QVERIFY(writeText(path + QStringLiteral("/.git/pvforge-repo"),
                      QStringLiteral("format=pvforge-git-repository\nversion=1\n")));
    error.clear();
    auto marked = ProjectStore::open(path, &error);
    QVERIFY2(marked.has_value(), qPrintable(error));
    QVERIFY(marked->gitEnabled());
    QVERIFY(marked->gitWarning().isEmpty());
}

void StoreTests::openRefusesGitDirPointer()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto created =
        ProjectStore::create(path, model::ProjectSettings(), &error, false);
    QVERIFY2(created.has_value(), qPrintable(error));
    QVERIFY(writeText(path + QStringLiteral("/.git"),
                      QStringLiteral("gitdir: /tmp/not-pvforge\n")));

    error.clear();
    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(!opened->gitEnabled());
    QVERIFY2(opened->gitWarning().contains(QStringLiteral(".git")),
             qPrintable(opened->gitWarning()));
}

void StoreTests::openRejectsSymlinkedTextFiles()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto created =
        ProjectStore::create(path, model::ProjectSettings(), &error, false);
    QVERIFY2(created.has_value(), qPrintable(error));

    // project.json is project data; a symlink could point at any local file.
    const QString projectFile = path + QStringLiteral("/project.json");
    const QString outside = temp.filePath(QStringLiteral("outside.json"));
    QVERIFY(QFile::rename(projectFile, outside));
    QVERIFY(QFile::link(outside, projectFile));

    error.clear();
    QVERIFY(!ProjectStore::open(path, &error, false));
    QVERIFY2(error.contains(QStringLiteral("符号链接")), qPrintable(error));

    // The same rule applies to entities/<uuid>.json.
    QVERIFY(QFile::remove(projectFile));
    QVERIFY(QFile::rename(outside, projectFile));
    const QString uuid = model::newUuid();
    const QString entityFile = path + QStringLiteral("/entities/") + uuid
                               + QStringLiteral(".json");
    const QString outsideEntity = temp.filePath(QStringLiteral("outside-entity.json"));
    QVERIFY(writeText(outsideEntity, QStringLiteral("{}")));
    QVERIFY(QFile::link(outsideEntity, entityFile));

    error.clear();
    QVERIFY(!ProjectStore::open(path, &error, false));
    QVERIFY2(error.contains(QStringLiteral("符号链接")), qPrintable(error));
}

void StoreTests::openRejectsOversizedTextFile()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto created =
        ProjectStore::create(path, model::ProjectSettings(), &error, false);
    QVERIFY2(created.has_value(), qPrintable(error));

    // 64 MiB + 1 sparse file: rejected by stat before any allocation.
    QFile projectFile(path + QStringLiteral("/project.json"));
    QVERIFY(projectFile.open(QIODevice::WriteOnly));
    QVERIFY(projectFile.resize(64LL * 1024LL * 1024LL + 1));
    projectFile.close();
    QVERIFY(QFileInfo(projectFile).size() > 64LL * 1024LL * 1024LL);

    error.clear();
    QVERIFY(!ProjectStore::open(path, &error, false));
    QVERIFY2(error.contains(QStringLiteral("64MB")), qPrintable(error));
}

void StoreTests::openRefusesUnsafeConfig()
{
    if (!gitAvailable())
        QSKIP("git 不可用，跳过依赖 git 的测试");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    QFile config(path + QStringLiteral("/.git/config"));
    QVERIFY(config.open(QIODevice::Append));
    config.write(QByteArrayLiteral("[core]\n\thooksPath = /tmp/evil-hooks\n"));
    config.close();

    error.clear();
    auto opened = ProjectStore::open(path, &error);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(!opened->gitEnabled());
    QVERIFY2(opened->gitWarning().contains(QStringLiteral("hookspath"),
                                          Qt::CaseInsensitive),
             qPrintable(opened->gitWarning()));

    // Refusing to enable Git must not silently accept the same repository.
    error.clear();
    QVERIFY(!opened->setGitEnabled(true, &error));
    QVERIFY2(error.contains(QStringLiteral("不安全的 Git 仓库")), qPrintable(error));
}
