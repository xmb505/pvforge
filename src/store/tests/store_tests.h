#pragma once

#include <QObject>

// Shared test-class declaration. The v3 root-subtree cases live in
// store_tree_tests.cpp; the historical tests stay in store_tests.cpp.
class StoreTests : public QObject
{
    Q_OBJECT

private slots:
    void createOpenRoundTrip();
    void saveEntityReopen();
    void saveProjectRoundTrip();
    void deleteEntityFile();
    void commitAndHistory();
    void assetImportCommit();
    void commitFailureReportsStderr();
    void undoRedoRestoresText();
    void undoProjectSettings();
    void undoNewAndDeletedFiles();
    void undoStackLimits();
    void assetsSurviveUndo();
    void corruptProjectJson();
    void corruptEntityJson();
    void nonGitDirectoryRejected();
    void missingGitCreateFallsBack();
    void createRejectsInvalidInput();
    void cleanProjectHasNoDirtyText();
    void dirtyTextDetectedOnOpen();
    void commitRecoveredTextCreatesCommit();
    void discardDirtyTextRestores();
    void restoreCommitRewindsText();
    void restoreCommitDeletesNewEntities();
    void restoreCommitRejectsDirtyText();
    void restoreCommitRejectsInvalidHash();
    void restoreCommitClearsStacksAndKeepsAssets();
    void restoreCommitRollsBackOnCommitFailure();
    void createWithoutGit();
    void openWithoutGitRepoAndGitRequested();
    void enableGitLater();
    void disableGitThenCommitStillUndoable();
    void setGitEnabledReportsMissingGit();
    void openRejectsFutureVersion();
    void openRejectsUnmigratableVersion();
    void openRejectsLegacyFlatFormat();
    void openRejectsFutureEntityVersion();
    void openRejectsStaleEntityVersion();
    void openStrictRequiredFields();
    void unknownFieldsSurviveReopen();

    // v3 root-subtree format (store_tree_tests.cpp).
    void nestedEntityTreeRoundTrip();
    void entityTreeRejectsInconsistentNesting();
    void entityTreeRejectsDuplicateUuid();
    void entityTreeRejectsRootFileNameMismatch();
};
