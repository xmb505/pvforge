#include "assets/Pvhtml.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

namespace pvf::assets {

namespace {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

bool isSafeRelativePath(const QString& path)
{
    if (path.isEmpty() || path.contains(QLatin1Char('\0')))
        return false;
    const QString native = QDir::fromNativeSeparators(path);
    if (QDir::isAbsolutePath(native))
        return false;
    if (native.size() >= 2 && native[0].isLetter() && native[1] == QLatin1Char(':'))
        return false;
    const QString clean = QDir::cleanPath(native);
    return clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"))
           && !clean.startsWith(QLatin1Char('/'));
}

bool copyFile(const QString& source, const QString& target, QString* error)
{
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
    {
        setError(error, QStringLiteral("无法创建目录: %1")
                            .arg(QFileInfo(target).absolutePath()));
        return false;
    }
    if (!QFile::copy(source, target))
    {
        setError(error, QStringLiteral("无法复制文件: %1 -> %2").arg(source, target));
        return false;
    }
    return true;
}

bool copyTreeContents(const QString& sourceDir, const QString& destDir, QString* error)
{
    if (!QDir().mkpath(destDir))
    {
        setError(error, QStringLiteral("无法创建目录: %1").arg(destDir));
        return false;
    }

    const QDir source(sourceDir);
    const QFileInfoList entries = source.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const QFileInfo& entry : entries)
    {
        if (entry.isSymLink())
        {
            setError(error, QStringLiteral("pvhtml 包内不允许符号链接: %1")
                                .arg(entry.fileName()));
            return false;
        }
        const QString target = destDir + QLatin1Char('/') + entry.fileName();
        if (entry.isDir())
        {
            if (!copyTreeContents(entry.absoluteFilePath(), target, error))
                return false;
        }
        else if (!copyFile(entry.absoluteFilePath(), target, error))
        {
            return false;
        }
    }
    return true;
}

bool validateZipEntries(const QString& zipPath, QString* error)
{
    const QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    if (unzip.isEmpty())
    {
        setError(error, QStringLiteral("需要 unzip 才能导入 zip 格式的 pvhtml"));
        return false;
    }

    QProcess list;
    list.start(unzip, {QStringLiteral("-Z1"), zipPath});
    if (!list.waitForFinished(120000) || list.exitStatus() != QProcess::NormalExit
        || list.exitCode() != 0)
    {
        setError(error, QStringLiteral("无法读取 zip 内容: %1")
                            .arg(QString::fromLocal8Bit(list.readAllStandardError()).trimmed()));
        return false;
    }

    const QList<QByteArray> lines = list.readAllStandardOutput().split('\n');
    for (const QByteArray& rawLine : lines)
    {
        QString entry = QString::fromLocal8Bit(rawLine).trimmed();
        if (entry.isEmpty())
            continue;
        entry.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!isSafeRelativePath(entry))
        {
            setError(error, QStringLiteral("zip 包含不安全的路径: %1").arg(entry));
            return false;
        }
    }
    return true;
}

bool extractZip(const QString& zipPath, const QString& destDir, QString* error)
{
    const QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    if (unzip.isEmpty())
    {
        setError(error, QStringLiteral("需要 unzip 才能导入 zip 格式的 pvhtml"));
        return false;
    }

    QProcess process;
    process.start(unzip, {QStringLiteral("-q"), QStringLiteral("-o"), zipPath,
                          QStringLiteral("-d"), destDir});
    if (!process.waitForFinished(300000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0)
    {
        setError(error, QStringLiteral("zip 解包失败: %1")
                            .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
        return false;
    }
    return true;
}

QString findBundleRoot(const QString& stageDir)
{
    const QFileInfo stage(stageDir);
    if (QFileInfo::exists(stageDir + QStringLiteral("/pvhtml.json"))
        || QFileInfo::exists(stageDir + QStringLiteral("/index.html")))
    {
        return stage.absoluteFilePath();
    }

    QStringList dirs = QDir(stageDir).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    dirs.removeAll(QStringLiteral("__MACOSX"));
    if (dirs.size() == 1)
    {
        const QString candidate = stage.absoluteFilePath() + QLatin1Char('/') + dirs.first();
        if (QFileInfo::exists(candidate + QStringLiteral("/pvhtml.json"))
            || QFileInfo::exists(candidate + QStringLiteral("/index.html")))
        {
            return candidate;
        }
    }
    return stage.absoluteFilePath();
}

bool readManifest(const QString& bundleRoot, PvhtmlManifest* manifest, QString* error)
{
    PvhtmlManifest result;
    const QString manifestPath = bundleRoot + QStringLiteral("/pvhtml.json");
    if (!QFileInfo::exists(manifestPath))
    {
        *manifest = result;
        return true;
    }

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(error, QStringLiteral("无法读取 %1: %2").arg(manifestPath, file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        setError(error, QStringLiteral("pvhtml.json 不是有效的 JSON 对象: %1")
                            .arg(parseError.errorString()));
        return false;
    }

    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("entry")))
    {
        const QJsonValue entry = object.value(QStringLiteral("entry"));
        if (!entry.isString() || entry.toString().isEmpty())
        {
            setError(error, QStringLiteral("pvhtml.json 的 entry 必须是非空字符串"));
            return false;
        }
        result.entry = entry.toString();
        if (!isSafeRelativePath(result.entry))
        {
            setError(error, QStringLiteral("pvhtml.json 的 entry 不是安全的相对路径: %1")
                                .arg(result.entry));
            return false;
        }
    }

    const QJsonValue logicalSize = object.value(QStringLiteral("logicalSize"));
    if (logicalSize.isObject())
    {
        const QJsonObject size = logicalSize.toObject();
        const QJsonValue width = size.value(QStringLiteral("width"));
        const QJsonValue height = size.value(QStringLiteral("height"));
        if (!width.isDouble() || !height.isDouble() || width.toInt() <= 0
            || height.toInt() <= 0)
        {
            setError(error, QStringLiteral("pvhtml.json 的 logicalSize.width/height 无效"));
            return false;
        }
        result.logicalSize = QSize(width.toInt(), height.toInt());
    }
    else if (logicalSize.isArray())
    {
        const QJsonArray size = logicalSize.toArray();
        if (size.size() != 2 || !size[0].isDouble() || !size[1].isDouble()
            || size[0].toInt() <= 0 || size[1].toInt() <= 0)
        {
            setError(error, QStringLiteral("pvhtml.json 的 logicalSize 数组无效"));
            return false;
        }
        result.logicalSize = QSize(size[0].toInt(), size[1].toInt());
    }
    else if (logicalSize.isUndefined())
    {
        const QJsonValue width = object.value(QStringLiteral("width"));
        const QJsonValue height = object.value(QStringLiteral("height"));
        if (width.isDouble() && height.isDouble() && width.toInt() > 0
            && height.toInt() > 0)
        {
            result.logicalSize = QSize(width.toInt(), height.toInt());
        }
    }
    else
    {
        setError(error, QStringLiteral("pvhtml.json 的 logicalSize 格式无效"));
        return false;
    }

    const QJsonValue variables = object.value(QStringLiteral("variables"));
    if (!variables.isUndefined())
    {
        if (!variables.isObject())
        {
            setError(error, QStringLiteral("pvhtml.json 的 variables 必须是对象"));
            return false;
        }
        result.variables = variables.toObject().toVariantMap();
    }

    *manifest = result;
    return true;
}

} // namespace

bool importPvhtml(const QString& sourceDirOrZip, const QString& destDir,
                  PvhtmlManifest* manifest, QString* error)
{
    const QFileInfo source(sourceDirOrZip);
    if (!source.exists())
    {
        setError(error, QStringLiteral("pvhtml 源不存在: %1").arg(sourceDirOrZip));
        return false;
    }

    QTemporaryDir stage;
    if (!stage.isValid())
    {
        setError(error, QStringLiteral("无法创建临时目录"));
        return false;
    }

    QString bundleRoot;
    if (source.isDir())
    {
        bundleRoot = source.absoluteFilePath();
    }
    else if (source.isFile())
    {
        QString zipError;
        if (!validateZipEntries(source.absoluteFilePath(), &zipError)
            || !extractZip(source.absoluteFilePath(), stage.path(), &zipError))
        {
            setError(error, zipError.isEmpty() ? QStringLiteral("zip 解包失败") : zipError);
            return false;
        }
        bundleRoot = findBundleRoot(stage.path());
    }
    else
    {
        setError(error, QStringLiteral("不支持的 pvhtml 源: %1").arg(sourceDirOrZip));
        return false;
    }

    if (QFileInfo(bundleRoot).isSymLink())
    {
        setError(error, QStringLiteral("pvhtml 包根目录不能是符号链接"));
        return false;
    }

    PvhtmlManifest parsed;
    if (!readManifest(bundleRoot, &parsed, error))
        return false;

    const QString entryPath = bundleRoot + QLatin1Char('/') + parsed.entry;
    if (!QFileInfo(entryPath).isFile())
    {
        setError(error, QStringLiteral("pvhtml 缺少入口文件: %1").arg(parsed.entry));
        return false;
    }

    const QString sourceAbsolute = QDir(bundleRoot).absolutePath();
    const QString destinationAbsolute = QFileInfo(destDir).absoluteFilePath();
    if (destinationAbsolute == sourceAbsolute
        || destinationAbsolute.startsWith(sourceAbsolute + QLatin1Char('/')))
    {
        setError(error, QStringLiteral("目标目录不能位于 pvhtml 源目录内部"));
        return false;
    }

    const QFileInfo destination(destDir);
    if (destination.exists() && !destination.isDir())
    {
        setError(error, QStringLiteral("目标路径不是目录: %1").arg(destDir));
        return false;
    }
    if (destination.isDir() && !QDir(destDir).isEmpty())
    {
        setError(error, QStringLiteral("目标目录必须为空: %1").arg(destDir));
        return false;
    }
    if (!QDir().mkpath(destDir))
    {
        setError(error, QStringLiteral("无法创建目标目录: %1").arg(destDir));
        return false;
    }

    if (!copyTreeContents(bundleRoot, destDir, error))
        return false;

    if (manifest)
        *manifest = parsed;
    return true;
}

} // namespace pvf::assets
