#pragma once

#include "core/Project.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pvf {

class History;

// Structured error codes shared by every write tool. The names are the wire
// values used by the future MCP bridge, so they stay snake_case.
enum class McpErrorCode
{
    None,
    InvalidArgument,
    PermissionDenied,
    Conflict,
    LimitExceeded,
    StoppedByHuman,
    NotAvailable
};

struct McpWriteResult
{
    bool ok = false;
    QJsonObject data;
    McpErrorCode error = McpErrorCode::None;
    QString message;

    static McpWriteResult success(const QJsonObject& data = QJsonObject());
    static McpWriteResult failure(McpErrorCode code, const QString& message);
    QJsonObject toJson() const;
    static QString codeName(McpErrorCode code);
};

// Applies the MCP write tools to a Project. The HTTP and UI layers are not
// involved: the caller arms the service explicitly, executes one tool call and
// renders the structured result. Every call is atomic and leaves one undo step
// in History when history is available.
//
// Scope is snapshotted when arm()/armUnscoped() is called. Execution checks the
// target entity's root ancestor against that snapshot, so moving the playhead
// can never widen what the AI may edit.
class McpCommandService
{
public:
    static constexpr int kMaxBatchOperations = 200;
    static constexpr int kMaxProjectEntities = 5000;
    static constexpr double kMaxFrame = 1000000.0;

    McpCommandService(Project* project, History* history = nullptr,
                      EntityId sceneRootId = kInvalidEntity, bool armed = false,
                      bool stopped = false);

    // Human unlock for one scene root folder. The folder id is the permanent
    // scope snapshot; the current playhead is never consulted again.
    bool arm(EntityId sceneRootId, QString* error = nullptr);
    // Explicit human unlock when there is no scene (gap / empty project).
    // Write tools stay refused unless a human calls this.
    bool armUnscoped(QString* error = nullptr);
    void disarm();
    // Emergency stop: current call rolls back, all later writes are refused
    // with stopped_by_human until a human arms the service again.
    void stop();

    bool isArmed() const { return m_armed; }
    bool isStopped() const { return m_stopped; }
    bool isUnscoped() const { return m_unscoped; }
    int epoch() const { return m_epoch; }
    EntityId sceneRootId() const { return m_sceneRootId; }
    QString sceneId() const { return m_sceneId; }
    QString sceneName() const { return m_sceneName; }

    // Global objects (cameras, audio, top-level logos) require this separate
    // human permission; scope alone never unlocks them.
    void setAllowGlobal(bool allowed) { m_allowGlobal = allowed; }
    bool allowGlobal() const { return m_allowGlobal; }

    static QStringList operationNames();

    McpWriteResult execute(const QString& operation,
                           const QJsonObject& args = QJsonObject());

private:
    struct Snapshot
    {
        QVector<Entity> entities;
        QVector<Scene> scenes;
        QVector<Annotation> annotations;
        EntityId activeCamera = kInvalidEntity;
        int monitorView = kWorkCameraView;
        bool modified = false;
    };

    struct SceneCreation
    {
        QString sceneId;
        QString name;
        int startFrame = 0;
        int endFrame = 0;
        QColor color;
        QString note;
        EntityId folderId = kInvalidEntity;
    };

    McpWriteResult executeBatch(const QJsonObject& args);
    McpWriteResult applyOperation(const QString& operation, const QJsonObject& args,
                                  QVector<SceneCreation>* createdScenes);

    Snapshot capture() const;
    void restore(const Snapshot& snapshot);
    void commit(const Snapshot& before, const QVector<SceneCreation>& createdScenes,
                const QString& text);

    bool checkWritable(McpWriteResult* error) const;
    bool checkTransaction(int startEpoch, McpWriteResult* error) const;

    EntityId rootAncestor(EntityId id) const;
    bool isGlobalObject(EntityId id) const;
    bool isSceneRoot(EntityId id) const { return id != kInvalidEntity && id == m_sceneRootId; }
    bool scopeAllows(EntityId id) const;
    bool ensureEntityWritable(EntityId id, McpWriteResult* error,
                              bool allowSceneRoot = false) const;

    bool ensureEntityCapacity(int added, McpWriteResult* error) const;

    McpWriteResult createEntity(const QJsonObject& args,
                                QVector<SceneCreation>* createdScenes);
    McpWriteResult updateEntity(const QJsonObject& args);
    McpWriteResult setKeyframes(const QJsonObject& args);
    McpWriteResult setEasing(const QJsonObject& args);
    McpWriteResult moveEntity(const QJsonObject& args);
    McpWriteResult createFolder(const QJsonObject& args);
    McpWriteResult retireEntity(const QJsonObject& args);
    McpWriteResult createScene(const QJsonObject& args,
                               QVector<SceneCreation>* createdScenes);

    Project* m_project = nullptr;
    History* m_history = nullptr;

    bool m_armed = false;
    bool m_stopped = false;
    bool m_unscoped = false;
    bool m_allowGlobal = false;
    int m_epoch = 0;

    EntityId m_sceneRootId = kInvalidEntity;
    QString m_sceneId;
    QString m_sceneName;
};

} // namespace pvf
