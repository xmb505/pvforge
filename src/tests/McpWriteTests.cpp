#include "McpWriteTests.h"

#include "LegacyTestSupport.h"

namespace {

struct McpWriteContext
{
    McpWriteContext(pvf::Project& projectIn, pvf::History& historyIn,
                    pvf::McpCommandService& serviceIn, QString& errorIn,
                    int& failuresIn)
        : project(projectIn)
        , history(historyIn)
        , service(serviceIn)
        , error(errorIn)
        , failures(failuresIn)
    {
        fail = [this](const QString& message) {
            qCritical() << "mcp-write-test:" << message;
            ++failures;
        };
        failIf = [this](bool condition, const QString& message) {
            if (condition)
                fail(message);
        };
        expectOk = [this](const pvf::McpWriteResult& result,
                          const QString& label) {
            if (result.ok)
                return true;
            fail(QStringLiteral("%1: %2 (%3)")
                     .arg(label, result.message,
                          pvf::McpWriteResult::codeName(result.error)));
            return false;
        };
        expectCode = [this](const pvf::McpWriteResult& result,
                            pvf::McpErrorCode code, const QString& label) {
            if (result.ok || result.error != code)
            {
                fail(QStringLiteral("%1: expected %2, got %3 (%4)")
                         .arg(label, pvf::McpWriteResult::codeName(code),
                              pvf::McpWriteResult::codeName(result.error),
                              result.message));
                return false;
            }
            return true;
        };
        entityName = [this](pvf::EntityId id) {
            const pvf::Entity* entity = project.entity(id);
            return entity ? entity->name : QString();
        };
    }

    pvf::Project& project;
    pvf::History& history;
    pvf::McpCommandService& service;
    QString& error;
    int& failures;

    pvf::EntityId sceneAFolder = pvf::kInvalidEntity;
    pvf::EntityId sceneBFolder = pvf::kInvalidEntity;
    pvf::EntityId aId = pvf::kInvalidEntity;
    pvf::EntityId bId = pvf::kInvalidEntity;
    pvf::EntityId cameraId = pvf::kInvalidEntity;
    pvf::EntityId textId = pvf::kInvalidEntity;
    pvf::EntityId folderId = pvf::kInvalidEntity;
    QString sceneAId;

    std::function<void(const QString&)> fail;
    std::function<void(bool, const QString&)> failIf;
    std::function<bool(const pvf::McpWriteResult&, const QString&)> expectOk;
    std::function<bool(const pvf::McpWriteResult&, pvf::McpErrorCode,
                       const QString&)>
        expectCode;
    std::function<QString(pvf::EntityId)> entityName;
};

// ---- Unarmed writes are refused, then arm the scene and exercise the core
// write operations ---------------------------------------------------------
void writeUnarmedAndArm(McpWriteContext& c)
{
    int& failures = c.failures;
    const auto& fail = c.fail;
    const auto& failIf = c.failIf;
    const auto& expectOk = c.expectOk;
    const auto& expectCode = c.expectCode;
    pvf::Project& project = c.project;
    pvf::History& history = c.history;
    auto& service = c.service;
    QString& error = c.error;
    const auto sceneAFolder = c.sceneAFolder;
    auto& textId = c.textId;
    auto& folderId = c.folderId;

    const int countBeforeUnarmed = int(project.entities().size());
    const pvf::McpWriteResult unarmedResult = service.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("未解锁")}});
    expectCode(unarmedResult, pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("unarmed create"));
    failIf(project.entities().size() != countBeforeUnarmed,
           QStringLiteral("unarmed create changed the project"));
    const QJsonObject unarmedJson = unarmedResult.toJson();
    failIf(unarmedJson.value(QStringLiteral("ok")).toBool(),
           QStringLiteral("error result claims ok"));
    failIf(unarmedJson.value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("permission_denied"),
           QStringLiteral("error result code mismatch"));

    failIf(!service.arm(sceneAFolder, &error),
           QStringLiteral("arm scene A: %1").arg(error));
    failIf(service.sceneId() != c.sceneAId,
           QStringLiteral("arm did not snapshot scene A"));
    failIf(service.sceneRootId() != sceneAFolder,
           QStringLiteral("arm did not snapshot the folder"));

    const QJsonObject createArgs{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("name"), QStringLiteral("AI 文本")},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("text"), QStringLiteral("你好")},
                     {QStringLiteral("size"), 48}}},
        {QStringLiteral("transform"),
         QJsonObject{{QStringLiteral("x"), 10.0},
                     {QStringLiteral("y"), 20.0}}},
        {QStringLiteral("frame"), 0.0}};
    const pvf::McpWriteResult createResult =
        service.execute(QStringLiteral("entity.create"), createArgs);
    if (expectOk(createResult, QStringLiteral("create text")))
    {
        textId = createResult.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* created = project.entity(textId);
        failIf(!created, QStringLiteral("created entity is missing"));
        if (created)
        {
            failIf(created->type != pvf::EntityType::Text,
                   QStringLiteral("created type is not Text"));
            failIf(created->parent != sceneAFolder,
                   QStringLiteral("created entity did not default to scene A"));
            failIf(created->data.value(QStringLiteral("text")).toString()
                       != QStringLiteral("你好"),
                   QStringLiteral("created text data mismatch"));
        }
        failIf(!qFuzzyCompare(project.value(textId, QStringLiteral("x"), 0) + 1.0,
                              11.0),
               QStringLiteral("created transform x mismatch"));
        failIf(!history.canUndo(), QStringLiteral("create did not reach History"));
        failIf(history.undoText().isEmpty(),
               QStringLiteral("create History text is empty"));
        history.undo(project);
        failIf(project.entity(textId) != nullptr,
               QStringLiteral("undo did not remove the created entity"));
        history.redo(project);
        failIf(project.entity(textId) == nullptr,
               QStringLiteral("redo did not restore the created entity"));
    }

    const QJsonObject updateArgs{
        {QStringLiteral("id"), textId},
        {QStringLiteral("name"), QStringLiteral("AI 文本 2")},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("text"), QStringLiteral("更新")}}},
        {QStringLiteral("transform"),
         QJsonObject{{QStringLiteral("x"), 30.0}}},
        {QStringLiteral("frame"), 0.0}};
    const pvf::McpWriteResult updateResult =
        service.execute(QStringLiteral("entity.update"), updateArgs);
    if (expectOk(updateResult, QStringLiteral("update text")))
    {
        const pvf::Entity* updated = project.entity(textId);
        failIf(!updated, QStringLiteral("updated entity is missing"));
        if (updated)
        {
            failIf(updated->name != QStringLiteral("AI 文本 2"),
                   QStringLiteral("update name mismatch"));
            failIf(updated->data.value(QStringLiteral("text")).toString()
                       != QStringLiteral("更新"),
                   QStringLiteral("update data mismatch"));
        }
        failIf(!qFuzzyCompare(project.value(textId, QStringLiteral("x"), 0) + 1.0,
                              31.0),
               QStringLiteral("update transform x mismatch"));
    }

    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("type"),
                                QStringLiteral("vector")}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("entity.update must not change type"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("data"),
                                QJsonObject{{QStringLiteral("sourcePath"),
                                             QStringLiteral("/etc/passwd")}}}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("entity.update path injection"));

    QJsonArray keyframes;
    keyframes.append(QJsonObject{{QStringLiteral("frame"), 0.0},
                                 {QStringLiteral("value"), 0.0}});
    keyframes.append(QJsonObject{{QStringLiteral("frame"), 60.0},
                                 {QStringLiteral("value"), 300.0},
                                 {QStringLiteral("ease"), QStringLiteral("easeinquad")}});
    const pvf::McpWriteResult keyframeResult = service.execute(
        QStringLiteral("keyframes.set"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("property"), QStringLiteral("x")},
                    {QStringLiteral("keys"), keyframes}});
    if (expectOk(keyframeResult, QStringLiteral("set keyframes")))
    {
        const pvf::Keyframe* key = project.keyframe(textId, QStringLiteral("x"), 60);
        failIf(!key, QStringLiteral("keyframe at 60 is missing"));
        if (key)
        {
            failIf(!qFuzzyCompare(key->value + 1.0, 301.0),
                   QStringLiteral("keyframe value mismatch"));
            failIf(key->ease.easing != pvf::Easing::EaseInQuad,
                   QStringLiteral("keyframe ease mismatch"));
        }
    }

    const pvf::McpWriteResult easingResult = service.execute(
        QStringLiteral("easing.set"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("property"), QStringLiteral("x")},
                    {QStringLiteral("frame"), 60.0},
                    {QStringLiteral("ease"), QStringLiteral("easeoutquad")}});
    if (expectOk(easingResult, QStringLiteral("set easing")))
    {
        const pvf::Keyframe* key = project.keyframe(textId, QStringLiteral("x"), 60);
        failIf(!key || key->ease.easing != pvf::Easing::EaseOutQuad,
               QStringLiteral("easing.set did not update the keyframe"));
    }

    const pvf::McpWriteResult folderResult = service.execute(
        QStringLiteral("folder.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("组")}});
    if (expectOk(folderResult, QStringLiteral("create folder")))
    {
        folderId = folderResult.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* folder = project.entity(folderId);
        failIf(!folder || !folder->isFolder(),
               QStringLiteral("created folder is missing"));
        failIf(folder && folder->parent != sceneAFolder,
               QStringLiteral("created folder did not default to scene A"));
    }

    const pvf::McpWriteResult moveResult = service.execute(
        QStringLiteral("entity.move"),
        QJsonObject{{QStringLiteral("id"), textId},
                    {QStringLiteral("parent"), folderId},
                    {QStringLiteral("index"), 0}});
    if (expectOk(moveResult, QStringLiteral("move entity")))
    {
        const pvf::Entity* moved = project.entity(textId);
        failIf(!moved || moved->parent != folderId,
               QStringLiteral("entity.move did not update the parent"));
    }
}

// ---- Scene boundary -------------------------------------------------------
void writeSceneBoundary(McpWriteContext& c)
{
    const auto& failIf = c.failIf;
    const auto& expectCode = c.expectCode;
    pvf::Project& project = c.project;
    auto& service = c.service;
    const auto bId = c.bId;
    const auto textId = c.textId;
    const auto sceneBFolder = c.sceneBFolder;
    const auto cameraId = c.cameraId;
    const auto aId = c.aId;
    const auto& entityName = c.entityName;

    const int countBeforeScope = int(project.entities().size());
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), bId},
                               {QStringLiteral("name"), QStringLiteral("越界")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("update entity in scene B"));
    expectCode(service.execute(
                   QStringLiteral("keyframes.set"),
                   QJsonObject{{QStringLiteral("id"), bId},
                               {QStringLiteral("property"), QStringLiteral("x")},
                               {QStringLiteral("keys"),
                                QJsonArray{QJsonObject{
                                    {QStringLiteral("frame"), 0.0},
                                    {QStringLiteral("value"), 1.0}}}}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("keyframes in scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.move"),
                   QJsonObject{{QStringLiteral("id"), textId},
                               {QStringLiteral("parent"), sceneBFolder}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("move entity into scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("越界")},
                               {QStringLiteral("parent"), sceneBFolder}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create entity in scene B"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("根层")},
                               {QStringLiteral("parent"), -1}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create entity at the root"));
    expectCode(service.execute(
                   QStringLiteral("folder.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("根层组")},
                               {QStringLiteral("parent"), -1}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("create root folder"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), cameraId},
                               {QStringLiteral("name"), QStringLiteral("越界摄像机")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("update global camera"));
    expectCode(service.execute(QStringLiteral("entities.retire"),
                               QJsonObject{{QStringLiteral("id"), bId}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("retire entity in scene B"));
    failIf(project.entities().size() != countBeforeScope,
           QStringLiteral("rejected scope writes changed the project"));
    failIf(entityName(aId) != QStringLiteral("场景 A 矩形"),
           QStringLiteral("rejected scope write renamed scene A entity"));

    service.setAllowGlobal(true);
    const pvf::McpWriteResult cameraResult = service.execute(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("id"), cameraId},
                    {QStringLiteral("name"), QStringLiteral("主摄像机 2")}});
    failIf(!c.expectOk(cameraResult, QStringLiteral("update camera with allowGlobal")),
           QStringLiteral("allowGlobal did not unlock the camera"));
    failIf(entityName(cameraId) != QStringLiteral("主摄像机 2"),
           QStringLiteral("camera rename did not apply"));
    service.setAllowGlobal(false);
}

// ---- Retire ---------------------------------------------------------------
void writeRetire(McpWriteContext& c)
{
    const auto& failIf = c.failIf;
    const auto& expectOk = c.expectOk;
    pvf::Project& project = c.project;
    pvf::History& history = c.history;
    auto& service = c.service;
    const auto textId = c.textId;
    const auto folderId = c.folderId;

    const int countBeforeRetire = int(project.entities().size());
    const pvf::McpWriteResult retireResult = service.execute(
        QStringLiteral("entities.retire"),
        QJsonObject{{QStringLiteral("id"), textId}});
    if (expectOk(retireResult, QStringLiteral("retire entity")))
    {
        const pvf::Entity* retired = project.entity(textId);
        failIf(!retired, QStringLiteral("retire deleted the entity"));
        if (retired)
        {
            failIf(!retired->locked, QStringLiteral("retired entity is not locked"));
            const pvf::Entity* sceneFolder = project.entity(retired->parent);
            failIf(!sceneFolder || sceneFolder->name != QStringLiteral("A"),
                   QStringLiteral("retired entity is not in _retired/A"));
            const pvf::Entity* retiredRoot =
                sceneFolder ? project.entity(sceneFolder->parent) : nullptr;
            failIf(!retiredRoot || retiredRoot->name != QStringLiteral("_retired"),
                   QStringLiteral("retired scene folder has no _retired parent"));
        }
        failIf(!qFuzzyIsNull(project.value(textId, QStringLiteral("opacity"), 0)),
               QStringLiteral("retired opacity at frame 0 is not 0"));
        failIf(!qFuzzyIsNull(project.effectiveOpacity(textId, 60)),
               QStringLiteral("retired effective opacity is not 0"));
        failIf(int(project.entities().size()) < countBeforeRetire,
               QStringLiteral("retire shrank the project"));
        history.undo(project);
        const pvf::Entity* restored = project.entity(textId);
        failIf(!restored || restored->locked,
               QStringLiteral("undo did not restore the retired entity"));
        failIf(restored && restored->parent != folderId,
               QStringLiteral("undo did not restore the old parent"));
        history.redo(project);
        const pvf::Entity* retiredAgain = project.entity(textId);
        failIf(!retiredAgain || !retiredAgain->locked,
               QStringLiteral("redo did not retire the entity again"));
    }
}

// ---- Batch atomicity ------------------------------------------------------
void writeBatch(McpWriteContext& c)
{
    const auto& failIf = c.failIf;
    const auto& expectOk = c.expectOk;
    const auto& expectCode = c.expectCode;
    pvf::Project& project = c.project;
    pvf::History& history = c.history;
    auto& service = c.service;
    const auto aId = c.aId;
    const auto sceneAFolder = c.sceneAFolder;
    const auto& entityName = c.entityName;

    const QString aOriginalName = entityName(aId);
    const int countBeforeFailedBatch = int(project.entities().size());
    const QString undoBeforeFailedBatch = history.undoText();
    QJsonArray failedOps;
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次矩形")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.update")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("id"), aId},
                     {QStringLiteral("name"), QStringLiteral("批次改名")}}}});
    failedOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.update")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("id"), 999999},
                     {QStringLiteral("name"), QStringLiteral("不存在")}}}});
    const pvf::McpWriteResult failedBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), failedOps}});
    expectCode(failedBatch, pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("failed batch"));
    failIf(project.entities().size() != countBeforeFailedBatch,
           QStringLiteral("failed batch did not roll back entity count"));
    failIf(entityName(aId) != aOriginalName,
           QStringLiteral("failed batch kept an entity rename"));
    failIf(history.undoText() != undoBeforeFailedBatch,
           QStringLiteral("failed batch pushed a History step"));
    for (const pvf::Entity& entity : project.entities())
        failIf(entity.name == QStringLiteral("批次矩形"),
               QStringLiteral("failed batch kept a created entity"));

    const int countBeforeGoodBatch = int(project.entities().size());
    QJsonArray goodOps;
    goodOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次 1")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    goodOps.append(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("entity.create")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("vector")},
                     {QStringLiteral("name"), QStringLiteral("批次 2")},
                     {QStringLiteral("parent"), sceneAFolder}}}});
    const pvf::McpWriteResult goodBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), goodOps}});
    if (expectOk(goodBatch, QStringLiteral("successful batch")))
    {
        failIf(project.entities().size() != countBeforeGoodBatch + 2,
               QStringLiteral("successful batch did not create both entities"));
        failIf(history.undoText() != QStringLiteral("AI 批量操作"),
               QStringLiteral("batch did not push one History step"));
        history.undo(project);
        failIf(project.entities().size() != countBeforeGoodBatch,
               QStringLiteral("batch undo did not remove both entities"));
        history.redo(project);
        failIf(project.entities().size() != countBeforeGoodBatch + 2,
               QStringLiteral("batch redo did not restore both entities"));
    }

    QJsonArray tooManyOps;
    for (int index = 0; index < pvf::McpCommandService::kMaxBatchOperations + 1;
         ++index)
    {
        tooManyOps.append(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("entity.update")},
            {QStringLiteral("args"),
             QJsonObject{{QStringLiteral("id"), aId},
                         {QStringLiteral("name"), QStringLiteral("限额")}}}});
    }
    expectCode(service.execute(QStringLiteral("batch.run"),
                               QJsonObject{{QStringLiteral("operations"), tooManyOps}}),
               pvf::McpErrorCode::LimitExceeded,
               QStringLiteral("batch over 200 operations"));
    failIf(entityName(aId) != aOriginalName,
           QStringLiteral("over-limit batch changed the project"));

    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}}),
               pvf::McpErrorCode::NotAvailable,
               QStringLiteral("create image without a path"));
    expectCode(service.execute(QStringLiteral("html.create_layer"), QJsonObject()),
               pvf::McpErrorCode::NotAvailable,
               QStringLiteral("HTML layer write"));
    expectCode(service.execute(
                   QStringLiteral("entity.update"),
                   QJsonObject{{QStringLiteral("id"), 999999},
                               {QStringLiteral("name"), QStringLiteral("不存在")}}),
               pvf::McpErrorCode::InvalidArgument,
               QStringLiteral("update missing entity"));
}

// ---- Stop during a single call and during a batch -------------------------
void writeStop(McpWriteContext& c)
{
    const auto& failIf = c.failIf;
    const auto& expectCode = c.expectCode;
    pvf::Project& project = c.project;
    pvf::History& history = c.history;
    auto& service = c.service;
    QString& error = c.error;
    const auto aId = c.aId;
    const auto sceneAFolder = c.sceneAFolder;
    const auto& entityName = c.entityName;

    const int countBeforeStop = int(project.entities().size());
    const QString undoBeforeStop = history.undoText();
    bool stopFired = false;
    QMetaObject::Connection stopConnection = QObject::connect(
        &project, &pvf::Project::changed, [&]() {
            if (stopFired)
                return;
            stopFired = true;
            service.stop();
        });
    const pvf::McpWriteResult stoppedCreate = service.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("急停文本")}});
    QObject::disconnect(stopConnection);
    expectCode(stoppedCreate, pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("stop during single create"));
    failIf(!stopFired, QStringLiteral("stop signal did not fire"));
    failIf(project.entities().size() != countBeforeStop,
           QStringLiteral("stopped create was not rolled back"));
    failIf(history.undoText() != undoBeforeStop,
           QStringLiteral("stopped create pushed a History step"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("急停后")}}),
               pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("write after stop"));

    failIf(!service.arm(sceneAFolder, &error),
           QStringLiteral("re-arm after stop: %1").arg(error));
    failIf(service.isStopped(), QStringLiteral("re-arm did not clear stopped"));
    const QString aBeforeStoppedBatch = entityName(aId);
    const QString undoBeforeStoppedBatch = history.undoText();
    stopFired = false;
    int changedCount = 0;
    stopConnection = QObject::connect(
        &project, &pvf::Project::changed, [&]() {
            ++changedCount;
            if (stopFired || changedCount < 3)
                return;
            stopFired = true;
            service.stop();
        });
    QJsonArray stoppedOps;
    for (int index = 0; index < 10; ++index)
    {
        stoppedOps.append(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("entity.update")},
            {QStringLiteral("args"),
             QJsonObject{{QStringLiteral("id"), aId},
                         {QStringLiteral("name"),
                          QStringLiteral("急停批次 %1").arg(index)}}}});
    }
    const pvf::McpWriteResult stoppedBatch = service.execute(
        QStringLiteral("batch.run"),
        QJsonObject{{QStringLiteral("operations"), stoppedOps}});
    QObject::disconnect(stopConnection);
    expectCode(stoppedBatch, pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("stop during batch"));
    failIf(!stopFired, QStringLiteral("batch stop signal did not fire"));
    failIf(entityName(aId) != aBeforeStoppedBatch,
           QStringLiteral("stopped batch was not rolled back"));
    failIf(history.undoText() != undoBeforeStoppedBatch,
           QStringLiteral("stopped batch pushed a History step"));
    expectCode(service.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("急停后 2")}}),
               pvf::McpErrorCode::StoppedByHuman,
               QStringLiteral("write after batch stop"));
}

// ---- scene.create scope, explicit unlock, project entity limit ------------
int writeSceneAndLimits(McpWriteContext& c)
{
    int& failures = c.failures;
    const auto& failIf = c.failIf;
    const auto& expectOk = c.expectOk;
    const auto& expectCode = c.expectCode;

    QString error;
    const QStringList operations = pvf::McpCommandService::operationNames();
    for (const QString& required :
         {QStringLiteral("entity.create"), QStringLiteral("entity.update"),
          QStringLiteral("keyframes.set"), QStringLiteral("easing.set"),
          QStringLiteral("entity.move"), QStringLiteral("folder.create"),
          QStringLiteral("entities.retire"), QStringLiteral("scene.create"),
          QStringLiteral("batch.run")})
        failIf(!operations.contains(required),
               QStringLiteral("operationNames missing %1").arg(required));

    pvf::Project sceneProject;
    pvf::Entity* sceneHolder =
        sceneProject.addVector(0, 100, 100, Qt::gray, Qt::white, 0);
    sceneProject.setValue(sceneHolder->id, QStringLiteral("x"), 0, 0.0, true);
    sceneProject.setValue(sceneHolder->id, QStringLiteral("x"), 300, 300.0, true);
    pvf::Scene* firstScene = sceneProject.addScene(
        QStringLiteral("S1"), 0, 100, QColor(90, 170, 255), QString(), &error);
    if (!firstScene)
    {
        qCritical() << "mcp-write-test: add scene S1" << error;
        return 1;
    }
    const pvf::EntityId firstSceneFolder = firstScene->folderId;
    pvf::History sceneHistory;
    pvf::McpCommandService sceneService(&sceneProject, &sceneHistory);
    failIf(!sceneService.arm(firstSceneFolder, &error),
           QStringLiteral("arm scene S1: %1").arg(error));

    expectCode(sceneService.execute(
                   QStringLiteral("scene.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("S2")},
                               {QStringLiteral("startFrame"), 50},
                               {QStringLiteral("endFrame"), 300}}),
               pvf::McpErrorCode::Conflict,
               QStringLiteral("scene.create before the last scene"));
    failIf(sceneProject.scenes().size() != 1,
           QStringLiteral("rejected scene.create changed the scene list"));

    const pvf::McpWriteResult sceneResult = sceneService.execute(
        QStringLiteral("scene.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("S2")},
                    {QStringLiteral("startFrame"), 150},
                    {QStringLiteral("endFrame"), 300}});
    QString newSceneId;
    pvf::EntityId newSceneFolder = pvf::kInvalidEntity;
    if (expectOk(sceneResult, QStringLiteral("scene.create append")))
    {
        newSceneId = sceneResult.data.value(QStringLiteral("id")).toString();
        newSceneFolder =
            sceneResult.data.value(QStringLiteral("folderId")).toInt();
        failIf(sceneProject.scenes().size() != 2,
               QStringLiteral("scene.create did not append"));
        failIf(sceneProject.scenes().last().id != newSceneId,
               QStringLiteral("scene.create did not append at the end"));
        sceneHistory.undo(sceneProject);
        failIf(sceneProject.scenes().size() != 1,
               QStringLiteral("scene.create undo did not remove the scene"));
        sceneHistory.redo(sceneProject);
        failIf(sceneProject.scenes().size() != 2,
               QStringLiteral("scene.create redo did not restore the scene"));
        if (sceneProject.scenes().size() == 2)
        {
            const pvf::EntityId redoneFolder =
                sceneProject.scenes().last().folderId;
            const pvf::Entity* redone = sceneProject.entity(redoneFolder);
            failIf(!redone || !redone->isFolder(),
                   QStringLiteral("scene.create redo left a dangling folder"));
            failIf(redoneFolder == newSceneFolder,
                   QStringLiteral("scene.create redo reused the removed folder"));
        }
    }

    const pvf::McpWriteResult scopedCreate = sceneService.execute(
        QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("作用域")}});
    if (expectOk(scopedCreate, QStringLiteral("create after scene.create")))
    {
        const pvf::EntityId createdId =
            scopedCreate.data.value(QStringLiteral("id")).toInt();
        const pvf::Entity* created = sceneProject.entity(createdId);
        failIf(!created || created->parent != firstSceneFolder,
               QStringLiteral("scene.create widened the write scope"));
        failIf(created && created->parent == newSceneFolder,
               QStringLiteral("new entity landed in the new scene"));
    }

    pvf::Project noSceneProject;
    pvf::Entity* noSceneEntity =
        noSceneProject.addVector(0, 100, 100, Qt::gray, Qt::white, 0);
    Q_UNUSED(noSceneEntity);
    pvf::History noSceneHistory;
    pvf::McpCommandService noSceneService(&noSceneProject, &noSceneHistory,
                                          pvf::kInvalidEntity, true);
    expectCode(noSceneService.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("无场景")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("armed without a scene"));
    expectCode(noSceneService.execute(
                   QStringLiteral("scene.create"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("无场景")}}),
               pvf::McpErrorCode::PermissionDenied,
               QStringLiteral("scene.create without a scene"));
    failIf(!noSceneService.armUnscoped(&error),
           QStringLiteral("arm unscoped no-scene project: %1").arg(error));
    expectOk(noSceneService.execute(
                 QStringLiteral("entity.create"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("name"), QStringLiteral("显式解锁")}}),
             QStringLiteral("create after explicit unscoped unlock"));

    pvf::Project fullProject;
    QVector<pvf::Entity> manyEntities;
    manyEntities.reserve(pvf::McpCommandService::kMaxProjectEntities);
    for (int index = 1; index <= pvf::McpCommandService::kMaxProjectEntities;
         ++index)
    {
        pvf::Entity entity;
        entity.id = index;
        entity.type = pvf::EntityType::Folder;
        entity.name = QStringLiteral("填充 %1").arg(index);
        manyEntities.append(entity);
    }
    fullProject.setEntities(manyEntities);
    pvf::McpCommandService fullService(&fullProject);
    failIf(!fullService.armUnscoped(&error),
           QStringLiteral("arm unscoped full project: %1").arg(error));
    expectCode(fullService.execute(
                   QStringLiteral("entity.create"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("name"), QStringLiteral("超限")}}),
               pvf::McpErrorCode::LimitExceeded,
               QStringLiteral("create beyond 5000 entities"));
    failIf(fullProject.entities().size()
               != pvf::McpCommandService::kMaxProjectEntities,
           QStringLiteral("limit rejection changed the full project"));
    return 0;
}

} // namespace

namespace pvf::tests {

int runMcpWriteTest()
{
    int failures = 0;
    pvf::Project project;
    project.fps = 30;
    project.width = 1280;
    project.height = 720;

    pvf::Entity* holder =
        project.addVector(0, 100, 100, QColor(80, 80, 80), Qt::white, 0);
    if (!holder)
        return 1;
    project.setValue(holder->id, QStringLiteral("x"), 0, 0.0, true);
    project.setValue(holder->id, QStringLiteral("x"), 240, 240.0, true);

    QString error;
    pvf::Scene* sceneA = project.addScene(QStringLiteral("A"), 0, 120,
                                          QColor(255, 80, 80),
                                          QStringLiteral("场景 A"), &error);
    if (!sceneA)
    {
        qCritical() << "mcp-write-test: add scene A" << error;
        return 1;
    }
    const pvf::EntityId sceneAFolder = sceneA->folderId;
    const QString sceneAId = sceneA->id;

    pvf::Scene* sceneB = project.addScene(QStringLiteral("B"), 150, 240,
                                          QColor(80, 200, 120),
                                          QStringLiteral("场景 B"), &error);
    if (!sceneB)
    {
        qCritical() << "mcp-write-test: add scene B" << error;
        return 1;
    }
    const pvf::EntityId sceneBFolder = sceneB->folderId;

    pvf::Entity* aEntity =
        project.addVector(0, 120, 90, QColor(230, 90, 90), Qt::white, 0);
    aEntity->name = QStringLiteral("场景 A 矩形");
    const pvf::EntityId aId = aEntity->id;
    project.setEntityParent(aId, sceneAFolder);
    pvf::Entity* bEntity =
        project.addVector(0, 120, 90, QColor(90, 140, 230), Qt::white, 0);
    bEntity->name = QStringLiteral("场景 B 矩形");
    const pvf::EntityId bId = bEntity->id;
    project.setEntityParent(bId, sceneBFolder);
    pvf::Entity* camera = project.addCamera(QStringLiteral("主摄像机"));
    const pvf::EntityId cameraId = camera->id;
    project.activeCamera = cameraId;

    pvf::History history;
    pvf::McpCommandService service(&project, &history);

    McpWriteContext context(project, history, service, error, failures);
    context.sceneAFolder = sceneAFolder;
    context.sceneBFolder = sceneBFolder;
    context.aId = aId;
    context.bId = bId;
    context.cameraId = cameraId;
    context.sceneAId = sceneAId;

    context.failIf(service.isArmed(), QStringLiteral("service starts armed"));
    context.failIf(service.isStopped(), QStringLiteral("service starts stopped"));

    writeUnarmedAndArm(context);
    writeSceneBoundary(context);
    writeRetire(context);
    writeBatch(context);
    writeStop(context);
    if (writeSceneAndLimits(context) != 0)
        return 1;

    qInfo() << "mcp-write-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

} // namespace pvf::tests
