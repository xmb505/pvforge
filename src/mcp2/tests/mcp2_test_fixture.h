#pragma once

// Shared scaffolding for the mcp2 suites. Before this header each file carried
// its own copy of gitAvailable()/Fixture/errorCode(); the copies only differed
// in the temp project name and in the safety confirmer, so those two stay as
// parameters instead of being flattened away.

#include "mcp2/Service.h"

#include "model/Model.h"
#include "store/Store.h"

#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <optional>

namespace pvf::mcp2test {

// Production only needs `git`; git-lfs is not a runtime dependency.
inline bool gitAvailable()
{
    QProcess process;
    process.start(QStringLiteral("git"), {QStringLiteral("--version")});
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
        return false;
    return process.exitStatus() == QProcess::NormalExit
           && process.exitCode() == 0;
}

inline constexpr char kSkipMessage[] = "git 不可用，跳过依赖 git 的测试";

inline QString errorCode(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
        .toString();
}

struct Fixture
{
    QTemporaryDir temp;
    std::optional<store::ProjectStore> store;
    std::unique_ptr<mcp2::Service> service;

    // projectFile keeps the historical temp project name per suite;
    // autoConfirmExternalImports is for the data-safety suite, which exercises
    // persistence rather than the human-confirmation dialog.
    bool init(const QString& projectFile = QStringLiteral("Demo.pvproj"),
              bool autoConfirmExternalImports = false)
    {
        if (!temp.isValid())
            return false;
        QString error;
        auto created = store::ProjectStore::create(temp.filePath(projectFile),
                                                   model::ProjectSettings(),
                                                   &error);
        if (!created)
        {
            qWarning("ProjectStore::create failed: %s", qPrintable(error));
            return false;
        }
        store = std::move(*created);
        service = std::make_unique<mcp2::Service>(&*store, &store->assets());
        if (autoConfirmExternalImports)
        {
            service->setExternalImportConfirmer([](const QString&) {
                return true;
            });
        }
        return true;
    }

    QJsonObject call(const QString& tool,
                     const QJsonObject& args = QJsonObject())
    {
        return service->execute(tool, args);
    }

    int historySize() { return store->history(0).size(); }
};

} // namespace pvf::mcp2test
