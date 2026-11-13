// Session rename test. The EditorSession shell replaced the legacy
// Project-based editor tests; only the rename capability survived the move.

#include "TestApp.h"
#include "TestRegistry.h"

namespace {

// Renaming moved from the old timeline inline editor (`renameEdit`, started by
// double-clicking a row) to the properties panel's 「名称」 field
// (`entityName`). The test follows the capability to the new entry point and
// keeps the undo assertion; the old window-title / close-prompt assertions it
// used to carry live in `--session-test`, which covers the unsaved marker.
int renameTest()
{
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("rename.pvproj"));

    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("Rename");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session = pvf::EditorSession::createProject(projectDir, settings, &error);
    if (!session)
    {
        qCritical() << "rename-test: create failed" << error;
        return 1;
    }

    auto& project = session->mutableProject();
    pvf::model::Entity entity;
    entity.uuid = pvf::model::newUuid();
    entity.name = QStringLiteral("原名字");
    entity.start = 0;
    entity.end = 60;
    entity.row = 0;
    entity.content.kind = pvf::ContentKind::Empty;
    const pvf::Uuid uuid = entity.uuid;
    project.entities.insert(uuid, entity);
    if (!session->document()->commitEntities({uuid}, QStringLiteral("rename setup"),
                                             &error))
    {
        qCritical() << "rename-test: setup commit failed" << error;
        return 1;
    }
    session->flush();

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1200, 800);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    int failures = 0;
    auto fail = [&failures](const char* text) {
        qCritical() << "rename-test:" << text;
        ++failures;
    };

    auto* live = window->editorSessionForTest();
    auto* document = live ? live->document() : nullptr;
    auto* panel = window->findChild<pvf::PropertiesPanel*>();
    if (!live || !document || !panel || !panel->sessionMode())
    {
        qCritical() << "rename-test: session window/panel missing";
        return 1;
    }

    document->setSelection(uuid);
    QApplication::processEvents();
    QApplication::processEvents();

    auto* nameEdit = panel->findChild<QLineEdit*>(QStringLiteral("entityName"));
    if (!nameEdit)
    {
        qCritical() << "rename-test: rename field missing";
        return 1;
    }
    if (nameEdit->text() != QStringLiteral("原名字"))
        fail("name field does not show the entity");

    auto name = [live, uuid] {
        return live->project().entities.value(uuid).name;
    };

    const int depthBefore = document->store()->undoDepth();
    nameEdit->setText(QStringLiteral("新名字"));
    QMetaObject::invokeMethod(nameEdit, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    live->flush(&error);
    if (name() != QStringLiteral("新名字"))
        fail("name edit did not reach the model");
    if (document->store()->undoDepth() != depthBefore + 1)
        fail("name edit was not one commit");

    if (!document->undo(&error))
        fail("undo failed");
    QApplication::processEvents();
    if (name() != QStringLiteral("原名字"))
        fail("undo did not restore the name");

    window->close();
    QApplication::processEvents();

    qInfo() << "rename-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerEditorTests(Registry& registry)
{
    registry.add(QStringLiteral("--rename-test"),
                 [](QApplication&) { return renameTest(); });
}

} // namespace pvf::tests
