#include "Store.h"

#include "store/ProjectStoreSupport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace pvf::store {

using namespace detail;

bool ProjectStore::undo(QString* error)
{
    if (!ensureWritable(error))
        return false;
    if (m_undoStack.isEmpty())
    {
        setError(error, QStringLiteral("没有可撤销的操作"));
        return false;
    }

    UndoEntry entry = m_undoStack.takeLast();
    if (!applySnapshot(entry.before, entry.assetsInTrashBefore, error))
    {
        m_undoStack.append(std::move(entry));
        return false;
    }

    if (!reload(error))
    {
        // Best effort: put the previous state back so the stack and the disk
        // stay consistent even though the reload failed.
        QString ignored;
        applySnapshot(entry.after, entry.assetsInTrashAfter, &ignored);
        reload(&ignored);
        m_undoStack.append(std::move(entry));
        return false;
    }

    // The restored state is on disk but not committed. Track it so the next
    // human save/flush writes it to git as the cold backup.
    for (auto it = entry.before.constBegin(); it != entry.before.constEnd(); ++it)
        noteExistingPath(it.key());

    m_redoStack.append(std::move(entry));
    trimStack(m_redoStack);
    return true;
}

bool ProjectStore::redo(QString* error)
{
    if (!ensureWritable(error))
        return false;
    if (m_redoStack.isEmpty())
    {
        setError(error, QStringLiteral("没有可重做的操作"));
        return false;
    }

    UndoEntry entry = m_redoStack.takeLast();
    if (!applySnapshot(entry.after, entry.assetsInTrashAfter, error))
    {
        m_redoStack.append(std::move(entry));
        return false;
    }

    if (!reload(error))
    {
        QString ignored;
        applySnapshot(entry.before, entry.assetsInTrashBefore, &ignored);
        reload(&ignored);
        m_redoStack.append(std::move(entry));
        return false;
    }

    for (auto it = entry.after.constBegin(); it != entry.after.constEnd(); ++it)
        noteExistingPath(it.key());

    m_undoStack.append(std::move(entry));
    trimStack(m_undoStack);
    return true;
}

bool ProjectStore::recordPendingAsUndo(const QString& message, QString* error)
{
    if (!recordTxnUndo(normalizedMessage(message), error))
        return false;
    clearDirty();
    return true;
}

#ifdef PVFORGE_TEST_HOOKS
void ProjectStore::setUndoLimitsForTest(int maxEntries, qint64 maxBytes)
{
    m_undoLimit = std::max(1, maxEntries);
    m_undoByteLimit = std::max<qint64>(1, maxBytes);
    trimStack(m_undoStack);
    trimStack(m_redoStack);
}
#endif

void ProjectStore::noteExistingPath(const QString& relativePath)
{
    if (m_txnBefore.contains(relativePath))
        return;
    QByteArray bytes;
    bool existed = false;
    if (!readFileBytes(absolutePath(m_root, relativePath), &bytes, &existed,
                       nullptr))
    {
        return;
    }
    m_txnBefore.insert(relativePath, existed ? bytes : QByteArray());
    m_txnPaths.insert(relativePath);
}

bool ProjectStore::recordTxnUndo(const QString& message, QString* error)
{
    QStringList candidates = m_txnPaths.values();
    std::sort(candidates.begin(), candidates.end());

    UndoEntry entry;
    entry.message = message;
    for (const QString& relativePath : std::as_const(candidates))
    {
        const QByteArray before = m_txnBefore.value(relativePath);
        QByteArray after;
        bool existed = false;
        if (!readFileBytes(absolutePath(m_root, relativePath), &after, &existed,
                           error))
        {
            return false;
        }
        if (!existed)
            after.clear();
        if (before == after)
            continue;
        entry.before.insert(relativePath, before);
        entry.after.insert(relativePath, after);
    }

    for (const QString& hash : std::as_const(m_txnAssets))
    {
        entry.assetsInTrashBefore.insert(hash, m_txnAssetBefore.value(hash));
        entry.assetsInTrashAfter.insert(
            hash, QFileInfo(assetTrashPath(m_root, hash)).isDir());
    }

    if (!entry.before.isEmpty() || !entry.assetsInTrashBefore.isEmpty())
        pushUndo(std::move(entry));
    return true;
}

void ProjectStore::pushUndo(UndoEntry entry)
{
    m_undoStack.append(std::move(entry));
    m_redoStack.clear();
    trimStack(m_undoStack);
}

void ProjectStore::trimStack(QVector<UndoEntry>& stack)
{
    const auto cost = [](const UndoEntry& entry) {
        qint64 bytes = 0;
        for (const QByteArray& before : entry.before)
            bytes += before.size();
        for (const QByteArray& after : entry.after)
            bytes += after.size();
        return bytes;
    };

    qint64 total = 0;
    for (const UndoEntry& entry : std::as_const(stack))
        total += cost(entry);

    while (!stack.isEmpty()
           && (stack.size() > m_undoLimit || total > m_undoByteLimit))
    {
        total -= cost(stack.first());
        stack.removeFirst();
    }
}

bool ProjectStore::applySnapshot(const QHash<QString, QByteArray>& snapshot,
                                 const QHash<QString, bool>& assetsInTrash,
                                 QString* error)
{
    QVector<TransactionOp> ops;
    ops.reserve(snapshot.size());
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
        ops.append(TransactionOp{it.key(), it.value(), it.value().isEmpty()});

    if (!ops.isEmpty())
    {
        if (!ensureWritable(error))
            return false;
        if (!verifyDigests(ops, error))
            return false;
        if (!applyFileTransaction(m_root, ops, error))
        {
            if (hasPendingTransaction(m_root))
            {
                setReadOnly(QStringLiteral(
                    "撤销时保存事务未完成，已留下可恢复日志。"
                    "请重新打开工程，程序会自动完成或回滚这次保存。"));
            }
            return false;
        }
        for (const TransactionOp& op : std::as_const(ops))
        {
            m_digests.insert(op.relativePath,
                             op.remove ? QString() : fileDigest(op.bytes));
        }
    }

    return applyAssetTrashState(assetsInTrash, error);
}

} // namespace pvf::store
