#include "Store.h"

#include "store/ProjectStoreSupport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace pvf::store {

using namespace detail;

bool ProjectStore::trashAsset(const AssetHash& hash, QString* error)
{
    if (!ensureWritable(error))
        return false;
    if (hash.isEmpty())
    {
        setError(error, QStringLiteral("素材哈希不能为空"));
        return false;
    }

    const QString source = assetLivePath(m_root, hash);
    if (!QFileInfo(source).isDir())
    {
        setError(error, QStringLiteral("素材不存在：%1").arg(hash));
        return false;
    }

    const QString target = assetTrashPath(m_root, hash);
    if (!QDir().mkpath(absolutePath(m_root, QStringLiteral(".trash"))))
    {
        setError(error, QStringLiteral("无法创建回收站目录：%1").arg(target));
        return false;
    }
    if (QFileInfo::exists(target) && !QDir(target).removeRecursively())
    {
        setError(error, QStringLiteral("无法清理回收站旧内容：%1").arg(target));
        return false;
    }
    // Record the pre-move location before touching the directory: undo reads
    // this map to know where the bytes came from.
    noteAssetTouch(hash);
    m_pendingHumanEdit = true;
    if (!QDir().rename(source, target))
    {
        setError(error,
                 QStringLiteral("无法把素材移入回收站：%1").arg(hash));
        return false;
    }

    return true;
}

QStringList ProjectStore::entitiesReferencingAsset(const AssetHash& hash) const
{
    QStringList uuids;
    for (auto it = m_project.entities.constBegin();
         it != m_project.entities.constEnd(); ++it)
    {
        if (it.value().content.kind == ContentKind::Asset
            && it.value().content.asset.hash == hash)
        {
            uuids << it.key();
        }
    }
    return uuids;
}

QSet<QString> ProjectStore::trashedAssets() const
{
    QSet<QString> hashes;
    const QDir trash(absolutePath(m_root, QStringLiteral(".trash")));
    for (const QString& name :
         trash.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        hashes.insert(name);
    }
    return hashes;
}

bool ProjectStore::applyAssetTrashState(const QHash<QString, bool>& state,
                                        QString* error)
{
    for (auto it = state.constBegin(); it != state.constEnd(); ++it)
    {
        const QString& hash = it.key();
        const QString assets = assetLivePath(m_root, hash);
        const QString trash = assetTrashPath(m_root, hash);
        const bool assetsExists = QFileInfo(assets).isDir();
        const bool trashExists = QFileInfo(trash).isDir();

        if (it.value())
        {
            if (trashExists)
            {
                // A re-import recreated the asset while the trash copy still
                // exists; keep the live bytes and leave the duplicate alone.
                continue;
            }
            if (!assetsExists)
                continue; // nothing left to move
            if (!QDir().mkpath(absolutePath(m_root, QStringLiteral(".trash")))
                || !QDir().rename(assets, trash))
            {
                setError(error,
                         QStringLiteral("无法把素材移入回收站：%1").arg(hash));
                return false;
            }
            continue;
        }

        if (!trashExists)
            continue; // already in place, or the bytes are gone for good
        if (assetsExists)
        {
            if (!QDir(trash).removeRecursively())
            {
                setError(error,
                         QStringLiteral("无法清理回收站副本：%1").arg(hash));
                return false;
            }
            continue;
        }
        QDir().mkpath(QFileInfo(assets).absolutePath());
        QDir().rename(trash, assets);
        if (!QFileInfo(assets).isDir())
        {
            setError(error, QStringLiteral("无法恢复素材：%1").arg(hash));
            return false;
        }
    }
    return true;
}

void ProjectStore::noteAssetTouch(const QString& hash)
{
    if (m_txnAssets.contains(hash))
        return;
    m_txnAssetBefore.insert(hash, QFileInfo(assetTrashPath(m_root, hash)).isDir());
    m_txnAssets.insert(hash);
}

} // namespace pvf::store
