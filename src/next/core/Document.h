#pragma once

#include "assets/AssetStore.h"
#include "model/Model.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace pvf::next {

// Shared editing context for the new UI. The app shell implements this with a
// store::ProjectStore + playhead + selection; panels only depend on this
// interface so they can be built and tested without the whole window.
//
// All writes go through commitEntity()/commitProject(). Human writes are saved
// immediately and merged into one git commit by a short debounce; commitEntity()
// only reports whether the save succeeded. Call flush() before closing,
// switching projects or exporting to force the pending commit out.
//
// Undo/redo operate on the store's in-memory snapshots and never run git.
class Document : public QObject
{
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);
    ~Document() override;

    virtual const model::Project& project() const = 0;
    virtual model::Project& mutableProject() = 0;
    virtual assets::AssetStore& assets() = 0;

    virtual int playhead() const = 0;
    virtual void setPlayhead(int frame) = 0;

    virtual Uuid selection() const = 0;
    virtual void setSelection(const Uuid& uuid) = 0;

    virtual bool commitEntity(const Uuid& uuid, const QString& message,
                              QString* error = nullptr) = 0;
    // Saves several entities and commits them as one operation. The default
    // implementation commits each entity separately; ProjectDocument overrides
    // it with one atomic commit.
    virtual bool commitEntities(const QVector<Uuid>& uuids, const QString& message,
                                QString* error = nullptr);
    // Deletes several entities and commits them as one operation. Default is
    // unsupported; ProjectDocument overrides it atomically.
    virtual bool deleteEntities(const QVector<Uuid>& uuids, const QString& message,
                                QString* error = nullptr);
    // Saves upserts and deletes in one operation/commit. Default is two separate
    // operations; ProjectDocument overrides it atomically.
    virtual bool commitChanges(const QVector<Uuid>& upserts,
                               const QVector<Uuid>& deletes,
                               const QString& message,
                               QString* error = nullptr);
    virtual bool commitProject(const QString& message, QString* error = nullptr) = 0;

    virtual bool undo(QString* error = nullptr) = 0;
    virtual bool redo(QString* error = nullptr) = 0;

    // Commits any pending debounced write immediately. Call before closing,
    // switching projects or exporting. *error is set (and commitFailed is
    // emitted) when the git commit fails; the saved edits stay on disk.
    virtual void flush(QString* error = nullptr);
    virtual bool hasPendingCommit() const;

signals:
    void changed();
    void selectionChanged();
    void playheadChanged();
    // A debounced (or explicit flush) git commit failed; the saved edits are
    // still on disk and remain pending.
    void commitFailed(const QString& message);
    void pendingCommitChanged(bool pending);
};

} // namespace pvf::next
