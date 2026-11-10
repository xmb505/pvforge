#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace pvf {

namespace model {
struct Project;
struct ProjectSettings;
}

namespace assets {
class AssetStore;
}

namespace next {
class ProjectDocument;
}

// Thin adapter between the legacy application shell and the new
// next::ProjectDocument.  It owns the document and exposes only the pieces the
// P1 shell needs; it deliberately keeps no shadow copy of the project model.
class EditorSession
{
public:
    ~EditorSession();

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    // Creates a new `.pvproj` directory project.  Git is off in P1: the store
    // still writes atomically and keeps in-memory undo.
    static std::unique_ptr<EditorSession> createProject(
        const QString& projectDir, const model::ProjectSettings& settings,
        QString* error = nullptr, bool useGit = false);

    // Opens an existing `.pvproj` directory.  Legacy single-file projects are
    // rejected with a migration hint instead of being parsed.
    static std::unique_ptr<EditorSession> openProject(const QString& projectDir,
                                                      QString* error = nullptr,
                                                      bool useGit = false);

    next::ProjectDocument* document() const { return m_document.get(); }

    const model::Project& project() const;
    model::Project& mutableProject();
    assets::AssetStore& assets();

    const model::ProjectSettings& settings() const;
    QString root() const;
    QString name() const;

    int playhead() const;
    void setPlayhead(int frame);
    void savePlayhead();

    // Commits any pending debounced write and persists the playhead.  The shell
    // calls this on Ctrl+S, close and project switch.
    void flush(QString* error = nullptr);

    bool hasPendingCommit() const;
    bool commitProject(const QString& message = QStringLiteral("pvf: 工程设置"),
                       QString* error = nullptr);

private:
    explicit EditorSession(std::unique_ptr<next::ProjectDocument> document);

    std::unique_ptr<next::ProjectDocument> m_document;
};

// Recent project directories, newest first.  Entries that are no longer a
// `.pvproj` directory with a project.json are dropped when read.
QStringList recentProjects();
void rememberRecentProject(const QString& path);

} // namespace pvf
