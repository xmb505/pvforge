#include "assets/AssetHash.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace pvf::assets {

namespace {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

void addBytes(QCryptographicHash& hash, const QByteArray& bytes)
{
    hash.addData(QByteArrayView(bytes));
}

void addSeparator(QCryptographicHash& hash)
{
    const char zero = '\0';
    hash.addData(QByteArrayView(&zero, 1));
}

} // namespace

QString sha256File(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(error, QStringLiteral("无法读取文件: %1 (%2)").arg(path, file.errorString()));
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
    {
        setError(error, QStringLiteral("读取文件失败: %1 (%2)").arg(path, file.errorString()));
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString sha256Directory(const QString& dir, QString* error)
{
    const QFileInfo info(dir);
    if (!info.exists() || !info.isDir())
    {
        setError(error, QStringLiteral("目录不存在: %1").arg(dir));
        return {};
    }

    const QDir root(dir);
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        files.append(root.relativeFilePath(it.filePath()));
    }
    std::sort(files.begin(), files.end());

    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& relative : files)
    {
        QFile file(root.filePath(relative));
        if (!file.open(QIODevice::ReadOnly))
        {
            setError(error, QStringLiteral("无法读取文件: %1 (%2)")
                                .arg(file.fileName(), file.errorString()));
            return {};
        }

        addBytes(hash, relative.toUtf8());
        addSeparator(hash);
        addBytes(hash, QByteArray::number(file.size()));
        addSeparator(hash);

        while (!file.atEnd())
        {
            const QByteArray chunk = file.read(1 << 16);
            if (chunk.isEmpty())
            {
                setError(error, QStringLiteral("读取文件失败: %1 (%2)")
                                    .arg(file.fileName(), file.errorString()));
                return {};
            }
            hash.addData(QByteArrayView(chunk));
        }
    }

    return QString::fromLatin1(hash.result().toHex());
}

} // namespace pvf::assets
