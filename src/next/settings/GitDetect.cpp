#include "next/settings/GitDetect.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace pvf::next {

GitInfo detectGit()
{
    GitInfo info;

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (executable.isEmpty()) {
        info.error = QStringLiteral("在 PATH 中找不到 git 可执行文件");
        return info;
    }
    info.executable = QFileInfo(executable).absoluteFilePath();

    QProcess process;
    process.start(info.executable, {QStringLiteral("--version")});
    if (!process.waitForStarted(3000)) {
        info.error = QStringLiteral("无法启动 git：%1").arg(process.errorString());
        return info;
    }
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();
        info.error = QStringLiteral("git --version 执行超时");
        return info;
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString stderrOutput =
        QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || output.isEmpty()) {
        info.error = !stderrOutput.isEmpty()
                         ? stderrOutput
                         : QStringLiteral("git --version 退出码 %1").arg(process.exitCode());
        return info;
    }

    info.found = true;
    info.version = output;
    return info;
}

} // namespace pvf::next
