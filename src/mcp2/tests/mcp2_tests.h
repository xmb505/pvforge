#pragma once

#include <QObject>

// Shared declaration so security tests live in their own translation unit while
// still belonging to the same QtTest class. QT_QPA / ctest run one case per
// process, so the class identity only matters for slot selection.
class Mcp2Tests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void createTreeOneCommit();
    void keyframesSetRespectsDuration();
    void solverDrivenTrackRejectsNormalKeys();
    void trimSolverRules();
    void trimSolverCutPose();
    void moveTrimReparentValidation();
    void reparentPreserveWorldKeepsPosition();
    void lockedEntityRejectsWrite();
    void deleteRequiresConfirm();
    void assetImportDeduplicates();
    void assetImportOutsideNeedsConfirm();
    void markersSetListRemove();
    void historyUndoRedo();
    void defaultRowIsHighestPlusOne();
    void twoCallsTwoCommits();
    void createManyRollsBack();
    void keyframesSetManyRollsBack();
    void assetAssignAndUnassign();
    void duplicateSubtreeOneCommit();
    void projectUpdateRejectsFpsAndResolution();
    void deleteManyMergesHierarchy();
    void listToolsAndSchema();
    void solverCreateUpdateDelete();
    void solverSpiralCreateAndEvaluate();
    void solverValidationRejects();
    void viewStateReturnsInjected();
    void previewRenderFrameWritesPng();
    void previewRefusesUnapprovedBase64();
    void previewTempFilesArePruned();
    void previewRejectsOversize();
    void solverEvaluateNestedEntity();
    void moveRowOnlyAndGetDefaults();
    void entityGetIncludesSolvers();
    void textualContentCanBecomeEmpty();
    void specialContentCreateAndUpdate();
    void specialContentRejectsUnknownFields();
    void writeFlagsReflectService();
    void previewDocumentContract();
    void previewRateLimit();
    void assetSourceInClamped();
    void assetMissingContentStillListed();
    void deleteReportsSolverDependencies();
    void reparentPreserveWorldRejectsKeyframes();
    void assetImportRejectsOversize();
    void stopBlocksWrites();
    void stopKeepsReadsWorking();
    void resumeRestoresWrites();
    void writesDisabledBlocksWrites();
    void writesEnabledAgain();
    void stopAndReadOnlyInteraction();
    void createManyAbortsMidBatch();
};
