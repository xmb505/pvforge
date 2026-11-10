#pragma once

#include "next/core/Document.h"
#include "store/Store.h"

#include <QTimer>

#include <memory>

namespace pvf::next {

// Document backed by a store::ProjectStore. Owns the store and exposes the
// playhead/selection state used by every panel.
class ProjectDocument : public Document
{
    Q_OBJECT

public:
    // `useGit` only expresses intent: create() fails without a git binary when
    // true, open() falls back to a plain text store when git is missing or the
    // project has no repository.
    static std::unique_ptr<ProjectDocument> create(const QString& projectDir,
                                                   const model::ProjectSettings& settings,
                                                   QString* error = nullptr,
                                                   bool useGit = true);
    static std::unique_ptr<ProjectDocument> open(const QString& projectDir,
                                                 QString* error = nullptr,
                                                 bool useGit = true);

    ~ProjectDocument() override;

    const model::Project& project() const override;
    model::Project& mutableProject() override;
    assets::AssetStore& assets() override;

    int playhead() const override;
    void setPlayhead(int frame) override;

    Uuid selection() const override;
    void setSelection(const Uuid& uuid) override;

    bool commitEntity(const Uuid& uuid, const QString& message,
                      QString* error = nullptr) override;
    bool commitEntities(const QVector<Uuid>& uuids, const QString& message,
                        QString* error = nullptr) override;
    bool deleteEntities(const QVector<Uuid>& uuids, const QString& message,
                        QString* error = nullptr) override;
    bool commitChanges(const QVector<Uuid>& upserts,
                       const QVector<Uuid>& deletes,
                       const QString& message,
                       QString* error = nullptr) override;
    bool commitProject(const QString& message, QString* error = nullptr) override;

    bool undo(QString* error = nullptr) override;
    bool redo(QString* error = nullptr) override;

    // AI write lock for the human path.  MCP writes bypass the Document and go
    // straight to the store, so this only gates panel/menu edits.  Defaults to
    // enabled; read-only views are unaffected.
    void setHumanWritesEnabled(bool enabled) { m_humanWritesEnabled = enabled; }
    bool humanWritesEnabled() const { return m_humanWritesEnabled; }

    void flush(QString* error = nullptr) override;
    bool hasPendingCommit() const override;

    // Debounce for coalescing human edits into one commit.  The production
    // default is 1 s; the settings dialog may change it at runtime.
    void setHumanCommitDelayMs(int ms);
    int humanCommitDelayMs() const;

    store::ProjectStore* store() { return m_store.get(); }
    const store::ProjectStore* store() const { return m_store.get(); }

    // Enables/disables git on the backing store.  Enabling a project without a
    // repository runs git init and creates the initial "pvf: init" commit.
    bool setGitEnabled(bool enabled, QString* error = nullptr);

    QString root() const;
    void reloadAfterHistory(QString* error = nullptr);

    // Playhead persistence: `<root>/.pvforge/playhead.json` (the directory is
    // gitignored and never written to project.json).  The shell calls
    // savePlayheadState() before closing or switching projects; open() loads
    // and clamps it.  A missing or corrupt file is ignored.
    void savePlayheadState() const;
    void loadPlayheadState();

    // Reloads the in-memory project and assets index after a writer outside
    // the Document (the MCP service) changed the project on disk.  The
    // playhead and a still-existing selection are kept, then changed() is
    // emitted so every panel refreshes from the new bytes.
    bool notifyExternalChange(QString* error = nullptr);

private:
    explicit ProjectDocument(std::unique_ptr<store::ProjectStore> store,
                             QObject* parent = nullptr);

    void scheduleCommit();
    int contentEnd() const;

    std::unique_ptr<store::ProjectStore> m_store;
    QTimer m_commitTimer;
    bool m_pendingCommit = false;
    bool m_humanWritesEnabled = true;
    int m_playhead = 0;
    Uuid m_selection;
};

} // namespace pvf::next
