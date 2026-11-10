#pragma once

#include "common/Types.h"
#include "next/settings/SettingsStore.h"

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
class MediaServices;
class PlaybackController;
class ProjectDocument;
class WorkbenchServices;
} // namespace next

class EditActions;

// Thin adapter between the legacy application shell and the new
// next::ProjectDocument.  It owns the document and the per-project backend
// services the old shell needs (media decoders, the workbench cache and the
// playback clock).  It deliberately keeps no shadow copy of the project model.
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
    // The only human write entry point; one gesture becomes one commit.
    EditActions* editActions() const { return m_editActions.get(); }

    const model::Project& project() const;
    model::Project& mutableProject();
    assets::AssetStore& assets();

    const model::ProjectSettings& settings() const;
    QString root() const;
    QString name() const;

    // Backend services.  MediaServices and WorkbenchServices must be destroyed
    // before the document; member order in the implementation guarantees it.
    next::MediaServices* media() const { return m_media.get(); }
    next::PlaybackController* playback() const { return m_playback.get(); }
    // Returns the workbench cache for the current camera, rebuilding it when
    // the active camera or the expand setting changed.  May return nullptr when
    // the cache is disabled.
    next::WorkbenchServices* workbench();
    const next::AppSettings& appSettings() const { return m_appSettings; }

    // Camera used for the viewport viewfinder overlay and the camera monitor.
    // The explicitly chosen monitor camera wins; otherwise the selected
    // camera.  Empty means "whole canvas" and no frame.
    Uuid activeCameraUuid() const;
    void setMonitorCameraUuid(const Uuid& uuid);
    // Mirrors the legacy "selecting a camera makes it the framing camera"
    // rule.  Clearing the selection returns to the explicit monitor choice.
    void syncSelectionCamera(const Uuid& selected);

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

    // Declared before the services so the services die first.
    std::unique_ptr<next::ProjectDocument> m_document;
    std::unique_ptr<next::SettingsStore> m_settingsStore;
    next::AppSettings m_appSettings;
    std::unique_ptr<next::MediaServices> m_media;
    std::unique_ptr<next::WorkbenchServices> m_workbench;
    std::unique_ptr<next::PlaybackController> m_playback;
    std::unique_ptr<EditActions> m_editActions;
    Uuid m_monitorCamera;
    bool m_monitorCameraExplicit = false;
    bool m_monitorCameraFromSelection = false;
};

// Recent project directories, newest first.  Entries that are no longer a
// `.pvproj` directory with a project.json are dropped when read.
QStringList recentProjects();
void rememberRecentProject(const QString& path);

} // namespace pvf
